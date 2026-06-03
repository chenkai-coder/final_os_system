#ifndef MYFS_SUPERBLOCK_H
#define MYFS_SUPERBLOCK_H

/*
 * 超级块结构与超级块操作接口。
 *
 * 超级块是文件系统的总控制信息，位于 disk.img 的第 0 块。
 *
 * 超级块中保存：
 * 1. 文件系统魔数和版本号。
 * 2. 块大小、总块数。
 * 3. inode 总数和空闲 inode 数。
 * 4. 数据块总数和空闲数据块数。
 * 5. inode 位图、inode 表、journal 区、数据区的位置。
 * 6. 根目录 inode 编号。
 * 7. 成组链接法当前空闲块组。
 * 8. 文件系统 clean / dirty 状态。
 * 9. 挂载次数和时间戳。
 *
 */

#include <stdint.h>
#include "type.h"
#include "config.h"

/*
 * 文件系统状态。
 *
 * CLEAN 表示上次正常卸载。
 * DIRTY 表示正在挂载使用，或者上次异常退出。
 */
typedef enum {
    MYFS_STATE_CLEAN = 0,
    MYFS_STATE_DIRTY = 1
} myfs_state_t;

/*
 * 超级块结构。
 *
 * 注意：
 * 该结构会直接写入磁盘第 0 块。
 * 因此后续修改字段时，需要考虑磁盘格式兼容性。
 */
typedef struct myfs_superblock {
    /*
     * 文件系统魔数，用于识别磁盘格式。
     */
    uint32_t magic;

    /*
     * 文件系统版本号。
     */
    uint32_t version;

    /*
     * 块大小和磁盘总块数。
     */
    uint32_t block_size;
    uint32_t total_blocks;

    /*
     * inode 总数和空闲 inode 数量。
     */
    uint32_t total_inodes;
    uint32_t free_inodes_count;

    /*
     * 空闲数据块数量。
     *
     * 当前第一阶段初始化为数据区总块数。
     * 后续实现成组链接法后，由 block_alloc/block_free 维护。
     */
    uint32_t free_blocks_count;

    /*
     * inode 位图区域。
     */
    myfs_block_t inode_bitmap_start;
    uint32_t inode_bitmap_blocks;

    /*
     * inode 表区域。
     */
    myfs_block_t inode_table_start;
    uint32_t inode_table_blocks;

    /*
     * journal 日志区域。
     *
     * 第一阶段只预留。
     */
    myfs_block_t journal_start;
    uint32_t journal_blocks;

    /*
     * 数据区。
     */
    myfs_block_t data_block_start;
    uint32_t data_blocks;

    /*
     * 根目录 inode 编号。
     *
     * 第一阶段先约定为 0。
     * 第二阶段真正实现 inode 后，需要初始化 root inode。
     */
    myfs_ino_t root_inode;

    /*
     * 成组链接法当前空闲块组。
     *
     * 第一阶段暂时不填充。
     * 第三阶段实现数据块分配器时使用。
     */
    uint32_t free_group_count;
    myfs_block_t free_group[MYFS_FREE_GROUP_SIZE];

    /*
     * 文件系统状态。
     *
     * mount 时标记 DIRTY。
     * umount 时标记 CLEAN。
     */
    uint32_t fs_state;

    /*
     * 挂载次数。
     */
    uint32_t mount_count;

    /*
     * 最近挂载时间和最近写入时间。
     */
    uint64_t last_mount_time;
    uint64_t last_write_time;

    /*
     * 超级块校验和。
     *
     * 用于检查超级块是否被破坏。
     */
    uint32_t checksum;
} myfs_superblock_t;

/*
 * 初始化一个超级块结构。
 *
 * 注意：
 * 该函数只初始化内存中的 sb，不负责写入磁盘。
 */
int myfs_super_init(
        myfs_superblock_t *sb,
        uint32_t total_blocks,
        uint32_t total_inodes
);

/*
 * 从 disk.img 第 0 块加载超级块到内存。
 */
int myfs_super_load(void);

/*
 * 将内存中的超级块同步写回 disk.img 第 0 块。
 */
int myfs_super_sync(void);

/*
 * 检查当前内存超级块是否合法。
 *
 * 检查内容包括：
 * 1. magic 是否正确。
 * 2. version 是否支持。
 * 3. block_size 是否正确。
 * 4. 磁盘布局是否越界。
 * 5. checksum 是否正确。
 */
int myfs_super_check(void);

/*
 * 获取当前内存中的超级块指针。
 *
 * 注意：
 * 返回的是内部全局超级块指针。
 * 上层不应该直接修改其中字段。
 */
myfs_superblock_t *myfs_super_get(void);

/*
 * 将文件系统状态标记为 DIRTY。
 *
 * mount 时调用。
 */
int myfs_super_mark_dirty(void);

/*
 * 将文件系统状态标记为 CLEAN。
 *
 * umount 时调用。
 */
int myfs_super_mark_clean(void);

#endif