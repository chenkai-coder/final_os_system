#ifndef MYFS_DISK_H
#define MYFS_DISK_H

/*
 * disk.h
 * ------------------------------------------------------------
 * 虚拟磁盘块设备接口。
 *
 * 作用：
 * 该模块负责把普通宿主文件 disk.img 模拟成一个块设备。
 *
 * 主要功能：
 * 1. 创建 disk.img。
 * 2. 打开 disk.img。
 * 3. 关闭 disk.img。
 * 4. 按 4KB 块读取。
 * 5. 按 4KB 块写入。
 *
 */

#include <stdint.h>
#include "type.h"

/*
 * 创建一个新的虚拟磁盘文件。
 *
 * 参数：
 * path         磁盘镜像路径，例如 "disk.img"
 * total_blocks 总块数，每块大小固定为 MYFS_BLOCK_SIZE
 *
 * 返回：
 * MYFS_OK 成功
 * 其他错误码表示失败
 */
int myfs_disk_create(const char *path, uint32_t total_blocks);

/*
 * 打开一个已有的虚拟磁盘文件。
 *
 * mount 时会调用该接口。
 */
int myfs_disk_open(const char *path);

/*
 * 关闭当前打开的虚拟磁盘文件。
 *
 * umount 时会调用该接口。
 */
int myfs_disk_close(void);

/*
 * 读取一个物理块。
 *
 * 参数：
 * block_id 物理块号
 * buf      输出缓冲区，至少需要 MYFS_BLOCK_SIZE 字节
 *
 * 说明：
 * 该接口一次读取完整 4KB 块。
 */
int myfs_disk_read_block(myfs_block_t block_id, void *buf);

/*
 * 写入一个物理块。
 *
 * 参数：
 * block_id 物理块号
 * buf      输入缓冲区，至少需要 MYFS_BLOCK_SIZE 字节
 *
 * 说明：
 * 该接口一次写入完整 4KB 块。
 */
int myfs_disk_write_block(myfs_block_t block_id, const void *buf);

/*
 * 获取当前磁盘总块数。
 */
uint32_t myfs_disk_total_blocks(void);

/*
 * 获取块大小。
 *
 * 当前固定返回 MYFS_BLOCK_SIZE。
 */
uint32_t myfs_disk_block_size(void);

/*
 * 判断磁盘是否已经打开。
 *
 * 返回：
 * 1 表示已经打开
 * 0 表示未打开
 */
int myfs_disk_is_open(void);

#endif