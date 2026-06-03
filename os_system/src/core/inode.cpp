#include "inode.h"
#include "superblock.h"
#include "disk.h"
#include "config.h"
#include "error.h"
#include "block_map.h"

#include <cstdio>
#include <cstring>
#include <ctime>

#include "journal.h"

/*
 * 获取当前时间戳。
 */
static uint64_t myfs_now(void) {
    return (uint64_t) time(NULL);
}

/*
 * 获取当前超级块指针。
 */
static myfs_superblock_t *get_sb(void) {
    return myfs_super_get();
}

/*
 * 检查 inode 结构体大小是否超过固定 inode 槽位大小。
 *
 * 磁盘 inode 表中每个 inode 固定占 MYFS_INODE_SIZE 字节。
 * 如果结构体超过这个大小，就会破坏 inode 表布局。
 */
static int check_inode_struct_size(void) {
    if (sizeof(myfs_inode_t) > MYFS_INODE_SIZE) {
        return MYFS_ERR_UNSUPPORTED;
    }

    return MYFS_OK;
}

/*
 * 检查 inode 编号是否合法。
 */
static int check_inode_id(myfs_ino_t inode_id) {
    myfs_superblock_t *sb = get_sb();

    if (sb == NULL) {
        return MYFS_ERR_NOT_MOUNTED;
    }

    if (inode_id >= sb->total_inodes) {
        return MYFS_ERR_INVALID_INODE;
    }

    return MYFS_OK;
}

/*
 * 根据 inode 编号计算它在 inode 位图中的位置。
 *
 * 输出：
 * block_id     位图所在物理块号
 * byte_offset  块内字节偏移
 * bit_mask     字节内 bit 掩码
 */
static int get_inode_bit_position(
        myfs_ino_t inode_id,
        myfs_block_t *block_id,
        uint32_t *byte_offset,
        uint8_t *bit_mask
) {
    int ret = check_inode_id(inode_id);
    if (ret != MYFS_OK) {
        return ret;
    }

    myfs_superblock_t *sb = get_sb();

    /*
     * 每个 inode 使用 1 bit。
     */
    uint32_t bit_index = inode_id;
    uint32_t global_byte_index = bit_index / 8;
    uint32_t bit_in_byte = bit_index % 8;

    *block_id = sb->inode_bitmap_start +
                global_byte_index / MYFS_BLOCK_SIZE;

    *byte_offset = global_byte_index % MYFS_BLOCK_SIZE;

    *bit_mask = (uint8_t) (1u << bit_in_byte);

    return MYFS_OK;
}

int myfs_inode_bitmap_clear_all(void) {
    myfs_superblock_t *sb = get_sb();

    if (sb == NULL) {
        return MYFS_ERR_NOT_MOUNTED;
    }

    unsigned char zero[MYFS_BLOCK_SIZE];

    memset(zero, 0, sizeof(zero));

    for (uint32_t i = 0; i < sb->inode_bitmap_blocks; i++) {
        int ret = myfs_journal_write_metadata_block(sb->inode_bitmap_start + i, zero);

        if (ret != MYFS_OK) {
            return ret;
        }
    }

    return MYFS_OK;
}

//检查是否已经占用
int myfs_inode_bitmap_test(myfs_ino_t inode_id) {
    myfs_block_t block_id;
    uint32_t byte_offset;
    uint8_t bit_mask;

    int ret = get_inode_bit_position(
            inode_id,
            &block_id,
            &byte_offset,
            &bit_mask
    );

    if (ret != MYFS_OK) {
        return ret;
    }

    unsigned char block[MYFS_BLOCK_SIZE];

    ret = myfs_disk_read_block(block_id, block);
    if (ret != MYFS_OK) {
        return ret;
    }

    return (block[byte_offset] & bit_mask) ? 1 : 0;
}

int myfs_inode_bitmap_set(myfs_ino_t inode_id) {
    myfs_block_t block_id;
    uint32_t byte_offset;
    uint8_t bit_mask;

    int ret = get_inode_bit_position(
            inode_id,
            &block_id,
            &byte_offset,
            &bit_mask
    );

    if (ret != MYFS_OK) {
        return ret;
    }

    unsigned char block[MYFS_BLOCK_SIZE];

    ret = myfs_disk_read_block(block_id, block);
    if (ret != MYFS_OK) {
        return ret;
    }

    block[byte_offset] |= bit_mask;

    // return myfs_disk_write_block(block_id, block);
    return myfs_journal_write_metadata_block(block_id, block);
}

int myfs_inode_bitmap_clear(myfs_ino_t inode_id) {
    myfs_block_t block_id;
    uint32_t byte_offset;
    uint8_t bit_mask;

    int ret = get_inode_bit_position(
            inode_id,
            &block_id,
            &byte_offset,
            &bit_mask
    );

    if (ret != MYFS_OK) {
        return ret;
    }

    unsigned char block[MYFS_BLOCK_SIZE];

    ret = myfs_disk_read_block(block_id, block);
    if (ret != MYFS_OK) {
        return ret;
    }

    block[byte_offset] &= (uint8_t) (~bit_mask);

    // return myfs_disk_write_block(block_id, block);
    return myfs_journal_write_metadata_block(block_id, block);
}

int myfs_inode_bitmap_alloc(myfs_ino_t *inode_id) {
    if (inode_id == NULL) {
        return MYFS_ERR_INVALID_ARG;
    }

    myfs_superblock_t *sb = get_sb();

    if (sb == NULL) {
        return MYFS_ERR_NOT_MOUNTED;
    }

    if (sb->free_inodes_count == 0) {
        return MYFS_ERR_NO_INODE;
    }

    /*
     * 第二阶段采用最简单的线性扫描。
     *
     * 之后如果追求性能，可以在超级块中保存 last_alloc_inode，
     * 下次从上次位置继续扫描。
     */
    for (myfs_ino_t i = 0; i < sb->total_inodes; i++) {
        int used = myfs_inode_bitmap_test(i);

        if (used < 0) {
            return used;
        }

        if (used == 0) {
            int ret = myfs_inode_bitmap_set(i);
            if (ret != MYFS_OK) {
                return ret;
            }

            *inode_id = i;
            return MYFS_OK;
        }
    }

    return MYFS_ERR_NO_INODE;
}

int myfs_inode_bitmap_free(myfs_ino_t inode_id) {
    int used = myfs_inode_bitmap_test(inode_id);

    if (used < 0) {
        return used;
    }

    if (used == 0) {
        /*
         * 重复释放 inode，说明调用者逻辑有问题。
         */
        return MYFS_ERR_INVALID_INODE;
    }

    return myfs_inode_bitmap_clear(inode_id);
}

uint32_t myfs_inode_bitmap_count_free(void) {
    myfs_superblock_t *sb = get_sb();

    if (sb == NULL) {
        return 0;
    }

    uint32_t free_count = 0;

    for (myfs_ino_t i = 0; i < sb->total_inodes; i++) {
        int used = myfs_inode_bitmap_test(i);

        if (used == 0) {
            free_count++;
        }
    }

    return free_count;
}


/*
 * 根据 inode_id 计算 inode 在 inode 表中的物理位置。
 *
 * 输出：
 * block_id : inode 所在的磁盘块号
 * offset   : inode 在该块中的字节偏移
 */
static int get_inode_table_position(
        myfs_ino_t inode_id,
        myfs_block_t *block_id,
        uint32_t *offset
) {
    int ret = check_inode_struct_size();
    if (ret != MYFS_OK) {
        return ret;
    }

    ret = check_inode_id(inode_id);
    if (ret != MYFS_OK) {
        return ret;
    }

    myfs_superblock_t *sb = get_sb();

    uint32_t inodes_per_block = MYFS_BLOCK_SIZE / MYFS_INODE_SIZE;

    uint32_t block_index = inode_id / inodes_per_block;
    uint32_t inode_index_in_block = inode_id % inodes_per_block;

    if (block_index >= sb->inode_table_blocks) {
        return MYFS_ERR_INVALID_INODE;
    }

    *block_id = sb->inode_table_start + block_index;
    *offset = inode_index_in_block * MYFS_INODE_SIZE;

    return MYFS_OK;
}

int myfs_inode_table_read(myfs_ino_t inode_id, myfs_inode_t *out) {
    if (out == NULL) {
        return MYFS_ERR_INVALID_ARG;
    }

    myfs_block_t block_id;
    uint32_t offset;

    int ret = get_inode_table_position(
            inode_id,
            &block_id,
            &offset
    );

    if (ret != MYFS_OK) {
        return ret;
    }

    unsigned char block[MYFS_BLOCK_SIZE];

    ret = myfs_disk_read_block(block_id, block);
    if (ret != MYFS_OK) {
        return ret;
    }

    memcpy(out, block + offset, sizeof(myfs_inode_t));

    return MYFS_OK;
}

int myfs_inode_table_write(myfs_ino_t inode_id, const myfs_inode_t *inode) {
    if (inode == NULL) {
        return MYFS_ERR_INVALID_ARG;
    }

    myfs_block_t block_id;
    uint32_t offset;

    int ret = get_inode_table_position(
            inode_id,
            &block_id,
            &offset
    );

    if (ret != MYFS_OK) {
        return ret;
    }

    unsigned char block[MYFS_BLOCK_SIZE];

    ret = myfs_disk_read_block(block_id, block);
    if (ret != MYFS_OK) {
        return ret;
    }

    /*
     * 每个 inode 槽位固定 MYFS_INODE_SIZE 字节。
     *
     * 先清空槽位，再写入 inode 结构体。
     * 这样可以避免旧数据残留。
     */
    memset(block + offset, 0, MYFS_INODE_SIZE);
    memcpy(block + offset, inode, sizeof(myfs_inode_t));

    // return myfs_disk_write_block(block_id, block);
    return myfs_journal_write_metadata_block(block_id, block);
}

int myfs_inode_table_clear_all(void) {
    myfs_superblock_t *sb = get_sb();

    if (sb == NULL) {
        return MYFS_ERR_NOT_MOUNTED;
    }

    unsigned char zero[MYFS_BLOCK_SIZE];

    memset(zero, 0, sizeof(zero));

    for (uint32_t i = 0; i < sb->inode_table_blocks; i++) {
        // int ret = myfs_disk_write_block(
        //         sb->inode_table_start + i,
        //         zero
        // );
        int ret = myfs_journal_write_metadata_block(sb->inode_table_start + i, zero);

        if (ret != MYFS_OK) {
            return ret;
        }
    }

    return MYFS_OK;
}

/*
 * 初始化 inode 基本字段。
 */
static void init_inode(
        myfs_inode_t *inode,
        myfs_ino_t inode_id,
        uint16_t type,
        uint16_t mode,
        uint32_t uid,
        uint32_t gid
) {
    uint64_t now = myfs_now();

    memset(inode, 0, sizeof(myfs_inode_t));

    inode->inode_id = inode_id;
    inode->type = type;
    inode->mode = mode;

    inode->uid = uid;
    inode->gid = gid;

    inode->size = 0;
    inode->block_count = 0;

    /*
     * 目录初始 link_count 设置为 2。
     *
     * 从 Unix 语义上看：
     * 1. 父目录中的目录项引用它
     * 2. 自己的 "." 引用它
     *
     * 第二阶段还没有真正目录项，但先保持这个语义。
     */
    if (type == MYFS_INODE_DIR) {
        inode->link_count = 2;
    } else {
        inode->link_count = 1;
    }

    inode->open_count = 0;

    inode->atime = now;
    inode->mtime = now;
    inode->ctime = now;
    inode->crtime = now;

    inode->indirect1 = 0;
    inode->indirect2 = 0;

    inode->checksum = 0;
}

int myfs_inode_alloc(
        uint16_t type,
        uint16_t mode,
        uint32_t uid,
        uint32_t gid,
        myfs_ino_t *inode_id
) {
    if (inode_id == NULL) {
        return MYFS_ERR_INVALID_ARG;
    }

    myfs_superblock_t *sb = get_sb();

    if (sb == NULL) {
        return MYFS_ERR_NOT_MOUNTED;
    }

    if (type != MYFS_INODE_FILE &&
        type != MYFS_INODE_DIR &&
        type != MYFS_INODE_SYMLINK) {
        return MYFS_ERR_INVALID_ARG;
    }

    if (sb->free_inodes_count == 0) {
        return MYFS_ERR_NO_INODE;
    }

    myfs_ino_t new_ino;

    int ret = myfs_inode_bitmap_alloc(&new_ino);
    if (ret != MYFS_OK) {
        return ret;
    }

    myfs_inode_t inode;

    init_inode(&inode, new_ino, type, mode, uid, gid);

    ret = myfs_inode_table_write(new_ino, &inode);
    if (ret != MYFS_OK) {
        /*
         * 如果 inode 表写入失败，需要回滚 inode 位图。
         */
        myfs_inode_bitmap_clear(new_ino);
        return ret;
    }

    /*
     * 更新超级块中的空闲 inode 数量。
     */
    if (sb->free_inodes_count > 0) {
        sb->free_inodes_count--;
    }

    ret = myfs_super_sync();
    if (ret != MYFS_OK) {
        return ret;
    }

    *inode_id = new_ino;

    return MYFS_OK;
}

int myfs_inode_free(myfs_ino_t inode_id) {
    int ret = check_inode_id(inode_id);

    if (ret != MYFS_OK) {
        return ret;
    }

    int used = myfs_inode_bitmap_test(inode_id);

    if (used < 0) {
        return used;
    }

    if (used == 0) {
        return MYFS_ERR_INVALID_INODE;
    }

    /*
     * 第四阶段开始，inode 可能已经通过 direct/indirect
     * 持有数据块或间接块。
     *
     * 因此释放 inode 前，必须先释放它拥有的全部物理块。
     *
     * 注意：
     * 当前仍然属于底层物理释放，不涉及目录项和路径。
     * 上层逻辑删除文件时，会先删除目录项并减少 link_count，
     * 最终触发 inode_free。
     */
    ret = myfs_inode_release_all_blocks(inode_id);
    if (ret != MYFS_OK) {
        return ret;
    }

    /*
     * 清空 inode 表项。
     */
    myfs_inode_t inode;

    memset(&inode, 0, sizeof(inode));
    inode.inode_id = inode_id;
    inode.type = MYFS_INODE_FREE;

    ret = myfs_inode_table_write(inode_id, &inode);
    if (ret != MYFS_OK) {
        return ret;
    }

    /*
     * 清除 inode 位图。
     */
    ret = myfs_inode_bitmap_free(inode_id);
    if (ret != MYFS_OK) {
        return ret;
    }

    myfs_superblock_t *sb = get_sb();

    if (sb == NULL) {
        return MYFS_ERR_NOT_MOUNTED;
    }

    sb->free_inodes_count++;

    return myfs_super_sync();
}

int myfs_inode_read(myfs_ino_t inode_id, myfs_inode_t *out) {
    if (out == NULL) {
        return MYFS_ERR_INVALID_ARG;
    }

    int ret = check_inode_id(inode_id);

    if (ret != MYFS_OK) {
        return ret;
    }

    int used = myfs_inode_bitmap_test(inode_id);

    if (used < 0) {
        return used;
    }

    if (used == 0) {
        return MYFS_ERR_INVALID_INODE;
    }

    return myfs_inode_table_read(inode_id, out);
}

int myfs_inode_write(myfs_ino_t inode_id, const myfs_inode_t *inode) {
    if (inode == NULL) {
        return MYFS_ERR_INVALID_ARG;
    }

    int ret = check_inode_id(inode_id);

    if (ret != MYFS_OK) {
        return ret;
    }

    int used = myfs_inode_bitmap_test(inode_id);

    if (used < 0) {
        return used;
    }

    if (used == 0) {
        return MYFS_ERR_INVALID_INODE;
    }

    myfs_inode_t tmp = *inode;

    /*
     * 防止调用者传入的 inode_id 和实际槽位不一致。
     */
    tmp.inode_id = inode_id;
    tmp.ctime = myfs_now();

    return myfs_inode_table_write(inode_id, &tmp);
}

int myfs_inode_inc_link(myfs_ino_t inode_id) {
    myfs_inode_t inode;

    int ret = myfs_inode_read(inode_id, &inode);
    if (ret != MYFS_OK) {
        return ret;
    }

    inode.link_count++;
    inode.ctime = myfs_now();

    return myfs_inode_table_write(inode_id, &inode);
}

int myfs_inode_dec_link(myfs_ino_t inode_id) {
    myfs_inode_t inode;

    int ret = myfs_inode_read(inode_id, &inode);
    if (ret != MYFS_OK) {
        return ret;
    }

    if (inode.link_count > 0) {
        inode.link_count--;
    }

    inode.ctime = myfs_now();

    /*
     * 如果没有目录项引用，也没有打开引用，就释放 inode。
     */
    if (inode.link_count == 0 && inode.open_count == 0) {
        return myfs_inode_free(inode_id);
    }

    return myfs_inode_table_write(inode_id, &inode);
}

int myfs_inode_inc_open(myfs_ino_t inode_id) {
    myfs_inode_t inode;

    int ret = myfs_inode_read(inode_id, &inode);
    if (ret != MYFS_OK) {
        return ret;
    }

    inode.open_count++;
    inode.ctime = myfs_now();

    return myfs_inode_table_write(inode_id, &inode);
}

int myfs_inode_dec_open(myfs_ino_t inode_id) {
    myfs_inode_t inode;

    int ret = myfs_inode_read(inode_id, &inode);
    if (ret != MYFS_OK) {
        return ret;
    }

    if (inode.open_count > 0) {
        inode.open_count--;
    }

    inode.ctime = myfs_now();

    /*
     * 如果文件已经没有目录项引用，并且也没有打开引用，则释放。
     */
    if (inode.link_count == 0 && inode.open_count == 0) {
        return myfs_inode_free(inode_id);
    }

    return myfs_inode_table_write(inode_id, &inode);
}

int myfs_inode_is_file(myfs_ino_t inode_id) {
    myfs_inode_t inode;

    int ret = myfs_inode_read(inode_id, &inode);
    if (ret != MYFS_OK) {
        return 0;
    }

    return inode.type == MYFS_INODE_FILE;
}

int myfs_inode_is_dir(myfs_ino_t inode_id) {
    myfs_inode_t inode;

    int ret = myfs_inode_read(inode_id, &inode);
    if (ret != MYFS_OK) {
        return 0;
    }

    return inode.type == MYFS_INODE_DIR;
}

int myfs_inode_is_symlink(myfs_ino_t inode_id) {
    myfs_inode_t inode;

    int ret = myfs_inode_read(inode_id, &inode);
    if (ret != MYFS_OK) {
        return 0;
    }

    return inode.type == MYFS_INODE_SYMLINK;
}



int myfs_inode_init_root(void) {
    myfs_superblock_t *sb = get_sb();

    if (sb == NULL) {
        return MYFS_ERR_NOT_MOUNTED;
    }

    /*
     * mkfs 后 inode 位图应为空。
     * 因此第一个分配到的 inode 应该是 0。
     */
    myfs_ino_t root_ino;

    int ret = myfs_inode_alloc(
            MYFS_INODE_DIR,
            0755,
            0,
            0,
            &root_ino
    );

    if (ret != MYFS_OK) {
        return ret;
    }

    if (root_ino != 0) {
        /*
         * 如果 root inode 不是 0，说明 mkfs 阶段 inode 位图状态异常。
         */
        return MYFS_ERR_CORRUPTED;
    }

    sb->root_inode = root_ino;

    return myfs_super_sync();
}

int myfs_inode_reset_open_counts(void) {
    myfs_superblock_t *sb = get_sb();
    if (sb == nullptr) return MYFS_ERR_NOT_MOUNTED;

    uint32_t reset_count = 0;
    for (myfs_ino_t ino = 0; ino < sb->total_inodes; ino++) {
        int used = myfs_inode_bitmap_test(ino);
        if (used <= 0) continue;

        myfs_inode_t inode;
        int ret = myfs_inode_read(ino, &inode);
        if (ret != MYFS_OK) continue;

        if (inode.open_count > 0) {
            inode.open_count = 0;
            myfs_inode_table_write(ino, &inode);
            reset_count++;
        }
    }

    if (reset_count > 0) {
        printf("[OK] reset open_count for %u inodes (crashed session cleanup)\n",
               reset_count);
    }
    return MYFS_OK;
}