#include "raw_file.h"
#include "block_map.h"
#include "inode.h"
#include "disk.h"
#include "cache.h"
#include "config.h"
#include "error.h"
#include "time.h"

#include <string.h>
#include <stdint.h>
#include <stdlib.h>

/*
 * raw_file.cpp
 * ------------------------------------------------------------
 * 第五阶段：基于 inode 的裸数据读写。
 *
 * 本模块仍然属于底层物理支撑层。
 *
 * 它只处理 inode_id + offset + size，
 * 不处理文件名、路径、目录项、文件描述符和 Shell。
 */


/* ============================================================
 * 1. 辅助函数
 * ============================================================
 */

/*
 * 计算两个 uint64_t 中较小的一个。
 */
static uint64_t min_u64(uint64_t a, uint64_t b) {
    return a < b ? a : b;
}

/*
 * 计算两个 uint32_t 中较小的一个。
 */
static uint32_t min_u32(uint32_t a, uint32_t b) {
    return a < b ? a : b;
}

/*
 * 判断 inode 类型是否允许拥有数据块。
 *
 * 当前阶段普通文件、目录、符号链接都允许拥有数据块。
 *
 * 说明：
 * 目录项逻辑由上层开发人员实现；
 * 但目录数据最终也会存储在 inode 的数据块中。
 */
static int inode_can_have_data(uint16_t type) {
    return type == MYFS_INODE_FILE ||
           type == MYFS_INODE_DIR ||
           type == MYFS_INODE_SYMLINK;
}


/* ============================================================
 * 2. 按 inode + offset 读取数据
 * ============================================================
 */

int myfs_inode_read_data(
        myfs_ino_t inode_id,
        uint64_t offset,
        void *buf,
        uint32_t size,
        uint32_t *bytes_read
) {
    if (buf == NULL || bytes_read == NULL) {
        return MYFS_ERR_INVALID_ARG;
    }

    *bytes_read = 0;

    if (size == 0) {
        return MYFS_OK;
    }

    myfs_inode_t inode;

    int ret = myfs_inode_read(inode_id, &inode);
    if (ret != MYFS_OK) {
        return ret;
    }

    if (!inode_can_have_data(inode.type)) {
        return MYFS_ERR_INVALID_INODE;
    }

    /*
     * 如果读取起点已经超过文件大小，则直接读取 0 字节。
     */
    if (offset >= inode.size) {
        return MYFS_OK;
    }

    /*
     * 最多只能读到文件末尾。
     */
    uint64_t readable_u64 = inode.size - offset;
    uint32_t to_read = (uint32_t) min_u64((uint64_t) size, readable_u64);

    unsigned char *out = (unsigned char *) buf;

    uint32_t done = 0;

    while (done < to_read) {
        uint64_t current_offset = offset + done;

        uint32_t logical_block =
                (uint32_t) (current_offset / MYFS_BLOCK_SIZE);

        uint32_t block_offset =
                (uint32_t) (current_offset % MYFS_BLOCK_SIZE);

        uint32_t bytes_in_block =
                MYFS_BLOCK_SIZE - block_offset;

        uint32_t remain =
                to_read - done;

        uint32_t chunk =
                min_u32(bytes_in_block, remain);

        myfs_block_t physical_block = 0;
        int is_hole = 0;

        /*
         * 读取时 create = 0。
         *
         * 如果该逻辑块没有分配物理块，则说明它是稀疏文件空洞。
         */
        ret = myfs_inode_get_data_block(
                inode_id,
                logical_block,
                0,
                &physical_block,
                &is_hole
        );

        if (ret != MYFS_OK) {
            return ret;
        }

        if (is_hole) {
            /*
             * 稀疏文件空洞读取时返回 0。
             */
            memset(out + done, 0, chunk);
        } else {
            unsigned char block_buf[MYFS_BLOCK_SIZE];

            ret = myfs_cache_read_block(physical_block, block_buf);
            if (ret != MYFS_OK) {
                return ret;
            }

            memcpy(out + done, block_buf + block_offset, chunk);
        }

        done += chunk;
    }

    *bytes_read = done;

    /*
     * 更新访问时间。
     *
     * 当前 inode_write 会更新 ctime。
     * 如果你后续希望 atime 更新不影响 ctime，可以单独写 inode_table_write。
     */
    inode.atime = (uint64_t) time(NULL);
    myfs_inode_write(inode_id, &inode);

    return MYFS_OK;
}


/* ============================================================
 * 3. 按 inode + offset 写入数据
 * ============================================================
 */

int myfs_inode_write_data(
        myfs_ino_t inode_id,
        uint64_t offset,
        const void *buf,
        uint32_t size,
        uint32_t *bytes_written
) {
    if (buf == NULL || bytes_written == NULL) {
        return MYFS_ERR_INVALID_ARG;
    }

    *bytes_written = 0;

    if (size == 0) {
        return MYFS_OK;
    }

    /*
     * 先读取 inode，只用于检查类型和获取原始 size。
     *
     * 注意：
     * 后面 myfs_inode_get_data_block(create=1) 可能会修改 inode 的
     * direct / indirect / block_count，并且已经写回 inode 表。
     *
     * 因此函数末尾不能继续使用这个旧 inode 直接写回，
     * 否则会把 block_map 刚写好的块映射覆盖掉。
     */
    myfs_inode_t inode;

    int ret = myfs_inode_read(inode_id, &inode);
    if (ret != MYFS_OK) {
        return ret;
    }

    if (!inode_can_have_data(inode.type)) {
        return MYFS_ERR_INVALID_INODE;
    }

    const unsigned char *input = (const unsigned char *) buf;

    uint32_t done = 0;

    while (done < size) {
        uint64_t current_offset = offset + done;

        uint32_t logical_block =
                (uint32_t) (current_offset / MYFS_BLOCK_SIZE);

        uint32_t block_offset =
                (uint32_t) (current_offset % MYFS_BLOCK_SIZE);

        uint32_t bytes_in_block =
                MYFS_BLOCK_SIZE - block_offset;

        uint32_t remain =
                size - done;

        uint32_t chunk =
                min_u32(bytes_in_block, remain);

        myfs_block_t physical_block = 0;
        int is_hole = 0;

        /*
         * 写入时 create = 1。
         *
         * 如果该 logical_block 还没有物理块，
         * block_map 会自动分配，并写回 inode 的 direct / indirect 字段。
         */
        ret = myfs_inode_get_data_block(
                inode_id,
                logical_block,
                1,
                &physical_block,
                &is_hole
        );

        if (ret != MYFS_OK) {
            return ret;
        }

        if (is_hole) {
            /*
             * create = 1 时正常不应该返回 hole。
             */
            return MYFS_ERR_CORRUPTED;
        }

        unsigned char block_buf[MYFS_BLOCK_SIZE];

        /*
         * 如果是整块覆盖，可以不用读取旧内容。
         *
         * 如果只是写入块的一部分，必须先读旧块，
         * 否则会把同一块中其他未写区域清掉。
         */
        if (block_offset == 0 && chunk == MYFS_BLOCK_SIZE) {
            memset(block_buf, 0, sizeof(block_buf));
        } else {
            ret = myfs_cache_read_block(physical_block, block_buf);
            if (ret != MYFS_OK) {
                return ret;
            }
        }

        memcpy(block_buf + block_offset, input + done, chunk);

        ret = myfs_cache_write_block(physical_block, block_buf, 0);
        if (ret != MYFS_OK) {
            return ret;
        }

        done += chunk;
    }

    /*
     * 关键修复：
     *
     * 必须重新读取 inode。
     *
     * 因为前面的 myfs_inode_get_data_block(create=1)
     * 可能已经修改并写回了 inode 的 direct / indirect / block_count。
     *
     * 如果这里继续使用函数开头读取的旧 inode，
     * 会把刚刚建立好的块映射覆盖掉，导致 read 时读成 hole。
     */
    ret = myfs_inode_read(inode_id, &inode);
    if (ret != MYFS_OK) {
        return ret;
    }

    /*
     * 更新 inode.size。
     */
    uint64_t end_pos = offset + done;

    if (end_pos > inode.size) {
        inode.size = end_pos;
    }

    inode.mtime = (uint64_t) time(NULL);
    inode.ctime = inode.mtime;

    ret = myfs_inode_write(inode_id, &inode);
    if (ret != MYFS_OK) {
        return ret;
    }

    *bytes_written = done;

    return MYFS_OK;
}

/* ============================================================
 * 4. 截断文件数据
 * ============================================================
 */

int myfs_inode_truncate_data(
        myfs_ino_t inode_id,
        uint64_t new_size
) {
    myfs_inode_t inode;

    int ret = myfs_inode_read(inode_id, &inode);
    if (ret != MYFS_OK) {
        return ret;
    }

    if (!inode_can_have_data(inode.type)) {
        return MYFS_ERR_INVALID_INODE;
    }

    uint64_t old_size = inode.size;

    /*
     * 情况一：
     * 扩大文件。
     *
     * 只修改逻辑大小，不立即分配数据块。
     * 这样中间未写入的区域就是稀疏文件空洞。
     */
    if (new_size >= old_size) {
        inode.size = new_size;
        inode.mtime = (uint64_t) time(NULL);
        inode.ctime = inode.mtime;

        return myfs_inode_write(inode_id, &inode);
    }

    /*
     * 情况二：
     * 缩小文件。
     *
     * 需要释放 new_size 之后的完整逻辑块。
     *
     * 注意：
     * 如果 new_size 落在某个块中间，该块仍然保留。
     * 块内 new_size 后面的残留数据不影响逻辑读取，
     * 因为 inode.size 限制了可读范围。
     */

    uint32_t old_last_block =
            (old_size == 0)
            ? 0
            : (uint32_t) ((old_size - 1) / MYFS_BLOCK_SIZE);

    uint32_t new_last_block =
            (new_size == 0)
            ? 0
            : (uint32_t) ((new_size - 1) / MYFS_BLOCK_SIZE);

    if (new_size == 0) {
        /*
         * 释放全部逻辑块。
         */
        ret = myfs_inode_release_all_blocks(inode_id);
        if (ret != MYFS_OK) {
            return ret;
        }

        ret = myfs_inode_read(inode_id, &inode);
        if (ret != MYFS_OK) {
            return ret;
        }

        inode.size = 0;
        inode.mtime = (uint64_t) time(NULL);
        inode.ctime = inode.mtime;

        return myfs_inode_write(inode_id, &inode);
    }

    /*
     * 释放 new_last_block 后面的所有逻辑块。
     */
    for (uint32_t logical = new_last_block + 1;
         logical <= old_last_block;
         logical++) {
        ret = myfs_inode_release_data_block(inode_id, logical);
        if (ret != MYFS_OK) {
            return ret;
        }
    }

    /*
     * 如果 new_size 落在一个块中间，可以把块内尾部清零。
     * 这不是必须的，但有助于避免后续调试时看到旧数据。
     */
    uint32_t tail_offset =
            (uint32_t) (new_size % MYFS_BLOCK_SIZE);

    if (tail_offset != 0) {
        myfs_block_t physical_block;
        int is_hole;

        ret = myfs_inode_bmap(
                inode_id,
                new_last_block,
                &physical_block,
                &is_hole
        );

        if (ret != MYFS_OK) {
            return ret;
        }

        if (!is_hole) {
            unsigned char block_buf[MYFS_BLOCK_SIZE];

            ret = myfs_cache_read_block(physical_block, block_buf);
            if (ret != MYFS_OK) {
                return ret;
            }

            memset(block_buf + tail_offset, 0,
                   MYFS_BLOCK_SIZE - tail_offset);

            ret = myfs_cache_write_block(physical_block, block_buf, 0);
            if (ret != MYFS_OK) {
                return ret;
            }
        }
    }

    ret = myfs_inode_read(inode_id, &inode);
    if (ret != MYFS_OK) {
        return ret;
    }

    inode.size = new_size;
    inode.mtime = (uint64_t) time(NULL);
    inode.ctime = inode.mtime;

    return myfs_inode_write(inode_id, &inode);
}


/* ============================================================
 * 5. 指定范围置零
 * ============================================================
 */

int myfs_inode_zero_range(
        myfs_ino_t inode_id,
        uint64_t offset,
        uint64_t length
) {
    if (length == 0) {
        return MYFS_OK;
    }

    /*
     * 简单实现：分块写入 0。
     */
    unsigned char zero_buf[MYFS_BLOCK_SIZE];

    memset(zero_buf, 0, sizeof(zero_buf));

    uint64_t done = 0;

    while (done < length) {
        uint32_t chunk =
                (uint32_t) min_u64(
                        MYFS_BLOCK_SIZE,
                        length - done
                );

        uint32_t written = 0;

        int ret = myfs_inode_write_data(
                inode_id,
                offset + done,
                zero_buf,
                chunk,
                &written
        );

        if (ret != MYFS_OK) {
            return ret;
        }

        if (written != chunk) {
            return MYFS_ERR_IO;
        }

        done += chunk;
    }

    return MYFS_OK;
}