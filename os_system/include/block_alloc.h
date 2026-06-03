#ifndef MYFS_BLOCK_ALLOC_H
#define MYFS_BLOCK_ALLOC_H

/*
 * 数据块空闲空间管理模块。
 * 本阶段实现“成组链接法”管理空闲数据块。
 *
 * 当前文件系统中：
 *
 * inode 使用 inode 位图管理；
 * 数据块使用成组链接法管理。
 *
 * 成组链接法基本思想：
 *
 * 1. 超级块中保存当前一组可直接分配的空闲块号。
 * 2. 当超级块中的空闲块号用完时，从某个空闲块中读取下一组空闲块号。
 * 3. 释放数据块时，如果当前组未满，就直接加入超级块 free_group。
 * 4. 如果当前组已满，就把当前 free_group 写入被释放的数据块中，
 *    然后让该块成为新的组管理块。
 *
 * 本模块对上层提供：
 *
 * myfs_block_group_init  初始化数据区空闲块链
 * myfs_block_alloc       分配一个数据块
 * myfs_block_free        释放一个数据块
 * myfs_block_free_count  获取空闲数据块数量
 */

#include <stdint.h>
#include "type.h"
#include "config.h"

/*
 * 空闲块组管理块结构。
 *
 * 当超级块中的 free_group 已满，再释放一个块时，
 * 会把超级块当前保存的一组空闲块号写入这个被释放的块中。
 *
 * 这个块就临时变成“空闲块组管理块”。
 *
 * 等以后分配到该组时，会先读取它保存的下一组空闲块号，
 * 然后这个块本身就可以作为普通数据块分配出去。
 */
typedef struct myfs_free_group_block {
    /*
     * 当前组中保存了多少个空闲块号。
     */
    uint32_t count;

    /*
     * 空闲块号数组。
     */
    myfs_block_t blocks[MYFS_FREE_GROUP_SIZE];

    /*
     * 预留校验和字段。
     * 当前阶段可以先不强制校验，后续 fsck/journal 阶段再完善。
     */
    uint32_t checksum;
} myfs_free_group_block_t;


/*
 * 初始化数据区空闲块链。
 *
 * mkfs 阶段调用。
 *
 * 参数：
 * data_start  数据区起始块号
 * data_blocks 数据区总块数
 *
 * 功能：
 * 将数据区所有块加入成组链接空闲块链。
 */
int myfs_block_group_init(
        myfs_block_t data_start,
        uint32_t data_blocks
);

/*
 * 分配一个空闲数据块。
 *
 * 参数：
 * block_id 输出分配到的数据块号
 *
 * 返回：
 * MYFS_OK 成功
 * MYFS_ERR_NO_SPACE 无空闲数据块
 */
int myfs_block_alloc(myfs_block_t *block_id);

/*
 * 释放一个数据块。
 *
 * 参数：
 * block_id 要释放的数据块号
 */
int myfs_block_free(myfs_block_t block_id);

/*
 * 判断某个块号是否属于合法数据区块。
 *
 * 返回：
 * 1 合法
 * 0 非法
 */
int myfs_block_is_valid_data_block(myfs_block_t block_id);

/*
 * 获取当前空闲数据块数量。
 */
uint32_t myfs_block_free_count(void);

/*
 * 清空一个数据块。
 *
 * 分配数据块后调用，避免旧的空闲组管理信息残留。
 */
int myfs_block_zero(myfs_block_t block_id);

#endif