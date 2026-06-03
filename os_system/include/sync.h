#ifndef MYFS_SYNC_H
#define MYFS_SYNC_H

/*
 * sync.h
 * ------------------------------------------------------------
 * 同步接口。
 *
 * 本模块属于底层物理支撑部分。
 *
 * 当前阶段主要负责：
 * 1. 将缓存中的 dirty 块刷回 disk.img。
 * 2. 将超级块同步到 disk.img。
 *
 */

#include "type.h"

/*
 * 同步整个文件系统。
 */
int myfs_sync(void);

/*
 * 同步某个物理块。
 */
int myfs_block_fsync(myfs_block_t block_id);

#endif