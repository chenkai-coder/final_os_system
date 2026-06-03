#include "cache.h"
#include "disk.h"
#include "config.h"
#include "error.h"

#include <unordered_map>
#include <list>
#include <array>
#include <cstring>

/*
 * cache.cpp
 * ------------------------------------------------------------
 * 块缓存 + LRU + write-back 实现。
 *
 * 注意：
 * 本缓存是内存结构，不写入 disk.img。
 * 因此可以使用 STL 容器。
 */

struct CacheBlock {
    myfs_block_t block_id;
    std::array<unsigned char, MYFS_BLOCK_SIZE> data;

    bool dirty;
    bool is_metadata;

    /*
     * 指向 LRU 链表中的位置，方便 O(1) 移动。
     */
    std::list<myfs_block_t>::iterator lru_it;
};

static int g_cache_initialized = 0;
static uint32_t g_cache_capacity = 0;

/*
 * LRU 链表。
 *
 * front 表示最近使用。
 * back 表示最久未使用。
 */
static std::list<myfs_block_t> g_lru_list;

/*
 * block_id -> CacheBlock
 */
static std::unordered_map<myfs_block_t, CacheBlock> g_cache_map;

static myfs_cache_stats_t g_stats;


/*
 * 将某个块移动到 LRU 链表头部。
 */
static void touch_block(myfs_block_t block_id) {
    auto it = g_cache_map.find(block_id);

    if (it == g_cache_map.end()) {
        return;
    }

    g_lru_list.erase(it->second.lru_it);
    g_lru_list.push_front(block_id);
    it->second.lru_it = g_lru_list.begin();
}


/*
 * 将一个缓存块写回磁盘。
 *
 * 只有 dirty 块才真正写磁盘。
 */
static int flush_cache_entry(CacheBlock &entry) {
    if (!entry.dirty) {
        return MYFS_OK;
    }

    int ret = myfs_disk_write_block(
            entry.block_id,
            entry.data.data()
    );

    if (ret != MYFS_OK) {
        return ret;
    }

    entry.dirty = false;
    g_stats.dirty_flush_count++;

    return MYFS_OK;
}


/*
 * 淘汰一个最久未使用的缓存块。
 */
static int evict_one_block(void) {
    if (g_lru_list.empty()) {
        return MYFS_OK;
    }

    myfs_block_t victim = g_lru_list.back();

    auto it = g_cache_map.find(victim);
    if (it == g_cache_map.end()) {
        g_lru_list.pop_back();
        return MYFS_OK;
    }

    int ret = flush_cache_entry(it->second);
    if (ret != MYFS_OK) {
        return ret;
    }

    g_lru_list.pop_back();
    g_cache_map.erase(it);

    g_stats.evict_count++;

    return MYFS_OK;
}


/*
 * 如果缓存已满，则淘汰直到有空间。
 */
static int ensure_capacity(void) {
    while (g_cache_map.size() >= g_cache_capacity) {
        int ret = evict_one_block();
        if (ret != MYFS_OK) {
            return ret;
        }
    }

    return MYFS_OK;
}


int myfs_cache_init(uint32_t capacity) {
    if (capacity == 0) {
        return MYFS_ERR_INVALID_ARG;
    }

    /*
     * 如果已经初始化，先关闭旧缓存。
     */
    if (g_cache_initialized) {
        int ret = myfs_cache_shutdown();
        if (ret != MYFS_OK) {
            return ret;
        }
    }

    g_cache_capacity = capacity;
    g_lru_list.clear();
    g_cache_map.clear();
    std::memset(&g_stats, 0, sizeof(g_stats));

    g_cache_initialized = 1;

    return MYFS_OK;
}


int myfs_cache_is_initialized(void) {
    return g_cache_initialized;
}


int myfs_cache_read_block(myfs_block_t block_id, void *buf) {
    if (!g_cache_initialized) {
        return myfs_disk_read_block(block_id, buf);
    }

    if (buf == nullptr) {
        return MYFS_ERR_INVALID_ARG;
    }

    g_stats.read_count++;

    auto it = g_cache_map.find(block_id);

    /*
     * 缓存命中。
     */
    if (it != g_cache_map.end()) {
        std::memcpy(buf, it->second.data.data(), MYFS_BLOCK_SIZE);
        touch_block(block_id);
        g_stats.hit_count++;
        return MYFS_OK;
    }

    /*
     * 缓存未命中，需要从磁盘读取。
     */
    g_stats.miss_count++;

    int ret = ensure_capacity();
    if (ret != MYFS_OK) {
        return ret;
    }

    CacheBlock entry;
    entry.block_id = block_id;
    entry.dirty = false;
    entry.is_metadata = false;

    ret = myfs_disk_read_block(block_id, entry.data.data());
    if (ret != MYFS_OK) {
        return ret;
    }

    g_lru_list.push_front(block_id);
    entry.lru_it = g_lru_list.begin();

    g_cache_map.emplace(block_id, entry);

    std::memcpy(buf, entry.data.data(), MYFS_BLOCK_SIZE);

    return MYFS_OK;
}


int myfs_cache_write_block(
        myfs_block_t block_id,
        const void *buf,
        int is_metadata
) {
    if (!g_cache_initialized) {
        return myfs_disk_write_block(block_id, buf);
    }

    if (buf == nullptr) {
        return MYFS_ERR_INVALID_ARG;
    }

    g_stats.write_count++;

    auto it = g_cache_map.find(block_id);

    /*
     * 如果该块已经在缓存中，直接覆盖缓存内容。
     */
    if (it != g_cache_map.end()) {
        std::memcpy(it->second.data.data(), buf, MYFS_BLOCK_SIZE);
        it->second.dirty = true;
        it->second.is_metadata = is_metadata != 0;

        touch_block(block_id);
        g_stats.hit_count++;

        return MYFS_OK;
    }

    /*
     * 缓存未命中，先保证容量，再新建缓存块。
     */
    g_stats.miss_count++;

    int ret = ensure_capacity();
    if (ret != MYFS_OK) {
        return ret;
    }

    CacheBlock entry;
    entry.block_id = block_id;
    std::memcpy(entry.data.data(), buf, MYFS_BLOCK_SIZE);
    entry.dirty = true;
    entry.is_metadata = is_metadata != 0;

    g_lru_list.push_front(block_id);
    entry.lru_it = g_lru_list.begin();

    g_cache_map.emplace(block_id, entry);

    return MYFS_OK;
}


int myfs_cache_flush_block(myfs_block_t block_id) {
    if (!g_cache_initialized) {
        return MYFS_ERR_NOT_MOUNTED;
    }

    auto it = g_cache_map.find(block_id);

    if (it == g_cache_map.end()) {
        return MYFS_OK;
    }

    return flush_cache_entry(it->second);
}


int myfs_cache_flush_all(void) {
    if (!g_cache_initialized) {
        return MYFS_ERR_NOT_MOUNTED;
    }

    /*
     * 简单起见，直接遍历全部缓存块。
     */
    for (auto &pair : g_cache_map) {
        int ret = flush_cache_entry(pair.second);
        if (ret != MYFS_OK) {
            return ret;
        }
    }

    return MYFS_OK;
}


int myfs_cache_sync(void) {
    return myfs_cache_flush_all();
}


int myfs_cache_shutdown(void) {
    if (!g_cache_initialized) {
        return MYFS_OK;
    }

    int ret = myfs_cache_flush_all();
    if (ret != MYFS_OK) {
        return ret;
    }

    g_cache_map.clear();
    g_lru_list.clear();
    g_cache_capacity = 0;
    g_cache_initialized = 0;

    return MYFS_OK;
}


void myfs_cache_get_stats(myfs_cache_stats_t *stats) {
    if (stats == nullptr) {
        return;
    }

    *stats = g_stats;
}


void myfs_cache_reset_stats(void) {
    std::memset(&g_stats, 0, sizeof(g_stats));
}

int myfs_cache_get_lru_list(myfs_block_t *blocks, uint32_t max_count, uint32_t *actual_count) {
    if (!g_cache_initialized || blocks == nullptr || actual_count == nullptr) {
        if (actual_count) *actual_count = 0;
        return MYFS_ERR_INVALID_ARG;
    }

    uint32_t count = 0;
    for (auto it = g_lru_list.begin(); it != g_lru_list.end() && count < max_count; ++it) {
        blocks[count++] = *it;
    }
    *actual_count = count;
    return MYFS_OK;
}

int myfs_cache_invalidate_block(myfs_block_t block_id) {
    if (!g_cache_initialized) {
        return MYFS_OK;
    }

    auto it = g_cache_map.find(block_id);
    if (it == g_cache_map.end()) {
        return MYFS_OK;
    }

    g_lru_list.erase(it->second.lru_it);
    g_cache_map.erase(it);

    return MYFS_OK;
}
