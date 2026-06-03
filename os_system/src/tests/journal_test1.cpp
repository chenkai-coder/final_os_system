#include "mount.h"
#include "superblock.h"
#include "journal.h"
#include "disk.h"
#include "error.h"
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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


static void test_journal_format_clean(void) {
    printf("\n[TEST] journal format clean\n");

    int is_clean = 0;

    check(
            myfs_journal_is_clean(&is_clean),
            "myfs_journal_is_clean"
    );

    check_true(is_clean == 1, "journal is clean after mkfs");
}


static void test_journal_write_metadata_block(void) {
    printf("\n[TEST] journal write metadata block\n");

    myfs_superblock_t *sb = myfs_super_get();

    check_true(sb != NULL, "superblock available");

    myfs_block_t target = sb->data_block_start;

    unsigned char write_buf[MYFS_BLOCK_SIZE];
    unsigned char read_buf[MYFS_BLOCK_SIZE];

    memset(write_buf, 0, sizeof(write_buf));
    memset(read_buf, 0, sizeof(read_buf));

    strcpy((char *) write_buf, "journal metadata write test");

    check(
            myfs_journal_write_metadata_block(target, write_buf),
            "myfs_journal_write_metadata_block"
    );

    check(
            myfs_disk_read_block(target, read_buf),
            "read target block after journal write"
    );

    check_true(
            strcmp((char *) write_buf, (char *) read_buf) == 0,
            "journal write applied to target block"
    );

    int is_clean = 0;

    check(
            myfs_journal_is_clean(&is_clean),
            "journal clean after committed write"
    );

    check_true(is_clean == 1, "journal is clean after normal commit");
}


static void test_journal_recover_committed_record(void) {
    printf("\n[TEST] journal recover committed record\n");

    myfs_superblock_t *sb = myfs_super_get();

    check_true(sb != NULL, "superblock available");

    myfs_block_t target = sb->data_block_start + 1;

    unsigned char old_buf[MYFS_BLOCK_SIZE];
    unsigned char new_buf[MYFS_BLOCK_SIZE];
    unsigned char read_buf[MYFS_BLOCK_SIZE];

    memset(old_buf, 0, sizeof(old_buf));
    memset(new_buf, 0, sizeof(new_buf));
    memset(read_buf, 0, sizeof(read_buf));

    strcpy((char *) old_buf, "old target content");
    strcpy((char *) new_buf, "new content from committed journal");

    /*
     * 先写入旧内容。
     */
    check(
            myfs_disk_write_block(target, old_buf),
            "write old target content"
    );

    /*
     * 构造一个 committed 但尚未回放的 journal。
     *
     * 这模拟：
     * journal header 已经 committed，
     * 但是系统崩溃，还没来得及把数据写入目标块。
     */
    check(
            myfs_journal_debug_create_committed_record(target, new_buf),
            "create committed journal record"
    );

    int is_clean = 1;

    check(
            myfs_journal_is_clean(&is_clean),
            "journal is not clean after debug committed record"
    );

    check_true(is_clean == 0, "journal is dirty/committed before recover");

    /*
     * 此时目标块应该仍然是旧内容。
     */
    check(
            myfs_disk_read_block(target, read_buf),
            "read target before recovery"
    );

    check_true(
            strcmp((char *) old_buf, (char *) read_buf) == 0,
            "target still old before recovery"
    );

    /*
     * 执行恢复。
     */
    check(
            myfs_journal_recover(),
            "myfs_journal_recover"
    );

    memset(read_buf, 0, sizeof(read_buf));

    check(
            myfs_disk_read_block(target, read_buf),
            "read target after recovery"
    );

    check_true(
            strcmp((char *) new_buf, (char *) read_buf) == 0,
            "target updated after recovery"
    );

    check(
            myfs_journal_is_clean(&is_clean),
            "journal clean after recovery"
    );

    check_true(is_clean == 1, "journal is clean after recovery");
}


int main(void) {
    const char *disk_path = "journal_test.img";

    printf("========================================\n");
    printf(" MYFS Stage8 Journal Test\n");
    printf("========================================\n");

    check(
            myfs_mkfs(disk_path, 32768, 4096),
            "myfs_mkfs"
    );

    check(
            myfs_mount(disk_path),
            "myfs_mount"
    );

    test_journal_format_clean();

    test_journal_write_metadata_block();

    test_journal_recover_committed_record();

    check(
            myfs_umount(),
            "myfs_umount"
    );

    printf("\n========================================\n");
    printf(" JOURNAL TEST PASSED\n");
    printf("========================================\n");

    return 0;
}