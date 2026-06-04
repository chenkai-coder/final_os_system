"""Command helpers for the MYFS core.

Dependency: pip install PyQt5
"""
from __future__ import annotations

from typing import Union

from config import COMMANDS


class FSCommands:
    """Factory methods for core commands."""

    @staticmethod
    def format() -> str:
        return COMMANDS["format"]

    @staticmethod
    def login(username: str, password: str) -> str:
        return COMMANDS["login"].format(username, password)

    @staticmethod
    def logout() -> str:
        return COMMANDS["logout"]

    @staticmethod
    def mkdir(path: str) -> str:
        return COMMANDS["mkdir"].format(path)

    @staticmethod
    def create(path: str) -> str:
        return COMMANDS["create"].format(path)

    @staticmethod
    def delete(path: str) -> str:
        return COMMANDS["delete"].format(path)

    @staticmethod
    def open_file(path: str, mode: str) -> str:
        return COMMANDS["open"].format(path, mode)

    @staticmethod
    def close(fd: int) -> str:
        return COMMANDS["close"].format(fd)

    @staticmethod
    def write(fd: int, data: Union[int, str]) -> str:
        return COMMANDS["write"].format(fd, data)

    @staticmethod
    def read(fd: int, size: int) -> str:
        return COMMANDS["read"].format(fd, size)

    @staticmethod
    def cd(path: str) -> str:
        return COMMANDS["cd"].format(path)

    @staticmethod
    def dir_list() -> str:
        return COMMANDS["dir"]

    @staticmethod
    def pwd() -> str:
        return COMMANDS["pwd"]

    @staticmethod
    def batch_mkdir(base: str, prefix: str, count: int) -> str:
        return COMMANDS["batch_mkdir"].format(base, prefix, count)

    @staticmethod
    def batch_create(base: str, prefix: str, count: int, size: int) -> str:
        return COMMANDS["batch_create"].format(base, prefix, count, size)

    @staticmethod
    def statfs() -> str:
        return COMMANDS["statfs"]

    @staticmethod
    def super() -> str:
        return COMMANDS["super"]

    @staticmethod
    def free_group() -> str:
        return COMMANDS["freegroup"]

    @staticmethod
    def cache() -> str:
        return COMMANDS["cache"]

    @staticmethod
    def fsck() -> str:
        return COMMANDS["fsck"]

    @staticmethod
    def bmap(inode_id: int, logical_block: int) -> str:
        return COMMANDS["bmap"].format(inode_id, logical_block)

    @staticmethod
    def inode(inode_id: int) -> str:
        return COMMANDS["inode"].format(inode_id)

    @staticmethod
    def blockmap(start_block: int, count: int) -> str:
        return COMMANDS["blockmap"].format(start_block, count)

    @staticmethod
    def dir_inode(path: str = "") -> str:
        if path:
            return COMMANDS["dirinode"].format(path)
        return COMMANDS["dirinode_cur"]

    @staticmethod
    def chmod(path: str, mode: str) -> str:
        return COMMANDS["chmod"].format(path, mode)

    @staticmethod
    def chown(path: str, uid: int, gid: int) -> str:
        return COMMANDS["chown"].format(path, uid, gid)

    @staticmethod
    def useradd(username: str, password: str, uid: int, gid: int) -> str:
        return COMMANDS["useradd"].format(username, password, uid, gid)

    @staticmethod
    def userdel(username: str) -> str:
        return COMMANDS["userdel"].format(username)

    @staticmethod
    def su(username: str, password: str) -> str:
        return COMMANDS["su"].format(username, password)

    @staticmethod
    def whoami() -> str:
        return COMMANDS["whoami"]

    @staticmethod
    def exit() -> str:
        return COMMANDS["exit"]

    @staticmethod
    def crash() -> str:
        """Simulate system crash (kill without umount)."""
        return COMMANDS["crash"]
