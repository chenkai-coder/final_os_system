#include "mount.h"
#include "superblock.h"
#include "inode.h"
#include "block_alloc.h"
#include "block_map.h"
#include "error.h"

#include <stdio.h>
#include <stdlib.h>

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

static void print_inode_blocks(myfs_ino_t inode_id) {
    myfs_inode_t inode;

    check(
            myfs_inode_read(inode_id, &inode),
            "read inode for print"
    );

    printf("\n---------- INODE BLOCK INFO ----------\n");
    printf("inode_id    = %u\n", inode.inode_id);
    printf("size        = %llu\n", (unsigned long long) inode.size);
    printf("block_count = %u\n", inode.block_count);
    printf("direct[0]   = %u\n", inode.direct[0]);
    printf("direct[11]  = %u\n", inode.direct[11]);
    printf("indirect1   = %u\n", inode.indirect1);
    printf("indirect2   = %u\n", inode.indirect2);
    printf("--------------------------------------\n\n");
}

static void test_direct_block(myfs_ino_t inode_id) {
    printf("\n[TEST] direct block mapping\n");

    myfs_block_t physical;
    int is_hole;

    /*
     * create = 0，尚未分配时应该是 hole。
     */
    check(
            myfs_inode_bmap(inode_id, 0, &physical, &is_hole),
            "bmap direct block before allocation"
    );

    check_true(is_hole == 1, "direct logical block 0 is hole before allocation");

    /*
     * create = 1，分配 direct[0]。
     */
    check(
            myfs_inode_get_data_block(
                    inode_id,
                    0,
                    1,
                    &physical,
                    &is_hole
            ),
            "allocate direct logical block 0"
    );

    printf("logical 0 -> physical %u\n", physical);

    check_true(is_hole == 0, "direct logical block 0 is not hole after allocation");
    check_true(myfs_block_is_valid_data_block(physical), "direct physical block valid");

    /*
     * 再次查询，不应重新分配，应返回同一个物理块。
     */
    myfs_block_t physical2;

    check(
            myfs_inode_bmap(inode_id, 0, &physical2, &is_hole),
            "bmap direct block after allocation"
    );

    check_true(is_hole == 0, "direct bmap not hole");
    check_true(physical2 == physical, "direct bmap returns same physical block");

    print_inode_blocks(inode_id);
}

static void test_single_indirect_block(myfs_ino_t inode_id) {
    printf("\n[TEST] single indirect block mapping\n");

    /*
     * 第一个一级间接逻辑块：
     * logical = 12
     */
    uint32_t logical = MYFS_DIRECT_BLOCKS;

    myfs_block_t physical;
    int is_hole;

    check(
            myfs_inode_get_data_block(
                    inode_id,
                    logical,
                    1,
                    &physical,
                    &is_hole
            ),
            "allocate first single indirect logical block"
    );

    printf("logical %u -> physical %u\n", logical, physical);

    check_true(is_hole == 0, "single indirect block not hole");
    check_true(myfs_block_is_valid_data_block(physical), "single indirect data block valid");

    myfs_inode_t inode;

    check(
            myfs_inode_read(inode_id, &inode),
            "read inode after single indirect allocation"
    );

    check_true(inode.indirect1 != 0, "indirect1 block allocated");
    check_true(myfs_block_is_valid_data_block(inode.indirect1), "indirect1 physical block valid");

    print_inode_blocks(inode_id);
}

static void test_double_indirect_block(myfs_ino_t inode_id) {
    printf("\n[TEST] double indirect block mapping\n");

    /*
     * 第一个二级间接逻辑块：
     *
     * direct 覆盖 12 个块
     * indirect1 覆盖 1024 个块
     *
     * 因此第一个二级间接逻辑块是：
     * 12 + 1024
     */
    uint32_t logical = MYFS_DIRECT_BLOCKS + MYFS_POINTERS_PER_BLOCK;

    myfs_block_t physical;
    int is_hole;

    check(
            myfs_inode_get_data_block(
                    inode_id,
                    logical,
                    1,
                    &physical,
                    &is_hole
            ),
            "allocate first double indirect logical block"
    );

    printf("logical %u -> physical %u\n", logical, physical);

    check_true(is_hole == 0, "double indirect block not hole");
    check_true(myfs_block_is_valid_data_block(physical), "double indirect data block valid");

    myfs_inode_t inode;

    check(
            myfs_inode_read(inode_id, &inode),
            "read inode after double indirect allocation"
    );

    check_true(inode.indirect2 != 0, "indirect2 block allocated");
    check_true(myfs_block_is_valid_data_block(inode.indirect2), "indirect2 physical block valid");

    print_inode_blocks(inode_id);
}

static void test_release_one_block(myfs_ino_t inode_id) {
    printf("\n[TEST] release one logical block\n");

    myfs_superblock_t *sb = myfs_super_get();

    check_true(sb != NULL, "superblock available");

    uint32_t free_before = sb->free_blocks_count;

    /*
     * 释放 logical block 0。
     */
    check(
            myfs_inode_release_data_block(inode_id, 0),
            "release direct logical block 0"
    );

    check_true(
            sb->free_blocks_count == free_before + 1,
            "free_blocks_count increases after releasing direct block"
    );

    myfs_block_t physical;
    int is_hole;

    check(
            myfs_inode_bmap(inode_id, 0, &physical, &is_hole),
            "bmap released logical block 0"
    );

    check_true(is_hole == 1, "released logical block becomes hole");

    print_inode_blocks(inode_id);
}

static void test_release_all_blocks(myfs_ino_t inode_id) {
    printf("\n[TEST] release all inode blocks\n");

    myfs_superblock_t *sb = myfs_super_get();

    check_true(sb != NULL, "superblock available");

    check(
            myfs_inode_release_all_blocks(inode_id),
            "myfs_inode_release_all_blocks"
    );

    myfs_inode_t inode;

    check(
            myfs_inode_read(inode_id, &inode),
            "read inode after release all"
    );

    check_true(inode.block_count == 0, "inode block_count becomes 0");
    check_true(inode.indirect1 == 0, "inode indirect1 cleared");
    check_true(inode.indirect2 == 0, "inode indirect2 cleared");

    for (uint32_t i = 0; i < MYFS_DIRECT_BLOCKS; i++) {
        check_true(inode.direct[i] == 0, "direct block pointer cleared");
    }

    print_inode_blocks(inode_id);
}

static void test_inode_free_releases_blocks(void) {
    printf("\n[TEST] inode_free releases mapped blocks\n");

    myfs_superblock_t *sb = myfs_super_get();

    check_true(sb != NULL, "superblock available");

    myfs_ino_t inode_id;

    check(
            myfs_inode_alloc(
                    MYFS_INODE_FILE,
                    0644,
                    0,
                    0,
                    &inode_id
            ),
            "allocate temp inode"
    );

    uint32_t free_before_alloc_blocks = sb->free_blocks_count;

    myfs_block_t physical;
    int is_hole;

    /*
     * 给这个 inode 分配几个不同区域的逻辑块。
     */
    check(
            myfs_inode_get_data_block(
                    inode_id,
                    0,
                    1,
                    &physical,
                    &is_hole
            ),
            "allocate temp direct block"
    );

    check(
            myfs_inode_get_data_block(
                    inode_id,
                    MYFS_DIRECT_BLOCKS,
                    1,
                    &physical,
                    &is_hole
            ),
            "allocate temp single indirect block"
    );

    check(
            myfs_inode_get_data_block(
                    inode_id,
                    MYFS_DIRECT_BLOCKS + MYFS_POINTERS_PER_BLOCK,
                    1,
                    &physical,
                    &is_hole
            ),
            "allocate temp double indirect block"
    );

    check_true(
            sb->free_blocks_count < free_before_alloc_blocks,
            "free_blocks_count decreases after mapped block allocation"
    );

    /*
     * inode_free 应该释放该 inode 占用的全部数据块和间接块。
     */
    check(
            myfs_inode_free(inode_id),
            "myfs_inode_free temp inode"
    );

    check_true(
            sb->free_blocks_count == free_before_alloc_blocks,
            "inode_free releases all mapped blocks"
    );
}

int main(void) {
    const char *disk_path = "block_map_test.img";

    printf("========================================\n");
    printf(" MYFS Stage4 Block Map Test\n");
    printf("========================================\n");

    check(
            myfs_mkfs(disk_path, 32768, 4096),
            "myfs_mkfs"
    );

    check(
            myfs_mount(disk_path),
            "myfs_mount"
    );

    myfs_ino_t inode_id;

    check(
            myfs_inode_alloc(
                    MYFS_INODE_FILE,
                    0644,
                    1000,
                    1000,
                    &inode_id
            ),
            "allocate file inode"
    );

    test_direct_block(inode_id);

    test_single_indirect_block(inode_id);

    test_double_indirect_block(inode_id);

    test_release_one_block(inode_id);

    test_release_all_blocks(inode_id);

    test_inode_free_releases_blocks();

    check(
            myfs_umount(),
            "myfs_umount"
    );

    printf("\n========================================\n");
    printf(" BLOCK MAP TEST PASSED\n");
    printf("========================================\n");

    return 0;
}