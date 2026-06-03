#include "fsck.h"
#include "superblock.h"
#include "inode.h"
#include "block_alloc.h"
#include "block_map.h"
#include "disk.h"
#include "config.h"
#include "error.h"

#include <unordered_set>
#include <unordered_map>
#include <vector>
#include <cstring>
#include <cstdio>

/*
 * fsck.cpp
 * ------------------------------------------------------------
 * MYFS 第七阶段：物理一致性检查。
 *
 * 本模块只负责底层结构检查：
 *
 * 1. inode 位图
 * 2. inode 表
 * 3. direct / indirect 块引用
 * 4. 成组链接法空闲块链
 * 5. 空闲块与已使用块是否冲突
 * 6. 数据区是否存在泄漏块
 *
 * 本模块不负责：
 *
 * 1. 路径解析
 * 2. 文件名
 * 3. 目录项语义
 * 4. 文件描述符
 * 5. Shell
 */


typedef struct fsck_context {
    const myfs_fsck_options_t *options;
    myfs_fsck_result_t *result;

    /*
     * inode 引用到的数据区块。
     *
     * 包括：
     * 1. 文件数据块
     * 2. 一级间接块本身
     * 3. 二级间接块本身
     * 4. 二级间接指向的一级间接块
     */
    std::unordered_set<myfs_block_t> used_blocks;

    /*
     * 统计每个 used block 被引用几次，用于发现重复引用。
     */
    std::unordered_map<myfs_block_t, uint32_t> used_ref_count;

    /*
     * 成组链接法空闲链中的块。
     */
    std::unordered_set<myfs_block_t> free_blocks;

    /*
     * 统计空闲块出现次数，用于发现空闲链重复。
     */
    std::unordered_map<myfs_block_t, uint32_t> free_ref_count;

} fsck_context_t;


static int verbose_enabled(const fsck_context_t *ctx) {
    return ctx->options != nullptr && ctx->options->verbose;
}


static void fsck_error(fsck_context_t *ctx) {
    ctx->result->errors_found++;
}


static void verbose_print(fsck_context_t *ctx, const char *msg) {
    if (verbose_enabled(ctx)) {
        printf("[fsck] %s\n", msg);
    }
}


static int is_valid_data_block(myfs_block_t block_id) {
    return myfs_block_is_valid_data_block(block_id);
}


/*
 * 记录一个 inode 引用到的数据区块。
 *
 * 如果块号非法，记录 invalid_block_refs。
 * 如果块号重复被引用，记录 duplicated_used_blocks。
 */
static void add_used_block(
        fsck_context_t *ctx,
        myfs_block_t block_id,
        const char *reason
) {
    if (block_id == 0) {
        return;
    }

    if (!is_valid_data_block(block_id)) {
        ctx->result->invalid_block_refs++;
        fsck_error(ctx);

        if (verbose_enabled(ctx)) {
            printf("[fsck] invalid used block %u from %s\n", block_id, reason);
        }

        return;
    }

    ctx->used_ref_count[block_id]++;

    if (ctx->used_ref_count[block_id] > 1) {
        ctx->result->duplicated_used_blocks++;
        fsck_error(ctx);

        if (verbose_enabled(ctx)) {
            printf("[fsck] duplicated used block %u from %s\n", block_id, reason);
        }
    }

    ctx->used_blocks.insert(block_id);
}


/*
 * 读取一个指针块。
 *
 * 指针块就是保存 myfs_block_t 数组的数据块。
 */
static int read_pointer_block(
        myfs_block_t block_id,
        myfs_block_t pointers[MYFS_POINTERS_PER_BLOCK]
) {
    if (!is_valid_data_block(block_id)) {
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
 * 扫描 inode 的 direct 块。
 */
static int scan_direct_blocks(
        fsck_context_t *ctx,
        const myfs_inode_t *inode
) {
    for (uint32_t i = 0; i < MYFS_DIRECT_BLOCKS; i++) {
        if (inode->direct[i] != 0) {
            add_used_block(ctx, inode->direct[i], "direct");
        }
    }

    return MYFS_OK;
}


/*
 * 扫描一级间接块。
 *
 * 注意：
 * inode->indirect1 本身也是一个被使用的数据区块。
 */
static int scan_single_indirect(
        fsck_context_t *ctx,
        const myfs_inode_t *inode
) {
    if (inode->indirect1 == 0) {
        return MYFS_OK;
    }

    add_used_block(ctx, inode->indirect1, "indirect1 block itself");

    if (!is_valid_data_block(inode->indirect1)) {
        return MYFS_OK;
    }

    myfs_block_t pointers[MYFS_POINTERS_PER_BLOCK];

    int ret = read_pointer_block(inode->indirect1, pointers);
    if (ret != MYFS_OK) {
        ctx->result->invalid_block_refs++;
        fsck_error(ctx);
        return MYFS_OK;
    }

    for (uint32_t i = 0; i < MYFS_POINTERS_PER_BLOCK; i++) {
        if (pointers[i] != 0) {
            add_used_block(ctx, pointers[i], "single indirect data");
        }
    }

    return MYFS_OK;
}


/*
 * 扫描二级间接块。
 *
 * inode->indirect2 本身是一个数据区块；
 * 它里面指向多个一级间接块；
 * 每个一级间接块再指向真正的数据块。
 */
static int scan_double_indirect(
        fsck_context_t *ctx,
        const myfs_inode_t *inode
) {
    if (inode->indirect2 == 0) {
        return MYFS_OK;
    }

    add_used_block(ctx, inode->indirect2, "indirect2 block itself");

    if (!is_valid_data_block(inode->indirect2)) {
        return MYFS_OK;
    }

    myfs_block_t level1_blocks[MYFS_POINTERS_PER_BLOCK];

    int ret = read_pointer_block(inode->indirect2, level1_blocks);
    if (ret != MYFS_OK) {
        ctx->result->invalid_block_refs++;
        fsck_error(ctx);
        return MYFS_OK;
    }

    for (uint32_t i = 0; i < MYFS_POINTERS_PER_BLOCK; i++) {
        if (level1_blocks[i] == 0) {
            continue;
        }

        add_used_block(ctx, level1_blocks[i], "double indirect level1 block");

        if (!is_valid_data_block(level1_blocks[i])) {
            continue;
        }

        myfs_block_t data_blocks[MYFS_POINTERS_PER_BLOCK];

        ret = read_pointer_block(level1_blocks[i], data_blocks);
        if (ret != MYFS_OK) {
            ctx->result->invalid_block_refs++;
            fsck_error(ctx);
            continue;
        }

        for (uint32_t j = 0; j < MYFS_POINTERS_PER_BLOCK; j++) {
            if (data_blocks[j] != 0) {
                add_used_block(ctx, data_blocks[j], "double indirect data");
            }
        }
    }

    return MYFS_OK;
}


/*
 * 检查 inode 位图和 inode 表。
 *
 * 规则：
 * 1. 位图为 1，则 inode 表中 type 不应为 FREE。
 * 2. 位图为 0，则 inode 表中 type 应为 FREE 或 0。
 * 3. 统计实际空闲 inode 数，与超级块 free_inodes_count 对比。
 * 4. 对被占用 inode 扫描 direct / indirect 块。
 */
static int check_inodes(fsck_context_t *ctx) {
    verbose_print(ctx, "checking inode bitmap and inode table");

    myfs_superblock_t *sb = myfs_super_get();
    if (sb == nullptr) {
        return MYFS_ERR_NOT_MOUNTED;
    }

    uint32_t actual_free_inodes = 0;

    for (myfs_ino_t ino = 0; ino < sb->total_inodes; ino++) {
        int used = myfs_inode_bitmap_test(ino);

        if (used < 0) {
            ctx->result->inode_state_errors++;
            fsck_error(ctx);
            continue;
        }

        myfs_inode_t inode;
        int ret = myfs_inode_table_read(ino, &inode);

        if (ret != MYFS_OK) {
            ctx->result->inode_state_errors++;
            fsck_error(ctx);
            continue;
        }

        if (used == 0) {
            actual_free_inodes++;

            if (inode.type != MYFS_INODE_FREE && inode.type != 0) {
                ctx->result->inode_state_errors++;
                fsck_error(ctx);

                if (verbose_enabled(ctx)) {
                    printf("[fsck] inode %u bitmap free but inode type = %u\n",
                           ino, inode.type);
                }
            }

            continue;
        }

        /*
         * used == 1
         */
        if (inode.type == MYFS_INODE_FREE || inode.type == 0) {
            ctx->result->inode_state_errors++;
            fsck_error(ctx);

            if (verbose_enabled(ctx)) {
                printf("[fsck] inode %u bitmap used but inode type is FREE\n", ino);
            }

            continue;
        }

        /*
         * 检查 inode_id 是否和槽位一致。
         */
        if (inode.inode_id != ino) {
            ctx->result->inode_state_errors++;
            fsck_error(ctx);

            if (verbose_enabled(ctx)) {
                printf("[fsck] inode %u has wrong inode_id %u\n",
                       ino, inode.inode_id);
            }
        }

        /*
         * 扫描该 inode 持有的数据块。
         */
        scan_direct_blocks(ctx, &inode);
        scan_single_indirect(ctx, &inode);
        scan_double_indirect(ctx, &inode);
    }

    if (actual_free_inodes != sb->free_inodes_count) {
        ctx->result->free_inode_count_errors++;
        fsck_error(ctx);

        if (verbose_enabled(ctx)) {
            printf("[fsck] free_inodes_count mismatch: super=%u actual=%u\n",
                   sb->free_inodes_count, actual_free_inodes);
        }
    }

    return MYFS_OK;
}


/*
 * 将一个空闲块记录到 free_blocks 集合中。
 */
static void add_free_block(
        fsck_context_t *ctx,
        myfs_block_t block_id
) {
    if (!is_valid_data_block(block_id)) {
        ctx->result->free_chain_errors++;
        fsck_error(ctx);

        if (verbose_enabled(ctx)) {
            printf("[fsck] invalid free block %u\n", block_id);
        }

        return;
    }

    ctx->free_ref_count[block_id]++;

    if (ctx->free_ref_count[block_id] > 1) {
        ctx->result->duplicated_free_blocks++;
        fsck_error(ctx);

        if (verbose_enabled(ctx)) {
            printf("[fsck] duplicated free block %u\n", block_id);
        }
    }

    ctx->free_blocks.insert(block_id);
}


/*
 * 检查成组链接法空闲块链。
 *
 * 成组链接法结构：
 *
 * 1. 超级块中保存当前 free_group。
 * 2. 如果后面还有更多空闲组，则当前组的第 0 个块通常是下一组管理块。
 * 3. 该管理块中保存下一组空闲块号。
 *
 * 这里按照 free_blocks_count 进行有限遍历，避免死循环。
 */
static int check_free_group_chain(fsck_context_t *ctx) {
    verbose_print(ctx, "checking grouped free block chain");

    myfs_superblock_t *sb = myfs_super_get();
    if (sb == nullptr) {
        return MYFS_ERR_NOT_MOUNTED;
    }

    if (sb->free_group_count > MYFS_FREE_GROUP_SIZE) {
        ctx->result->free_chain_errors++;
        fsck_error(ctx);
        return MYFS_OK;
    }

    if (sb->free_blocks_count == 0) {
        if (sb->free_group_count != 0) {
            ctx->result->free_chain_errors++;
            fsck_error(ctx);
        }

        return MYFS_OK;
    }

    if (sb->free_group_count == 0) {
        ctx->result->free_chain_errors++;
        fsck_error(ctx);
        return MYFS_OK;
    }

    std::vector<myfs_block_t> current_group;

    for (uint32_t i = 0; i < sb->free_group_count; i++) {
        current_group.push_back(sb->free_group[i]);
    }

    uint32_t counted = 0;

    /*
     * 防止空闲组管理块形成环。
     */
    std::unordered_set<myfs_block_t> visited_group_blocks;

    while (!current_group.empty() && counted < sb->free_blocks_count) {
        uint32_t group_count = (uint32_t) current_group.size();

        if (group_count > MYFS_FREE_GROUP_SIZE) {
            ctx->result->free_chain_errors++;
            fsck_error(ctx);
            break;
        }

        for (uint32_t i = 0; i < group_count; i++) {
            if (counted >= sb->free_blocks_count) {
                break;
            }

            add_free_block(ctx, current_group[i]);
            counted++;
        }

        if (counted >= sb->free_blocks_count) {
            break;
        }

        /*
         * 如果还有剩余空闲块，那么当前组的第 0 个块应当是下一组管理块。
         */
        myfs_block_t next_group_block = current_group[0];

        if (!is_valid_data_block(next_group_block)) {
            ctx->result->free_chain_errors++;
            fsck_error(ctx);
            break;
        }

        if (visited_group_blocks.count(next_group_block)) {
            ctx->result->free_chain_errors++;
            fsck_error(ctx);

            if (verbose_enabled(ctx)) {
                printf("[fsck] free group chain loop at block %u\n",
                       next_group_block);
            }

            break;
        }

        visited_group_blocks.insert(next_group_block);

        myfs_free_group_block_t group_block;
        unsigned char buf[MYFS_BLOCK_SIZE];

        int ret = myfs_disk_read_block(next_group_block, buf);
        if (ret != MYFS_OK) {
            ctx->result->free_chain_errors++;
            fsck_error(ctx);
            break;
        }

        memcpy(&group_block, buf, sizeof(group_block));

        if (group_block.count == 0 ||
            group_block.count > MYFS_FREE_GROUP_SIZE) {
            ctx->result->free_chain_errors++;
            fsck_error(ctx);

            if (verbose_enabled(ctx)) {
                printf("[fsck] invalid free group count %u at block %u\n",
                       group_block.count, next_group_block);
            }

            break;
        }

        current_group.clear();

        for (uint32_t i = 0; i < group_block.count; i++) {
            current_group.push_back(group_block.blocks[i]);
        }
    }

    if (counted != sb->free_blocks_count) {
        ctx->result->free_block_count_errors++;
        fsck_error(ctx);

        if (verbose_enabled(ctx)) {
            printf("[fsck] free block count mismatch: super=%u traversed=%u unique=%zu\n",
                   sb->free_blocks_count,
                   counted,
                   ctx->free_blocks.size());
        }
    }

    if (ctx->free_blocks.size() != sb->free_blocks_count) {
        ctx->result->free_block_count_errors++;
        fsck_error(ctx);

        if (verbose_enabled(ctx)) {
            printf("[fsck] free block unique count mismatch: super=%u unique=%zu\n",
                   sb->free_blocks_count,
                   ctx->free_blocks.size());
        }
    }

    return MYFS_OK;
}


/*
 * 检查 used_blocks 和 free_blocks 是否冲突。
 */
static int check_used_free_conflict(fsck_context_t *ctx) {
    verbose_print(ctx, "checking used/free block conflicts");

    for (myfs_block_t block : ctx->used_blocks) {
        if (ctx->free_blocks.count(block)) {
            ctx->result->used_free_conflicts++;
            fsck_error(ctx);

            if (verbose_enabled(ctx)) {
                printf("[fsck] block %u is both used and free\n", block);
            }
        }
    }

    return MYFS_OK;
}


/*
 * 检查数据区泄漏块。
 *
 * 数据区中每个块应该满足：
 *
 * 1. 要么被 inode 引用；
 * 2. 要么出现在成组链接空闲链中。
 *
 * 如果都不满足，说明该块泄漏。
 */
static int check_leaked_blocks(fsck_context_t *ctx) {
    verbose_print(ctx, "checking leaked blocks");

    myfs_superblock_t *sb = myfs_super_get();
    if (sb == nullptr) {
        return MYFS_ERR_NOT_MOUNTED;
    }

    for (myfs_block_t block = sb->data_block_start;
         block < sb->total_blocks;
         block++) {
        int used = ctx->used_blocks.count(block) != 0;
        int free = ctx->free_blocks.count(block) != 0;

        if (!used && !free) {
            ctx->result->leaked_blocks++;
            fsck_error(ctx);

            if (verbose_enabled(ctx)) {
                printf("[fsck] leaked block %u\n", block);
            }
        }
    }

    return MYFS_OK;
}


int myfs_fsck_run(
        const myfs_fsck_options_t *options,
        myfs_fsck_result_t *result
) {
    if (result == nullptr) {
        return MYFS_ERR_INVALID_ARG;
    }

    memset(result, 0, sizeof(myfs_fsck_result_t));

    myfs_superblock_t *sb = myfs_super_get();
    if (sb == nullptr) {
        return MYFS_ERR_NOT_MOUNTED;
    }

    /*
     * 当前阶段不支持自动修复。
     */
    if (options != nullptr && options->repair) {
        return MYFS_ERR_UNSUPPORTED;
    }

    fsck_context_t ctx;
    ctx.options = options;
    ctx.result = result;

    int ret;

    ret = myfs_super_check();
    if (ret != MYFS_OK) {
        result->errors_found++;
        return MYFS_OK;
    }

    ret = check_inodes(&ctx);
    if (ret != MYFS_OK) {
        return ret;
    }

    ret = check_free_group_chain(&ctx);
    if (ret != MYFS_OK) {
        return ret;
    }

    ret = check_used_free_conflict(&ctx);
    if (ret != MYFS_OK) {
        return ret;
    }

    ret = check_leaked_blocks(&ctx);
    if (ret != MYFS_OK) {
        return ret;
    }

    return MYFS_OK;
}


void myfs_fsck_print_result(
        const myfs_fsck_result_t *result
) {
    if (result == nullptr) {
        return;
    }

    printf("\n========== MYFS FSCK RESULT ==========\n");
    printf("errors_found              = %u\n", result->errors_found);
    printf("errors_fixed              = %u\n", result->errors_fixed);
    printf("inode_state_errors        = %u\n", result->inode_state_errors);
    printf("free_inode_count_errors   = %u\n", result->free_inode_count_errors);
    printf("invalid_block_refs        = %u\n", result->invalid_block_refs);
    printf("duplicated_used_blocks    = %u\n", result->duplicated_used_blocks);
    printf("duplicated_free_blocks    = %u\n", result->duplicated_free_blocks);
    printf("used_free_conflicts       = %u\n", result->used_free_conflicts);
    printf("leaked_blocks             = %u\n", result->leaked_blocks);
    printf("free_chain_errors         = %u\n", result->free_chain_errors);
    printf("free_block_count_errors   = %u\n", result->free_block_count_errors);
    printf("======================================\n\n");
}