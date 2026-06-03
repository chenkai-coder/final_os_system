#ifndef MYFS_CACHE_H
#define MYFS_CACHE_H

/*
 * cache.h
 * ------------------------------------------------------------
 * 块缓存模块。
 *
 * 本模块属于文件系统物理层/底层支撑模块。
 *
 * 作用：
 * 1. 缓存 disk.img 中的 4KB 物理块。
 * 2. 减少频繁 myfs_disk_read_block / myfs_disk_write_block。
 * 3. 使用 LRU 策略淘汰长期未使用的缓存块。
 * 4. 支持 write-back，即写操作先写缓存，稍后 flush 到磁盘。
 */

#include <stdint.h>
#include "type.h"

/*
 * 缓存统计信息。
 */
typedef struct myfs_cache_stats {
    uint64_t read_count;         /* 读缓存次数 */
    uint64_t write_count;        /* 写缓存次数 */
    uint64_t hit_count;          /* 命中次数 */
    uint64_t miss_count;         /* 未命中次数 */
    uint64_t evict_count;        /* 淘汰次数 */
    uint64_t dirty_flush_count;  /* 脏块刷回次数 */
} myfs_cache_stats_t;

/*
 * 初始化块缓存。
 *
 * 参数：
 * capacity：最多缓存多少个块。
 */
int myfs_cache_init(uint32_t capacity);

/*
 * 判断缓存是否已经初始化。
 */
int myfs_cache_is_initialized(void);

/*
 * 通过缓存读取一个物理块。
 *
 * 如果缓存命中，直接从缓存复制。
 * 如果缓存未命中，从 disk.img 读取并加入缓存。
 */
int myfs_cache_read_block(myfs_block_t block_id, void *buf);

/*
 * 通过缓存写入一个物理块。
 *
 * write-back 策略：
 * 1. 数据先写入缓存。
 * 2. 标记为 dirty。
 * 3. 不立即写回磁盘。
 * 4. flush/sync/evict/关闭缓存时再写回磁盘。
 */
int myfs_cache_write_block(
        myfs_block_t block_id,
        const void *buf,
        int is_metadata
);

/*
 * 刷回指定块。
 *
 * 如果该块在缓存中且为 dirty，则写回磁盘。
 */
int myfs_cache_flush_block(myfs_block_t block_id);

/*
 * 刷回全部 dirty 缓存块。
 */
int myfs_cache_flush_all(void);

/*
 * sync 等价操作。
 */
int myfs_cache_sync(void);

/*
 * 关闭缓存。
 *
 * 会先 flush 所有 dirty 块，然后清空缓存结构。
 */
int myfs_cache_shutdown(void);

/*
 * 获取缓存统计信息。
 */
void myfs_cache_get_stats(myfs_cache_stats_t *stats);

/*
 * 清空统计信息。
 */
void myfs_cache_reset_stats(void);

/*
 * 获取当前 LRU 列表中的物理块。
 */
int myfs_cache_get_lru_list(myfs_block_t *blocks, uint32_t max_count, uint32_t *actual_count);
int myfs_cache_invalidate_block(myfs_block_t block_id);

#endif
