#ifndef MYFS_DEBUG_H
#define MYFS_DEBUG_H

/*
 * debug.h
 * ------------------------------------------------------------
 * MYFS 物理层调试与统计接口。
 *
 * 本模块只面向底层物理结构：
 *
 * 1. superblock
 * 2. inode
 * 3. inode bitmap
 * 4. block map
 * 5. grouped free list
 * 6. cache stats
 *
 */

#include <stdint.h>
#include "type.h"

/*
 * 文件系统总体统计信息。
 */
typedef struct myfs_statfs {
    uint32_t magic;
    uint32_t version;
    uint32_t block_size;

    uint32_t total_blocks;
    uint32_t total_inodes;

    uint32_t free_inodes;
    uint32_t used_inodes;

    uint32_t data_block_start;
    uint32_t data_blocks;
    uint32_t free_blocks;
    uint32_t used_blocks;

    uint32_t inode_bitmap_start;
    uint32_t inode_bitmap_blocks;

    uint32_t inode_table_start;
    uint32_t inode_table_blocks;

    uint32_t journal_start;
    uint32_t journal_blocks;

    uint32_t root_inode;
    uint32_t fs_state;
    uint32_t mount_count;
} myfs_statfs_t;


/*
 * 获取文件系统总体统计信息。
 */
int myfs_statfs(myfs_statfs_t *out);


/*
 * 打印文件系统总体统计信息。
 */
int myfs_debug_print_statfs(void);


/*
 * 打印超级块信息。
 */
int myfs_debug_dump_super(void);


/*
 * 打印指定 inode 的详细物理信息。
 */
int myfs_debug_dump_inode(myfs_ino_t inode_id);


/*
 * 打印指定 inode 的逻辑块映射。
 *
 * create = 0，只查询，不分配。
 */
int myfs_debug_bmap(
        myfs_ino_t inode_id,
        uint32_t logical_block
);


/*
 * 打印当前超级块中的成组链接空闲块组。
 *
 * 注意：
 * 这里只打印超级块当前 free_group，不完整遍历整个空闲链。
 * 完整一致性检查由 fsck 模块负责。
 */
int myfs_debug_dump_free_group(void);


/*
 * 打印缓存统计信息。
 *
 * 如果缓存未初始化，会输出提示。
 */
int myfs_debug_dump_cache(void);


/*
 * 打印某个物理块的十六进制内容。
 *
 * max_bytes 表示最多打印多少字节。
 * 建议传 64、128、256，不建议直接打印完整 4096 字节。
 */
int myfs_debug_hexdump_block(
        myfs_block_t block_id,
        uint32_t max_bytes
);

/*
 * 恢复磁盘块映射关系。
 *
 * 扫描所有inode，重建它们与磁盘块的映射关系。
 * 用于在文件系统异常后恢复inode-link-disk block关系。
 */
int myfs_debug_recover_blocks(void);

#endif