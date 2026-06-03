#include "disk.h"
#include "config.h"
#include "error.h"
#include "type.h"

#include <stdio.h>
#include <stdint.h>

static FILE *g_disk = NULL;
static uint32_t g_total_blocks = 0;

int myfs_disk_create(const char *path, uint32_t total_blocks) {
    if (path == NULL || total_blocks == 0) {
        return MYFS_ERR_INVALID_ARG;
    }

    FILE *fp = fopen(path, "wb+");
    if (fp == NULL) {
        return MYFS_ERR_IO;
    }

    uint64_t total_bytes = (uint64_t) total_blocks * MYFS_BLOCK_SIZE;

    if (total_bytes == 0) {
        fclose(fp);
        return MYFS_ERR_INVALID_ARG;
    }

    /*
     * 建立固定大小的 disk.img。
     * fseek 到最后一个字节再写 0，可以快速扩展文件。
     */
    if (fseek(fp, (long) (total_bytes - 1), SEEK_SET) != 0) {
        fclose(fp);
        return MYFS_ERR_IO;
    }

    if (fputc(0, fp) == EOF) {
        fclose(fp);
        return MYFS_ERR_IO;
    }

    fflush(fp);
    fclose(fp);

    return MYFS_OK;
}

int myfs_disk_open(const char *path) {
    if (path == NULL) {
        return MYFS_ERR_INVALID_ARG;
    }

    if (g_disk != NULL) {
        return MYFS_ERR_ALREADY_MOUNTED;
    }

    g_disk = fopen(path, "rb+");
    if (g_disk == NULL) {
        return MYFS_ERR_IO;
    }

    if (fseek(g_disk, 0, SEEK_END) != 0) {
        fclose(g_disk);
        g_disk = NULL;
        return MYFS_ERR_IO;
    }

    long size = ftell(g_disk);
    if (size < 0 || size % MYFS_BLOCK_SIZE != 0) {
        fclose(g_disk);
        g_disk = NULL;
        return MYFS_ERR_IO;
    }

    g_total_blocks = (uint32_t) (size / MYFS_BLOCK_SIZE);

    if (fseek(g_disk, 0, SEEK_SET) != 0) {
        fclose(g_disk);
        g_disk = NULL;
        g_total_blocks = 0;
        return MYFS_ERR_IO;
    }

    return MYFS_OK;
}

int myfs_disk_close(void) {
    if (g_disk == NULL) {
        return MYFS_OK;
    }

    fflush(g_disk);
    fclose(g_disk);

    g_disk = NULL;
    g_total_blocks = 0;

    return MYFS_OK;
}

int myfs_disk_read_block(myfs_block_t block_id, void *buf) {
    if (g_disk == NULL) {
        return MYFS_ERR_NOT_MOUNTED;
    }

    if (buf == NULL) {
        return MYFS_ERR_INVALID_ARG;
    }

    if (block_id >= g_total_blocks) {
        return MYFS_ERR_INVALID_BLOCK;
    }

    uint64_t offset = (uint64_t) block_id * MYFS_BLOCK_SIZE;

    if (fseek(g_disk, (long) offset, SEEK_SET) != 0) {
        return MYFS_ERR_IO;
    }

    size_t n = fread(buf, 1, MYFS_BLOCK_SIZE, g_disk);
    if (n != MYFS_BLOCK_SIZE) {
        return MYFS_ERR_IO;
    }

    return MYFS_OK;
}

int myfs_disk_write_block(myfs_block_t block_id, const void *buf) {
    if (g_disk == NULL) {
        return MYFS_ERR_NOT_MOUNTED;
    }

    if (buf == NULL) {
        return MYFS_ERR_INVALID_ARG;
    }

    if (block_id >= g_total_blocks) {
        return MYFS_ERR_INVALID_BLOCK;
    }

    uint64_t offset = (uint64_t) block_id * MYFS_BLOCK_SIZE;

    if (fseek(g_disk, (long) offset, SEEK_SET) != 0) {
        return MYFS_ERR_IO;
    }

    size_t n = fwrite(buf, 1, MYFS_BLOCK_SIZE, g_disk);
    if (n != MYFS_BLOCK_SIZE) {
        return MYFS_ERR_IO;
    }

    fflush(g_disk);

    return MYFS_OK;
}

uint32_t myfs_disk_total_blocks(void) {
    return g_total_blocks;
}

uint32_t myfs_disk_block_size(void) {
    return MYFS_BLOCK_SIZE;
}

int myfs_disk_is_open(void) {
    return g_disk != NULL;
}