#include "mount.h"
#include "disk.h"
#include "layout.h"
#include "superblock.h"
#include "inode.h"
#include "config.h"
#include "error.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"

/*
 * stage2_full_test.c
 * ------------------------------------------------------------
 * 当前阶段完整功能测试文件。
 *
 * 测试范围：
 *
 * 1. layout 磁盘布局计算
 * 2. disk.img 创建、打开、关闭
 * 3. 物理块读写
 * 4. mkfs 格式化
 * 5. mount 挂载
 * 6. superblock 超级块字段检查
 * 7. clean / dirty 状态检查
 * 8. inode 位图检查
 * 9. root inode 检查
 * 10. inode 分配
 * 11. inode 读取
 * 12. inode 写回
 * 13. inode 表直接读取
 * 14. link_count 增减
 * 15. open_count 增减
 * 16. inode 类型判断
 * 17. inode 释放
 * 18. umount 卸载
 *
 * 注意：
 * 当前阶段还没有数据块分配器，因此物理块读写测试只选择数据区起始块，
 * 不能把它当成真正文件数据读写。
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


static void print_superblock(const myfs_superblock_t *sb) {
    printf("\n========== SUPERBLOCK ==========\n");
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
    printf("================================\n\n");
}


static void print_inode(const myfs_inode_t *inode) {
    printf("\n---------- INODE ----------\n");
    printf("inode_id    = %u\n", inode->inode_id);
    printf("type        = %u\n", inode->type);
    printf("mode        = %o\n", inode->mode);
    printf("uid         = %u\n", inode->uid);
    printf("gid         = %u\n", inode->gid);
    printf("size        = %llu\n", (unsigned long long) inode->size);
    printf("block_count = %u\n", inode->block_count);
    printf("link_count  = %u\n", inode->link_count);
    printf("open_count  = %u\n", inode->open_count);
    printf("direct[0]   = %u\n", inode->direct[0]);
    printf("indirect1   = %u\n", inode->indirect1);
    printf("indirect2   = %u\n", inode->indirect2);
    printf("---------------------------\n\n");
}


static void test_layout(uint32_t total_blocks, uint32_t total_inodes) {
    printf("\n[TEST] layout build and check\n");

    myfs_layout_t layout;

    check(
            myfs_layout_build(total_blocks, total_inodes, &layout),
            "myfs_layout_build"
    );

    check(
            myfs_layout_check(total_blocks, &layout),
            "myfs_layout_check"
    );

    printf("superblock_start      = %u\n", layout.superblock_start);
    printf("inode_bitmap_start    = %u\n", layout.inode_bitmap_start);
    printf("inode_bitmap_blocks   = %u\n", layout.inode_bitmap_blocks);
    printf("inode_table_start     = %u\n", layout.inode_table_start);
    printf("inode_table_blocks    = %u\n", layout.inode_table_blocks);
    printf("journal_start         = %u\n", layout.journal_start);
    printf("journal_blocks        = %u\n", layout.journal_blocks);
    printf("data_block_start      = %u\n", layout.data_block_start);
    printf("data_blocks           = %u\n", layout.data_blocks);

    check_true(layout.superblock_start == 0, "superblock starts at block 0");
    check_true(layout.inode_bitmap_start > layout.superblock_start, "inode bitmap after superblock");
    check_true(layout.inode_table_start > layout.inode_bitmap_start, "inode table after inode bitmap");
    check_true(layout.journal_start > layout.inode_table_start, "journal after inode table");
    check_true(layout.data_block_start > layout.journal_start, "data area after journal");
    check_true(layout.data_block_start < total_blocks, "data area inside disk");
}


static void test_raw_disk_layer(const char *disk_path) {
    printf("\n[TEST] raw disk create/open/read/write/close\n");

    const uint32_t test_blocks = 128;

    check(
            myfs_disk_create(disk_path, test_blocks),
            "myfs_disk_create"
    );

    check(
            myfs_disk_open(disk_path),
            "myfs_disk_open"
    );

    check_true(myfs_disk_is_open() == 1, "disk is open");
    check_true(myfs_disk_total_blocks() == test_blocks, "disk total blocks correct");
    check_true(myfs_disk_block_size() == MYFS_BLOCK_SIZE, "disk block size correct");

    unsigned char write_buf[MYFS_BLOCK_SIZE];
    unsigned char read_buf[MYFS_BLOCK_SIZE];

    memset(write_buf, 0, sizeof(write_buf));
    memset(read_buf, 0, sizeof(read_buf));

    strcpy((char *) write_buf, "MYFS physical block read/write test");

    check(
            myfs_disk_write_block(10, write_buf),
            "myfs_disk_write_block"
    );

    check(
            myfs_disk_read_block(10, read_buf),
            "myfs_disk_read_block"
    );

    check_true(
            strcmp((char *) write_buf, (char *) read_buf) == 0,
            "read block content equals written content"
    );

    check(
            myfs_disk_close(),
            "myfs_disk_close"
    );

    check_true(myfs_disk_is_open() == 0, "disk is closed");
}


static void test_mkfs_mount_superblock(const char *disk_path,
                                       uint32_t total_blocks,
                                       uint32_t total_inodes) {
    printf("\n[TEST] mkfs / mount / superblock\n");

    check(
            myfs_mkfs(disk_path, total_blocks, total_inodes),
            "myfs_mkfs"
    );

    check(
            myfs_mount(disk_path),
            "myfs_mount"
    );

    check_true(myfs_is_mounted() == 1, "file system is mounted");

    myfs_superblock_t *sb = myfs_super_get();

    check_true(sb != NULL, "myfs_super_get not NULL");

    print_superblock(sb);

    check_true(sb->magic == MYFS_MAGIC, "superblock magic correct");
    check_true(sb->version == MYFS_VERSION, "superblock version correct");
    check_true(sb->block_size == MYFS_BLOCK_SIZE, "superblock block size correct");
    check_true(sb->total_blocks == total_blocks, "superblock total blocks correct");
    check_true(sb->total_inodes == total_inodes, "superblock total inodes correct");
    check_true(sb->root_inode == 0, "root inode is 0");
    check_true(sb->fs_state == MYFS_STATE_DIRTY, "mounted file system is dirty");

    /*
     * mkfs 阶段已经创建 root inode，占用了 inode 0。
     * 因此空闲 inode 数量应该是 total_inodes - 1。
     */
    check_true(
            sb->free_inodes_count == total_inodes - 1,
            "free inode count after root inode init"
    );

    check_true(sb->data_block_start < sb->total_blocks, "data block start valid");
    check_true(sb->data_blocks == sb->total_blocks - sb->data_block_start, "data block count valid");
    check_true(sb->free_blocks_count == sb->data_blocks, "free blocks count equals data blocks before block allocator");
}


static void test_physical_block_in_data_area(void) {
    printf("\n[TEST] physical block read/write in data area\n");

    myfs_superblock_t *sb = myfs_super_get();

    check_true(sb != NULL, "superblock available");

    /*
     * 当前阶段还没有成组链接法，也没有文件数据块分配。
     * 这里直接选择数据区起始块进行物理读写测试。
     *
     * 注意：
     * 这只是测试底层物理块读写能力，不代表真正的文件写入。
     */
    myfs_block_t test_block = sb->data_block_start;

    unsigned char write_buf[MYFS_BLOCK_SIZE];
    unsigned char read_buf[MYFS_BLOCK_SIZE];

    memset(write_buf, 0, sizeof(write_buf));
    memset(read_buf, 0, sizeof(read_buf));

    strcpy((char *) write_buf, "MYFS data area physical block test");

    check(
            myfs_disk_write_block(test_block, write_buf),
            "write physical data-area block"
    );

    check(
            myfs_disk_read_block(test_block, read_buf),
            "read physical data-area block"
    );

    check_true(
            strcmp((char *) write_buf, (char *) read_buf) == 0,
            "data-area physical block content correct"
    );
}


static void test_inode_bitmap_and_root_inode(uint32_t total_inodes) {
    printf("\n[TEST] inode bitmap and root inode\n");

    myfs_superblock_t *sb = myfs_super_get();

    check_true(sb != NULL, "superblock available");

    /*
     * root inode 是 mkfs 时创建的，因此 inode 0 应该被占用。
     */
    int used = myfs_inode_bitmap_test(0);

    check_true(used == 1, "root inode bitmap bit is used");

    /*
     * inode 1 此时应该还是空闲。
     */
    used = myfs_inode_bitmap_test(1);

    check_true(used == 0, "inode 1 bitmap bit is free before allocation");

    uint32_t free_count = myfs_inode_bitmap_count_free();

    check_true(
            free_count == total_inodes - 1,
            "inode bitmap free count equals total_inodes - 1"
    );

    myfs_inode_t root;

    check(
            myfs_inode_read(sb->root_inode, &root),
            "read root inode"
    );

    print_inode(&root);

    check_true(root.inode_id == 0, "root inode id is 0");
    check_true(root.type == MYFS_INODE_DIR, "root inode type is directory");
    check_true(root.mode == 0755, "root inode mode is 0755");
    check_true(root.uid == 0, "root inode uid is 0");
    check_true(root.gid == 0, "root inode gid is 0");
    check_true(root.link_count == 2, "root inode link_count is 2");
    check_true(root.open_count == 0, "root inode open_count is 0");

    check_true(myfs_inode_is_dir(0) == 1, "myfs_inode_is_dir(root) true");
    check_true(myfs_inode_is_file(0) == 0, "myfs_inode_is_file(root) false");
    check_true(myfs_inode_is_symlink(0) == 0, "myfs_inode_is_symlink(root) false");
}


static void test_inode_alloc_read_write_free(uint32_t total_inodes) {
    printf("\n[TEST] inode alloc/read/write/free\n");

    myfs_superblock_t *sb = myfs_super_get();

    check_true(sb != NULL, "superblock available");

    uint32_t free_before = sb->free_inodes_count;

    myfs_ino_t file_ino;

    check(
            myfs_inode_alloc(
                    MYFS_INODE_FILE,
                    0644,
                    1000,
                    1000,
                    &file_ino
            ),
            "myfs_inode_alloc file"
    );

    printf("allocated file inode = %u\n", file_ino);

    /*
     * root inode 占用 0，所以第一个新文件 inode 应该是 1。
     */
    check_true(file_ino == 1, "first allocated file inode is 1");

    check_true(
            sb->free_inodes_count == free_before - 1,
            "free inode count decreases after allocation"
    );

    check_true(
            myfs_inode_bitmap_test(file_ino) == 1,
            "allocated inode bitmap bit is used"
    );

    myfs_inode_t file_inode;

    check(
            myfs_inode_read(file_ino, &file_inode),
            "myfs_inode_read allocated file"
    );

    print_inode(&file_inode);

    check_true(file_inode.type == MYFS_INODE_FILE, "allocated inode type is file");
    check_true(file_inode.mode == 0644, "allocated inode mode is 0644");
    check_true(file_inode.uid == 1000, "allocated inode uid is 1000");
    check_true(file_inode.gid == 1000, "allocated inode gid is 1000");
    check_true(file_inode.size == 0, "new file size is 0");
    check_true(file_inode.block_count == 0, "new file block_count is 0");
    check_true(file_inode.link_count == 1, "new file link_count is 1");
    check_true(file_inode.open_count == 0, "new file open_count is 0");

    check_true(myfs_inode_is_file(file_ino) == 1, "myfs_inode_is_file true");
    check_true(myfs_inode_is_dir(file_ino) == 0, "myfs_inode_is_dir false");
    check_true(myfs_inode_is_symlink(file_ino) == 0, "myfs_inode_is_symlink false");

    /*
     * 测试 inode_write。
     * 当前阶段还没有真正文件内容，但可以修改 inode 元数据字段。
     */
    file_inode.size = 12345;
    file_inode.mode = 0600;

    check(
            myfs_inode_write(file_ino, &file_inode),
            "myfs_inode_write modified inode"
    );

    myfs_inode_t modified;

    check(
            myfs_inode_read(file_ino, &modified),
            "read modified inode"
    );

    check_true(modified.size == 12345, "modified inode size saved");
    check_true(modified.mode == 0600, "modified inode mode saved");

    /*
     * 测试 inode 表直接读取。
     * 这个接口偏底层，这里用于验证 inode 表读写正确。
     */
    myfs_inode_t direct_read;

    check(
            myfs_inode_table_read(file_ino, &direct_read),
            "myfs_inode_table_read"
    );

    check_true(direct_read.inode_id == file_ino, "direct inode table read inode_id correct");
    check_true(direct_read.size == 12345, "direct inode table read size correct");

    /*
     * 测试 link_count。
     */
    check(
            myfs_inode_inc_link(file_ino),
            "myfs_inode_inc_link"
    );

    check(
            myfs_inode_read(file_ino, &modified),
            "read after inc link"
    );

    check_true(modified.link_count == 2, "link_count becomes 2");

    check(
            myfs_inode_dec_link(file_ino),
            "myfs_inode_dec_link"
    );

    check(
            myfs_inode_read(file_ino, &modified),
            "read after dec link"
    );

    check_true(modified.link_count == 1, "link_count becomes 1");

    /*
     * 测试 open_count。
     */
    check(
            myfs_inode_inc_open(file_ino),
            "myfs_inode_inc_open"
    );

    check(
            myfs_inode_read(file_ino, &modified),
            "read after inc open"
    );

    check_true(modified.open_count == 1, "open_count becomes 1");

    check(
            myfs_inode_dec_open(file_ino),
            "myfs_inode_dec_open"
    );

    check(
            myfs_inode_read(file_ino, &modified),
            "read after dec open"
    );

    check_true(modified.open_count == 0, "open_count becomes 0");

    /*
     * 测试 inode_free。
     */
    uint32_t free_before_free = sb->free_inodes_count;

    check(
            myfs_inode_free(file_ino),
            "myfs_inode_free"
    );

    check_true(
            sb->free_inodes_count == free_before_free + 1,
            "free inode count increases after inode_free"
    );

    check_true(
            myfs_inode_bitmap_test(file_ino) == 0,
            "freed inode bitmap bit is clear"
    );

    int ret = myfs_inode_read(file_ino, &modified);

    check_true(
            ret == MYFS_ERR_INVALID_INODE,
            "reading freed inode returns invalid inode"
    );

    uint32_t free_count = myfs_inode_bitmap_count_free();

    check_true(
            free_count == total_inodes - 1,
            "free inode count returns to total_inodes - 1 after free"
    );
}


static void test_inode_dec_link_auto_free(void) {
    printf("\n[TEST] inode dec_link auto free\n");

    myfs_ino_t tmp_ino;

    check(
            myfs_inode_alloc(
                    MYFS_INODE_FILE,
                    0644,
                    0,
                    0,
                    &tmp_ino
            ),
            "allocate temp inode"
    );

    myfs_inode_t tmp_inode;

    check(
            myfs_inode_read(tmp_ino, &tmp_inode),
            "read temp inode"
    );

    check_true(tmp_inode.link_count == 1, "temp inode initial link_count is 1");

    /*
     * link_count 从 1 减到 0，且 open_count 为 0，
     * 因此 myfs_inode_dec_link 应该自动释放该 inode。
     */
    check(
            myfs_inode_dec_link(tmp_ino),
            "dec_link temp inode to zero"
    );

    int ret = myfs_inode_read(tmp_ino, &tmp_inode);

    check_true(
            ret == MYFS_ERR_INVALID_INODE,
            "inode auto freed after link_count reaches zero"
    );
}


static void test_symlink_inode_alloc(void) {
    printf("\n[TEST] symlink inode allocation\n");

    myfs_ino_t symlink_ino;

    check(
            myfs_inode_alloc(
                    MYFS_INODE_SYMLINK,
                    0777,
                    2000,
                    2000,
                    &symlink_ino
            ),
            "allocate symlink inode"
    );

    myfs_inode_t inode;

    check(
            myfs_inode_read(symlink_ino, &inode),
            "read symlink inode"
    );

    print_inode(&inode);

    check_true(inode.type == MYFS_INODE_SYMLINK, "inode type is symlink");
    check_true(myfs_inode_is_symlink(symlink_ino) == 1, "myfs_inode_is_symlink true");

    check(
            myfs_inode_free(symlink_ino),
            "free symlink inode"
    );
}


static void test_umount_and_clean_state(const char *disk_path) {
    printf("\n[TEST] umount and clean state\n");

    check(
            myfs_umount(),
            "myfs_umount"
    );

    check_true(myfs_is_mounted() == 0, "file system is not mounted after umount");

    /*
     * umount 后，文件系统应该被标记为 CLEAN。
     *
     * 为了检查磁盘上的 clean 状态，需要直接打开 disk.img，
     * 加载超级块，但不调用 myfs_mount。
     *
     * 因为 myfs_mount 会立刻把状态改成 DIRTY。
     */
    check(
            myfs_disk_open(disk_path),
            "open disk directly after umount"
    );

    check(
            myfs_super_load(),
            "load superblock directly after umount"
    );

    myfs_superblock_t *sb = myfs_super_get();

    check_true(sb != NULL, "superblock available after direct load");
    check_true(sb->fs_state == MYFS_STATE_CLEAN, "fs_state is clean after umount");

    check(
            myfs_disk_close(),
            "close disk after direct clean check"
    );
}


int main(void) {
    const char *raw_disk_path = "raw_disk_test.img";
    const char *fs_disk_path = "stage2_full_test.img";

    uint32_t total_blocks = 32768;    /* 128MB */
    uint32_t total_inodes = 4096;

    printf("========================================\n");
    printf(" MYFS Stage2 Full Test\n");
    printf("========================================\n");

    test_layout(total_blocks, total_inodes);

    test_raw_disk_layer(raw_disk_path);

    test_mkfs_mount_superblock(
            fs_disk_path,
            total_blocks,
            total_inodes
    );

    test_physical_block_in_data_area();

    test_inode_bitmap_and_root_inode(total_inodes);

    test_inode_alloc_read_write_free(total_inodes);

    test_inode_dec_link_auto_free();

    test_symlink_inode_alloc();

    test_umount_and_clean_state(fs_disk_path);

    printf("\n========================================\n");
    printf(" ALL STAGE2 TESTS PASSED\n");
    printf("========================================\n");

    return 0;
}