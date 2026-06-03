#ifndef MYFS_FSCK_H
#define MYFS_FSCK_H

/*
 * fsck.h
 * ------------------------------------------------------------
 * MYFS 物理一致性检查模块。
 *
 * 本模块只检查文件系统底层物理结构，不检查路径、目录项语义、
 * 文件名、文件描述符和 Shell 命令。
 *
 * 当前检查内容：
 *
 * 1. 超级块基本字段。
 * 2. inode 位图与 inode 表是否一致。
 * 3. free_inodes_count 是否正确。
 * 4. inode direct 块引用是否合法。
 * 5. 一级间接块是否合法。
 * 6. 二级间接块是否合法。
 * 7. 数据块是否被多个 inode 重复引用。
 * 8. 成组链接法空闲块链是否合法。
 * 9. 空闲块是否重复。
 * 10. 数据块是否同时出现在“已使用集合”和“空闲集合”中。
 * 11. 数据区是否存在泄漏块。
 *
 */

#include <stdint.h>

typedef struct myfs_fsck_options {
    /*
     * 当前阶段暂不实现修复。
     * 先保留字段，后续可以扩展 --repair。
     */
    int repair;

    /*
     * 是否输出详细信息。
     */
    int verbose;
} myfs_fsck_options_t;


typedef struct myfs_fsck_result {
    /*
     * 总错误数。
     */
    uint32_t errors_found;

    /*
     * 当前阶段暂不修复，所以一般为 0。
     */
    uint32_t errors_fixed;

    /*
     * inode 位图和 inode 表不一致数量。
     */
    uint32_t inode_state_errors;

    /*
     * free_inodes_count 与实际统计不一致。
     */
    uint32_t free_inode_count_errors;

    /*
     * 非法数据块引用数量。
     */
    uint32_t invalid_block_refs;

    /*
     * 已使用数据块重复引用数量。
     */
    uint32_t duplicated_used_blocks;

    /*
     * 空闲链中重复块数量。
     */
    uint32_t duplicated_free_blocks;

    /*
     * 数据块同时出现在已使用集合和空闲集合中的数量。
     */
    uint32_t used_free_conflicts;

    /*
     * 数据区中既未被 inode 引用，也不在空闲链中的块数量。
     */
    uint32_t leaked_blocks;

    /*
     * 成组链接法空闲链结构错误数量。
     */
    uint32_t free_chain_errors;

    /*
     * free_blocks_count 与实际空闲链统计不一致。
     */
    uint32_t free_block_count_errors;
} myfs_fsck_result_t;


/*
 * 执行文件系统物理一致性检查。
 *
 * 要求：
 * 文件系统必须已经 mount。
 *
 * 返回：
 * MYFS_OK 表示检查过程执行成功。
 *
 * 注意：
 * MYFS_OK 不代表没有错误。
 * 是否有错误应查看 result->errors_found。
 */
int myfs_fsck_run(
        const myfs_fsck_options_t *options,
        myfs_fsck_result_t *result
);

/*
 * 打印 fsck 检查结果。
 */
void myfs_fsck_print_result(
        const myfs_fsck_result_t *result
);

#endif