#include "mount.h"
#include "inode.h"
#include "raw_file.h"
#include "fsck.h"
#include "superblock.h"
#include "error.h"
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "block_map.h"


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


static void run_fsck_expect_clean(const char *msg) {
    myfs_fsck_options_t options;
    options.repair = 0;
    options.verbose = 1;

    myfs_fsck_result_t result;

    check(
            myfs_fsck_run(&options, &result),
            msg
    );

    myfs_fsck_print_result(&result);

    check_true(
            result.errors_found == 0,
            "fsck reports no errors"
    );
}


static void create_test_file_with_blocks(void) {
    printf("\n[TEST] create file with direct / indirect / double indirect data\n");

    myfs_ino_t inode_id;

    check(
            myfs_inode_alloc(
                    MYFS_INODE_FILE,
                    0644,
                    1000,
                    1000,
                    &inode_id
            ),
            "allocate test file inode"
    );

    const char *small_text = "hello fsck direct block";

    uint32_t written = 0;

    check(
            myfs_inode_write_data(
                    inode_id,
                    0,
                    small_text,
                    (uint32_t) strlen(small_text),
                    &written
            ),
            "write direct block data"
    );

    /*
     * 写入一级间接区域。
     */
    const char *indirect_text = "hello fsck single indirect block";

    check(
            myfs_inode_write_data(
                    inode_id,
                    (uint64_t) MYFS_DIRECT_BLOCKS * MYFS_BLOCK_SIZE,
                    indirect_text,
                    (uint32_t) strlen(indirect_text),
                    &written
            ),
            "write single indirect data"
    );

    /*
     * 写入二级间接区域。
     */
    const char *double_text = "hello fsck double indirect block";

    uint64_t double_offset =
            (uint64_t) (MYFS_DIRECT_BLOCKS + MYFS_POINTERS_PER_BLOCK)
            * MYFS_BLOCK_SIZE;

    check(
            myfs_inode_write_data(
                    inode_id,
                    double_offset,
                    double_text,
                    (uint32_t) strlen(double_text),
                    &written
            ),
            "write double indirect data"
    );

    myfs_inode_t inode;

    check(
            myfs_inode_read(inode_id, &inode),
            "read inode after fsck test writes"
    );

    printf("test inode_id    = %u\n", inode.inode_id);
    printf("test inode size  = %llu\n", (unsigned long long) inode.size);
    printf("test block_count = %u\n", inode.block_count);

    check_true(inode.block_count > 0, "test inode has allocated blocks");
}


static void test_fsck_after_mkfs(void) {
    printf("\n[TEST] fsck after mkfs\n");

    run_fsck_expect_clean("fsck after mkfs");
}


static void test_fsck_after_file_writes(void) {
    printf("\n[TEST] fsck after file writes\n");

    create_test_file_with_blocks();

    run_fsck_expect_clean("fsck after file writes");
}


static void test_fsck_after_truncate_and_free(void) {
    printf("\n[TEST] fsck after truncate and inode free\n");

    myfs_ino_t inode_id;

    check(
            myfs_inode_alloc(
                    MYFS_INODE_FILE,
                    0644,
                    0,
                    0,
                    &inode_id
            ),
            "allocate truncate test inode"
    );

    char data[8192];

    memset(data, 'A', sizeof(data));

    uint32_t written = 0;

    check(
            myfs_inode_write_data(
                    inode_id,
                    0,
                    data,
                    sizeof(data),
                    &written
            ),
            "write truncate test data"
    );

    check(
            myfs_inode_truncate_data(
                    inode_id,
                    1024
            ),
            "truncate test inode"
    );

    run_fsck_expect_clean("fsck after truncate");

    check(
            myfs_inode_free(inode_id),
            "free truncate test inode"
    );

    run_fsck_expect_clean("fsck after inode free");
}


int main(void) {
    const char *disk_path = "fsck_test.img";

    printf("========================================\n");
    printf(" MYFS Stage7 FSCK Test\n");
    printf("========================================\n");

    check(
            myfs_mkfs(disk_path, 32768, 4096),
            "myfs_mkfs"
    );

    check(
            myfs_mount(disk_path),
            "myfs_mount"
    );

    test_fsck_after_mkfs();

    test_fsck_after_file_writes();

    test_fsck_after_truncate_and_free();

    check(
            myfs_umount(),
            "myfs_umount"
    );

    printf("\n========================================\n");
    printf(" FSCK TEST PASSED\n");
    printf("========================================\n");

    return 0;
}