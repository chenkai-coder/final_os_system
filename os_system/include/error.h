#ifndef MYFS_ERROR_H
#define MYFS_ERROR_H

/*
 * error.h
 * ------------------------------------------------------------
 * 文件系统统一错误码定义。
 *
 * 作用：
 * 所有底层接口统一返回这些错误码，方便上层判断失败原因。
 *
 * 约定：
 * 1. MYFS_OK 表示成功。
 * 2. 负数表示错误。
 * 3. 上层可以通过 myfs_strerror() 将错误码转换为字符串。
 */

typedef enum {
    /*
     * 操作成功。
     */
    MYFS_OK = 0,

    /*
     * 磁盘读写失败。
     */
    MYFS_ERR_IO = -1,

    /*
     * 数据块空间不足。
     */
    MYFS_ERR_NO_SPACE = -2,

    /*
     * inode 数量不足。
     */
    MYFS_ERR_NO_INODE = -3,

    /*
     * 非法块号，例如 block_id 越界。
     */
    MYFS_ERR_INVALID_BLOCK = -4,

    /*
     * 非法 inode 编号。
     */
    MYFS_ERR_INVALID_INODE = -5,

    /*
     * 文件系统尚未挂载。
     */
    MYFS_ERR_NOT_MOUNTED = -6,

    /*
     * 文件系统已经挂载，不能重复挂载。
     */
    MYFS_ERR_ALREADY_MOUNTED = -7,

    /*
     * 文件系统结构损坏，例如超级块字段不合法。
     */
    MYFS_ERR_CORRUPTED = -8,

    /*
     * 参数非法，例如传入 NULL 指针。
     */
    MYFS_ERR_INVALID_ARG = -9,

    /*
     * 当前功能暂不支持。
     */
    MYFS_ERR_UNSUPPORTED = -10,

    /*
     * 内存不足。
     */
    MYFS_ERR_NO_MEMORY = -11,

    /*
     * 校验和错误。
     */
    MYFS_ERR_CHECKSUM = -12
} myfs_error_t;

/*
 * 将错误码转换为字符串。
 *
 * 示例：
 * int ret = myfs_mount("disk.img");
 * if (ret != MYFS_OK) {
 *     printf("mount failed: %s\n", myfs_strerror(ret));
 * }
 */
const char *myfs_strerror(int err);

#endif