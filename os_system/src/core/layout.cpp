#include "layout.h"
#include "config.h"
#include "error.h"

static uint32_t div_round_up_u32(uint32_t a, uint32_t b) {
    return (a + b - 1) / b;
}

int myfs_layout_build(
        uint32_t total_blocks,
        uint32_t total_inodes,
        myfs_layout_t *layout
) {
    if (layout == NULL || total_blocks == 0 || total_inodes == 0) {
        return MYFS_ERR_INVALID_ARG;
    }

    layout->superblock_start = 0;

    /*
     * inode 位图：
     * 每个 inode 占 1 bit。
     */
    uint32_t inode_bitmap_bytes = div_round_up_u32(total_inodes, 8);

    uint32_t inode_bitmap_blocks = div_round_up_u32(
            inode_bitmap_bytes,
            MYFS_BLOCK_SIZE
    );

    /*
     * inode 表：
     * 按固定 256 字节计算。
     */
    uint32_t inode_table_bytes = total_inodes * MYFS_INODE_SIZE;

    uint32_t inode_table_blocks = div_round_up_u32(
            inode_table_bytes,
            MYFS_BLOCK_SIZE
    );

    /*
     * journal 区：
     * 第一阶段只预留，不实现日志。
     * 如果磁盘较小，自动缩小 journal 区。
     */
    uint32_t journal_blocks = MYFS_DEFAULT_JOURNAL_BLOCKS;

    uint32_t max_reasonable_journal = total_blocks / 16;

    if (max_reasonable_journal < MYFS_MIN_JOURNAL_BLOCKS) {
        journal_blocks = MYFS_MIN_JOURNAL_BLOCKS;
    } else if (journal_blocks > max_reasonable_journal) {
        journal_blocks = max_reasonable_journal;
    }

    layout->inode_bitmap_start = 1;
    layout->inode_bitmap_blocks = inode_bitmap_blocks;

    layout->inode_table_start =
            layout->inode_bitmap_start + layout->inode_bitmap_blocks;
    layout->inode_table_blocks = inode_table_blocks;

    layout->journal_start =
            layout->inode_table_start + layout->inode_table_blocks;
    layout->journal_blocks = journal_blocks;

    layout->data_block_start =
            layout->journal_start + layout->journal_blocks;

    if (layout->data_block_start >= total_blocks) {
        return MYFS_ERR_NO_SPACE;
    }

    layout->data_blocks = total_blocks - layout->data_block_start;

    return myfs_layout_check(total_blocks, layout);
}

int myfs_layout_check(
        uint32_t total_blocks,
        const myfs_layout_t *layout
) {
    if (layout == NULL || total_blocks == 0) {
        return MYFS_ERR_INVALID_ARG;
    }

    if (layout->superblock_start != 0) {
        return MYFS_ERR_CORRUPTED;
    }

    if (layout->inode_bitmap_start <= layout->superblock_start) {
        return MYFS_ERR_CORRUPTED;
    }

    if (layout->inode_table_start <
        layout->inode_bitmap_start + layout->inode_bitmap_blocks) {
        return MYFS_ERR_CORRUPTED;
    }

    if (layout->journal_start <
        layout->inode_table_start + layout->inode_table_blocks) {
        return MYFS_ERR_CORRUPTED;
    }

    if (layout->data_block_start <
        layout->journal_start + layout->journal_blocks) {
        return MYFS_ERR_CORRUPTED;
    }

    if (layout->data_block_start >= total_blocks) {
        return MYFS_ERR_NO_SPACE;
    }

    if (layout->data_blocks != total_blocks - layout->data_block_start) {
        return MYFS_ERR_CORRUPTED;
    }

    return MYFS_OK;
}