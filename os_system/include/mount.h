#ifndef MYFS_MOUNT_H
#define MYFS_MOUNT_H

/*
 * mount.h
 * ------------------------------------------------------------
 * 文件系统生命周期管理接口。
 *
 * 作用：
 * 负责格式化、挂载、卸载文件系统。
 *
 * 当前第一阶段实现：
 * 1. myfs_mkfs   创建并初始化 disk.img
 * 2. myfs_mount  打开 disk.img 并加载超级块
 * 3. myfs_umount 标记 clean 并关闭 disk.img
 *
 */

#include <stdint.h>

/*
 * 格式化文件系统。
 *
 * 参数：
 * disk_path     磁盘镜像路径
 * total_blocks  磁盘总块数
 * total_inodes  inode 总数
 *
 * 功能：
 * 1. 创建 disk.img。
 * 2. 计算磁盘布局。
 * 3. 初始化超级块。
 * 4. 清空 inode 位图区。
 * 5. 清空 inode 表区。
 * 6. 清空 journal 区。
 * 7. 将超级块写入 block 0。
 */
int myfs_mkfs(
        const char *disk_path,
        uint32_t total_blocks,
        uint32_t total_inodes
);

/*
 * 挂载文件系统。
 *
 * 功能：
 * 1. 打开 disk.img。
 * 2. 读取超级块。
 * 3. 检查 magic、版本号、布局等信息。
 * 4. 将文件系统状态标记为 DIRTY。
 *
 * 注意：
 * 第一阶段如果发现文件系统上次未 clean 卸载，只输出警告。
 * 后续实现 journal 后，应在这里执行 journal_recover。
 */
int myfs_mount(const char *disk_path);

/*
 * 卸载文件系统。
 *
 * 功能：
 * 1. 将文件系统状态标记为 CLEAN。
 * 2. 同步超级块。
 * 3. 关闭 disk.img。
 *
 * 后续实现缓存后，还需要在这里 flush 所有脏块。
 */
int myfs_umount(void);

/*
 * 判断文件系统是否已经挂载。
 *
 * 返回：
 * 1 表示已挂载
 * 0 表示未挂载
 */
int myfs_is_mounted(void);

#endif