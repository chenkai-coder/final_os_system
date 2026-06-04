#include "physical_api.h"

#include "mount.h"
#include "inode.h"
#include "raw_file.h"
#include "block_map.h"
#include "block_alloc.h"
#include "debug.h"
#include "fsck.h"
#include "cache.h"
#include "sync.h"
#include "error.h"
#include "superblock.h"
#include "disk.h"

#include <cstring>
#include <vector>
#include <unordered_set>

/*
 * physical_api.cpp
 * ------------------------------------------------------------
 * MYFS 物理层统一对外接口实现。
 *
 * 该文件主要做一件事：
 *
 * 把底层多个模块的函数包装成稳定接口，提供给上层逻辑模块使用。
 *
 * 上层不需要关心：
 *
 * disk.cpp
 * superblock.cpp
 * inode.cpp
 * block_alloc.cpp
 * block_map.cpp
 * raw_file.cpp
 * cache.cpp
 * fsck.cpp
 * journal.cpp
 *
 * 上层只需要调用 physical_api.h 中声明的函数。
 */


/* ============================================================
 * 1. 文件系统生命周期接口
 * ============================================================
 */

int myfs_phys_format(
        const char *disk_path,
        uint32_t total_blocks,
        uint32_t total_inodes
) {
    return myfs_mkfs(disk_path, total_blocks, total_inodes);
}


int myfs_phys_mount(const char *disk_path) {
    return myfs_mount(disk_path);
}


int myfs_phys_umount(void) {
    /*
     * 先尝试同步缓存和超级块。
     *
     * 如果没有初始化缓存，myfs_sync 内部会处理。
     */
    myfs_sync();

    /*
     * 如果缓存已经初始化，关闭缓存。
     */
    if (myfs_cache_is_initialized()) {
        int ret = myfs_cache_shutdown();
        if (ret != MYFS_OK) {
            return ret;
        }
    }

    return myfs_umount();
}


int myfs_phys_is_mounted(void) {
    return myfs_is_mounted();
}


/* ============================================================
 * 2. inode 管理接口
 * ============================================================
 */

int myfs_phys_inode_create(
        uint16_t type,
        uint16_t mode,
        uint32_t uid,
        uint32_t gid,
        myfs_ino_t *inode_id
) {
    return myfs_inode_alloc(
            type,
            mode,
            uid,
            gid,
            inode_id
    );
}


int myfs_phys_inode_free(myfs_ino_t inode_id) {
    return myfs_inode_free(inode_id);
}


int myfs_phys_inode_get_info(
        myfs_ino_t inode_id,
        myfs_phys_inode_info_t *info
) {
    if (info == nullptr) {
        return MYFS_ERR_INVALID_ARG;
    }

    myfs_inode_t inode;

    int ret = myfs_inode_read(inode_id, &inode);
    if (ret != MYFS_OK) {
        return ret;
    }

    memset(info, 0, sizeof(myfs_phys_inode_info_t));

    info->inode_id = inode.inode_id;
    info->type = inode.type;
    info->mode = inode.mode;

    info->uid = inode.uid;
    info->gid = inode.gid;

    info->size = inode.size;
    info->block_count = inode.block_count;

    info->link_count = inode.link_count;
    info->open_count = inode.open_count;

    info->atime = inode.atime;
    info->mtime = inode.mtime;
    info->ctime = inode.ctime;
    info->crtime = inode.crtime;

    return MYFS_OK;
}


int myfs_phys_inode_set_attr(
        myfs_ino_t inode_id,
        uint16_t mode,
        uint32_t uid,
        uint32_t gid
) {
    myfs_inode_t inode;

    int ret = myfs_inode_read(inode_id, &inode);
    if (ret != MYFS_OK) {
        return ret;
    }

    inode.mode = mode;
    inode.uid = uid;
    inode.gid = gid;

    return myfs_inode_write(inode_id, &inode);
}

int myfs_phys_get_root_inode(myfs_ino_t *root_inode) {
    if (root_inode == nullptr) {
        return MYFS_ERR_INVALID_ARG;
    }

    myfs_phys_statfs_t st;

    int ret = myfs_phys_statfs(&st);
    if (ret != MYFS_OK) {
        return ret;
    }

    *root_inode = st.root_inode;

    return MYFS_OK;
}

int myfs_phys_inode_is_file(myfs_ino_t inode_id) {
    myfs_phys_inode_info_t info;

    int ret = myfs_phys_inode_get_info(inode_id, &info);
    if (ret != MYFS_OK) {
        return 0;
    }

    return info.type == MYFS_PHYS_INODE_FILE;
}

int myfs_phys_inode_is_dir(myfs_ino_t inode_id) {
    myfs_phys_inode_info_t info;

    int ret = myfs_phys_inode_get_info(inode_id, &info);
    if (ret != MYFS_OK) {
        return 0;
    }

    return info.type == MYFS_PHYS_INODE_DIR;
}

int myfs_phys_inode_is_symlink(myfs_ino_t inode_id) {
    myfs_phys_inode_info_t info;

    int ret = myfs_phys_inode_get_info(inode_id, &info);
    if (ret != MYFS_OK) {
        return 0;
    }

    return info.type == MYFS_PHYS_INODE_SYMLINK;
}

int myfs_phys_inode_inc_link(myfs_ino_t inode_id) {
    return myfs_inode_inc_link(inode_id);
}


int myfs_phys_inode_dec_link(myfs_ino_t inode_id) {
    return myfs_inode_dec_link(inode_id);
}


int myfs_phys_inode_inc_open(myfs_ino_t inode_id) {
    return myfs_inode_inc_open(inode_id);
}


int myfs_phys_inode_dec_open(myfs_ino_t inode_id) {
    return myfs_inode_dec_open(inode_id);
}


/* ============================================================
 * 3. inode 裸数据读写接口
 * ============================================================
 */

int myfs_phys_read(
        myfs_ino_t inode_id,
        uint64_t offset,
        void *buf,
        uint32_t size,
        uint32_t *bytes_read
) {
    return myfs_inode_read_data(
            inode_id,
            offset,
            buf,
            size,
            bytes_read
    );
}


int myfs_phys_write(
        myfs_ino_t inode_id,
        uint64_t offset,
        const void *buf,
        uint32_t size,
        uint32_t *bytes_written
) {
    return myfs_inode_write_data(
            inode_id,
            offset,
            buf,
            size,
            bytes_written
    );
}


int myfs_phys_truncate(
        myfs_ino_t inode_id,
        uint64_t new_size
) {
    return myfs_inode_truncate_data(
            inode_id,
            new_size
    );
}


int myfs_phys_zero_range(
        myfs_ino_t inode_id,
        uint64_t offset,
        uint64_t length
) {
    return myfs_inode_zero_range(
            inode_id,
            offset,
            length
    );
}


/* ============================================================
 * 4. 逻辑块映射接口
 * ============================================================
 */

int myfs_phys_bmap(
        myfs_ino_t inode_id,
        uint32_t logical_block,
        myfs_phys_bmap_result_t *result
) {
    if (result == nullptr) {
        return MYFS_ERR_INVALID_ARG;
    }

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

    result->inode_id = inode_id;
    result->logical_block = logical_block;
    result->is_hole = is_hole;
    result->physical_block = physical_block;

    return MYFS_OK;
}

int myfs_phys_cache_get_lru_list(uint32_t *blocks, uint32_t max_count, uint32_t *actual_count) {
    return myfs_cache_get_lru_list(blocks, max_count, actual_count);
}


/* ============================================================
 * 5. 统计和同步接口
 * ============================================================
 */

int myfs_phys_statfs(myfs_phys_statfs_t *out) {
    if (out == nullptr) {
        return MYFS_ERR_INVALID_ARG;
    }

    myfs_statfs_t st;

    int ret = myfs_statfs(&st);
    if (ret != MYFS_OK) {
        return ret;
    }

    memset(out, 0, sizeof(myfs_phys_statfs_t));

    out->magic = st.magic;
    out->version = st.version;
    out->block_size = st.block_size;

    out->total_blocks = st.total_blocks;
    out->total_inodes = st.total_inodes;

    out->used_inodes = st.used_inodes;
    out->free_inodes = st.free_inodes;

    out->data_block_start = st.data_block_start;
    out->data_blocks = st.data_blocks;
    out->used_blocks = st.used_blocks;
    out->free_blocks = st.free_blocks;

    out->root_inode = st.root_inode;
    out->fs_state = st.fs_state;
    out->mount_count = st.mount_count;

    return MYFS_OK;
}

int myfs_phys_get_free_group(uint32_t *count, uint32_t *blocks, uint32_t max_blocks) {
    if (count == nullptr || blocks == nullptr) {
        return MYFS_ERR_INVALID_ARG;
    }
    myfs_superblock_t *sb = myfs_super_get();
    if (sb == nullptr) {
        return MYFS_ERR_NOT_MOUNTED;
    }
    *count = sb->free_group_count;
    uint32_t to_copy = (sb->free_group_count < max_blocks) ? sb->free_group_count : max_blocks;
    for (uint32_t i = 0; i < to_copy; i++) {
        blocks[i] = sb->free_group[i];
    }
    return MYFS_OK;
}

int myfs_phys_get_used_blocks_in_range(
        myfs_block_t start_block,
        uint32_t count,
        uint32_t *used_blocks,
        uint32_t max_blocks,
        uint32_t *actual_count
) {
    if (used_blocks == nullptr || actual_count == nullptr) {
        return MYFS_ERR_INVALID_ARG;
    }
    *actual_count = 0;

    myfs_superblock_t *sb = myfs_super_get();
    if (sb == nullptr) {
        return MYFS_ERR_NOT_MOUNTED;
    }

    if (count == 0 || max_blocks == 0) {
        return MYFS_OK;
    }

    myfs_block_t end_block = start_block + (myfs_block_t) count;
    if (start_block < sb->data_block_start || end_block > sb->total_blocks) {
        return MYFS_ERR_INVALID_ARG;
    }

    std::vector<uint8_t> is_free(count, 0);

    if (sb->free_blocks_count == 0) {
        uint32_t out = 0;
        for (uint32_t i = 0; i < count && out < max_blocks; i++) {
            used_blocks[out++] = (uint32_t) (start_block + (myfs_block_t) i);
        }
        *actual_count = out;
        return MYFS_OK;
    }

    if (sb->free_group_count == 0 || sb->free_group_count > MYFS_FREE_GROUP_SIZE) {
        return MYFS_ERR_CORRUPTED;
    }

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

    uint32_t out = 0;
    for (uint32_t i = 0; i < count && out < max_blocks; i++) {
        if (is_free[i] == 0) {
            used_blocks[out++] = (uint32_t) (start_block + (myfs_block_t) i);
        }
    }
    *actual_count = out;
    return MYFS_OK;
}

int myfs_phys_sync(void) {
    return myfs_sync();
}


int myfs_phys_fsync_block(myfs_block_t block_id) {
    return myfs_block_fsync(block_id);
}


/* ============================================================
 * 6. 缓存接口
 * ============================================================
 */

int myfs_phys_cache_init(uint32_t capacity) {
    return myfs_cache_init(capacity);
}


int myfs_phys_cache_shutdown(void) {
    return myfs_cache_shutdown();
}


int myfs_phys_cache_get_stats(myfs_phys_cache_stats_t *stats) {
    if (stats == nullptr) {
        return MYFS_ERR_INVALID_ARG;
    }

    myfs_cache_stats_t inner;

    myfs_cache_get_stats(&inner);

    stats->read_count = inner.read_count;
    stats->write_count = inner.write_count;
    stats->hit_count = inner.hit_count;
    stats->miss_count = inner.miss_count;
    stats->evict_count = inner.evict_count;
    stats->dirty_flush_count = inner.dirty_flush_count;

    return MYFS_OK;
}


/* ============================================================
 * 7. fsck 接口
 * ============================================================
 */

int myfs_phys_fsck(
        int verbose,
        myfs_phys_fsck_result_t *result
) {
    if (result == nullptr) {
        return MYFS_ERR_INVALID_ARG;
    }

    myfs_fsck_options_t options;
    options.repair = 0;
    options.verbose = verbose;

    myfs_fsck_result_t inner;

    int ret = myfs_fsck_run(&options, &inner);
    if (ret != MYFS_OK) {
        return ret;
    }

    memset(result, 0, sizeof(myfs_phys_fsck_result_t));

    result->errors_found = inner.errors_found;
    result->errors_fixed = inner.errors_fixed;

    result->inode_state_errors = inner.inode_state_errors;
    result->free_inode_count_errors = inner.free_inode_count_errors;

    result->invalid_block_refs = inner.invalid_block_refs;
    result->duplicated_used_blocks = inner.duplicated_used_blocks;
    result->duplicated_free_blocks = inner.duplicated_free_blocks;
    result->used_free_conflicts = inner.used_free_conflicts;
    result->leaked_blocks = inner.leaked_blocks;

    result->free_chain_errors = inner.free_chain_errors;
    result->free_block_count_errors = inner.free_block_count_errors;

    return MYFS_OK;
}


/* ============================================================
 * 8. 调试打印接口
 * ============================================================
 */

int myfs_phys_debug_statfs(void) {
    return myfs_debug_print_statfs();
}


int myfs_phys_debug_super(void) {
    return myfs_debug_dump_super();
}


int myfs_phys_debug_inode(myfs_ino_t inode_id) {
    return myfs_debug_dump_inode(inode_id);
}


int myfs_phys_debug_bmap(
        myfs_ino_t inode_id,
        uint32_t logical_block
) {
    return myfs_debug_bmap(
            inode_id,
            logical_block
    );
}


int myfs_phys_debug_free_group(void) {
    return myfs_debug_dump_free_group();
}


int myfs_phys_debug_cache(void) {
    return myfs_debug_dump_cache();
}


int myfs_phys_debug_hexdump(
        myfs_block_t block_id,
        uint32_t max_bytes
) {
    return myfs_debug_hexdump_block(
            block_id,
            max_bytes
    );
}

int myfs_phys_debug_recover_blocks(void) {
    return myfs_debug_recover_blocks();
}

int myfs_phys_debug_blockmap_range(
        myfs_block_t start_block,
        uint32_t count
) {
    return myfs_debug_blockmap_range(start_block, count);
}


/* ============================================================
 * 9. 错误信息
 * ============================================================
 */

const char *myfs_phys_strerror(int err) {
    return myfs_strerror(err);
}
