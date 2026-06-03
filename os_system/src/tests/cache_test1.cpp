#include "mount.h"
#include "superblock.h"
#include "cache.h"
#include "sync.h"
#include "disk.h"
#include "config.h"
#include "error.h"

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

static void print_cache_stats(void) {
    myfs_cache_stats_t stats;

    myfs_cache_get_stats(&stats);

    printf("\n---------- CACHE STATS ----------\n");
    printf("read_count        = %llu\n", (unsigned long long) stats.read_count);
    printf("write_count       = %llu\n", (unsigned long long) stats.write_count);
    printf("hit_count         = %llu\n", (unsigned long long) stats.hit_count);
    printf("miss_count        = %llu\n", (unsigned long long) stats.miss_count);
    printf("evict_count       = %llu\n", (unsigned long long) stats.evict_count);
    printf("dirty_flush_count = %llu\n", (unsigned long long) stats.dirty_flush_count);
    printf("---------------------------------\n\n");
}


static void test_cache_basic_read_write(void) {
    printf("\n[TEST] cache basic read/write\n");

    myfs_superblock_t *sb = myfs_super_get();

    check_true(sb != NULL, "superblock available");

    myfs_block_t block = sb->data_block_start;

    unsigned char write_buf[MYFS_BLOCK_SIZE];
    unsigned char read_buf[MYFS_BLOCK_SIZE];

    memset(write_buf, 0, sizeof(write_buf));
    memset(read_buf, 0, sizeof(read_buf));

    strcpy((char *) write_buf, "hello cache write-back layer");

    /*
     * 写入缓存，不立刻写磁盘。
     */
    check(
            myfs_cache_write_block(block, write_buf, 0),
            "myfs_cache_write_block"
    );

    /*
     * 立即从缓存读，应该命中并读出刚写入的数据。
     */
    check(
            myfs_cache_read_block(block, read_buf),
            "myfs_cache_read_block"
    );

    check_true(
            strcmp((char *) write_buf, (char *) read_buf) == 0,
            "cache read content equals written content"
    );

    print_cache_stats();
}


static void test_cache_flush_to_disk(void) {
    printf("\n[TEST] cache flush to disk\n");

    myfs_superblock_t *sb = myfs_super_get();

    check_true(sb != NULL, "superblock available");

    myfs_block_t block = sb->data_block_start + 1;

    unsigned char write_buf[MYFS_BLOCK_SIZE];
    unsigned char disk_buf[MYFS_BLOCK_SIZE];

    memset(write_buf, 0, sizeof(write_buf));
    memset(disk_buf, 0, sizeof(disk_buf));

    strcpy((char *) write_buf, "this block should be flushed to disk");

    check(
            myfs_cache_write_block(block, write_buf, 0),
            "cache write before flush"
    );

    check(
            myfs_cache_flush_block(block),
            "myfs_cache_flush_block"
    );

    /*
     * 绕过缓存，直接从磁盘读取，验证 flush 是否真的落盘。
     */
    check(
            myfs_disk_read_block(block, disk_buf),
            "direct disk read after cache flush"
    );

    check_true(
            strcmp((char *) write_buf, (char *) disk_buf) == 0,
            "flushed disk content correct"
    );

    print_cache_stats();
}


static void test_lru_eviction(void) {
    printf("\n[TEST] LRU eviction\n");

    myfs_superblock_t *sb = myfs_super_get();

    check_true(sb != NULL, "superblock available");

    /*
     * 当前测试中缓存容量设置为 4。
     * 连续写入 8 个不同块，应触发淘汰。
     */
    for (uint32_t i = 0; i < 8; i++) {
        unsigned char buf[MYFS_BLOCK_SIZE];

        memset(buf, 0, sizeof(buf));

        snprintf(
                (char *) buf,
                sizeof(buf),
                "LRU test block %u",
                i
        );

        check(
                myfs_cache_write_block(
                        sb->data_block_start + 10 + i,
                        buf,
                        0
                ),
                "cache write for LRU"
        );
    }

    myfs_cache_stats_t stats;

    myfs_cache_get_stats(&stats);

    check_true(
            stats.evict_count > 0,
            "LRU eviction happened"
    );

    /*
     * flush all，确保被缓存但尚未淘汰的 dirty 块也落盘。
     */
    check(
            myfs_cache_flush_all(),
            "myfs_cache_flush_all after LRU"
    );

    print_cache_stats();
}


static void test_sync_interface(void) {
    printf("\n[TEST] sync interface\n");

    myfs_superblock_t *sb = myfs_super_get();

    check_true(sb != NULL, "superblock available");

    myfs_block_t block = sb->data_block_start + 50;

    unsigned char write_buf[MYFS_BLOCK_SIZE];
    unsigned char disk_buf[MYFS_BLOCK_SIZE];

    memset(write_buf, 0, sizeof(write_buf));
    memset(disk_buf, 0, sizeof(disk_buf));

    strcpy((char *) write_buf, "sync interface test");

    check(
            myfs_cache_write_block(block, write_buf, 0),
            "cache write before sync"
    );

    check(
            myfs_sync(),
            "myfs_sync"
    );

    check(
            myfs_disk_read_block(block, disk_buf),
            "direct disk read after sync"
    );

    check_true(
            strcmp((char *) write_buf, (char *) disk_buf) == 0,
            "sync flushed cache data to disk"
    );

    print_cache_stats();
}


int main(void) {
    const char *disk_path = "cache_test.img";

    printf("========================================\n");
    printf(" MYFS Stage6 Cache Test\n");
    printf("========================================\n");

    check(
            myfs_mkfs(disk_path, 32768, 4096),
            "myfs_mkfs"
    );

    check(
            myfs_mount(disk_path),
            "myfs_mount"
    );

    /*
     * 测试时故意设置较小容量，方便触发 LRU。
     */
    check(
            myfs_cache_init(4),
            "myfs_cache_init"
    );

    test_cache_basic_read_write();

    test_cache_flush_to_disk();

    test_lru_eviction();

    test_sync_interface();

    check(
            myfs_cache_shutdown(),
            "myfs_cache_shutdown"
    );

    check(
            myfs_umount(),
            "myfs_umount"
    );

    printf("\n========================================\n");
    printf(" CACHE TEST PASSED\n");
    printf("========================================\n");

    return 0;
}