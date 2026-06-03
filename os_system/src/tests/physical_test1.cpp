#include "mount.h"
#include "superblock.h"
#include "disk.h"
#include "error.h"
#include "config.h"

#include <stdio.h>
#include <stdlib.h>

static void check(int ret, const char *msg) {
    if (ret != MYFS_OK) {
        fprintf(stderr, "%s failed: %s\n", msg, myfs_strerror(ret));
        exit(1);
    }
}

static void print_superblock(const myfs_superblock_t *sb) {
    printf("========== MYFS SUPERBLOCK ==========\n");
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
    printf("fs_state            = %u\n", sb->fs_state);
    printf("mount_count         = %u\n", sb->mount_count);
    printf("checksum            = 0x%x\n", sb->checksum);
    printf("=====================================\n");
}

int main(void) {
    const char *disk_path = "stage1_disk.img";

    /*
     * 32768 个块，每块 4096 字节，总大小 128MB。
     */
    uint32_t total_blocks = 32768;
    uint32_t total_inodes = 4096;

    check(myfs_mkfs(disk_path, total_blocks, total_inodes), "myfs_mkfs");

    check(myfs_mount(disk_path), "myfs_mount");

    myfs_superblock_t *sb = myfs_super_get();

    if (sb == NULL) {
        fprintf(stderr, "myfs_super_get returned NULL\n");
        return 1;
    }

    print_superblock(sb);

    if (sb->magic != 0x20235840u) {
        fprintf(stderr, "bad magic\n");
        return 1;
    }

    if (sb->block_size != MYFS_BLOCK_SIZE) {
        fprintf(stderr, "bad block size\n");
        return 1;
    }

    if (sb->total_blocks != total_blocks) {
        fprintf(stderr, "bad total blocks\n");
        return 1;
    }

    if (sb->total_inodes != total_inodes) {
        fprintf(stderr, "bad total inodes\n");
        return 1;
    }

    if (sb->data_block_start >= sb->total_blocks) {
        fprintf(stderr, "bad data block start\n");
        return 1;
    }

    if (sb->fs_state != MYFS_STATE_DIRTY) {
        fprintf(stderr, "mounted fs should be dirty\n");
        return 1;
    }

    check(myfs_umount(), "myfs_umount");

    /*
     * 重新挂载一次，验证超级块确实可以从磁盘读回来。
     */
    check(myfs_mount(disk_path), "myfs_mount again");

    sb = myfs_super_get();

    if (sb == NULL) {
        fprintf(stderr, "myfs_super_get returned NULL after remount\n");
        return 1;
    }

    print_superblock(sb);

    check(myfs_umount(), "myfs_umount again");

    printf("stage1 test passed\n");

    return 0;
}