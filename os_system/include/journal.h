#ifndef MYFS_JOURNAL_H
#define MYFS_JOURNAL_H

/*
 * journal.h
 * ------------------------------------------------------------
 * MYFS 元数据日志模块。
 *
 * 本模块属于底层物理层，只负责保护磁盘上的元数据块。
 *
 * 当前实现的是简化版 write-ahead logging：
 *
 * 1. 修改元数据块前，先把目标块号和新内容写入 journal 区。
 * 2. 写入 journal header，标记事务 committed。
 * 3. 再把新内容写回真正的目标块。
 * 4. 全部写完后清空 journal header。
 *
 * 如果程序在第 2 步之后、第 4 步之前崩溃，
 * 下次 mount 时 journal_recover 会根据 journal 区重放事务。
 */

#include <stdint.h>
#include "type.h"
#include "config.h"

#define MYFS_JOURNAL_MAGIC 0x4A4E4C31u   /* "JNL1" */
#define MYFS_JOURNAL_CLEAN 0u
#define MYFS_JOURNAL_COMMITTED 1u

/*
 * 一个 journal header 最多记录多少个元数据块。
 *
 * header 保存在 journal_start 这一块中。
 * journal 数据块从 journal_start + 1 开始。
 */
#define MYFS_JOURNAL_MAX_ENTRIES 128u

typedef struct myfs_journal_entry {
    myfs_block_t target_block;     /* 最终要写入的真实块号 */
    myfs_block_t journal_block;    /* journal 区中保存副本的块号 */
    uint32_t flags;
} myfs_journal_entry_t;

typedef struct myfs_journal_header {
    uint32_t magic;
    uint32_t state;
    uint32_t txid;
    uint32_t entry_count;
    uint32_t checksum;

    myfs_journal_entry_t entries[MYFS_JOURNAL_MAX_ENTRIES];
} myfs_journal_header_t;


/*
 * 格式化 journal 区。
 *
 * mkfs 时调用，写入 clean header。
 */
int myfs_journal_format(void);

/*
 * 启动一次事务。
 */
int myfs_journal_begin(void);

/*
 * 将一个元数据块加入当前事务。
 *
 * 参数：
 * target_block：最终要写入的真实块号
 * buf：该块的新内容，大小必须为 4096 字节
 */
int myfs_journal_log_block(myfs_block_t target_block, const void *buf);

/*
 * 提交当前事务。
 */
int myfs_journal_commit(void);

/*
 * 放弃当前事务。
 */
int myfs_journal_abort(void);

/*
 * 崩溃恢复。
 *
 * mount 时调用。
 * 如果发现 journal header 为 COMMITTED，则重放 journal。
 */
int myfs_journal_recover(void);

/*
 * 写一个元数据块。
 *
 * 这是给其他底层模块使用的便捷接口：
 *
 * begin -> log one block -> commit
 */
int myfs_journal_write_metadata_block(myfs_block_t block_id, const void *buf);

/*
 * 判断 journal 是否干净。
 */
int myfs_journal_is_clean(int *is_clean);


/*
 * 测试辅助接口：
 * 构造一个“已提交但尚未回放”的 journal 记录。
 *
 * 正常业务代码不要调用它。
 */
int myfs_journal_debug_create_committed_record(
        myfs_block_t target_block,
        const void *buf
);

#endif