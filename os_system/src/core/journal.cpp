#include "journal.h"
#include "superblock.h"
#include "disk.h"
#include "cache.h"
#include "error.h"

#include <vector>
#include <array>
#include <cstring>

/*
 * journal.cpp
 * ------------------------------------------------------------
 * 简化版元数据日志。
 *
 * journal 区布局：
 *
 * journal_start:
 *     journal header
 *
 * journal_start + 1:
 *     第 0 个日志数据块
 *
 * journal_start + 2:
 *     第 1 个日志数据块
 *
 * ...
 *
 * journal_start + n:
 *     第 n-1 个日志数据块
 */

struct JournalMemEntry {
    myfs_block_t target_block;
    std::array<unsigned char, MYFS_BLOCK_SIZE> data;
};

static int g_journal_active = 0;
static uint32_t g_next_txid = 1;
static std::vector<JournalMemEntry> g_entries;


/*
 * 获取超级块。
 */
static myfs_superblock_t *get_sb(void) {
    return myfs_super_get();
}


/*
 * 简单 checksum。
 *
 * 当前只用于发现明显破坏，不是安全哈希。
 */
static uint32_t calc_header_checksum(const myfs_journal_header_t *header) {
    myfs_journal_header_t tmp = *header;
    tmp.checksum = 0;

    const unsigned char *p = (const unsigned char *) &tmp;
    uint32_t hash = 2166136261u;

    for (size_t i = 0; i < sizeof(myfs_journal_header_t); i++) {
        hash ^= p[i];
        hash *= 16777619u;
    }

    return hash;
}


/*
 * 检查 journal 区是否足够容纳最大 entry 数。
 */
static int check_journal_capacity(void) {
    myfs_superblock_t *sb = get_sb();

    if (sb == nullptr) {
        return MYFS_ERR_NOT_MOUNTED;
    }

    if (sb->journal_blocks < 2) {
        return MYFS_ERR_NO_SPACE;
    }

    if (MYFS_JOURNAL_MAX_ENTRIES > sb->journal_blocks - 1) {
        return MYFS_ERR_NO_SPACE;
    }

    return MYFS_OK;
}


/*
 * 写 journal header。
 *
 * 注意：
 * 写 journal header 必须直接写磁盘，不能再经过 journal。
 */
static int write_journal_header(myfs_journal_header_t *header) {
    myfs_superblock_t *sb = get_sb();

    if (sb == nullptr) {
        return MYFS_ERR_NOT_MOUNTED;
    }

    unsigned char block[MYFS_BLOCK_SIZE];

    memset(block, 0, sizeof(block));

    header->checksum = calc_header_checksum(header);

    memcpy(block, header, sizeof(myfs_journal_header_t));

    return myfs_disk_write_block(sb->journal_start, block);
}


/*
 * 读取 journal header。
 */
static int read_journal_header(myfs_journal_header_t *header) {
    if (header == nullptr) {
        return MYFS_ERR_INVALID_ARG;
    }

    myfs_superblock_t *sb = get_sb();

    if (sb == nullptr) {
        return MYFS_ERR_NOT_MOUNTED;
    }

    unsigned char block[MYFS_BLOCK_SIZE];

    int ret = myfs_disk_read_block(sb->journal_start, block);
    if (ret != MYFS_OK) {
        return ret;
    }

    memcpy(header, block, sizeof(myfs_journal_header_t));

    return MYFS_OK;
}


/*
 * 清空 journal header。
 */
static int clear_journal_header(void) {
    myfs_journal_header_t header;

    memset(&header, 0, sizeof(header));

    header.magic = MYFS_JOURNAL_MAGIC;
    header.state = MYFS_JOURNAL_CLEAN;
    header.txid = g_next_txid;
    header.entry_count = 0;

    return write_journal_header(&header);
}


int myfs_journal_format(void) {
    int ret = check_journal_capacity();
    if (ret != MYFS_OK) {
        return ret;
    }

    g_journal_active = 0;
    g_entries.clear();

    return clear_journal_header();
}


int myfs_journal_begin(void) {
    int ret = check_journal_capacity();
    if (ret != MYFS_OK) {
        return ret;
    }

    if (g_journal_active) {
        return MYFS_ERR_INVALID_ARG;
    }

    g_entries.clear();
    g_journal_active = 1;

    return MYFS_OK;
}


int myfs_journal_log_block(myfs_block_t target_block, const void *buf) {
    if (!g_journal_active) {
        return MYFS_ERR_INVALID_ARG;
    }

    if (buf == nullptr) {
        return MYFS_ERR_INVALID_ARG;
    }

    if (g_entries.size() >= MYFS_JOURNAL_MAX_ENTRIES) {
        return MYFS_ERR_NO_SPACE;
    }

    /*
     * 如果同一个目标块在同一事务中重复记录，
     * 只保留最后一次内容。
     */
    for (auto &entry : g_entries) {
        if (entry.target_block == target_block) {
            memcpy(entry.data.data(), buf, MYFS_BLOCK_SIZE);
            return MYFS_OK;
        }
    }

    JournalMemEntry entry;

    entry.target_block = target_block;
    memcpy(entry.data.data(), buf, MYFS_BLOCK_SIZE);

    g_entries.push_back(entry);

    return MYFS_OK;
}


int myfs_journal_commit(void) {
    if (!g_journal_active) {
        return MYFS_ERR_INVALID_ARG;
    }

    myfs_superblock_t *sb = get_sb();

    if (sb == nullptr) {
        return MYFS_ERR_NOT_MOUNTED;
    }

    if (g_entries.empty()) {
        g_journal_active = 0;
        return MYFS_OK;
    }

    if (g_entries.size() > MYFS_JOURNAL_MAX_ENTRIES ||
        g_entries.size() > sb->journal_blocks - 1) {
        return MYFS_ERR_NO_SPACE;
    }

    /*
     * 第一步：
     * 先把每个新块内容写入 journal 数据区。
     */
    for (size_t i = 0; i < g_entries.size(); i++) {
        myfs_block_t journal_block = sb->journal_start + 1 + (myfs_block_t) i;

        int ret = myfs_disk_write_block(
                journal_block,
                g_entries[i].data.data()
        );

        if (ret != MYFS_OK) {
            return ret;
        }
    }

    /*
     * 第二步：
     * 写入 committed header。
     *
     * 一旦 header 写入成功，说明该事务必须在崩溃恢复时被重放。
     */
    myfs_journal_header_t header;

    memset(&header, 0, sizeof(header));

    header.magic = MYFS_JOURNAL_MAGIC;
    header.state = MYFS_JOURNAL_COMMITTED;
    header.txid = g_next_txid++;
    header.entry_count = (uint32_t) g_entries.size();

    for (size_t i = 0; i < g_entries.size(); i++) {
        header.entries[i].target_block = g_entries[i].target_block;
        header.entries[i].journal_block =
                sb->journal_start + 1 + (myfs_block_t) i;
        header.entries[i].flags = 0;
    }

    int ret = write_journal_header(&header);
    if (ret != MYFS_OK) {
        return ret;
    }

    /*
     * 第三步：
     * 把 journal 中记录的新内容写到真实位置。
     */
    for (size_t i = 0; i < g_entries.size(); i++) {
        ret = myfs_disk_write_block(
                g_entries[i].target_block,
                g_entries[i].data.data()
        );

        if (ret != MYFS_OK) {
            return ret;
        }

        /*
         * 关键：journal 直接写磁盘绕过了缓存。
         * 必须 invalidate 缓存中的旧数据（如 myfs_block_zero 写入的全 0），
         * 否则后续缓存刷回时会用旧数据覆盖磁盘上的正确数据。
         */
        if (myfs_cache_is_initialized()) {
            myfs_cache_invalidate_block(g_entries[i].target_block);
        }
    }

    /*
     * 第四步：
     * 清空 journal header，表示事务已经完成。
     */
    ret = clear_journal_header();
    if (ret != MYFS_OK) {
        return ret;
    }

    g_entries.clear();
    g_journal_active = 0;

    return MYFS_OK;
}


int myfs_journal_abort(void) {
    if (!g_journal_active) {
        return MYFS_OK;
    }

    g_entries.clear();
    g_journal_active = 0;

    return clear_journal_header();
}


int myfs_journal_recover(void) {
    myfs_superblock_t *sb = get_sb();

    if (sb == nullptr) {
        return MYFS_ERR_NOT_MOUNTED;
    }

    myfs_journal_header_t header;

    int ret = read_journal_header(&header);
    if (ret != MYFS_OK) {
        return ret;
    }

    /*
     * 全 0 journal 区也视为 clean。
     */
    if (header.magic == 0) {
        return clear_journal_header();
    }

    if (header.magic != MYFS_JOURNAL_MAGIC) {
        return MYFS_ERR_CORRUPTED;
    }

    uint32_t expected = calc_header_checksum(&header);
    if (expected != header.checksum) {
        return MYFS_ERR_CHECKSUM;
    }

    if (header.state == MYFS_JOURNAL_CLEAN) {
        return MYFS_OK;
    }

    if (header.state != MYFS_JOURNAL_COMMITTED) {
        return MYFS_ERR_CORRUPTED;
    }

    if (header.entry_count > MYFS_JOURNAL_MAX_ENTRIES ||
        header.entry_count > sb->journal_blocks - 1) {
        return MYFS_ERR_CORRUPTED;
    }

    /*
     * 重放事务。
     */
    unsigned char block[MYFS_BLOCK_SIZE];

    for (uint32_t i = 0; i < header.entry_count; i++) {
        myfs_block_t journal_block = header.entries[i].journal_block;
        myfs_block_t target_block = header.entries[i].target_block;

        if (journal_block <= sb->journal_start ||
            journal_block >= sb->journal_start + sb->journal_blocks) {
            return MYFS_ERR_CORRUPTED;
        }

        if (target_block >= sb->total_blocks) {
            return MYFS_ERR_CORRUPTED;
        }

        ret = myfs_disk_read_block(journal_block, block);
        if (ret != MYFS_OK) {
            return ret;
        }

        ret = myfs_disk_write_block(target_block, block);
        if (ret != MYFS_OK) {
            return ret;
        }

        /*
         * 关键：journal 恢复直接写磁盘绕过了缓存。
         * 必须 invalidate 缓存中的旧数据，避免缓存刷回时覆盖。
         */
        if (myfs_cache_is_initialized()) {
            myfs_cache_invalidate_block(target_block);
        }
    }

    /*
     * 重放完成后清空 journal。
     */
    return clear_journal_header();
}


int myfs_journal_write_metadata_block(myfs_block_t block_id, const void *buf) {
    if (buf == nullptr) {
        return MYFS_ERR_INVALID_ARG;
    }

    int ret = myfs_journal_begin();
    if (ret != MYFS_OK) {
        return ret;
    }

    ret = myfs_journal_log_block(block_id, buf);
    if (ret != MYFS_OK) {
        myfs_journal_abort();
        return ret;
    }

    ret = myfs_journal_commit();
    if (ret != MYFS_OK) {
        myfs_journal_abort();
        return ret;
    }

    return MYFS_OK;
}


int myfs_journal_is_clean(int *is_clean) {
    if (is_clean == nullptr) {
        return MYFS_ERR_INVALID_ARG;
    }

    myfs_journal_header_t header;

    int ret = read_journal_header(&header);
    if (ret != MYFS_OK) {
        return ret;
    }

    if (header.magic == 0) {
        *is_clean = 1;
        return MYFS_OK;
    }

    if (header.magic != MYFS_JOURNAL_MAGIC) {
        return MYFS_ERR_CORRUPTED;
    }

    *is_clean = header.state == MYFS_JOURNAL_CLEAN;

    return MYFS_OK;
}


int myfs_journal_debug_create_committed_record(
        myfs_block_t target_block,
        const void *buf
) {
    if (buf == nullptr) {
        return MYFS_ERR_INVALID_ARG;
    }

    myfs_superblock_t *sb = get_sb();

    if (sb == nullptr) {
        return MYFS_ERR_NOT_MOUNTED;
    }

    if (sb->journal_blocks < 2) {
        return MYFS_ERR_NO_SPACE;
    }

    myfs_block_t journal_data_block = sb->journal_start + 1;

    int ret = myfs_disk_write_block(journal_data_block, buf);
    if (ret != MYFS_OK) {
        return ret;
    }

    myfs_journal_header_t header;

    memset(&header, 0, sizeof(header));

    header.magic = MYFS_JOURNAL_MAGIC;
    header.state = MYFS_JOURNAL_COMMITTED;
    header.txid = g_next_txid++;
    header.entry_count = 1;

    header.entries[0].target_block = target_block;
    header.entries[0].journal_block = journal_data_block;
    header.entries[0].flags = 0;

    return write_journal_header(&header);
}