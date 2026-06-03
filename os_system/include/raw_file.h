#ifndef MYFS_RAW_FILE_H
#define MYFS_RAW_FILE_H

/*
 * raw_file.h
 * ------------------------------------------------------------
 * 基于 inode 的裸文件数据读写模块。
 *
 * 本模块属于文件系统底层/物理支撑层。
 *
 * 它不处理：
 * 1. 文件名
 * 2. 路径
 * 3. 目录项
 * 4. 文件描述符 fd
 * 5. open/read/write/close 的用户接口
 * 6. Shell 命令
 *
 * 它只处理：
 * 1. inode_id
 * 2. offset
 * 3. size
 * 4. logical_block
 * 5. physical_block
 *
 */

#include <stdint.h>
#include "type.h"

/*
 * 按 inode + offset 读取数据。
 *
 * 参数：
 * inode_id:
 *     要读取的 inode 编号。
 *
 * offset:
 *     文件内部偏移量，单位字节。
 *
 * buf:
 *     输出缓冲区。
 *
 * size:
 *     希望读取的字节数。
 *
 * bytes_read:
 *     实际读取的字节数。
 *
 * 说明：
 * 1. 如果 offset >= inode.size，则读取 0 字节。
 * 2. 如果读取范围超过文件大小，只读取到文件末尾。
 * 3. 如果某个逻辑块没有物理块，说明是稀疏文件空洞，读取时返回 0。
 */
int myfs_inode_read_data(
        myfs_ino_t inode_id,
        uint64_t offset,
        void *buf,
        uint32_t size,
        uint32_t *bytes_read
);

/*
 * 按 inode + offset 写入数据。
 *
 * 参数：
 * inode_id:
 *     要写入的 inode 编号。
 *
 * offset:
 *     文件内部偏移量，单位字节。
 *
 * buf:
 *     输入缓冲区。
 *
 * size:
 *     希望写入的字节数。
 *
 * bytes_written:
 *     实际写入的字节数。
 *
 * 说明：
 * 1. 写入时会自动通过 block_map 分配物理块。
 * 2. 支持跨块写入。
 * 3. 支持 seek 到远位置后写入，形成稀疏文件。
 * 4. 写入超过原文件大小时，会更新 inode.size。
 */
int myfs_inode_write_data(
        myfs_ino_t inode_id,
        uint64_t offset,
        const void *buf,
        uint32_t size,
        uint32_t *bytes_written
);

/*
 * 截断 inode 对应文件的逻辑大小。
 *
 * 参数：
 * inode_id:
 *     inode 编号。
 *
 * new_size:
 *     新的文件大小。
 *
 * 功能：
 * 1. 如果 new_size 小于原 size，释放超出部分的数据块。
 * 2. 如果 new_size 大于原 size，只扩大逻辑大小，不立即分配物理块。
 *    这样可以形成稀疏文件。
 */
int myfs_inode_truncate_data(
        myfs_ino_t inode_id,
        uint64_t new_size
);

/*
 * 将指定范围置零。
 *
 * 当前实现方式：
 * 直接写入 0 字节。
 *
 * 后续如果要优化稀疏文件，可以在整块置零时释放物理块。
 */
int myfs_inode_zero_range(
        myfs_ino_t inode_id,
        uint64_t offset,
        uint64_t length
);

#endif