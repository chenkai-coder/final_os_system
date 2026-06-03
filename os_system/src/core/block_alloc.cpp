#include "block_alloc.h"
#include "superblock.h"
#include "disk.h"
#include "cache.h"
#include "config.h"
#include "error.h"

#include <string.h>
#include <iostream>
#include <cstdio>

#include "journal.h"

/*
 * block_alloc.c
 成组链接法数据块分配器。
 *
 * 本文件内部逻辑分为：
 *
 * 1. 辅助函数
 * 2. 空闲组管理块读写
 * 3. 初始化成组链接空闲块链
 * 4. 数据块分配 block_alloc
 * 5. 数据块释放 block_free
 */


/* ============================================================
 * 1. 辅助函数
 * ============================================================
 */

/*
 * 获取当前内存超级块。
 */
static myfs_superblock_t *get_sb(void) {
    return myfs_super_get();
}

/*
 * 判断 block_id 是否属于数据区范围。
 *
 * 注意：
 * 只有数据区中的块才能被 block_alloc / block_free 管理。
 *
 * 超级块、inode 位图、inode 表、journal 区都不能被当作普通数据块释放。
 */
int myfs_block_is_valid_data_block(myfs_block_t block_id) {
    myfs_superblock_t *sb = get_sb();

    if (sb == NULL) {
        return 0;
    }

    if (block_id < sb->data_block_start) {
        return 0;
    }

    if (block_id >= sb->total_blocks) {
        return 0;
    }

    return 1;
}

/*
 * 清空一个数据块。
 *
 * 分配一个块给文件使用前，最好先清零。
 * 因为这个块之前可能保存过：
 *
 * 1. 旧文件数据
 * 2. 空闲块组管理信息
 */
int myfs_block_zero(myfs_block_t block_id) {
    if (!myfs_block_is_valid_data_block(block_id)) {
        return MYFS_ERR_INVALID_BLOCK;
    }

    unsigned char zero[MYFS_BLOCK_SIZE];

    memset(zero, 0, sizeof(zero));

    /*
     * 关键修复：
     * 如果缓存已初始化，必须通过缓存写入零，
     * 而不是直接写磁盘。
     *
     * 原因：
     * 当一个数据块被释放后重新分配时，缓存中可能
     * 仍然保留着旧数据。如果这里直接写磁盘（绕过缓存），
     * 后续 myfs_cache_read_block() 会从缓存读到旧数据，
     * 导致部分块写入时出现数据污染。
     *
     * 通过缓存写入可以确保：
     * 1. 缓存中的旧数据被零覆盖。
     * 2. 后续从缓存读取时得到正确的零。
     * 3. flush 时零会被写入磁盘。
     */
    if (myfs_cache_is_initialized()) {
        return myfs_cache_write_block(block_id, zero, 0);
    }

    return myfs_disk_write_block(block_id, zero);
}

/*
 * 写入空闲组管理块。
 *
 * group 会被写入某个空闲数据块。
 * 该数据块暂时作为“下一组空闲块号”的保存位置。
 */
static int write_free_group_block(
        myfs_block_t block_id,
        const myfs_free_group_block_t *group
) {
    if (group == NULL) {
        return MYFS_ERR_INVALID_ARG;
    }

    if (!myfs_block_is_valid_data_block(block_id)) {
        return MYFS_ERR_INVALID_BLOCK;
    }

    unsigned char buf[MYFS_BLOCK_SIZE];

    memset(buf, 0, sizeof(buf));
    memcpy(buf, group, sizeof(myfs_free_group_block_t));

    // return myfs_disk_write_block(block_id, buf);
    return myfs_journal_write_metadata_block(block_id, buf);
}

/*
 * 读取空闲组管理块。
 *
 * 当超级块当前 free_group 用完时，
 * 需要从最后一个空闲块中读取下一组空闲块号。
 */
static int read_free_group_block(
        myfs_block_t block_id,
        myfs_free_group_block_t *group
) {
    if (group == NULL) {
        return MYFS_ERR_INVALID_ARG;
    }

    if (!myfs_block_is_valid_data_block(block_id)) {
        return MYFS_ERR_INVALID_BLOCK;
    }

    unsigned char buf[MYFS_BLOCK_SIZE];

    int ret = myfs_disk_read_block(block_id, buf);
    if (ret != MYFS_OK) {
        return ret;
    }

    memcpy(group, buf, sizeof(myfs_free_group_block_t));

    /*
     * 基本合法性检查。
     */
    if (group->count > MYFS_FREE_GROUP_SIZE) {
        return MYFS_ERR_CORRUPTED;
    }

    for (uint32_t i = 0; i < group->count; i++) {
        if (!myfs_block_is_valid_data_block(group->blocks[i])) {
            return MYFS_ERR_CORRUPTED;
        }
    }

    return MYFS_OK;
}


/* ============================================================
 * 2. 内部释放函数
 * ============================================================
 */

/*
 * internal_free_block_no_sync
 * ------------------------------------------------------------
 * 内部使用的释放数据块函数。
 *
 * 和 myfs_block_free 的区别：
 * 1. 不进行 super_sync。
 * 2. 主要用于 mkfs 阶段批量初始化空闲块链。
 *
 * 成组链接释放逻辑：
 *
 * 情况一：超级块当前 free_group 未满
 *
 *     直接把 block_id 放入 sb->free_group。
 *
 * 情况二：超级块当前 free_group 已满
 *
 *     1. 把当前 free_group 保存到 block_id 这个块中。
 *     2. 清空超级块中的 free_group。
 *     3. 让 block_id 成为新的 free_group[0]。
 *
 * 这样 block_id 就临时作为“下一组空闲块号”的管理块。
 */
static int internal_free_block_no_sync(myfs_block_t block_id) {
    myfs_superblock_t *sb = get_sb();

    if (sb == NULL) {
        return MYFS_ERR_NOT_MOUNTED;
    }

    if (!myfs_block_is_valid_data_block(block_id)) {
        return MYFS_ERR_INVALID_BLOCK;
    }

    if (myfs_cache_is_initialized()) {
        myfs_cache_invalidate_block(block_id);
    }

    /*
     * 当前超级块中的空闲块组未满，直接压入。
     */
    if (sb->free_group_count < MYFS_FREE_GROUP_SIZE) {
        sb->free_group[sb->free_group_count] = block_id;
        sb->free_group_count++;
        sb->free_blocks_count++;
        return MYFS_OK;
    }

    /*
     * 当前超级块中的空闲块组已经满了。
     *
     * 需要把当前这一组写入 block_id。
     * 然后让 block_id 自己作为新的组管理块，放入超级块 free_group[0]。
     */
    myfs_free_group_block_t group;

    memset(&group, 0, sizeof(group));

    group.count = sb->free_group_count;

    for (uint32_t i = 0; i < sb->free_group_count; i++) {
        group.blocks[i] = sb->free_group[i];
    }

    int ret = write_free_group_block(block_id, &group);
    if (ret != MYFS_OK) {
        return ret;
    }

    std::cout << "[SYNC] GROUP_STORE " << group.count << " " << block_id << std::endl;
    fflush(stdout);

    /*
     * 重置超级块当前组。
     */
    memset(sb->free_group, 0, sizeof(sb->free_group));

    sb->free_group_count = 1;
    sb->free_group[0] = block_id;

    sb->free_blocks_count++;

    return MYFS_OK;
}


/* ============================================================
 * 3. 初始化成组链接空闲块链
 * ============================================================
 */

int myfs_block_group_init(
        myfs_block_t data_start,
        uint32_t data_blocks
) {
    myfs_superblock_t *sb = get_sb();

    if (sb == NULL) {
        return MYFS_ERR_NOT_MOUNTED;
    }

    if (data_blocks == 0) {
        return MYFS_ERR_NO_SPACE;
    }

    /*
     * 检查传入的数据区范围是否和超级块一致。
     */
    if (data_start != sb->data_block_start ||
        data_blocks != sb->data_blocks) {
        return MYFS_ERR_INVALID_ARG;
    }

    /*
     * 初始化前先清空超级块中的成组链接字段。
     *
     * 注意：
     * super_init 中虽然已经给 free_blocks_count 赋过初值，
     * 但这里要重新由成组链接法构建出来。
     */
    sb->free_group_count = 0;
    sb->free_blocks_count = 0;
    memset(sb->free_group, 0, sizeof(sb->free_group));

    /*
     * 将数据区所有块加入空闲块链。
     *
     * 这里从后往前加入，是为了让之后从栈顶分配时，
     * 更容易先分配靠前的数据块。
     *
     * 例如数据区是 100 ~ 999，
     * 反向释放后，分配时通常会先拿到较小的块号。
     */
    for (uint32_t i = 0; i < data_blocks; i++) {
        myfs_block_t block_id = data_start + data_blocks - 1 - i;

        int ret = internal_free_block_no_sync(block_id);
        if (ret != MYFS_OK) {
            return ret;
        }
    }

    /*
     * 将初始化后的超级块写回磁盘。
     */
    return myfs_super_sync();
}


/* ============================================================
 * 4. 分配数据块
 * ============================================================
 */

int myfs_block_alloc(myfs_block_t *block_id) {
    if (block_id == NULL) {
        return MYFS_ERR_INVALID_ARG;
    }

    myfs_superblock_t *sb = get_sb();

    if (sb == NULL) {
        return MYFS_ERR_NOT_MOUNTED;
    }

    if (sb->free_blocks_count == 0) {
        return MYFS_ERR_NO_SPACE;
    }

    if (sb->free_group_count == 0) {
        /*
         * 正常情况下 free_blocks_count > 0 时，
         * free_group_count 不应该为 0。
         */
        return MYFS_ERR_CORRUPTED;
    }

    /*
     * 情况一：
     * 超级块当前组里有多个空闲块号。
     *
     * 直接从数组末尾弹出一个块号即可。
     */
    if (sb->free_group_count > 1) {
        sb->free_group_count--;

        myfs_block_t allocated = sb->free_group[sb->free_group_count];
        sb->free_group[sb->free_group_count] = 0;

        sb->free_blocks_count--;

        int ret = myfs_super_sync();
        if (ret != MYFS_OK) {
            return ret;
        }

        /*
         * 清零分配出去的块，避免旧数据残留。
         */
        ret = myfs_block_zero(allocated);
        if (ret != MYFS_OK) {
            return ret;
        }

        *block_id = allocated;
        std::cout << "[SYNC] BLOCK_ALLOC " << allocated << std::endl;
        fflush(stdout);
        return MYFS_OK;
    }

    /*
     * 情况二：
     * free_group_count == 1。
     *
     * 这时超级块中只剩一个块号。
     *
     * 它可能有两种情况：
     *
     * 1. 如果 free_blocks_count == 1：
     *    说明这是整个文件系统最后一个空闲块，
     *    它就是普通空闲块，直接分配出去。
     *
     * 2. 如果 free_blocks_count > 1：
     *    说明这个块是“空闲组管理块”，里面保存了下一组空闲块号。
     *    需要先读取它的内容，把下一组加载到超级块中，
     *    然后该块本身就可以作为普通数据块分配出去。
     */
    myfs_block_t allocated = sb->free_group[0];

    if (sb->free_blocks_count == 1) {
        sb->free_group[0] = 0;
        sb->free_group_count = 0;
        sb->free_blocks_count--;

        int ret = myfs_super_sync();
        if (ret != MYFS_OK) {
            return ret;
        }

        ret = myfs_block_zero(allocated);
        if (ret != MYFS_OK) {
            return ret;
        }

        *block_id = allocated;
        std::cout << "[SYNC] BLOCK_ALLOC " << allocated << std::endl;
        fflush(stdout);
        return MYFS_OK;
    }

    /*
     * free_blocks_count > 1，说明 allocated 是组管理块。
     */
    myfs_free_group_block_t group;

    int ret = read_free_group_block(allocated, &group);
    if (ret != MYFS_OK) {
        return ret;
    }

    /*
     * 将下一组空闲块号加载到超级块中。
     */
    memset(sb->free_group, 0, sizeof(sb->free_group));

    sb->free_group_count = group.count;

    for (uint32_t i = 0; i < group.count; i++) {
        sb->free_group[i] = group.blocks[i];
    }

    std::cout << "[SYNC] GROUP_LOAD " << group.count << " " << allocated << std::endl;
    fflush(stdout);

    /*
     * allocated 这个组管理块本身被分配出去。
     */
    sb->free_blocks_count--;

    ret = myfs_super_sync();
    if (ret != MYFS_OK) {
        return ret;
    }

    /*
     * 清零这个块，避免它之前保存的空闲组信息残留。
     */
    ret = myfs_block_zero(allocated);
    if (ret != MYFS_OK) {
        return ret;
    }

    *block_id = allocated;
    std::cout << "[SYNC] BLOCK_ALLOC " << allocated << std::endl;
    fflush(stdout);

    return MYFS_OK;
}


/* ============================================================
 * 5. 释放数据块
 * ============================================================
 */

int myfs_block_free(myfs_block_t block_id) {
    if (!myfs_block_is_valid_data_block(block_id)) {
        return MYFS_ERR_INVALID_BLOCK;
    }

    int ret = internal_free_block_no_sync(block_id);
    if (ret != MYFS_OK) {
        return ret;
    }

    ret = myfs_super_sync();
    if (ret != MYFS_OK) {
        return ret;
    }

    std::cout << "[SYNC] BLOCK_FREE " << block_id << std::endl;
    fflush(stdout);

    return MYFS_OK;
}


/* ============================================================
 * 6. 查询空闲块数量
 * ============================================================
 */

uint32_t myfs_block_free_count(void) {
    myfs_superblock_t *sb = get_sb();

    if (sb == NULL) {
        return 0;
    }

    return sb->free_blocks_count;
}
