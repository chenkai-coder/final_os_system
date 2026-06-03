#ifndef MYFS_TYPES_H
#define MYFS_TYPES_H

/*
 * type.h
 * ------------------------------------------------------------
 * 文件系统基本类型定义头文件。
 *
 * 作用：
 * 统一定义块号、inode 号、文件大小、时间戳等类型。
 *
 * 好处：
 * 1. 提高代码可读性。
 * 2. 后续如果需要扩大块号或 inode 号范围，只需要修改这里。
 */

#include <stdint.h>

/*
 * 物理块号类型。
 *
 * disk.img 被划分为若干个 4KB 块，每个块都有一个编号。
 */
typedef uint32_t myfs_block_t;

/*
 * inode 编号类型。
 *
 * 每个文件、目录、符号链接都会对应一个 inode 编号。
 */
typedef uint32_t myfs_ino_t;

/*
 * 文件大小类型。
 *
 * 使用 64 位整数，方便后续支持较大文件和稀疏文件。
 */
typedef uint64_t myfs_size_t;

/*
 * 时间戳类型。
 *
 * 用于 atime、mtime、ctime、crtime 等文件时间字段。
 */
typedef uint64_t myfs_time_t;

#endif