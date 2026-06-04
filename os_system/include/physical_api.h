#ifndef MYFS_PHYSICAL_API_H
#define MYFS_PHYSICAL_API_H

/*
 * physical_api.h
 * ------------------------------------------------------------
 * MYFS 物理层统一对外接口。
 *
 * 该文件是底层开发人员提供给上层开发人员使用的统一头文件。
 *
 * 上层逻辑模块，例如：
 *
 * path.cpp
 * dir.cpp
 * file.cpp
 * fd_table.cpp
 * shell.cpp
 *
 * 原则上只需要包含：
 *
 *     #include "physical_api.h"
 *
 * 不建议上层直接包含 disk.h、superblock.h、block_alloc.h 等内部头文件。
 *
 * 本接口只暴露物理层稳定能力：
 *
 * 1. 文件系统格式化、挂载、卸载
 * 2. inode 创建、释放、读取、写回
 * 3. inode 裸数据读写
 * 4. 逻辑块到物理块映射查询
 * 5. truncate / zero_range
 * 6. sync / cache / fsck / debug
 *
 */

#include <stdint.h>
#include "type.h"
#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif


/* ============================================================
 * 物理层 inode 类型
 * ============================================================
 */

#define MYFS_PHYS_INODE_FREE    0
#define MYFS_PHYS_INODE_FILE    1
#define MYFS_PHYS_INODE_DIR     2
#define MYFS_PHYS_INODE_SYMLINK 3


/* ============================================================
 * 物理层 inode 信息结构
 *
 * 这个结构是给上层读取 inode 状态用的。
 * 它不是磁盘 inode 的完整内部结构，避免上层直接修改 direct/indirect。
 * ============================================================
 */

typedef struct myfs_phys_inode_info {
    myfs_ino_t inode_id;

    uint16_t type;
    uint16_t mode;

    uint32_t uid;
    uint32_t gid;

    uint64_t size;
    uint32_t block_count;

    uint32_t link_count;
    uint32_t open_count;

    uint64_t atime;
    uint64_t mtime;
    uint64_t ctime;
    uint64_t crtime;
} myfs_phys_inode_info_t;


/* ============================================================
 * 文件系统总体统计信息
 * ============================================================
 */

typedef struct myfs_phys_statfs {
    uint32_t magic;
    uint32_t version;
    uint32_t block_size;

    uint32_t total_blocks;
    uint32_t total_inodes;

    uint32_t used_inodes;
    uint32_t free_inodes;

    uint32_t data_block_start;
    uint32_t data_blocks;
    uint32_t used_blocks;
    uint32_t free_blocks;

    uint32_t root_inode;

    uint32_t fs_state;
    uint32_t mount_count;
} myfs_phys_statfs_t;


/* ============================================================
 * 物理块映射信息
 * ============================================================
 */

typedef struct myfs_phys_bmap_result {
    myfs_ino_t inode_id;
    uint32_t logical_block;

    int is_hole;
    myfs_block_t physical_block;
} myfs_phys_bmap_result_t;


/* ============================================================
 * fsck 检查结果
 * ============================================================
 */

typedef struct myfs_phys_fsck_result {
    uint32_t errors_found;
    uint32_t errors_fixed;

    uint32_t inode_state_errors;
    uint32_t free_inode_count_errors;

    uint32_t invalid_block_refs;
    uint32_t duplicated_used_blocks;
    uint32_t duplicated_free_blocks;
    uint32_t used_free_conflicts;
    uint32_t leaked_blocks;

    uint32_t free_chain_errors;
    uint32_t free_block_count_errors;
} myfs_phys_fsck_result_t;


/* ============================================================
 * cache 统计信息
 * ============================================================
 */

typedef struct myfs_phys_cache_stats {
    uint64_t read_count;
    uint64_t write_count;
    uint64_t hit_count;
    uint64_t miss_count;
    uint64_t evict_count;
    uint64_t dirty_flush_count;
} myfs_phys_cache_stats_t;


/* ============================================================
 * 1. 文件系统生命周期接口
 * ============================================================
 */

/*
 * 格式化文件系统。
 *
 * 上层初始化 disk.img 时调用。
 */
int myfs_phys_format(
        const char *disk_path,
        uint32_t total_blocks,
        uint32_t total_inodes
);

/*
 * 挂载文件系统。
 *
 * 挂载时会：
 * 1. 打开 disk.img
 * 2. 加载超级块
 * 3. 执行 journal recovery
 * 4. 标记文件系统为 dirty
 */
int myfs_phys_mount(const char *disk_path);

/*
 * 卸载文件系统。
 *
 * 卸载时会：
 * 1. sync
 * 2. 关闭 cache
 * 3. 标记 clean
 * 4. 关闭 disk.img
 */
int myfs_phys_umount(void);

/*
 * 判断是否已挂载。
 */
int myfs_phys_is_mounted(void);


/* ============================================================
 * 2. inode 管理接口
 * ============================================================
 */

/*
 * 创建 inode。
 *
 * 上层 create/mkdir/symlink 时可以调用。
 */
int myfs_phys_inode_create(
        uint16_t type,
        uint16_t mode,
        uint32_t uid,
        uint32_t gid,
        myfs_ino_t *inode_id
);

/*
 * 释放 inode。
 *
 * 注意：
 * 该函数会释放 inode 占用的所有底层物理块。
 * 上层应先处理目录项、link_count 等逻辑，再调用该接口。
 */
int myfs_phys_inode_free(myfs_ino_t inode_id);

/*
 * 获取 inode 基本信息。
 */
int myfs_phys_inode_get_info(
        myfs_ino_t inode_id,
        myfs_phys_inode_info_t *info
);

/*
 * 修改 inode 基本属性。
 *
 * 当前只允许修改 mode、uid、gid。
 * size、block_count、direct/indirect 不允许上层直接改，
 * 应通过 write/truncate 等物理接口间接维护。
 */
int myfs_phys_inode_set_attr(
        myfs_ino_t inode_id,
        uint16_t mode,
        uint32_t uid,
        uint32_t gid
);

/*
 * 获取 root inode
 */
int myfs_phys_get_root_inode(myfs_ino_t *root_inode);

/*
 * 判断 inode 类型
 */
int myfs_phys_inode_is_file(myfs_ino_t inode_id);
int myfs_phys_inode_is_dir(myfs_ino_t inode_id);
int myfs_phys_inode_is_symlink(myfs_ino_t inode_id);

/*
 * 增加硬链接计数。
 */
int myfs_phys_inode_inc_link(myfs_ino_t inode_id);

/*
 * 减少硬链接计数。
 */
int myfs_phys_inode_dec_link(myfs_ino_t inode_id);

/*
 * 增加打开计数。
 */
int myfs_phys_inode_inc_open(myfs_ino_t inode_id);

/*
 * 减少打开计数。
 */
int myfs_phys_inode_dec_open(myfs_ino_t inode_id);


/* ============================================================
 * 3. inode 裸数据读写接口
 *
 * 上层 file.cpp 可以基于这些接口实现 fd read/write。
 * ============================================================
 */

/*
 * 从指定 inode 的 offset 位置读取数据。
 */
int myfs_phys_read(
        myfs_ino_t inode_id,
        uint64_t offset,
        void *buf,
        uint32_t size,
        uint32_t *bytes_read
);

/*
 * 向指定 inode 的 offset 位置写入数据。
 */
int myfs_phys_write(
        myfs_ino_t inode_id,
        uint64_t offset,
        const void *buf,
        uint32_t size,
        uint32_t *bytes_written
);

/*
 * 截断 inode 数据。
 */
int myfs_phys_truncate(
        myfs_ino_t inode_id,
        uint64_t new_size
);

/*
 * 指定范围置零。
 */
int myfs_phys_zero_range(
        myfs_ino_t inode_id,
        uint64_t offset,
        uint64_t length
);


/* ============================================================
 * 4. 逻辑块映射接口
 * ============================================================
 */

/*
 * 查询 inode 的逻辑块对应的物理块。
 *
 * 只查询，不分配。
 */
int myfs_phys_bmap(
        myfs_ino_t inode_id,
        uint32_t logical_block,
        myfs_phys_bmap_result_t *result
);


/* ============================================================
 * 5. 统计和同步接口
 * ============================================================
 */

/*
 * 获取文件系统总体信息。
 */
int myfs_phys_statfs(myfs_phys_statfs_t *out);

/*
 * 获取空闲组信息。
 */
int myfs_phys_get_free_group(uint32_t *count, uint32_t *blocks, uint32_t max_blocks);

int myfs_phys_get_used_blocks_in_range(
        myfs_block_t start_block,
        uint32_t count,
        uint32_t *used_blocks,
        uint32_t max_blocks,
        uint32_t *actual_count
);

/*
 * 同步整个文件系统。
 */
int myfs_phys_sync(void);

/*
 * 同步某个物理块。
 */
int myfs_phys_fsync_block(myfs_block_t block_id);


/* ============================================================
 * 6. 缓存接口
 * ============================================================
 */

/*
 * 初始化物理块缓存。
 */
int myfs_phys_cache_init(uint32_t capacity);

/*
 * 关闭物理块缓存。
 */
int myfs_phys_cache_shutdown(void);

/*
 * 获取缓存统计信息。
 */
int myfs_phys_cache_get_stats(myfs_phys_cache_stats_t *stats);

int myfs_phys_cache_get_lru_list(uint32_t *blocks, uint32_t max_count, uint32_t *actual_count);


/* ============================================================
 * 7. fsck 接口
 * ============================================================
 */

/*
 * 执行物理一致性检查。
 *
 * verbose:
 * 1 输出详细信息
 * 0 不输出详细信息
 */
int myfs_phys_fsck(
        int verbose,
        myfs_phys_fsck_result_t *result
);


/* ============================================================
 * 8. 调试打印接口
 *
 * 这些接口主要给 Shell 或调试工具调用。
 * ============================================================
 */

int myfs_phys_debug_statfs(void);

int myfs_phys_debug_super(void);

int myfs_phys_debug_inode(myfs_ino_t inode_id);

int myfs_phys_debug_bmap(
        myfs_ino_t inode_id,
        uint32_t logical_block
);

int myfs_phys_debug_free_group(void);

int myfs_phys_debug_cache(void);

int myfs_phys_debug_hexdump(
        myfs_block_t block_id,
        uint32_t max_bytes
);

int myfs_phys_debug_recover_blocks(void);

int myfs_phys_debug_blockmap_range(
        myfs_block_t start_block,
        uint32_t count
);


/* ============================================================
 * 9. 错误信息
 * ============================================================
 */

const char *myfs_phys_strerror(int err);


#ifdef __cplusplus
}
#endif

#endif
