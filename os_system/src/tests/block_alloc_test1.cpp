#include "mount.h"
#include "superblock.h"
#include "block_alloc.h"
#include "disk.h"
#include "config.h"
#include "error.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * block_alloc_test1.c
 * ------------------------------------------------------------
 * 第三阶段成组链接法测试。
 *
 * 测试内容：
 *
 * 1. mkfs 时是否初始化空闲块链。
 * 2. free_blocks_count 是否正确。
 * 3. block_alloc 是否能分配数据区块。
 * 4. 分配后 free_blocks_count 是否减少。
 * 5. 分配出的块是否属于数据区。
 * 6. 能否跨组分配超过 MYFS_FREE_GROUP_SIZE 个块。
 * 7. 分配出的块是否重复。
 * 8. block_free 是否能释放块。
 * 9. 释放后 free_blocks_count 是否恢复。
 * 10. 分配出的块是否已清零。
 */

static void check(int ret, const char *msg) {
    if (ret != MYFS_OK) {
        fprintf(stderr, "[FAIL] %s: %s\n", msg, myfs_strerror(ret));
        exit(1);
    }

    printf("[PASS] %s\n", msg);
}

static void check_true(int condition, const char *msg) {
    if (!condition) {
        fprintf(stderr, "[FAIL] %s\n", msg);
        exit(1);
    }

    printf("[PASS] %s\n", msg);
}

static int has_duplicate(const myfs_block_t *blocks, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        for (uint32_t j = i + 1; j < count; j++) {
            if (blocks[i] == blocks[j]) {
                return 1;
            }
        }
    }

    return 0;
}

static void test_initial_free_chain(uint32_t total_blocks, uint32_t total_inodes) {
    printf("\n[TEST] initial grouped free list after mkfs\n");

    check(
            myfs_mkfs("block_alloc_test.img", total_blocks, total_inodes),
            "myfs_mkfs"
    );

    check(
            myfs_mount("block_alloc_test.img"),
            "myfs_mount"
    );

    myfs_superblock_t *sb = myfs_super_get();

    check_true(sb != NULL, "superblock available");

    printf("data_block_start  = %u\n", sb->data_block_start);
    printf("data_blocks       = %u\n", sb->data_blocks);
    printf("free_blocks_count = %u\n", sb->free_blocks_count);
    printf("free_group_count  = %u\n", sb->free_group_count);

    check_true(
            sb->free_blocks_count == sb->data_blocks,
            "free_blocks_count equals data_blocks after mkfs"
    );

    check_true(
            sb->free_group_count > 0,
            "free_group_count greater than 0 after block group init"
    );

    check_true(
            sb->free_group_count <= MYFS_FREE_GROUP_SIZE,
            "free_group_count within group size"
    );

    for (uint32_t i = 0; i < sb->free_group_count; i++) {
        check_true(
                myfs_block_is_valid_data_block(sb->free_group[i]) == 1,
                "superblock free_group block is valid data block"
        );
    }
}

static void test_alloc_one_block(void) {
    printf("\n[TEST] allocate one data block\n");

    myfs_superblock_t *sb = myfs_super_get();

    check_true(sb != NULL, "superblock available");

    uint32_t free_before = sb->free_blocks_count;

    myfs_block_t block_id;

    check(
            myfs_block_alloc(&block_id),
            "myfs_block_alloc one block"
    );

    printf("allocated block = %u\n", block_id);

    check_true(
            myfs_block_is_valid_data_block(block_id) == 1,
            "allocated block is valid data block"
    );

    check_true(
            sb->free_blocks_count == free_before - 1,
            "free_blocks_count decreases after one allocation"
    );

    /*
     * 检查分配出的块是否已经被清零。
     */
    unsigned char buf[MYFS_BLOCK_SIZE];

    check(
            myfs_disk_read_block(block_id, buf),
            "read allocated block"
    );

    int all_zero = 1;

    for (uint32_t i = 0; i < MYFS_BLOCK_SIZE; i++) {
        if (buf[i] != 0) {
            all_zero = 0;
            break;
        }
    }

    check_true(all_zero, "allocated block is zeroed");

    /*
     * 再释放回去。
     */
    check(
            myfs_block_free(block_id),
            "myfs_block_free one block"
    );

    check_true(
            sb->free_blocks_count == free_before,
            "free_blocks_count recovers after free"
    );
}

static void test_cross_group_alloc_free(void) {
    printf("\n[TEST] cross-group allocation and free\n");

    myfs_superblock_t *sb = myfs_super_get();

    check_true(sb != NULL, "superblock available");

    /*
     * 分配超过一个成组大小的块数，用于测试跨组加载。
     */
    const uint32_t alloc_count = MYFS_FREE_GROUP_SIZE + 50;

    myfs_block_t blocks[MYFS_FREE_GROUP_SIZE + 50];

    uint32_t free_before = sb->free_blocks_count;

    for (uint32_t i = 0; i < alloc_count; i++) {
        check(
                myfs_block_alloc(&blocks[i]),
                "myfs_block_alloc cross group"
        );

        check_true(
                myfs_block_is_valid_data_block(blocks[i]) == 1,
                "cross-group allocated block valid"
        );
    }

    check_true(
            sb->free_blocks_count == free_before - alloc_count,
            "free_blocks_count decreases after cross-group allocation"
    );

    check_true(
            has_duplicate(blocks, alloc_count) == 0,
            "allocated blocks have no duplicates"
    );

    for (uint32_t i = 0; i < alloc_count; i++) {
        check(
                myfs_block_free(blocks[i]),
                "myfs_block_free cross group"
        );
    }

    check_true(
            sb->free_blocks_count == free_before,
            "free_blocks_count recovers after cross-group free"
    );
}

static void test_invalid_block_free(void) {
    printf("\n[TEST] invalid block free\n");

    myfs_superblock_t *sb = myfs_super_get();

    check_true(sb != NULL, "superblock available");

    /*
     * 尝试释放超级块 block 0。
     * 这不属于数据区，必须失败。
     */
    int ret = myfs_block_free(0);

    check_true(
            ret == MYFS_ERR_INVALID_BLOCK,
            "freeing block 0 should fail"
    );

    /*
     * 尝试释放 inode 表区域块。
     * 也必须失败。
     */
    ret = myfs_block_free(sb->inode_table_start);

    check_true(
            ret == MYFS_ERR_INVALID_BLOCK,
            "freeing inode table block should fail"
    );
}

int main(void) {
    uint32_t total_blocks = 32768;
    uint32_t total_inodes = 4096;

    printf("========================================\n");
    printf(" MYFS Stage3 Block Alloc Test\n");
    printf("========================================\n");

    test_initial_free_chain(total_blocks, total_inodes);

    test_alloc_one_block();

    test_cross_group_alloc_free();

    test_invalid_block_free();

    check(
            myfs_umount(),
            "myfs_umount"
    );

    printf("\n========================================\n");
    printf(" BLOCK ALLOC TEST PASSED\n");
    printf("========================================\n");

    return 0;
}