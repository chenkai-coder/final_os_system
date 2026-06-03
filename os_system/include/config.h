#ifndef MYFS_CONFIG_H
#define MYFS_CONFIG_H

/*
 * config.h
 * ------------------------------------------------------------
 * 文件系统全局配置参数头文件。
 *
 * 作用：
 * 1. 定义块大小。
 * 2. 定义文件系统版本号。
 * 3. 定义 inode、日志区、成组链接法等核心参数。
 *
 * 注意：
 * 这些配置会影响磁盘布局。
 * 一旦已经格式化出 disk.img，随意修改这些参数可能导致旧磁盘无法识别。
 */

/*
 * 文件系统块大小。
 *
 * 本项目固定使用 4KB 块。
 * disk.img 的读写、inode 表、数据区、日志区都以 4KB 为基本单位。
 */
#define MYFS_BLOCK_SIZE 4096u

/*
 * 文件系统格式版本号。
 *
 * 后续如果磁盘布局或超级块格式发生变化，可以增加版本号。
 */
#define MYFS_VERSION 1u

/*
 * 第一阶段还没有真正实现 inode 结构。
 * 但为了提前计算 inode 表大小，暂定每个 inode 占 256 字节。
 *
 * 后续正式定义 myfs_inode_t 时，应尽量保证：
 * sizeof(myfs_inode_t) <= MYFS_INODE_SIZE
 *
 * 如果超过 256 字节，需要同步修改该宏，并重新格式化 disk.img。
 */
#define MYFS_INODE_SIZE 256u

/*
 * 默认 inode 数量。
 *
 * 如果 mkfs 时用户没有指定 inode 数量，可以使用这个默认值。
 */
#define MYFS_DEFAULT_TOTAL_INODES 4096u

/*
 * 默认日志区块数。
 *
 * 第一阶段暂时不实现 journaling，但先在磁盘布局中预留 journal 区。
 * 这样后续实现元数据日志时，不需要重新修改磁盘布局。
 */
#define MYFS_DEFAULT_JOURNAL_BLOCKS 1024u

/*
 * 最小日志区块数。
 *
 * 当磁盘较小时，日志区会自动缩小，但不能小于该值。
 */
#define MYFS_MIN_JOURNAL_BLOCKS 16u

/*
 * 成组链接法每组保存的空闲块号数量。
 *
 * 后续实现数据块空闲空间管理时使用。
 * 超级块中会保存当前一组空闲块号。
 */
#define MYFS_FREE_GROUP_SIZE 100u

/*
 * 块缓存容量。
 *
 * 表示最多缓存多少个 4KB 块。
 * 后续可以根据测试结果调整。
 */
#define MYFS_CACHE_CAPACITY 128u

#endif