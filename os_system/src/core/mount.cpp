#include "mount.h"
#include "disk.h"
#include "superblock.h"
#include "config.h"
#include "error.h"
#include "inode.h"
#include "block_alloc.h"

#include <string.h>
#include <stdio.h>

#include "journal.h"

/*
 * g_mounted 用于记录当前进程中 MYFS 是否已经挂载。
 * 真实文件系统中，挂载状态通常由操作系统内核维护；
 * 本项目是用户态模拟文件系统，所以用一个全局变量简单维护。
 */
static int g_mounted = 0;


/*
 * 将指定物理块清零。
 * 参数：
 * block_id：要清零的物理块号。
 *
 * 功能：
 * 1. 构造一个全 0 的 4KB 缓冲区。
 * 2. 调用 myfs_disk_write_block 写入对应块。
 *
 * 使用场景：
 * mkfs 格式化时，需要清空 inode 位图区、inode 表区、journal 区等元数据区域。
 */
static int zero_block(myfs_block_t block_id) {
    unsigned char zero[MYFS_BLOCK_SIZE];

    /*
     * 将整个缓冲区填充为 0。
     * MYFS_BLOCK_SIZE 当前固定为 4096 字节。
     */
    memset(zero, 0, sizeof(zero));

    /*
     * 把全 0 缓冲区写入指定物理块。
     */
    return myfs_disk_write_block(block_id, zero);
}


/*
 * 清空一段连续的物理块区域。
 *
 * 参数：
 * start：起始块号。
 * count：连续清空的块数量。
 *
 * 功能：
 * 从 start 开始，依次清空 count 个块：
 */
static int zero_block_range(myfs_block_t start, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        int ret = zero_block(start + i);

        if (ret != MYFS_OK) {
            return ret;
        }
    }

    return MYFS_OK;
}

/*
 * 格式化一个新的 MYFS 文件系统。
 */
int myfs_mkfs(
        const char *disk_path,
        uint32_t total_blocks,
        uint32_t total_inodes
) {
    if (disk_path == NULL || total_blocks == 0 || total_inodes == 0) {
        return MYFS_ERR_INVALID_ARG;
    }

    /*
     * 第一步：创建 disk.img。
     */
    int ret = myfs_disk_create(disk_path, total_blocks);
    if (ret != MYFS_OK) {
        return ret;
    }

    /*
     * 第二步：打开刚刚创建的 disk.img。
     */
    ret = myfs_disk_open(disk_path);
    if (ret != MYFS_OK) {
        return ret;
    }

    /*
     * 第三步：初始化一个临时超级块结构。
     */
    myfs_superblock_t sb;

    ret = myfs_super_init(&sb, total_blocks, total_inodes);
    if (ret != MYFS_OK) {
        myfs_disk_close();
        return ret;
    }

    /*
     * 第四步：清空 inode 位图区域。
     * 格式化时，所有 inode 都应该处于空闲状态，
     */
    ret = zero_block_range(sb.inode_bitmap_start, sb.inode_bitmap_blocks);
    if (ret != MYFS_OK) {
        myfs_disk_close();
        return ret;
    }

    /*
     * 第五步：清空 inode 表区域。
     *
     * inode 表用于保存每个 inode 的具体元数据，
     * 包括文件类型、权限、大小、时间戳、链接计数、数据块指针等。
     *
     * 格式化时，inode 表中不应该残留旧文件系统的数据，
     */
    ret = zero_block_range(sb.inode_table_start, sb.inode_table_blocks);
    if (ret != MYFS_OK) {
        myfs_disk_close();
        return ret;
    }

    /*
     * 第六步：清空 journal 区域。
     */
    ret = zero_block_range(sb.journal_start, sb.journal_blocks);
    if (ret != MYFS_OK) {
        myfs_disk_close();
        return ret;
    }

    /*
     * 第七步：将初始超级块写入磁盘第 0 块。
     *
     * 超级块是整个文件系统的“总说明书”，固定保存在 block 0。
     *
     * 1. 先准备一个 4KB 缓冲区。
     * 2. 将其清零。
     * 3. 把 sb 拷贝到缓冲区开头。
     * 4. 整块写入 block 0。
     */
    unsigned char block[MYFS_BLOCK_SIZE];

    memset(block, 0, sizeof(block));
    memcpy(block, &sb, sizeof(sb));

    ret = myfs_disk_write_block(0, block);
    if (ret != MYFS_OK) {
        myfs_disk_close();
        return ret;
    }

    /*
     * 第八步：加载超级块到内存。
     *
     * inode 模块中的函数，例如：
     *
     *     myfs_inode_bitmap_clear_all()
     *     myfs_inode_table_clear_all()
     *     myfs_inode_init_root()
     *
     * 内部都需要通过 myfs_super_get() 获取超级块信息，
     * 从而知道 inode 位图和 inode 表在磁盘上的位置。
     *
     * 因此，必须先调用 myfs_super_load()，
     * 把刚才写入 block 0 的超级块读入全局内存超级块。
     */
    ret = myfs_super_load();
    if (ret != MYFS_OK) {
        myfs_disk_close();
        return ret;
    }


    /*
     * 初始化 journal 区。
     */
    ret = myfs_journal_format();
    if (ret != MYFS_OK) {
        myfs_disk_close();
        return ret;
    }

    /*
     * 第九步：通过 inode 模块清空 inode 位图。
     *
     * 前面已经用 zero_block_range 从物理层清空过一次。
     * 这里再次通过 inode 模块清空，主要是为了让 inode 模块完成自己的初始化逻辑。
     *
     * 目前这一步看起来重复，但从分层角度更清晰：
     *
     * zero_block_range：
     *     物理层清零，不关心该区域含义。
     *
     * myfs_inode_bitmap_clear_all：
     *     inode 模块清零，明确知道这是 inode 位图。
     */
    ret = myfs_inode_bitmap_clear_all();
    if (ret != MYFS_OK) {
        myfs_disk_close();
        return ret;
    }

    /*
     * 第十步：通过 inode 模块清空 inode 表。
     *
     * 同理，虽然前面已经物理清零，
     * 这里仍然通过 inode 表接口进行一次模块级初始化。
     */
    ret = myfs_inode_table_clear_all();
    if (ret != MYFS_OK) {
        myfs_disk_close();
        return ret;
    }

    /*
     * 初始化数据区空闲块链。
     *
     * 第三阶段开始，数据块不再只是简单记录 free_blocks_count，
     * 而是通过成组链接法组织起来。
     *
     * 该函数会：
     * 1. 清空超级块中的 free_group。
     * 2. 将数据区所有块加入成组链接空闲块链。
     * 3. 更新 free_blocks_count。
     * 4. 将超级块同步回磁盘。
     *
     * 注意：
     * 当前 root inode 还不占用数据块。
     * 后续目录模块完成后，root 目录才会真正分配数据块保存 "." 和 ".."。
     */
    ret = myfs_block_group_init(sb.data_block_start, sb.data_blocks);
    if (ret != MYFS_OK) {
        myfs_disk_close();
        return ret;
    }


    /*
     * 第十一步：初始化 root inode。
     *
     * root inode 是根目录 "/" 对应的 inode。
     *
     * 在本文件系统中，约定：
     *
     *     root_inode = 0
     *
     * myfs_inode_init_root() 内部会：
     * 1. 调用 myfs_inode_alloc 分配第一个 inode。
     * 2. 将其类型设置为 MYFS_INODE_DIR。
     * 3. 设置权限为 0755。
     * 4. 设置 uid/gid 为 0。
     * 5. 设置 link_count 为 2。
     * 6. 更新超级块中的 root_inode 字段。
     *
     * 当前第二阶段只创建 root inode 本体。
     *
     * 后续目录模块完成后，还需要：
     * 1. 为 root 目录分配数据块。
     * 2. 在 root 目录数据块中写入 "." 和 ".." 目录项。
     */
    ret = myfs_inode_init_root();
    if (ret != MYFS_OK) {
        myfs_disk_close();
        return ret;
    }

    /*
     * 第十二步：格式化完成后标记文件系统为 clean。
     *
     * clean 表示文件系统处于正常、一致、未挂载状态。
     *
     * mount 时会将其标记为 dirty；
     * umount 时会再次标记为 clean。
     *
     * 如果下次挂载时发现状态仍然是 dirty，
     * 说明上次可能异常退出，需要 journal 或 fsck 恢复。
     */
    ret = myfs_super_mark_clean();
    if (ret != MYFS_OK) {
        myfs_disk_close();
        return ret;
    }

    /*
     * 第十三步：关闭 disk.img。
     *
     * mkfs 是一次性格式化操作，完成后不保持挂载状态。
     */
    myfs_disk_close();

    return MYFS_OK;
}


/*
 * myfs_mount
 * ------------------------------------------------------------
 * 挂载一个已经格式化好的 MYFS 文件系统。
 *
 * 参数：
 * disk_path：磁盘镜像路径。
 *
 * 功能：
 * 1. 打开 disk.img。
 * 2. 从 block 0 加载超级块。
 * 3. 检查超级块合法性。
 * 4. 如果发现上次未正常卸载，输出警告。
 * 5. 将文件系统状态标记为 dirty。
 * 6. 设置内存挂载标志 g_mounted = 1。
 *
 * 当前阶段：
 * 只检测 dirty 状态并输出警告。
 *
 * 后续实现 journal 后：
 * 如果发现 dirty，应调用 journal_recover() 进行崩溃恢复。
 */
int myfs_mount(const char *disk_path) {
    /*
     * 参数检查。
     */
    if (disk_path == NULL) {
        return MYFS_ERR_INVALID_ARG;
    }

    /*
     * 防止重复挂载。
     */
    if (g_mounted) {
        return MYFS_ERR_ALREADY_MOUNTED;
    }

    /*
     * 第一步：打开 disk.img。
     */
    int ret = myfs_disk_open(disk_path);
    if (ret != MYFS_OK) {
        return ret;
    }

    /*
     * 第二步：加载超级块。
     *
     * myfs_super_load 内部会从磁盘第 0 块读取超级块，
     * 并调用 myfs_super_check 检查其合法性。
     */
    ret = myfs_super_load();
    if (ret != MYFS_OK) {
        myfs_disk_close();
        return ret;
    }

    /*
     * 【设计说明】
     * 挂载时不自动执行 journal 恢复。
     *
     * 如果上次崩溃时存在 committed 事务，
     * journal header 会保留 COMMITTED 状态，
     * 由用户通过 recover 命令手动触发恢复。
     *
     * 这样可以在恢复前用 fsck 查看文件系统不一致状态，
     * 恢复后再用 fsck 验证一致性已恢复。
     */
    // ret = myfs_journal_recover();
    // if (ret != MYFS_OK) {
    //     myfs_disk_close();
    //     return ret;
    // }

    /*
     * 第三步：获取内存中的超级块。
     */
    myfs_superblock_t *sb = myfs_super_get();

    if (sb == NULL) {
        myfs_disk_close();
        return MYFS_ERR_CORRUPTED;
    }

    /*
     * 检测文件系统是否上次未正常卸载（fs_state == DIRTY）。
     *
     * DIRTY 表示上次可能是 crash 退出或异常断电。
     * 挂载时不自动恢复——由用户通过 recover 命令手动触发。
     */
    if (sb->fs_state == MYFS_STATE_DIRTY) {
        printf("[WARN] filesystem is DIRTY — previous unmount was not clean\n"
               "       use 'fsck' to check, then 'recover' to repair\n");
    }

    /*
     * 第五步：将文件系统标记为 dirty。
     *
     * 这是挂载文件系统时必须做的事情。
     *
     * 含义：
     * 从现在开始，文件系统可能会发生修改。
     * 如果程序异常退出，来不及执行 umount，
     * 那么磁盘上的 fs_state 就会保持 dirty。
     *
     * 下次 mount 时就可以据此判断需要恢复。
     */
    ret = myfs_super_mark_dirty();
    if (ret != MYFS_OK) {
        myfs_disk_close();
        return ret;
    }

    /*
     * 第六步：设置内存挂载标志。
     *
     * 注意：
     * 这个标志只表示当前进程已经挂载 MYFS。
     */
    g_mounted = 1;

    return MYFS_OK;
}


/*
 * myfs_umount
 * ------------------------------------------------------------
 * 卸载当前文件系统。
 *
 * 功能：
 * 1. 检查是否已经挂载。
 * 2. 将超级块状态标记为 clean。
 * 3. 关闭 disk.img。
 * 4. 清除内存挂载标志。
 *
 * 当前阶段：
 * 只需要同步超级块并关闭磁盘。
 *
 * 后续实现缓存后：
 * umount 时必须先 flush 所有 dirty 缓存块。
 *
 * 后续实现 journal 后：
 * umount 时还需要确保日志 checkpoint 完成。
 */
int myfs_umount(void) {
    /*
     * 如果没有挂载，就不能卸载。
     */
    if (!g_mounted) {
        return MYFS_ERR_NOT_MOUNTED;
    }

    /*
     * 第一步：标记文件系统为 clean。
     *
     * 表示当前文件系统已经正常结束使用。
     */
    int ret = myfs_super_mark_clean();
    if (ret != MYFS_OK) {
        return ret;
    }

    /*
     * 第二步：关闭磁盘镜像文件。
     *
     * myfs_disk_close 内部会 fflush 并关闭 FILE 指针。
     */
    ret = myfs_disk_close();
    if (ret != MYFS_OK) {
        return ret;
    }

    /*
     * 第三步：更新内存挂载状态。
     */
    g_mounted = 0;

    return MYFS_OK;
}


/*
 * myfs_is_mounted
 * ------------------------------------------------------------
 * 判断当前进程中 MYFS 是否处于挂载状态。
 *
 * 返回：
 * 1 表示已经挂载；
 * 0 表示未挂载。
 *
 * 注意：
 * 该函数只检查当前进程内的 g_mounted 变量，
 * 不代表 disk.img 是否被其他进程打开。
 */
int myfs_is_mounted(void) {
    return g_mounted;
}