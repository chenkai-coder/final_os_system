#include "superblock.h"
#include "layout.h"
#include "disk.h"
#include "common.h"
#include "error.h"

#include <string.h>
#include <time.h>

static myfs_superblock_t g_superblock;
static int g_superblock_loaded = 0;

static uint64_t myfs_now(void) {
    return (uint64_t) time(NULL);
}

/*
 * 简单 checksum。
 * 这不是安全哈希，只用于发现超级块明显损坏。
 */
static uint32_t myfs_super_calc_checksum(const myfs_superblock_t *sb) {
    myfs_superblock_t tmp = *sb;
    tmp.checksum = 0;

    const unsigned char *p = (const unsigned char *) &tmp;
    uint32_t hash = 2166136261u;

    for (size_t i = 0; i < sizeof(myfs_superblock_t); i++) {
        hash ^= p[i];
        hash *= 16777619u;
    }

    return hash;
}

int myfs_super_init(
        myfs_superblock_t *sb,
        uint32_t total_blocks,
        uint32_t total_inodes
) {
    if (sb == NULL || total_blocks == 0 || total_inodes == 0) {
        return MYFS_ERR_INVALID_ARG;
    }

    myfs_layout_t layout;

    int ret = myfs_layout_build(total_blocks, total_inodes, &layout);
    if (ret != MYFS_OK) {
        return ret;
    }

    memset(sb, 0, sizeof(myfs_superblock_t));

    sb->magic = MYFS_MAGIC;
    sb->version = MYFS_VERSION;

    sb->block_size = MYFS_BLOCK_SIZE;
    sb->total_blocks = total_blocks;

    sb->total_inodes = total_inodes;
    sb->free_inodes_count = total_inodes;

    sb->free_blocks_count = layout.data_blocks;

    sb->inode_bitmap_start = layout.inode_bitmap_start;
    sb->inode_bitmap_blocks = layout.inode_bitmap_blocks;

    sb->inode_table_start = layout.inode_table_start;
    sb->inode_table_blocks = layout.inode_table_blocks;

    sb->journal_start = layout.journal_start;
    sb->journal_blocks = layout.journal_blocks;

    sb->data_block_start = layout.data_block_start;
    sb->data_blocks = layout.data_blocks;

    /*
     * 第一阶段还不创建根目录。
     * 这里只先约定 root_inode = 0。
     * 第二阶段实现 inode 后，再真正初始化 root inode。
     */
    sb->root_inode = 0;

    /*
     * 成组链接法字段先置空。
     * 第三阶段实现 block_group_init 时再填充。
     */
    sb->free_group_count = 0;

    sb->fs_state = MYFS_STATE_CLEAN;
    sb->mount_count = 0;

    sb->last_mount_time = 0;
    sb->last_write_time = myfs_now();

    sb->checksum = myfs_super_calc_checksum(sb);

    return MYFS_OK;
}

int myfs_super_load(void) {
    unsigned char block[MYFS_BLOCK_SIZE];

    int ret = myfs_disk_read_block(0, block);
    if (ret != MYFS_OK) {
        return ret;
    }

    memcpy(&g_superblock, block, sizeof(myfs_superblock_t));
    g_superblock_loaded = 1;

    return myfs_super_check();
}

int myfs_super_sync(void) {
    if (!g_superblock_loaded) {
        return MYFS_ERR_NOT_MOUNTED;
    }

    unsigned char block[MYFS_BLOCK_SIZE];

    memset(block, 0, sizeof(block));

    g_superblock.last_write_time = myfs_now();
    g_superblock.checksum = myfs_super_calc_checksum(&g_superblock);

    memcpy(block, &g_superblock, sizeof(myfs_superblock_t));

    return myfs_disk_write_block(0, block);
}

int myfs_super_check(void) {
    if (!g_superblock_loaded) {
        return MYFS_ERR_NOT_MOUNTED;
    }

    if (g_superblock.magic != MYFS_MAGIC) {
        return MYFS_ERR_CORRUPTED;
    }

    if (g_superblock.version != MYFS_VERSION) {
        return MYFS_ERR_UNSUPPORTED;
    }

    if (g_superblock.block_size != MYFS_BLOCK_SIZE) {
        return MYFS_ERR_CORRUPTED;
    }

    if (g_superblock.total_blocks != myfs_disk_total_blocks()) {
        return MYFS_ERR_CORRUPTED;
    }

    if (g_superblock.total_inodes == 0) {
        return MYFS_ERR_CORRUPTED;
    }

    if (g_superblock.inode_bitmap_start == 0) {
        return MYFS_ERR_CORRUPTED;
    }

    if (g_superblock.inode_table_start <= g_superblock.inode_bitmap_start) {
        return MYFS_ERR_CORRUPTED;
    }

    if (g_superblock.journal_start <= g_superblock.inode_table_start) {
        return MYFS_ERR_CORRUPTED;
    }

    if (g_superblock.data_block_start <= g_superblock.journal_start) {
        return MYFS_ERR_CORRUPTED;
    }

    if (g_superblock.data_block_start >= g_superblock.total_blocks) {
        return MYFS_ERR_CORRUPTED;
    }

    if (g_superblock.data_blocks !=
        g_superblock.total_blocks - g_superblock.data_block_start) {
        return MYFS_ERR_CORRUPTED;
    }

    if (g_superblock.free_inodes_count > g_superblock.total_inodes) {
        return MYFS_ERR_CORRUPTED;
    }

    if (g_superblock.free_blocks_count > g_superblock.data_blocks) {
        return MYFS_ERR_CORRUPTED;
    }

    if (g_superblock.free_group_count > MYFS_FREE_GROUP_SIZE) {
        return MYFS_ERR_CORRUPTED;
    }

    uint32_t expected = myfs_super_calc_checksum(&g_superblock);
    if (expected != g_superblock.checksum) {
        return MYFS_ERR_CHECKSUM;
    }

    return MYFS_OK;
}

myfs_superblock_t *myfs_super_get(void) {
    if (!g_superblock_loaded) {
        return NULL;
    }

    return &g_superblock;
}

int myfs_super_mark_dirty(void) {
    if (!g_superblock_loaded) {
        return MYFS_ERR_NOT_MOUNTED;
    }

    g_superblock.fs_state = MYFS_STATE_DIRTY;
    g_superblock.mount_count += 1;
    g_superblock.last_mount_time = myfs_now();

    return myfs_super_sync();
}

int myfs_super_mark_clean(void) {
    if (!g_superblock_loaded) {
        return MYFS_ERR_NOT_MOUNTED;
    }

    g_superblock.fs_state = MYFS_STATE_CLEAN;

    return myfs_super_sync();
}