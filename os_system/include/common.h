#ifndef MYFS_COMMON_H
#define MYFS_COMMON_H

/*
 * common.h
 * ------------------------------------------------------------
 * 文件系统公共基础定义头文件。
 *
 * 作用：
 * 1. 引入常用标准类型头文件。
 * 2. 定义文件系统魔数 MYFS_MAGIC。
 * 3. 定义通用布尔宏 MYFS_TRUE / MYFS_FALSE。
 *
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/*
 * 文件系统魔数。
 *
 * 作用：
 * 用于识别 disk.img 是否为本文件系统格式。
 *
 * mount 时会读取超级块并检查 magic 字段。
 * 如果 magic 不等于 MYFS_MAGIC，说明该磁盘镜像不是合法的 MYFS 文件系统。
 */
#define MYFS_MAGIC 0x20235840u

/*
 * 通用真假值宏。
 *
 * C 语言中虽然有 bool 类型，但项目中有些接口可能仍然使用 int 表示真假。
 */
#define MYFS_TRUE 1
#define MYFS_FALSE 0

#endif