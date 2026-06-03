#include "physical_api.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "error.h"


static void check(int ret, const char *msg) {
    if (ret != MYFS_OK) {
        fprintf(stderr, "[FAIL] %s: %s\n", msg, myfs_phys_strerror(ret));
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


static void test_lifecycle(void) {
    printf("\n[TEST] physical api lifecycle\n");

    check(
            myfs_phys_format(
                    "physical_api_test.img",
                    32768,
                    4096
            ),
            "myfs_phys_format"
    );

    check(
            myfs_phys_mount("physical_api_test.img"),
            "myfs_phys_mount"
    );

    check_true(
            myfs_phys_is_mounted() == 1,
            "myfs_phys_is_mounted true"
    );
}


static myfs_ino_t test_inode_create(void) {
    printf("\n[TEST] physical api inode create/info\n");

    myfs_ino_t inode_id;

    check(
            myfs_phys_inode_create(
                    MYFS_PHYS_INODE_FILE,
                    0644,
                    1000,
                    1000,
                    &inode_id
            ),
            "myfs_phys_inode_create"
    );

    printf("created inode = %u\n", inode_id);

    myfs_phys_inode_info_t info;

    check(
            myfs_phys_inode_get_info(
                    inode_id,
                    &info
            ),
            "myfs_phys_inode_get_info"
    );

    check_true(info.inode_id == inode_id, "inode id correct");
    check_true(info.type == MYFS_PHYS_INODE_FILE, "inode type file");
    check_true(info.mode == 0644, "inode mode 0644");
    check_true(info.uid == 1000, "inode uid 1000");
    check_true(info.gid == 1000, "inode gid 1000");

    check(
            myfs_phys_inode_set_attr(
                    inode_id,
                    0600,
                    2000,
                    2000
            ),
            "myfs_phys_inode_set_attr"
    );

    check(
            myfs_phys_inode_get_info(
                    inode_id,
                    &info
            ),
            "get info after set attr"
    );

    check_true(info.mode == 0600, "inode mode changed to 0600");
    check_true(info.uid == 2000, "inode uid changed");
    check_true(info.gid == 2000, "inode gid changed");

    return inode_id;
}


static void test_raw_read_write(myfs_ino_t inode_id) {
    printf("\n[TEST] physical api raw read/write\n");

    const char *text = "hello from physical api";

    uint32_t written = 0;

    check(
            myfs_phys_write(
                    inode_id,
                    0,
                    text,
                    (uint32_t) strlen(text),
                    &written
            ),
            "myfs_phys_write"
    );

    check_true(
            written == strlen(text),
            "written size correct"
    );

    char buf[128];

    memset(buf, 0, sizeof(buf));

    uint32_t read = 0;

    check(
            myfs_phys_read(
                    inode_id,
                    0,
                    buf,
                    sizeof(buf) - 1,
                    &read
            ),
            "myfs_phys_read"
    );

    check_true(
            read == strlen(text),
            "read size correct"
    );

    check_true(
            strcmp(buf, text) == 0,
            "read content correct"
    );
}


static void test_bmap_truncate_zero(myfs_ino_t inode_id) {
    printf("\n[TEST] physical api bmap/truncate/zero_range\n");

    myfs_phys_bmap_result_t bmap;

    check(
            myfs_phys_bmap(
                    inode_id,
                    0,
                    &bmap
            ),
            "myfs_phys_bmap"
    );

    check_true(
            bmap.is_hole == 0,
            "logical block 0 is mapped"
    );

    printf("logical %u -> physical %u\n",
           bmap.logical_block,
           bmap.physical_block);

    check(
            myfs_phys_zero_range(
                    inode_id,
                    0,
                    5
            ),
            "myfs_phys_zero_range"
    );

    char buf[64];

    memset(buf, 0xFF, sizeof(buf));

    uint32_t read = 0;

    check(
            myfs_phys_read(
                    inode_id,
                    0,
                    buf,
                    sizeof(buf),
                    &read
            ),
            "read after zero range"
    );

    check_true(
            buf[0] == 0,
            "zero range worked at offset 0"
    );

    check(
            myfs_phys_truncate(
                    inode_id,
                    3
            ),
            "myfs_phys_truncate"
    );

    myfs_phys_inode_info_t info;

    check(
            myfs_phys_inode_get_info(
                    inode_id,
                    &info
            ),
            "get info after truncate"
    );

    check_true(
            info.size == 3,
            "inode size after truncate is 3"
    );
}


static void test_statfs_cache_fsck_debug(myfs_ino_t inode_id) {
    printf("\n[TEST] physical api statfs/cache/fsck/debug\n");

    check(
            myfs_phys_cache_init(8),
            "myfs_phys_cache_init"
    );

    myfs_phys_statfs_t st;

    check(
            myfs_phys_statfs(&st),
            "myfs_phys_statfs"
    );

    printf("used_inodes = %u\n", st.used_inodes);
    printf("used_blocks = %u\n", st.used_blocks);

    check_true(st.used_inodes >= 2, "used inode count valid");

    myfs_phys_cache_stats_t cache_stats;

    check(
            myfs_phys_cache_get_stats(&cache_stats),
            "myfs_phys_cache_get_stats"
    );

    myfs_phys_fsck_result_t fsck_result;

    check(
            myfs_phys_fsck(
                    0,
                    &fsck_result
            ),
            "myfs_phys_fsck"
    );

    check_true(
            fsck_result.errors_found == 0,
            "fsck reports no errors"
    );

    check(
            myfs_phys_debug_statfs(),
            "myfs_phys_debug_statfs"
    );

    check(
            myfs_phys_debug_inode(inode_id),
            "myfs_phys_debug_inode"
    );

    check(
            myfs_phys_debug_bmap(inode_id, 0),
            "myfs_phys_debug_bmap"
    );

    check(
            myfs_phys_debug_cache(),
            "myfs_phys_debug_cache"
    );

    check(
            myfs_phys_sync(),
            "myfs_phys_sync"
    );

    check(
            myfs_phys_cache_shutdown(),
            "myfs_phys_cache_shutdown"
    );
}


static void test_inode_free_and_umount(myfs_ino_t inode_id) {
    printf("\n[TEST] physical api inode free and umount\n");

    check(
            myfs_phys_inode_free(inode_id),
            "myfs_phys_inode_free"
    );

    check(
            myfs_phys_umount(),
            "myfs_phys_umount"
    );

    check_true(
            myfs_phys_is_mounted() == 0,
            "not mounted after umount"
    );
}


int main(void) {
    printf("========================================\n");
    printf(" MYFS Physical API Test\n");
    printf("========================================\n");

    test_lifecycle();

    myfs_ino_t inode_id = test_inode_create();

    test_raw_read_write(inode_id);

    test_bmap_truncate_zero(inode_id);

    test_statfs_cache_fsck_debug(inode_id);

    test_inode_free_and_umount(inode_id);

    printf("\n========================================\n");
    printf(" PHYSICAL API TEST PASSED\n");
    printf("========================================\n");

    return 0;
}