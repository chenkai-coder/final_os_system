"""Global configuration for the MYFS visual shell.

Dependency: pip install PyQt5

Disk image: "disk.img" — relative path resolved from the fs_ui/ directory.
The C++ backend uses this same relative path.
"""
from pathlib import Path

BASE_DIR = Path(__file__).resolve().parent

# C++ executable path (adjust if your build output differs).
FS_CORE_PATH = str((BASE_DIR / ".." / "os_system" / "build" / "Debug" / "vfs_upper.exe").resolve())

# Disk parameters (UI heatmap shows a window of blocks, not the full data zone).
DATA_BLOCK_COUNT = 512
HEATMAP_COLS = 32
HEATMAP_PAGE_STEP = HEATMAP_COLS
DATA_START_BLOCK = 1282
FREE_GROUP_SIZE = 100
GROUP_STACK_VISIBLE = 16
GROUP_ANIMATION_MS = 140

# Colors
COLOR_FREE = "#808080"
COLOR_ALLOCATED = "#22c55e"
COLOR_METADATA = "#3b82f6"
COLOR_SELECTED = "#fbbf24"

# Commands
COMMANDS = {
    "format": "format\n",
    "login": "login {} {}\n",
    "logout": "logout\n",
    "mkdir": "mkdir {}\n",
    "create": "create {}\n",
    "delete": "delete {}\n",
    "open": "open {} {}\n",
    "close": "close {}\n",
    "write": "write {} {}\n",
    "read": "read {} {}\n",
    "cd": "cd {}\n",
    "dir": "dir\n",
    "pwd": "pwd\n",
    "batch_mkdir": "batch_mkdir {} {} {}\n",
    "batch_create": "batch_create {} {} {} {}\n",
    "statfs": "statfs\n",
    "super": "super\n",
    "freegroup": "freegroup\n",
    "cache": "cache\n",
    "fsck": "fsck\n",
    "bmap": "bmap {} {}\n",
    "inode": "inode {}\n",
    "blockmap": "blockmap {} {}\n",
    "chmod": "chmod {} {}\n",
    "chown": "chown {} {} {}\n",
    "useradd": "useradd {} {} {} {}\n",
    "userdel": "userdel {}\n",
    "su": "su {} {}\n",
    "whoami": "whoami\n",
    "exit": "exit\n",
    "crash": "crash\n",
}
