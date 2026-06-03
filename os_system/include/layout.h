#ifndef MYFS_LAYOUT_H
#define MYFS_LAYOUT_H

/*
 * 磁盘布局计算模块。
 *
 * 作用：
 * 根据 total_blocks 和 total_inodes 计算文件系统各区域在 disk.img 中的位置。
 *
 * 当前磁盘布局：
 *
 * block 0                      : 超级块
 * block 1 ~ x                  : inode 位图区
 * block x+1 ~ y                : inode 表区
 * block y+1 ~ z                : journal 日志区
 * block z+1 ~ disk end         : 数据区
 *
 * 注意：
 * 该模块只负责计算布局，不负责真正写磁盘。
 */

#include <stdint.h>
#include "type.h"

/*
 * 文件系统磁盘布局信息。
 */
typedef struct myfs_layout {
    /*
     * 超级块起始块。
     * 当前固定为 0。
     */
    myfs_block_t superblock_start;

    /*
     * inode 位图起始块和占用块数。
     */
    myfs_block_t inode_bitmap_start;
    uint32_t inode_bitmap_blocks;

    /*
     * inode 表起始块和占用块数。
     */
    myfs_block_t inode_table_start;
    uint32_t inode_table_blocks;

    /*
     * journal 日志区起始块和占用块数。
     * 第一阶段只预留，不真正实现日志。
     */
    myfs_block_t journal_start;
    uint32_t journal_blocks;

    /*
     * 数据区起始块和数据区总块数。
     */
    myfs_block_t data_block_start;
    uint32_t data_blocks;
} myfs_layout_t;

/*
 * 根据磁盘总块数和 inode 总数计算磁盘布局。
 *
 * 参数：
 * total_blocks 磁盘总块数
 * total_inodes inode 总数
 * layout       输出布局结果
 */
int myfs_layout_build(
        uint32_t total_blocks,
        uint32_t total_inodes,
        myfs_layout_t *layout
);

/*
 * 检查布局是否合法。
 *
 * 主要检查：
 * 1. 各区域是否按顺序排列。
 * 2. 各区域是否越界。
 * 3. 数据区是否存在。
 */
int myfs_layout_check(
        uint32_t total_blocks,
        const myfs_layout_t *layout
);

#endif