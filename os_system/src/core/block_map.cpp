#include "block_map.h"
#include "block_alloc.h"
#include "inode.h"
#include "disk.h"
#include "config.h"
#include "error.h"

#include <string.h>

#include "journal.h"

/*
 * block_map.cpp
 * ------------------------------------------------------------
 * 第四阶段：文件逻辑块到物理块映射。
 *
 * 本模块属于底层物理部分。
 *
 * 它不处理：
 *     文件名
 *     路径
 *     目录项
 *     文件描述符
 *     Shell 命令
 *
 * 它只处理：
 *     inode_id
 *     logical_block
 *     physical_block
 *     direct / indirect 块指针
 */


/* ============================================================
 * 1. 通用辅助函数
 * ============================================================
 */

/*
 * 检查逻辑块号是否超过当前文件系统支持范围。
 */
static int check_logical_block(uint32_t logical_block) {
    if (logical_block >= MYFS_MAX_LOGICAL_BLOCKS) {
        return MYFS_ERR_INVALID_ARG;
    }

    return MYFS_OK;
}

/*
 * 读取一个间接块中的块号数组。
 *
 * 每个间接块本质上就是一个 myfs_block_t 数组。
 * 一个 4KB 块可以保存 1024 个 uint32_t 块号。
 */
static int read_pointer_block(
        myfs_block_t block_id,
        myfs_block_t pointers[MYFS_POINTERS_PER_BLOCK]
) {
    if (!myfs_block_is_valid_data_block(block_id)) {
        return MYFS_ERR_INVALID_BLOCK;
    }

    unsigned char buf[MYFS_BLOCK_SIZE];

    int ret = myfs_disk_read_block(block_id, buf);
    if (ret != MYFS_OK) {
        return ret;
    }

    memcpy(pointers, buf, MYFS_BLOCK_SIZE);

    return MYFS_OK;
}

/*
 * 写回一个间接块中的块号数组。
 */
static int write_pointer_block(
        myfs_block_t block_id,
        const myfs_block_t pointers[MYFS_POINTERS_PER_BLOCK]
) {
    if (!myfs_block_is_valid_data_block(block_id)) {
        return MYFS_ERR_INVALID_BLOCK;
    }

    unsigned char buf[MYFS_BLOCK_SIZE];

    memset(buf, 0, sizeof(buf));
    memcpy(buf, pointers, MYFS_BLOCK_SIZE);

    // return myfs_disk_write_block(block_id, buf);
    return myfs_journal_write_metadata_block(block_id, buf);
}

/*
 * 判断一个间接块是否全空。
 *
 * 如果一个间接块中的所有指针都是 0，
 * 说明它已经没有必要继续占用物理块，可以释放。
 */
static int pointer_block_is_empty(
        const myfs_block_t pointers[MYFS_POINTERS_PER_BLOCK]
) {
    for (uint32_t i = 0; i < MYFS_POINTERS_PER_BLOCK; i++) {
        if (pointers[i] != 0) {
            return 0;
        }
    }

    return 1;
}

/*
 * 分配一个新的间接块。
 *
 * 间接块本身也是数据区中的一个物理块。
 * 只不过它保存的不是文件内容，而是物理块号数组。
 */
static int alloc_pointer_block(myfs_block_t *block_id) {
    if (block_id == NULL) {
        return MYFS_ERR_INVALID_ARG;
    }

    int ret = myfs_block_alloc(block_id);
    if (ret != MYFS_OK) {
        return ret;
    }

    /*
     * myfs_block_alloc 内部已经会清零。
     * 这里再次清零是为了强调：间接块初始必须全 0。
     */
    ret = myfs_block_zero(*block_id);
    if (ret != MYFS_OK) {
        return ret;
    }

    return MYFS_OK;
}

/*
 * 增加 inode 的 block_count。
 *
 * block_count 表示该 inode 占用的物理块数量。
 * 包括：
 *     文件数据块
 *     一级间接块
 *     二级间接块
 */
static void inode_block_count_inc(myfs_inode_t *inode) {
    inode->block_count++;
}

/*
 * 减少 inode 的 block_count，避免下溢。
 */
static void inode_block_count_dec(myfs_inode_t *inode) {
    if (inode->block_count > 0) {
        inode->block_count--;
    }
}


/* ============================================================
 * 2. 查询最大逻辑块数
 * ============================================================
 */

uint32_t myfs_inode_max_logical_blocks(void) {
    return MYFS_MAX_LOGICAL_BLOCKS;
}


/* ============================================================
 * 3. direct 直接块映射
 * ============================================================
 */

static int get_direct_block(
        myfs_inode_t *inode,
        uint32_t logical_block,
        int create,
        myfs_block_t *physical_block,
        int *is_hole
) {
    myfs_block_t current = inode->direct[logical_block];

    /*
     * 该逻辑块已经有物理块。
     */
    if (current != 0) {
        *physical_block = current;
        *is_hole = 0;
        return MYFS_OK;
    }

    /*
     * 没有物理块，并且调用者只是查询。
     * 这说明该逻辑块是 hole。
     */
    if (!create) {
        *physical_block = 0;
        *is_hole = 1;
        return MYFS_OK;
    }

    /*
     * 需要创建物理块。
     */
    myfs_block_t new_block;

    int ret = myfs_block_alloc(&new_block);
    if (ret != MYFS_OK) {
        return ret;
    }

    inode->direct[logical_block] = new_block;
    inode_block_count_inc(inode);

    *physical_block = new_block;
    *is_hole = 0;

    return MYFS_OK;
}


/* ============================================================
 * 4. 一级间接块映射
 * ============================================================
 */

static int get_single_indirect_block(
        myfs_inode_t *inode,
        uint32_t logical_block,
        int create,
        myfs_block_t *physical_block,
        int *is_hole
) {
    /*
     * logical_block 进入该函数前仍然是整个文件内的逻辑块号。
     * 这里需要减去 direct 部分。
     */
    uint32_t index = logical_block - MYFS_DIRECT_BLOCKS;

    /*
     * 如果一级间接块还不存在。
     */
    if (inode->indirect1 == 0) {
        if (!create) {
            *physical_block = 0;
            *is_hole = 1;
            return MYFS_OK;
        }

        /*
         * 创建一级间接块。
         */
        myfs_block_t new_indirect;

        int ret = alloc_pointer_block(&new_indirect);
        if (ret != MYFS_OK) {
            return ret;
        }

        inode->indirect1 = new_indirect;
        inode_block_count_inc(inode);
    }

    myfs_block_t pointers[MYFS_POINTERS_PER_BLOCK];

    int ret = read_pointer_block(inode->indirect1, pointers);
    if (ret != MYFS_OK) {
        return ret;
    }

    /*
     * 该逻辑块已经有物理块。
     */
    if (pointers[index] != 0) {
        *physical_block = pointers[index];
        *is_hole = 0;
        return MYFS_OK;
    }

    /*
     * 没有物理块，并且只是查询。
     */
    if (!create) {
        *physical_block = 0;
        *is_hole = 1;
        return MYFS_OK;
    }

    /*
     * 创建文件数据块。
     */
    myfs_block_t new_data_block;

    ret = myfs_block_alloc(&new_data_block);
    if (ret != MYFS_OK) {
        return ret;
    }

    pointers[index] = new_data_block;
    inode_block_count_inc(inode);

    ret = write_pointer_block(inode->indirect1, pointers);
    if (ret != MYFS_OK) {
        return ret;
    }

    *physical_block = new_data_block;
    *is_hole = 0;

    return MYFS_OK;
}


/* ============================================================
 * 5. 二级间接块映射
 * ============================================================
 */

static int get_double_indirect_block(
        myfs_inode_t *inode,
        uint32_t logical_block,
        int create,
        myfs_block_t *physical_block,
        int *is_hole
) {
    /*
     * 减去 direct 和一级间接能覆盖的逻辑块。
     */
    uint32_t remain =
            logical_block - MYFS_DIRECT_BLOCKS - MYFS_POINTERS_PER_BLOCK;

    uint32_t first_index = remain / MYFS_POINTERS_PER_BLOCK;
    uint32_t second_index = remain % MYFS_POINTERS_PER_BLOCK;

    /*
     * 二级间接块还不存在。
     */
    if (inode->indirect2 == 0) {
        if (!create) {
            *physical_block = 0;
            *is_hole = 1;
            return MYFS_OK;
        }

        myfs_block_t new_double;

        int ret = alloc_pointer_block(&new_double);
        if (ret != MYFS_OK) {
            return ret;
        }

        inode->indirect2 = new_double;
        inode_block_count_inc(inode);
    }

    /*
     * 读取二级间接块。
     * 它里面保存的是一批一级间接块的块号。
     */
    myfs_block_t level1_blocks[MYFS_POINTERS_PER_BLOCK];

    int ret = read_pointer_block(inode->indirect2, level1_blocks);
    if (ret != MYFS_OK) {
        return ret;
    }

    /*
     * 对应的一级间接块不存在。
     */
    if (level1_blocks[first_index] == 0) {
        if (!create) {
            *physical_block = 0;
            *is_hole = 1;
            return MYFS_OK;
        }

        myfs_block_t new_level1;

        ret = alloc_pointer_block(&new_level1);
        if (ret != MYFS_OK) {
            return ret;
        }

        level1_blocks[first_index] = new_level1;
        inode_block_count_inc(inode);

        ret = write_pointer_block(inode->indirect2, level1_blocks);
        if (ret != MYFS_OK) {
            return ret;
        }
    }

    /*
     * 读取对应的一级间接块。
     * 它里面保存的才是最终数据块号。
     */
    myfs_block_t data_blocks[MYFS_POINTERS_PER_BLOCK];

    ret = read_pointer_block(level1_blocks[first_index], data_blocks);
    if (ret != MYFS_OK) {
        return ret;
    }

    if (data_blocks[second_index] != 0) {
        *physical_block = data_blocks[second_index];
        *is_hole = 0;
        return MYFS_OK;
    }

    if (!create) {
        *physical_block = 0;
        *is_hole = 1;
        return MYFS_OK;
    }

    myfs_block_t new_data_block;

    ret = myfs_block_alloc(&new_data_block);
    if (ret != MYFS_OK) {
        return ret;
    }

    data_blocks[second_index] = new_data_block;
    inode_block_count_inc(inode);

    ret = write_pointer_block(level1_blocks[first_index], data_blocks);
    if (ret != MYFS_OK) {
        return ret;
    }

    *physical_block = new_data_block;
    *is_hole = 0;

    return MYFS_OK;
}


/* ============================================================
 * 6. 对外：获取逻辑块对应的物理块
 * ============================================================
 */

int myfs_inode_get_data_block(
        myfs_ino_t inode_id,
        uint32_t logical_block,
        int create,
        myfs_block_t *physical_block,
        int *is_hole
) {
    if (physical_block == NULL || is_hole == NULL) {
        return MYFS_ERR_INVALID_ARG;
    }

    int ret = check_logical_block(logical_block);
    if (ret != MYFS_OK) {
        return ret;
    }

    myfs_inode_t inode;

    ret = myfs_inode_read(inode_id, &inode);
    if (ret != MYFS_OK) {
        return ret;
    }

    /*
     * 目前只有普通文件、目录、符号链接可以拥有数据块。
     * 但这个模块不关心它是路径意义上的什么，只要不是 FREE 即可。
     */
    if (inode.type == MYFS_INODE_FREE) {
        return MYFS_ERR_INVALID_INODE;
    }

    if (logical_block < MYFS_DIRECT_BLOCKS) {
        ret = get_direct_block(
                &inode,
                logical_block,
                create,
                physical_block,
                is_hole
        );
    } else if (logical_block <
               MYFS_DIRECT_BLOCKS + MYFS_POINTERS_PER_BLOCK) {
        ret = get_single_indirect_block(
                &inode,
                logical_block,
                create,
                physical_block,
                is_hole
        );
    } else {
        ret = get_double_indirect_block(
                &inode,
                logical_block,
                create,
                physical_block,
                is_hole
        );
    }

    if (ret != MYFS_OK) {
        return ret;
    }

    /*
     * 如果 create = 1，inode 的 direct/indirect 指针和 block_count
     * 可能已经发生变化，需要写回 inode 表。
     *
     * 如果 create = 0，只是查询，写回也不会影响正确性，
     * 但没有必要。
     */
    if (create) {
        ret = myfs_inode_write(inode_id, &inode);
        if (ret != MYFS_OK) {
            return ret;
        }
    }

    return MYFS_OK;
}

int myfs_inode_bmap(
        myfs_ino_t inode_id,
        uint32_t logical_block,
        myfs_block_t *physical_block,
        int *is_hole
) {
    return myfs_inode_get_data_block(
            inode_id,
            logical_block,
            0,
            physical_block,
            is_hole
    );
}


/* ============================================================
 * 7. 释放某一个逻辑块
 * ============================================================
 */

int myfs_inode_release_data_block(
        myfs_ino_t inode_id,
        uint32_t logical_block
) {
    int ret = check_logical_block(logical_block);
    if (ret != MYFS_OK) {
        return ret;
    }

    myfs_inode_t inode;

    ret = myfs_inode_read(inode_id, &inode);
    if (ret != MYFS_OK) {
        return ret;
    }

    /*
     * direct 区域释放。
     */
    if (logical_block < MYFS_DIRECT_BLOCKS) {
        myfs_block_t old = inode.direct[logical_block];

        if (old == 0) {
            return MYFS_OK;
        }

        ret = myfs_block_free(old);
        if (ret != MYFS_OK) {
            return ret;
        }

        inode.direct[logical_block] = 0;
        inode_block_count_dec(&inode);

        return myfs_inode_write(inode_id, &inode);
    }

    /*
     * 一级间接释放。
     */
    if (logical_block < MYFS_DIRECT_BLOCKS + MYFS_POINTERS_PER_BLOCK) {
        if (inode.indirect1 == 0) {
            return MYFS_OK;
        }

        uint32_t index = logical_block - MYFS_DIRECT_BLOCKS;

        myfs_block_t pointers[MYFS_POINTERS_PER_BLOCK];

        ret = read_pointer_block(inode.indirect1, pointers);
        if (ret != MYFS_OK) {
            return ret;
        }

        if (pointers[index] == 0) {
            return MYFS_OK;
        }

        ret = myfs_block_free(pointers[index]);
        if (ret != MYFS_OK) {
            return ret;
        }

        pointers[index] = 0;
        inode_block_count_dec(&inode);

        /*
         * 如果一级间接块已经全空，也释放一级间接块本身。
         */
        if (pointer_block_is_empty(pointers)) {
            ret = myfs_block_free(inode.indirect1);
            if (ret != MYFS_OK) {
                return ret;
            }

            inode.indirect1 = 0;
            inode_block_count_dec(&inode);
        } else {
            ret = write_pointer_block(inode.indirect1, pointers);
            if (ret != MYFS_OK) {
                return ret;
            }
        }

        return myfs_inode_write(inode_id, &inode);
    }

    /*
     * 二级间接释放。
     */
    if (inode.indirect2 == 0) {
        return MYFS_OK;
    }

    uint32_t remain =
            logical_block - MYFS_DIRECT_BLOCKS - MYFS_POINTERS_PER_BLOCK;

    uint32_t first_index = remain / MYFS_POINTERS_PER_BLOCK;
    uint32_t second_index = remain % MYFS_POINTERS_PER_BLOCK;

    myfs_block_t level1_blocks[MYFS_POINTERS_PER_BLOCK];

    ret = read_pointer_block(inode.indirect2, level1_blocks);
    if (ret != MYFS_OK) {
        return ret;
    }

    if (level1_blocks[first_index] == 0) {
        return MYFS_OK;
    }

    myfs_block_t data_blocks[MYFS_POINTERS_PER_BLOCK];

    ret = read_pointer_block(level1_blocks[first_index], data_blocks);
    if (ret != MYFS_OK) {
        return ret;
    }

    if (data_blocks[second_index] == 0) {
        return MYFS_OK;
    }

    ret = myfs_block_free(data_blocks[second_index]);
    if (ret != MYFS_OK) {
        return ret;
    }

    data_blocks[second_index] = 0;
    inode_block_count_dec(&inode);

    /*
     * 如果该一级间接块已经空了，释放它。
     */
    if (pointer_block_is_empty(data_blocks)) {
        ret = myfs_block_free(level1_blocks[first_index]);
        if (ret != MYFS_OK) {
            return ret;
        }

        level1_blocks[first_index] = 0;
        inode_block_count_dec(&inode);
    } else {
        ret = write_pointer_block(level1_blocks[first_index], data_blocks);
        if (ret != MYFS_OK) {
            return ret;
        }
    }

    /*
     * 如果二级间接块也已经全空，释放它。
     */
    if (pointer_block_is_empty(level1_blocks)) {
        ret = myfs_block_free(inode.indirect2);
        if (ret != MYFS_OK) {
            return ret;
        }

        inode.indirect2 = 0;
        inode_block_count_dec(&inode);
    } else {
        ret = write_pointer_block(inode.indirect2, level1_blocks);
        if (ret != MYFS_OK) {
            return ret;
        }
    }

    return myfs_inode_write(inode_id, &inode);
}


/* ============================================================
 * 8. 释放 inode 占用的全部块
 * ============================================================
 */

int myfs_inode_release_all_blocks(myfs_ino_t inode_id) {
    myfs_inode_t inode;

    int ret = myfs_inode_read(inode_id, &inode);
    if (ret != MYFS_OK) {
        return ret;
    }

    /*
     * 释放 direct 块。
     */
    for (uint32_t i = 0; i < MYFS_DIRECT_BLOCKS; i++) {
        if (inode.direct[i] != 0) {
            ret = myfs_block_free(inode.direct[i]);
            if (ret != MYFS_OK) {
                return ret;
            }

            inode.direct[i] = 0;
        }
    }

    /*
     * 释放一级间接块管理的数据块和一级间接块本身。
     */
    if (inode.indirect1 != 0) {
        myfs_block_t pointers[MYFS_POINTERS_PER_BLOCK];

        ret = read_pointer_block(inode.indirect1, pointers);
        if (ret != MYFS_OK) {
            return ret;
        }

        for (uint32_t i = 0; i < MYFS_POINTERS_PER_BLOCK; i++) {
            if (pointers[i] != 0) {
                ret = myfs_block_free(pointers[i]);
                if (ret != MYFS_OK) {
                    return ret;
                }
            }
        }

        ret = myfs_block_free(inode.indirect1);
        if (ret != MYFS_OK) {
            return ret;
        }

        inode.indirect1 = 0;
    }

    /*
     * 释放二级间接块。
     */
    if (inode.indirect2 != 0) {
        myfs_block_t level1_blocks[MYFS_POINTERS_PER_BLOCK];

        ret = read_pointer_block(inode.indirect2, level1_blocks);
        if (ret != MYFS_OK) {
            return ret;
        }

        for (uint32_t i = 0; i < MYFS_POINTERS_PER_BLOCK; i++) {
            if (level1_blocks[i] == 0) {
                continue;
            }

            myfs_block_t data_blocks[MYFS_POINTERS_PER_BLOCK];

            ret = read_pointer_block(level1_blocks[i], data_blocks);
            if (ret != MYFS_OK) {
                return ret;
            }

            for (uint32_t j = 0; j < MYFS_POINTERS_PER_BLOCK; j++) {
                if (data_blocks[j] != 0) {
                    ret = myfs_block_free(data_blocks[j]);
                    if (ret != MYFS_OK) {
                        return ret;
                    }
                }
            }

            ret = myfs_block_free(level1_blocks[i]);
            if (ret != MYFS_OK) {
                return ret;
            }
        }

        ret = myfs_block_free(inode.indirect2);
        if (ret != MYFS_OK) {
            return ret;
        }

        inode.indirect2 = 0;
    }

    memset(inode.direct, 0, sizeof(inode.direct));
    inode.block_count = 0;
    inode.size = 0;

    return myfs_inode_write(inode_id, &inode);
}