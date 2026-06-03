#include "sync.h"
#include "cache.h"
#include "superblock.h"
#include "error.h"

/*
 * sync.cpp
 * ------------------------------------------------------------
 * 文件系统同步接口。
 *
 * 当前阶段：
 * 1. 刷回缓存 dirty 块。
 * 2. 同步超级块。
 *
 * 后续实现 journaling 后，可以在这里加入 journal commit/checkpoint。
 */

int myfs_sync(void) {
    if (myfs_cache_is_initialized()) {
        int ret = myfs_cache_flush_all();
        if (ret != MYFS_OK) {
            return ret;
        }
    }

    /*
     * 超级块仍然使用 superblock 模块自己的同步接口。
     */
    return myfs_super_sync();
}


int myfs_block_fsync(myfs_block_t block_id) {
    if (!myfs_cache_is_initialized()) {
        return MYFS_OK;
    }

    return myfs_cache_flush_block(block_id);
}