#include "mount.h"
#include "inode.h"
#include "raw_file.h"
#include "block_map.h"
#include "cache.h"
#include "debug.h"
#include "error.h"
#include "config.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>


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


static myfs_ino_t create_sample_inode(void) {
    myfs_ino_t inode_id;

    check(
            myfs_inode_alloc(
                    MYFS_INODE_FILE,
                    0644,
                    1000,
                    1000,
                    &inode_id
            ),
            "allocate sample inode"
    );

    const char *text = "hello debug module";

    uint32_t written = 0;

    check(
            myfs_inode_write_data(
                    inode_id,
                    0,
                    text,
                    (uint32_t) strlen(text),
                    &written
            ),
            "write sample data"
    );

    /*
     * 写入一级间接区域。
     */
    check(
            myfs_inode_write_data(
                    inode_id,
                    (uint64_t) MYFS_DIRECT_BLOCKS * MYFS_BLOCK_SIZE,
                    text,
                    (uint32_t) strlen(text),
                    &written
            ),
            "write sample indirect data"
    );

    return inode_id;
}


int main(void) {
    const char *disk_path = "debug_test.img";

    printf("========================================\n");
    printf(" MYFS Debug Interface Test\n");
    printf("========================================\n");

    check(
            myfs_mkfs(disk_path, 32768, 4096),
            "myfs_mkfs"
    );

    check(
            myfs_mount(disk_path),
            "myfs_mount"
    );

    check(
            myfs_cache_init(8),
            "myfs_cache_init"
    );

    myfs_ino_t inode_id = create_sample_inode();

    myfs_statfs_t statfs_info;

    check(
            myfs_statfs(&statfs_info),
            "myfs_statfs"
    );

    check_true(
            statfs_info.used_inodes >= 2,
            "used inode count valid"
    );

    check_true(
            statfs_info.used_blocks > 0,
            "used data block count valid"
    );

    check(
            myfs_debug_print_statfs(),
            "myfs_debug_print_statfs"
    );

    check(
            myfs_debug_dump_super(),
            "myfs_debug_dump_super"
    );

    check(
            myfs_debug_dump_inode(0),
            "myfs_debug_dump_inode root"
    );

    check(
            myfs_debug_dump_inode(inode_id),
            "myfs_debug_dump_inode sample"
    );

    check(
            myfs_debug_bmap(inode_id, 0),
            "myfs_debug_bmap direct"
    );

    check(
            myfs_debug_bmap(inode_id, MYFS_DIRECT_BLOCKS),
            "myfs_debug_bmap indirect"
    );

    check(
            myfs_debug_dump_free_group(),
            "myfs_debug_dump_free_group"
    );

    check(
            myfs_debug_dump_cache(),
            "myfs_debug_dump_cache"
    );

    /*
     * 打印超级块前 128 字节。
     */
    check(
            myfs_debug_hexdump_block(0, 128),
            "myfs_debug_hexdump_block superblock"
    );

    check(
            myfs_cache_shutdown(),
            "myfs_cache_shutdown"
    );

    check(
            myfs_umount(),
            "myfs_umount"
    );

    printf("\n========================================\n");
    printf(" DEBUG TEST PASSED\n");
    printf("========================================\n");

    return 0;
}