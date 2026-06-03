#ifndef MYFS_INODE_H
#define MYFS_INODE_H

#include <stdint.h>
#include "type.h"
#include "config.h"

/*
 * inode 类型。
 */
typedef enum {
    MYFS_INODE_FREE = 0,       /* 空闲 inode */
    MYFS_INODE_FILE = 1,       /* 普通文件 */
    MYFS_INODE_DIR = 2,        /* 目录 */
    MYFS_INODE_SYMLINK = 3     /* 符号链接 */
} myfs_inode_type_t;

/*
 * inode 结构。
 *
 * config.h 中定义了：
 *
 *     #define MYFS_INODE_SIZE 256u
 *
 * 因此该结构体大小必须 <= 256 字节。
 *
 * 当前结构体通过 reserved 字段进行预留，
 * 一方面方便后续扩展，另一方面保证 inode 槽位大小稳定。
 */
typedef struct myfs_inode {
    /*
     * inode 编号。
     */
    myfs_ino_t inode_id;

    /*
     * inode 类型：
     * 普通文件、目录、符号链接等。
     */
    uint16_t type;

    /*
     * 权限位。
     *
     * 例如：
     * 普通文件 0644
     * 目录     0755
     */
    uint16_t mode;

    /*
     * 所有者用户 ID 和所属组 ID。
     */
    uint32_t uid;
    uint32_t gid;

    /*
     * 文件逻辑大小，单位：字节。
     */
    uint64_t size;

    /*
     * 文件实际占用的数据块数量。
     */
    uint32_t block_count;

    /*
     * 硬链接计数。
     */
    uint32_t link_count;

    /*
     * 打开计数。
     */
    uint32_t open_count;

    /*
     * 时间戳。
     *
     * atime  : 最近访问时间
     * mtime  : 最近内容修改时间
     * ctime  : inode 元数据修改时间
     * crtime : 创建时间
     */
    uint64_t atime;
    uint64_t mtime;
    uint64_t ctime;
    uint64_t crtime;

    /*
     * 直接块指针。
     */
    myfs_block_t direct[12];

    /*
     * 一级间接块、二级间接块。
     */
    myfs_block_t indirect1;
    myfs_block_t indirect2;

    /*
     * inode 校验和。
     */
    uint32_t checksum;

    /*
     * 预留字段。
     *
     * 当前 inode 结构体最终大小应为 256 字节。
     * 后续可以把该区域改造成 inline_data、小文件内联数据区等。
     */
    uint8_t reserved[120];

} myfs_inode_t;


/* ============================================================
 * 对上层主要开放的 inode 管理接口
 * ============================================================
 */

/*
 * 分配一个新的 inode。
 *
 * 参数：
 * type      inode 类型
 * mode      权限位
 * uid       用户 ID
 * gid       组 ID
 * inode_id  输出分配到的 inode 编号
 */
int myfs_inode_alloc(
        uint16_t type,
        uint16_t mode,
        uint32_t uid,
        uint32_t gid,
        myfs_ino_t *inode_id
);

/*
 * 释放一个 inode。
 *
 * 当前阶段只释放 inode 本身，不释放数据块。
 * 因为数据块分配器还没有实现。
 */
int myfs_inode_free(myfs_ino_t inode_id);

/*
 * 读取 inode。
 */
int myfs_inode_read(myfs_ino_t inode_id, myfs_inode_t *out);

/*
 * 写回 inode。
 */
int myfs_inode_write(myfs_ino_t inode_id, const myfs_inode_t *inode);

/*
 * 增加硬链接计数。
 */
int myfs_inode_inc_link(myfs_ino_t inode_id);

/*
 * 减少硬链接计数。
 *
 * 如果 link_count 和 open_count 都为 0，则释放 inode。
 */
int myfs_inode_dec_link(myfs_ino_t inode_id);

/*
 * 增加打开计数。
 */
int myfs_inode_inc_open(myfs_ino_t inode_id);

/*
 * 减少打开计数。
 *
 * 如果 link_count 和 open_count 都为 0，则释放 inode。
 */
int myfs_inode_dec_open(myfs_ino_t inode_id);

/*
 * 判断 inode 是否为普通文件。
 *
 * 返回：
 * 1 是
 * 0 不是或读取失败
 */
int myfs_inode_is_file(myfs_ino_t inode_id);

/*
 * 判断 inode 是否为目录。
 */
int myfs_inode_is_dir(myfs_ino_t inode_id);

/*
 * 判断 inode 是否为符号链接。
 */
int myfs_inode_is_symlink(myfs_ino_t inode_id);

/*
 * 初始化根目录 inode。
 *
 * mkfs 阶段调用。
 * 当前阶段只创建 root inode 本体。
 * 后续目录模块完成后，再给 root 目录写入 "." 和 ".." 目录项。
 */
int myfs_inode_init_root(void);

/*
 * 重置所有 inode 的 open_count 为 0。
 *
 * mount 时调用。
 * 当进程重启后，所有文件描述符都已经释放，
 * 但磁盘上的 open_count 可能保留了崩溃前的值。
 * 此函数把所有 open_count 归零。
 */
int myfs_inode_reset_open_counts(void);


/* ============================================================
 * inode 模块内部辅助接口
 *
 * 说明：
 * 这些函数虽然声明在 inode.h 中，但主要供 mount.c 或 inode.c 内部使用。
 * 普通上层逻辑不建议直接调用。
 * ============================================================
 */

/*
 * 清空 inode 位图。
 */
int myfs_inode_bitmap_clear_all(void);

/*
 * 分配 inode 位图中的一个空闲 inode 编号。
 */
int myfs_inode_bitmap_alloc(myfs_ino_t *inode_id);

/*
 * 释放 inode 位图中的一个 inode 编号。
 */
int myfs_inode_bitmap_free(myfs_ino_t inode_id);

/*
 * 测试 inode 是否已经被占用。
 *
 * 返回：
 * 1 已占用
 * 0 空闲
 * 负数 错误码
 */
int myfs_inode_bitmap_test(myfs_ino_t inode_id);

/*
 * 将 inode 位设置为 1。
 */
int myfs_inode_bitmap_set(myfs_ino_t inode_id);

/*
 * 将 inode 位清除为 0。
 */
int myfs_inode_bitmap_clear(myfs_ino_t inode_id);

/*
 * 统计空闲 inode 数。
 */
uint32_t myfs_inode_bitmap_count_free(void);

/*
 * 清空 inode 表。
 *
 * mkfs 阶段调用。
 */
int myfs_inode_table_clear_all(void);

/*
 * 从 inode 表中直接读取 inode。
 *
 * 主要给 inode.c 内部使用。
 */
int myfs_inode_table_read(myfs_ino_t inode_id, myfs_inode_t *out);

/*
 * 向 inode 表中直接写入 inode。
 *
 * 主要给 inode.c 内部使用。
 */
int myfs_inode_table_write(myfs_ino_t inode_id, const myfs_inode_t *inode);

#endif