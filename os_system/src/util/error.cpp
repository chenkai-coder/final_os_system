#include "error.h"

const char *myfs_strerror(int err) {
    switch (err) {
    case MYFS_OK:
        return "OK";
    case MYFS_ERR_IO:
        return "I/O error";
    case MYFS_ERR_NO_SPACE:
        return "no space left";
    case MYFS_ERR_NO_INODE:
        return "no inode left";
    case MYFS_ERR_INVALID_BLOCK:
        return "invalid block";
    case MYFS_ERR_INVALID_INODE:
        return "invalid inode";
    case MYFS_ERR_NOT_MOUNTED:
        return "file system is not mounted";
    case MYFS_ERR_ALREADY_MOUNTED:
        return "file system is already mounted";
    case MYFS_ERR_CORRUPTED:
        return "file system is corrupted";
    case MYFS_ERR_INVALID_ARG:
        return "invalid argument";
    case MYFS_ERR_UNSUPPORTED:
        return "unsupported operation";
    case MYFS_ERR_NO_MEMORY:
        return "no memory";
    case MYFS_ERR_CHECKSUM:
        return "checksum error";
    default:
        return "unknown error";
    }
}