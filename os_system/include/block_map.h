#ifndef MYFS_BLOCK_MAP_H
#define MYFS_BLOCK_MAP_H

/*
 * block_map.h
 * ------------------------------------------------------------
 * 文件逻辑块到物理块映射模块。
 *
 * 本模块属于文件系统底层/物理层，不处理路径、文件名、目录项、
 * 文件描述符和用户命令。
 *
 * 它只负责：
 *
 * 1. 根据 inode_id 和 logical_block 找到 physical_block。
 * 2. 支持 direct[12] 直接块。
 * 3. 支持一级间接块 indirect1。
 * 4. 支持二级间接块 indirect2。
 * 5. 在 create = 1 时自动分配物理块。
 * 6. 在 create = 0 时只查询，不分配。
 * 7. 支持释放某个逻辑块。
 * 8. 支持释放某个 inode 占用的全部数据块。
 *
 * 上层文件模块以后实现 read/write 时，可以基于本模块完成：
 *
 * offset -> logical_block -> physical_block
 */

#include <stdint.h>
#include "type.h"

/*
 * 每个 inode 中直接块数量。
 * 当前 inode 结构中 direct[12]。
 */
#define MYFS_DIRECT_BLOCKS 12u

/*
 * 每个间接块中能保存多少个块号。
 *
 * 一个块大小为 4096 字节。
 * 一个块号 myfs_block_t 为 uint32_t，占 4 字节。
 *
 * 4096 / 4 = 1024
 */
#define MYFS_POINTERS_PER_BLOCK 1024u

/*
 * 最大逻辑块数量：
 *
 * direct 部分：
 *     12
 *
 * 一级间接：
 *     1024
 *
 * 二级间接：
 *     1024 * 1024
 */
#define MYFS_MAX_LOGICAL_BLOCKS \
    (MYFS_DIRECT_BLOCKS + MYFS_POINTERS_PER_BLOCK + \
    MYFS_POINTERS_PER_BLOCK * MYFS_POINTERS_PER_BLOCK)


/*
 * 获取 inode 中某个逻辑块对应的物理块。
 *
 * 参数：
 * inode_id:
 *     inode 编号。
 *
 * logical_block:
 *     文件内部逻辑块号，从 0 开始。
 *
 * create:
 *     0：只查询，不分配。如果该逻辑块没有物理块，则返回 hole。
 *     1：如果该逻辑块没有物理块，则自动分配。
 *
 * physical_block:
 *     输出物理块号。
 *
 * is_hole:
 *     输出是否为空洞。
 *     1 表示该逻辑块没有物理块。
 *     0 表示 physical_block 有效。
 *
 * 返回：
 * MYFS_OK 成功
 * 其他错误码表示失败
 */
int myfs_inode_get_data_block(
        myfs_ino_t inode_id,
        uint32_t logical_block,
        int create,
        myfs_block_t *physical_block,
        int *is_hole
);

/*
 * 只查询逻辑块映射，不创建物理块。
 *
 * 这个接口主要给 bmap 命令或调试工具使用。
 */
int myfs_inode_bmap(
        myfs_ino_t inode_id,
        uint32_t logical_block,
        myfs_block_t *physical_block,
        int *is_hole
);

/*
 * 释放 inode 中某个逻辑块对应的物理块。
 *
 * 如果该逻辑块本来就是 hole，则直接成功返回。
 */
int myfs_inode_release_data_block(
        myfs_ino_t inode_id,
        uint32_t logical_block
);

/*
 * 释放某个 inode 占用的全部数据块和间接块。
 *
 * inode_free 时应该调用该接口。
 */
int myfs_inode_release_all_blocks(myfs_ino_t inode_id);

/*
 * 返回当前文件系统支持的最大逻辑块数量。
 */
uint32_t myfs_inode_max_logical_blocks(void);

#endif