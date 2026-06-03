#include "mount.h"
#include "superblock.h"
#include "inode.h"
#include "block_alloc.h"
#include "block_map.h"
#include "raw_file.h"
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

static void test_small_write_read(myfs_ino_t inode_id) {
    printf("\n[TEST] small write/read\n");

    const char *text = "hello myfs raw file layer";

    uint32_t written = 0;

    check(
            myfs_inode_write_data(
                    inode_id,
                    0,
                    text,
                    (uint32_t) strlen(text),
                    &written
            ),
            "write small data"
    );

    check_true(written == strlen(text), "small write size correct");

    char buf[128];

    memset(buf, 0, sizeof(buf));

    uint32_t read = 0;

    check(
            myfs_inode_read_data(
                    inode_id,
                    0,
                    buf,
                    sizeof(buf) - 1,
                    &read
            ),
            "read small data"
    );

    check_true(read == strlen(text), "small read size correct");
    check_true(strcmp(buf, text) == 0, "small read content correct");

    myfs_inode_t inode;

    check(myfs_inode_read(inode_id, &inode), "read inode after small write");

    check_true(inode.size == strlen(text), "inode size after small write correct");
    check_true(inode.block_count >= 1, "inode block_count after small write greater than 0");
}

static void test_cross_block_write_read(myfs_ino_t inode_id) {
    printf("\n[TEST] cross-block write/read\n");

    /*
     * 构造一个超过 4KB 的数据，测试跨块写入。
     */
    const uint32_t data_size = MYFS_BLOCK_SIZE + 200;

    unsigned char *write_buf = (unsigned char *) malloc(data_size);
    unsigned char *read_buf = (unsigned char *) malloc(data_size);

    check_true(write_buf != NULL, "malloc write_buf");
    check_true(read_buf != NULL, "malloc read_buf");

    for (uint32_t i = 0; i < data_size; i++) {
        write_buf[i] = (unsigned char) ('A' + (i % 26));
    }

    memset(read_buf, 0, data_size);

    uint32_t written = 0;

    check(
            myfs_inode_write_data(
                    inode_id,
                    0,
                    write_buf,
                    data_size,
                    &written
            ),
            "write cross-block data"
    );

    check_true(written == data_size, "cross-block written size correct");

    uint32_t read = 0;

    check(
            myfs_inode_read_data(
                    inode_id,
                    0,
                    read_buf,
                    data_size,
                    &read
            ),
            "read cross-block data"
    );

    check_true(read == data_size, "cross-block read size correct");
    check_true(memcmp(write_buf, read_buf, data_size) == 0, "cross-block content correct");

    free(write_buf);
    free(read_buf);
}

static void test_sparse_file(myfs_ino_t inode_id) {
    printf("\n[TEST] sparse file behavior\n");

    /*
     * 在远位置写入，形成稀疏文件。
     */
    const uint64_t sparse_offset = (uint64_t) MYFS_BLOCK_SIZE * 10 + 123;

    const char *text = "sparse tail";

    uint32_t written = 0;

    check(
            myfs_inode_write_data(
                    inode_id,
                    sparse_offset,
                    text,
                    (uint32_t) strlen(text),
                    &written
            ),
            "write sparse tail"
    );

    check_true(written == strlen(text), "sparse write size correct");

    /*
     * 读取中间空洞区域，应该全是 0。
     */
    unsigned char hole_buf[64];

    memset(hole_buf, 0xFF, sizeof(hole_buf));

    uint32_t read = 0;

    check(
            myfs_inode_read_data(
                    inode_id,
                    MYFS_BLOCK_SIZE * 5,
                    hole_buf,
                    sizeof(hole_buf),
                    &read
            ),
            "read sparse hole"
    );

    check_true(read == sizeof(hole_buf), "sparse hole read size correct");

    int all_zero = 1;

    for (uint32_t i = 0; i < sizeof(hole_buf); i++) {
        if (hole_buf[i] != 0) {
            all_zero = 0;
            break;
        }
    }

    check_true(all_zero, "sparse hole reads as zero");

    /*
     * 读取远位置写入的数据。
     */
    char tail_buf[64];

    memset(tail_buf, 0, sizeof(tail_buf));

    check(
            myfs_inode_read_data(
                    inode_id,
                    sparse_offset,
                    tail_buf,
                    sizeof(tail_buf) - 1,
                    &read
            ),
            "read sparse tail"
    );

    check_true(read == strlen(text), "sparse tail read size correct");
    check_true(strcmp(tail_buf, text) == 0, "sparse tail content correct");
}

static void test_truncate(myfs_ino_t inode_id) {
    printf("\n[TEST] truncate data\n");

    const char *text = "abcdefghijklmnopqrstuvwxyz";

    uint32_t written = 0;

    check(
            myfs_inode_write_data(
                    inode_id,
                    0,
                    text,
                    (uint32_t) strlen(text),
                    &written
            ),
            "write before truncate"
    );

    check(
            myfs_inode_truncate_data(
                    inode_id,
                    10
            ),
            "truncate to 10 bytes"
    );

    myfs_inode_t inode;

    check(myfs_inode_read(inode_id, &inode), "read inode after truncate");

    check_true(inode.size == 10, "inode size after truncate is 10");

    char buf[64];

    memset(buf, 0, sizeof(buf));

    uint32_t read = 0;

    check(
            myfs_inode_read_data(
                    inode_id,
                    0,
                    buf,
                    sizeof(buf) - 1,
                    &read
            ),
            "read after truncate"
    );

    check_true(read == 10, "read size after truncate is 10");
    check_true(strncmp(buf, "abcdefghij", 10) == 0, "truncate content correct");

    /*
     * 扩大文件到更大尺寸，不应立即分配中间块。
     */
    check(
            myfs_inode_truncate_data(
                    inode_id,
                    MYFS_BLOCK_SIZE * 20
            ),
            "expand truncate to sparse size"
    );

    check(myfs_inode_read(inode_id, &inode), "read inode after expand truncate");

    check_true(inode.size == MYFS_BLOCK_SIZE * 20, "inode size after expand truncate correct");

    unsigned char sparse_buf[32];

    memset(sparse_buf, 0xFF, sizeof(sparse_buf));

    check(
            myfs_inode_read_data(
                    inode_id,
                    MYFS_BLOCK_SIZE * 10,
                    sparse_buf,
                    sizeof(sparse_buf),
                    &read
            ),
            "read expanded sparse area"
    );

    int all_zero = 1;

    for (uint32_t i = 0; i < sizeof(sparse_buf); i++) {
        if (sparse_buf[i] != 0) {
            all_zero = 0;
            break;
        }
    }

    check_true(all_zero, "expanded sparse area reads as zero");
}

static void test_zero_range(myfs_ino_t inode_id) {
    printf("\n[TEST] zero range\n");

    const char *text = "0123456789ABCDEFGHIJ";

    uint32_t written = 0;

    check(
            myfs_inode_write_data(
                    inode_id,
                    0,
                    text,
                    (uint32_t) strlen(text),
                    &written
            ),
            "write before zero_range"
    );

    check(
            myfs_inode_zero_range(
                    inode_id,
                    5,
                    5
            ),
            "zero range offset 5 length 5"
    );

    char buf[64];

    memset(buf, 0, sizeof(buf));

    uint32_t read = 0;

    check(
            myfs_inode_read_data(
                    inode_id,
                    0,
                    buf,
                    20,
                    &read
            ),
            "read after zero_range"
    );

    check_true(read == 20, "read size after zero_range correct");

    check_true(buf[0] == '0', "zero_range prefix ok");
    check_true(buf[4] == '4', "zero_range prefix end ok");
    check_true(buf[5] == 0, "zero_range zero begin ok");
    check_true(buf[9] == 0, "zero_range zero end ok");
    check_true(buf[10] == 'A', "zero_range suffix ok");
}

static void test_inode_free_releases_raw_file_blocks(void) {
    printf("\n[TEST] inode_free releases raw file blocks\n");

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

    uint32_t free_before = sb->free_blocks_count;

    /*
     * 写入超过 direct + indirect1 的位置，确保 direct/indirect 都被使用。
     */
    uint64_t offset =
            (uint64_t) (MYFS_DIRECT_BLOCKS + MYFS_POINTERS_PER_BLOCK) *
            MYFS_BLOCK_SIZE;

    const char *text = "deep block data";

    uint32_t written = 0;

    check(
            myfs_inode_write_data(
                    inode_id,
                    offset,
                    text,
                    (uint32_t) strlen(text),
                    &written
            ),
            "write deep block data"
    );

    check_true(sb->free_blocks_count < free_before, "free blocks decreased after deep write");

    check(
            myfs_inode_free(inode_id),
            "inode_free temp raw file inode"
    );

    check_true(sb->free_blocks_count == free_before, "inode_free releases raw file blocks");
}

int main(void) {
    const char *disk_path = "raw_file_test.img";

    printf("========================================\n");
    printf(" MYFS Stage5 Raw File Test\n");
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

    test_small_write_read(inode_id);

    test_cross_block_write_read(inode_id);

    test_sparse_file(inode_id);

    test_truncate(inode_id);

    test_zero_range(inode_id);

    test_inode_free_releases_raw_file_blocks();

    check(
            myfs_umount(),
            "myfs_umount"
    );

    printf("\n========================================\n");
    printf(" RAW FILE TEST PASSED\n");
    printf("========================================\n");

    return 0;
}