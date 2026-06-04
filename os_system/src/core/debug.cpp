#include "debug.h"

#include "common.h"
#include "config.h"
#include "error.h"
#include "superblock.h"
#include "inode.h"
#include "block_alloc.h"
#include "block_map.h"
#include "disk.h"
#include "cache.h"
#include "journal.h"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <unordered_set>
#include <vector>

/*
 * debug.cpp
 * ------------------------------------------------------------
 * MYFS 物理层调试与统计接口实现。
 *
 * 本模块用于向上层或测试程序提供底层状态观察能力。
 *
 * 它不改变文件系统状态，主要做：
 *
 * 1. 读取超级块
 * 2. 读取 inode
 * 3. 查询 block map
 * 4. 打印 free_group
 * 5. 打印 cache stats
 * 6. hexdump 物理块
 */


/*
 * 将 fs_state 转换为字符串。
 */
static const char *state_to_string(uint32_t state) {
    if (state == MYFS_STATE_CLEAN) {
        return "CLEAN";
    }

    if (state == MYFS_STATE_DIRTY) {
        return "DIRTY";
    }

    return "UNKNOWN";
}


/*
 * 将 inode 类型转换为字符串。
 */
static const char *inode_type_to_string(uint16_t type) {
    switch (type) {
        case MYFS_INODE_FREE:
            return "FREE";
        case MYFS_INODE_FILE:
            return "FILE";
        case MYFS_INODE_DIR:
            return "DIR";
        case MYFS_INODE_SYMLINK:
            return "SYMLINK";
        default:
            return "UNKNOWN";
    }
}


int myfs_statfs(myfs_statfs_t *out) {
    if (out == nullptr) {
        return MYFS_ERR_INVALID_ARG;
    }

    myfs_superblock_t *sb = myfs_super_get();

    if (sb == nullptr) {
        return MYFS_ERR_NOT_MOUNTED;
    }

    memset(out, 0, sizeof(myfs_statfs_t));

    out->magic = sb->magic;
    out->version = sb->version;
    out->block_size = sb->block_size;

    out->total_blocks = sb->total_blocks;
    out->total_inodes = sb->total_inodes;

    out->free_inodes = sb->free_inodes_count;
    out->used_inodes = sb->total_inodes - sb->free_inodes_count;

    out->data_block_start = sb->data_block_start;
    out->data_blocks = sb->data_blocks;
    out->free_blocks = sb->free_blocks_count;
    out->used_blocks = sb->data_blocks - sb->free_blocks_count;

    out->inode_bitmap_start = sb->inode_bitmap_start;
    out->inode_bitmap_blocks = sb->inode_bitmap_blocks;

    out->inode_table_start = sb->inode_table_start;
    out->inode_table_blocks = sb->inode_table_blocks;

    out->journal_start = sb->journal_start;
    out->journal_blocks = sb->journal_blocks;

    out->root_inode = sb->root_inode;
    out->fs_state = sb->fs_state;
    out->mount_count = sb->mount_count;

    return MYFS_OK;
}


int myfs_debug_print_statfs(void) {
    myfs_statfs_t st;

    int ret = myfs_statfs(&st);
    if (ret != MYFS_OK) {
        return ret;
    }

    printf("\n========== MYFS STATFS ==========\n");
    printf("magic               = 0x%x\n", st.magic);
    printf("version             = %u\n", st.version);
    printf("block_size          = %u\n", st.block_size);

    printf("total_blocks        = %u\n", st.total_blocks);
    printf("total_inodes        = %u\n", st.total_inodes);

    printf("used_inodes         = %u\n", st.used_inodes);
    printf("free_inodes         = %u\n", st.free_inodes);

    printf("data_block_start    = %u\n", st.data_block_start);
    printf("data_blocks         = %u\n", st.data_blocks);
    printf("used_blocks         = %u\n", st.used_blocks);
    printf("free_blocks         = %u\n", st.free_blocks);

    printf("inode_bitmap_start  = %u\n", st.inode_bitmap_start);
    printf("inode_bitmap_blocks = %u\n", st.inode_bitmap_blocks);

    printf("inode_table_start   = %u\n", st.inode_table_start);
    printf("inode_table_blocks  = %u\n", st.inode_table_blocks);

    printf("journal_start       = %u\n", st.journal_start);
    printf("journal_blocks      = %u\n", st.journal_blocks);

    printf("root_inode          = %u\n", st.root_inode);
    printf("fs_state            = %u (%s)\n", st.fs_state, state_to_string(st.fs_state));
    printf("mount_count         = %u\n", st.mount_count);
    printf("=================================\n\n");

    return MYFS_OK;
}


int myfs_debug_dump_super(void) {
    myfs_superblock_t *sb = myfs_super_get();

    if (sb == nullptr) {
        return MYFS_ERR_NOT_MOUNTED;
    }

    printf("\n========== MYFS SUPERBLOCK ==========\n");

    printf("magic               = 0x%x\n", sb->magic);
    printf("version             = %u\n", sb->version);
    printf("block_size          = %u\n", sb->block_size);
    printf("total_blocks        = %u\n", sb->total_blocks);

    printf("total_inodes        = %u\n", sb->total_inodes);
    printf("free_inodes_count   = %u\n", sb->free_inodes_count);

    printf("free_blocks_count   = %u\n", sb->free_blocks_count);

    printf("inode_bitmap_start  = %u\n", sb->inode_bitmap_start);
    printf("inode_bitmap_blocks = %u\n", sb->inode_bitmap_blocks);

    printf("inode_table_start   = %u\n", sb->inode_table_start);
    printf("inode_table_blocks  = %u\n", sb->inode_table_blocks);

    printf("journal_start       = %u\n", sb->journal_start);
    printf("journal_blocks      = %u\n", sb->journal_blocks);

    printf("data_block_start    = %u\n", sb->data_block_start);
    printf("data_blocks         = %u\n", sb->data_blocks);

    printf("root_inode          = %u\n", sb->root_inode);

    printf("free_group_count    = %u\n", sb->free_group_count);

    printf("fs_state            = %u (%s)\n", sb->fs_state, state_to_string(sb->fs_state));
    printf("mount_count         = %u\n", sb->mount_count);

    printf("last_mount_time     = %llu\n", (unsigned long long) sb->last_mount_time);
    printf("last_write_time     = %llu\n", (unsigned long long) sb->last_write_time);

    printf("checksum            = 0x%x\n", sb->checksum);

    printf("=====================================\n\n");

    return MYFS_OK;
}


int myfs_debug_dump_inode(myfs_ino_t inode_id) {
    myfs_inode_t inode;

    int ret = myfs_inode_read(inode_id, &inode);
    if (ret != MYFS_OK) {
        return ret;
    }

    printf("\n========== MYFS INODE %u ==========\n", inode_id);

    printf("inode_id            = %u\n", inode.inode_id);
    printf("type                = %u (%s)\n", inode.type, inode_type_to_string(inode.type));
    printf("mode                = %o\n", inode.mode);

    printf("uid                 = %u\n", inode.uid);
    printf("gid                 = %u\n", inode.gid);

    printf("size                = %llu\n", (unsigned long long) inode.size);
    printf("block_count         = %u\n", inode.block_count);

    printf("link_count          = %u\n", inode.link_count);
    printf("open_count          = %u\n", inode.open_count);

    {
        char time_buf[32];
        time_t t;
        t = (time_t)inode.atime;  strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", localtime(&t));
        printf("atime               = %s\n", time_buf);
        t = (time_t)inode.mtime;  strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", localtime(&t));
        printf("mtime               = %s\n", time_buf);
        t = (time_t)inode.ctime;  strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", localtime(&t));
        printf("ctime               = %s\n", time_buf);
        t = (time_t)inode.crtime; strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", localtime(&t));
        printf("crtime              = %s\n", time_buf);
    }

    printf("direct blocks:\n");
    for (uint32_t i = 0; i < MYFS_DIRECT_BLOCKS; i++) {
        printf("  direct[%02u]        = %u\n", i, inode.direct[i]);
    }

    /*
     * 打印一级间接块信息，并同步到上层。
     */
    printf("indirect1           = %u\n", inode.indirect1);
    if (inode.indirect1 != 0) {
        uint32_t ind1_buf[MYFS_POINTERS_PER_BLOCK];
        memset(ind1_buf, 0, sizeof(ind1_buf));
        int r = myfs_cache_read_block(inode.indirect1, ind1_buf);
        if (r == MYFS_OK) {
            printf("[SYNC] INODE_INDIRECT1 %u %u", inode_id, inode.indirect1);
            for (uint32_t i = 0; i < 16 && i < MYFS_POINTERS_PER_BLOCK; i++) {
                printf(" %u", ind1_buf[i]);
            }
            printf("\n");
        }
    }

    /*
     * 打印二级间接块信息，并同步到上层。
     */
    printf("indirect2           = %u\n", inode.indirect2);
    if (inode.indirect2 != 0) {
        uint32_t ind2_buf[MYFS_POINTERS_PER_BLOCK];
        memset(ind2_buf, 0, sizeof(ind2_buf));
        int r = myfs_cache_read_block(inode.indirect2, ind2_buf);
        if (r == MYFS_OK) {
            printf("[SYNC] INODE_INDIRECT2 %u %u", inode_id, inode.indirect2);
            for (uint32_t i = 0; i < MYFS_POINTERS_PER_BLOCK; i++) {
                if (ind2_buf[i] != 0) {
                    /* 输出格式：L1块号:数据块1:数据块2... */
                    printf(" %u", ind2_buf[i]);
                    uint32_t l1_buf[MYFS_POINTERS_PER_BLOCK];
                    memset(l1_buf, 0, sizeof(l1_buf));
                    if (myfs_cache_read_block(ind2_buf[i], l1_buf) == MYFS_OK) {
                        for (uint32_t j = 0; j < MYFS_POINTERS_PER_BLOCK; j++) {
                            if (l1_buf[j] != 0) {
                                printf(":%u", l1_buf[j]);
                            }
                        }
                    }
                }
            }
            printf("\n");
        }
    }

    // printf("flags               = 0x%x\n", inode.flags);
    printf("checksum            = 0x%x\n", inode.checksum);

    printf("===================================\n\n");

    return MYFS_OK;
}


int myfs_debug_bmap(
        myfs_ino_t inode_id,
        uint32_t logical_block
) {
    myfs_block_t physical_block = 0;
    int is_hole = 0;

    int ret = myfs_inode_bmap(
            inode_id,
            logical_block,
            &physical_block,
            &is_hole
    );

    if (ret != MYFS_OK) {
        return ret;
    }

    printf("\n========== MYFS BMAP ==========\n");
    printf("inode_id       = %u\n", inode_id);
    printf("logical_block  = %u\n", logical_block);

    if (is_hole) {
        printf("mapping        = HOLE\n");
    } else {
        printf("physical_block = %u\n", physical_block);
    }

    printf("===============================\n\n");

    return MYFS_OK;
}


int myfs_debug_dump_free_group(void) {
    myfs_superblock_t *sb = myfs_super_get();

    if (sb == nullptr) {
        return MYFS_ERR_NOT_MOUNTED;
    }

    printf("\n========== MYFS FREE GROUP ==========\n");

    printf("free_blocks_count = %u\n", sb->free_blocks_count);
    printf("free_group_count  = %u\n", sb->free_group_count);

    printf("current superblock free_group:\n");

    for (uint32_t i = 0; i < sb->free_group_count; i++) {
        printf("  free_group[%03u] = %u\n", i, sb->free_group[i]);
    }

    printf("=====================================\n\n");

    return MYFS_OK;
}


int myfs_debug_dump_cache(void) {
    if (!myfs_cache_is_initialized()) {
        printf("\n========== MYFS CACHE ==========\n");
        printf("cache is not initialized\n");
        printf("===============================\n\n");
        return MYFS_OK;
    }

    myfs_cache_stats_t stats;

    myfs_cache_get_stats(&stats);

    printf("\n========== MYFS CACHE STATS ==========\n");

    printf("read_count        = %llu\n", (unsigned long long) stats.read_count);
    printf("write_count       = %llu\n", (unsigned long long) stats.write_count);
    printf("hit_count         = %llu\n", (unsigned long long) stats.hit_count);
    printf("miss_count        = %llu\n", (unsigned long long) stats.miss_count);
    printf("evict_count       = %llu\n", (unsigned long long) stats.evict_count);
    printf("dirty_flush_count = %llu\n", (unsigned long long) stats.dirty_flush_count);

    printf("======================================\n\n");

    return MYFS_OK;
}

int myfs_debug_recover_blocks(void) {
    myfs_superblock_t *sb = myfs_super_get();

    if (sb == nullptr) {
        return MYFS_ERR_NOT_MOUNTED;
    }

    printf("\n========== MYFS BLOCK RECOVERY ==========\n");
    printf("Step 1: Reading journal entries to recover block mappings...\n");

    /* 第一步：从journal中读取entries，恢复block映射 */
    /* journal.recover()已经恢复了inode位图，现在需要恢复block映射 */
    
    /* 读取journal header */
    unsigned char journal_block_data[MYFS_BLOCK_SIZE];
    int ret = myfs_disk_read_block(sb->journal_start, journal_block_data);
    if (ret != MYFS_OK) {
        printf("[RECOVER] Failed to read journal header: %d\n", ret);
        return ret;
    }

    myfs_journal_header_t *header = (myfs_journal_header_t *)journal_block_data;
    
    if (header->magic != MYFS_JOURNAL_MAGIC) {
        printf("[RECOVER] Journal header magic mismatch\n");
        return MYFS_ERR_CORRUPTED;
    }

    printf("[RECOVER] Journal entries: %u\n", header->entry_count);

    /* 收集所有journal entries中的target_block */
    std::unordered_set<myfs_block_t> journal_blocks;
    uint32_t journal_block_count = 0;

    for (uint32_t i = 0; i < header->entry_count; i++) {
        myfs_block_t target_block = header->entries[i].target_block;
        myfs_block_t journal_block = header->entries[i].journal_block;
        
        printf("[RECOVER] Entry %u: target=%u, journal=%u\n", 
               i, target_block, journal_block);
        
        if (target_block != 0) {
            journal_blocks.insert(target_block);
            journal_block_count++;
        }
    }

    printf("\nStep 2: Scanning all inodes to collect used blocks...\n");

    /* 第二步：收集所有被inode引用的块 */
    std::unordered_set<myfs_block_t> used_blocks;
    uint32_t total_inodes = sb->total_inodes;
    uint32_t recovered_count = 0;

    for (uint32_t inode_id = 0; inode_id < total_inodes; inode_id++) {
        myfs_inode_t inode;
        int ret = myfs_inode_read(inode_id, &inode);

        if (ret != MYFS_OK || inode.type == MYFS_INODE_FREE) {
            continue;
        }

        printf("[RECOVER] INODE %u type=%u size=%llu blocks=%llu link_count=%u\n",
               inode_id, inode.type, (unsigned long long)inode.size, (unsigned long long)inode.block_count, inode.link_count);

        printf("[SYNC] INODE_INFO %u %u %llu %llu %u %u %u",
               inode_id, inode.type, (unsigned long long)inode.size, (unsigned long long)inode.block_count,
               inode.link_count, inode.open_count, inode.checksum);

        /* 收集direct blocks */
        for (uint32_t i = 0; i < 12; i++) {
            if (inode.direct[i] != 0) {
                printf(" %u", inode.direct[i]);
                used_blocks.insert(inode.direct[i]);
            }
        }
        printf("\n");

        /* 收集indirect1 blocks */
        if (inode.indirect1 != 0) {
            used_blocks.insert(inode.indirect1);

            uint32_t ind1_buf[MYFS_POINTERS_PER_BLOCK];
            memset(ind1_buf, 0, sizeof(ind1_buf));
            int r = myfs_cache_read_block(inode.indirect1, ind1_buf);
            if (r == MYFS_OK) {
                printf("[SYNC] INODE_INDIRECT1 %u %u", inode_id, inode.indirect1);
                /* 向前端输出前 16 项（含 0），保证位置正确 */
                for (uint32_t i = 0; i < 16 && i < MYFS_POINTERS_PER_BLOCK; i++) {
                    printf(" %u", ind1_buf[i]);
                }
                printf("\n");
                /* 收集全部非零条目到 used_blocks */
                for (uint32_t i = 0; i < MYFS_POINTERS_PER_BLOCK; i++) {
                    if (ind1_buf[i] != 0) {
                        used_blocks.insert(ind1_buf[i]);
                    }
                }
            }
        }

        /* 收集indirect2 blocks */
        if (inode.indirect2 != 0) {
            used_blocks.insert(inode.indirect2);

            uint32_t ind2_buf[MYFS_POINTERS_PER_BLOCK];
            memset(ind2_buf, 0, sizeof(ind2_buf));
            int r = myfs_cache_read_block(inode.indirect2, ind2_buf);
            if (r == MYFS_OK) {
                printf("[SYNC] INODE_INDIRECT2 %u %u", inode_id, inode.indirect2);
                /* 向前端输出前 16 项 */
                for (uint32_t i = 0; i < 16 && i < MYFS_POINTERS_PER_BLOCK; i++) {
                    if (ind2_buf[i] != 0) {
                        printf(" %u", ind2_buf[i]);
                    }
                }
                /* 收集全部条目到 used_blocks */
                for (uint32_t i = 0; i < MYFS_POINTERS_PER_BLOCK; i++) {
                    if (ind2_buf[i] != 0) {
                        used_blocks.insert(ind2_buf[i]);

                        /* 收集 L1 块指向的数据块 */
                        uint32_t l1_buf[MYFS_POINTERS_PER_BLOCK];
                        memset(l1_buf, 0, sizeof(l1_buf));
                        if (myfs_cache_read_block(ind2_buf[i], l1_buf) == MYFS_OK) {
                            for (uint32_t j = 0; j < MYFS_POINTERS_PER_BLOCK; j++) {
                                if (l1_buf[j] != 0) {
                                    used_blocks.insert(l1_buf[j]);
                                }
                            }
                        }
                    }
                }
                printf("\n");
            }
        }

        recovered_count++;
    }

    /* 第三步：将journal blocks加入到used_blocks中 */
    printf("\nStep 3: Adding journal blocks to used set...\n");
    uint32_t journal_blocks_added = 0;
    
    for (myfs_block_t block : journal_blocks) {
        if (used_blocks.find(block) == used_blocks.end()) {
            used_blocks.insert(block);
            journal_blocks_added++;
            printf("[RECOVER] Added journal block %u to used set\n", block);
        }
    }

    printf("\nStep 4: Collecting free blocks from group chain...\n");
    std::unordered_set<myfs_block_t> free_blocks;

    if (sb->free_group_count > 0 && sb->free_group_count <= MYFS_FREE_GROUP_SIZE) {
        std::vector<myfs_block_t> current_group;
        for (uint32_t i = 0; i < sb->free_group_count; i++) {
            current_group.push_back(sb->free_group[i]);
        }

        std::unordered_set<myfs_block_t> visited;
        uint32_t counted = 0;

        while (!current_group.empty() && counted < sb->free_blocks_count) {
            uint32_t group_count = (uint32_t)current_group.size();

            for (uint32_t i = 0; i < group_count; i++) {
                myfs_block_t blk = current_group[i];
                if (myfs_block_is_valid_data_block(blk)) {
                    free_blocks.insert(blk);
                    counted++;
                }
            }

            /* 检查第一个块是否指向下一组（成组链接：空闲组管理块始终在 group[0]）*/
            myfs_block_t next_group_block = current_group[0];
            if (visited.count(next_group_block)) {
                break; /* 防止环 */
            }
            visited.insert(next_group_block);

            /* 读取下一组 */
            if (counted < sb->free_blocks_count) {
                uint32_t next_group_buf[MYFS_BLOCK_SIZE / sizeof(uint32_t)];
                if (myfs_disk_read_block(next_group_block, next_group_buf) == MYFS_OK) {
                    current_group.clear();
                    uint32_t next_count = next_group_buf[0];
                    for (uint32_t i = 1; i <= next_count && i <= MYFS_FREE_GROUP_SIZE; i++) {
                        if (next_group_buf[i] != 0) {
                            current_group.push_back(next_group_buf[i]);
                        }
                    }
                } else {
                    break;
                }
            }
        }
    }

    /* 第五步：找出leaked blocks并释放它们 */
    printf("\nStep 5: Recovering leaked blocks to free chain...\n");
    uint32_t leaked_count = 0;

    for (myfs_block_t block = sb->data_block_start; block < sb->total_blocks; block++) {
        int used = used_blocks.count(block) != 0;
        int free = free_blocks.count(block) != 0;

        if (!used && !free) {
            /* 这是leaked block，释放它 */
            myfs_block_free(block);
            leaked_count++;
        }
    }

    /*
     * 第六步：修正 free_blocks_count。
     *
     * 如果崩溃导致超级块中的 free_blocks_count 与实际空闲链长度不一致，
     * 这里基于已用块数量计算出正确的空闲块数并写回超级块。
     */
    {
        uint32_t correct_free = sb->data_blocks - (uint32_t)used_blocks.size();

        if (correct_free != sb->free_blocks_count) {
            printf("\nStep 6: Correcting free_blocks_count: super=%u actual=%u\n",
                   sb->free_blocks_count, correct_free);
            sb->free_blocks_count = correct_free;
            myfs_super_sync();
        }
    }

    printf("\nRecovery complete:\n");
    printf("  - %u journal entries processed\n", journal_block_count);
    printf("  - %u journal blocks added to used set\n", journal_blocks_added);
    printf("  - %u inodes scanned\n", recovered_count);
    printf("  - %u used blocks collected\n", (uint32_t)used_blocks.size());
    printf("  - %u free blocks collected\n", (uint32_t)free_blocks.size());
    printf("  - %u leaked blocks recovered to free chain\n", leaked_count);
    printf("=========================================\n\n");

    return MYFS_OK;
}


int myfs_debug_hexdump_block(
        myfs_block_t block_id,
        uint32_t max_bytes
) {
    if (max_bytes == 0) {
        return MYFS_OK;
    }

    if (max_bytes > MYFS_BLOCK_SIZE) {
        max_bytes = MYFS_BLOCK_SIZE;
    }

    unsigned char buf[MYFS_BLOCK_SIZE];

    int ret = myfs_disk_read_block(block_id, buf);
    if (ret != MYFS_OK) {
        return ret;
    }

    printf("\n========== HEXDUMP BLOCK %u ==========\n", block_id);

    for (uint32_t i = 0; i < max_bytes; i++) {
        if (i % 16 == 0) {
            printf("%04x: ", i);
        }

        printf("%02x ", buf[i]);

        if (i % 16 == 15 || i + 1 == max_bytes) {
            printf("\n");
        }
    }

    printf("======================================\n\n");

    return MYFS_OK;
}

int myfs_debug_blockmap_range(
        myfs_block_t start_block,
        uint32_t count
) {
    myfs_superblock_t *sb = myfs_super_get();

    if (sb == nullptr) {
        return MYFS_ERR_NOT_MOUNTED;
    }

    if (count == 0) {
        printf("\n========== MYFS BLOCKMAP RANGE ==========\n");
        printf("Range:        block %u ~ %u (0 blocks)\n", start_block, start_block);
        printf("Used blocks:  0 / 0\n");
        printf("========================================\n\n");
        return MYFS_OK;
    }

    myfs_block_t end_block = start_block + (myfs_block_t) count;
    if (start_block < sb->data_block_start || end_block > sb->total_blocks) {
        return MYFS_ERR_INVALID_ARG;
    }

    /* Build the free block set by traversing the free chain */
    std::vector<uint8_t> is_free(count, 0);

    if (sb->free_blocks_count > 0 &&
        sb->free_group_count > 0 &&
        sb->free_group_count <= MYFS_FREE_GROUP_SIZE) {

        std::vector<myfs_block_t> current_group;
        current_group.reserve(sb->free_group_count);
        for (uint32_t i = 0; i < sb->free_group_count; i++) {
            current_group.push_back(sb->free_group[i]);
        }

        uint32_t counted = 0;
        std::unordered_set<myfs_block_t> visited_group_blocks;

        while (!current_group.empty() && counted < sb->free_blocks_count) {
            uint32_t group_count = (uint32_t) current_group.size();
            if (group_count == 0 || group_count > MYFS_FREE_GROUP_SIZE) {
                break;
            }

            for (uint32_t i = 0; i < group_count && counted < sb->free_blocks_count; i++) {
                myfs_block_t blk = current_group[i];
                if (blk >= start_block && blk < end_block) {
                    is_free[(size_t) (blk - start_block)] = 1;
                }
                counted++;
            }

            if (counted >= sb->free_blocks_count) {
                break;
            }

            myfs_block_t next_group_block = current_group[0];
            if (visited_group_blocks.count(next_group_block)) {
                break;
            }
            visited_group_blocks.insert(next_group_block);

            unsigned char buf[MYFS_BLOCK_SIZE];
            int ret = myfs_disk_read_block(next_group_block, buf);
            if (ret != MYFS_OK) {
                break;
            }

            myfs_free_group_block_t group_block;
            std::memset(&group_block, 0, sizeof(group_block));
            std::memcpy(&group_block, buf, sizeof(group_block));

            if (group_block.count == 0 || group_block.count > MYFS_FREE_GROUP_SIZE) {
                break;
            }

            current_group.clear();
            current_group.reserve(group_block.count);
            for (uint32_t i = 0; i < group_block.count; i++) {
                current_group.push_back(group_block.blocks[i]);
            }
        }
    }

    /* Collect used blocks */
    std::vector<myfs_block_t> used_list;
    for (uint32_t i = 0; i < count; i++) {
        if (is_free[i] == 0) {
            used_list.push_back(start_block + (myfs_block_t) i);
        }
    }

    uint32_t used_count = (uint32_t) used_list.size();
    uint32_t free_count = count - used_count;

    printf("\n========== MYFS BLOCKMAP RANGE ==========\n");
    printf("Range:        block %u ~ %u (%u blocks)\n",
           start_block, end_block - 1, count);

    if (count > 0) {
        printf("Used blocks:  %u / %u (%.2f%%)\n",
               used_count, count,
               (double) used_count / (double) count * 100.0);
        printf("Free blocks:  %u / %u (%.2f%%)\n",
               free_count, count,
               (double) free_count / (double) count * 100.0);
    }

    if (used_count > 0) {
        printf("\nUsed block list:\n");
        for (uint32_t i = 0; i < used_count; i++) {
            printf("  %-8u", used_list[i]);
            if ((i + 1) % 8 == 0 || i + 1 == used_count) {
                printf("\n");
            }
        }
    } else {
        printf("\nAll blocks in this range are FREE.\n");
    }

    printf("========================================\n\n");

    return MYFS_OK;
}