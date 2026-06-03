"""[SYNC] protocol parser.

Dependency: pip install PyQt5
"""
from __future__ import annotations

import re
from typing import Dict, Optional, Pattern

from PyQt5.QtCore import QObject, pyqtSignal

from config import DATA_BLOCK_COUNT, DATA_START_BLOCK


class SyncParser(QObject):
    """Parse lines from the core process and emit typed signals."""

    block_allocated = pyqtSignal(int)
    block_freed = pyqtSignal(int)
    tree_updated = pyqtSignal(str)
    user_logged_in = pyqtSignal(int)
    user_logged_out = pyqtSignal(int)
    dir_changed = pyqtSignal(str)
    group_event = pyqtSignal(str)
    group_loaded = pyqtSignal(int, int)
    group_stored = pyqtSignal(int, int)
    group_items = pyqtSignal(list)
    cache_lru = pyqtSignal(list)
    super_info = pyqtSignal(int, int, int, int, int)
    plain_output = pyqtSignal(str)
    inode_info = pyqtSignal(object)
    used_blocks = pyqtSignal(int, int, list)
    inode_indirect1 = pyqtSignal(int, int, list)  # inode_id, ptr_block, [data_blocks...]
    inode_indirect2 = pyqtSignal(int, int, list)  # inode_id, ptr_block, [l1_blocks...]

    def __init__(self) -> None:
        super().__init__()
        self._prompt_pattern = re.compile(r"^myfs:.*?>\s*")
        self._data_start_block = DATA_START_BLOCK
        self._data_block_total = DATA_BLOCK_COUNT
        self._inode_capture: Optional[dict] = None
        self._patterns: Dict[str, Pattern[str]] = {
            "block_alloc": re.compile(r"\[SYNC\]\s+BLOCK_ALLOC\s+(\d+)"),
            "block_free": re.compile(r"\[SYNC\]\s+BLOCK_FREE\s+(\d+)"),
            "tree_update": re.compile(r"\[SYNC\]\s+TREE_UPDATE\s+(.+)$"),
            "login": re.compile(r"\[SYNC\]\s+LOGIN\s+(\d+)"),
            "logout": re.compile(r"\[SYNC\]\s+LOGOUT\s+(\d+)"),
            "chdir": re.compile(r"\[SYNC\]\s+CHDIR\s+(.+)$"),
            "group_load": re.compile(r"\[SYNC\]\s+GROUP_LOAD\s+(\d+)\s+(\d+)$"),
            "group_store": re.compile(r"\[SYNC\]\s+GROUP_STORE\s+(\d+)\s+(\d+)$"),
            "group_items": re.compile(r"\[SYNC\]\s+GROUP_ITEMS\s+(.+)$"),
            "cache_lru": re.compile(r"\[SYNC\]\s+CACHE_LRU\s+(\d+)\s+(.+)$"),
            "super": re.compile(r"\[SYNC\]\s+SUPER\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)$"),
            "used_blocks": re.compile(r"\[SYNC\]\s+USED_BLOCKS\s+(\d+)\s+(\d+)\s+(\d+)\s*(.*)$"),
            "inode_indirect1": re.compile(r"\[SYNC\]\s+INODE_INDIRECT1\s+(\d+)\s+(\d+)\s*(.*)$"),
            "inode_indirect2": re.compile(r"\[SYNC\]\s+INODE_INDIRECT2\s+(\d+)\s+(\d+)\s*(.*)$"),
        }
        self._inode_start_pattern = re.compile(r"^=+\s*MYFS INODE\s+(\d+)\s*=+$")
        self._inode_end_pattern = re.compile(r"^=+\s*$")
        self._inode_kv_pattern = re.compile(r"^([a-zA-Z_][a-zA-Z0-9_]*)\s*=\s*(\d+)\s*$")
        self._inode_direct_pattern = re.compile(r"^direct\[(\d+)\]\s*=\s*(\d+)\s*$")

    def _strip_prompt(self, line: str) -> str:
        return self._prompt_pattern.sub("", line)

    def parse_line(self, line: str) -> None:
        """Parse a single line and emit matching signals."""
        clean_line = self._strip_prompt(line)
        match_line = clean_line.strip()
        if not match_line:
            self.plain_output.emit(clean_line)
            return

        if self._try_parse_inode_dump(match_line):
            self.plain_output.emit(clean_line)
            return

        match = self._patterns["super"].search(match_line)
        if match:
            data_start = int(match.group(1))
            data_blocks = int(match.group(2))
            free_group_size = int(match.group(3))
            block_size = int(match.group(4))
            total_blocks = int(match.group(5))

            if data_blocks > 0:
                self._data_start_block = data_start
                self._data_block_total = data_blocks

            self.super_info.emit(
                data_start,
                data_blocks,
                free_group_size,
                block_size,
                total_blocks,
            )
            self.plain_output.emit(clean_line)
            return

        match = self._patterns["block_alloc"].search(match_line)
        if match:
            block_no = int(match.group(1))
            rel = block_no - self._data_start_block
            if 0 <= rel < self._data_block_total:
                self.block_allocated.emit(rel)
            return

        match = self._patterns["block_free"].search(match_line)
        if match:
            block_no = int(match.group(1))
            rel = block_no - self._data_start_block
            if 0 <= rel < self._data_block_total:
                self.block_freed.emit(rel)
            return

        match = self._patterns["tree_update"].search(match_line)
        if match:
            self.tree_updated.emit(match.group(1).strip())
            return

        match = self._patterns["login"].search(match_line)
        if match:
            self.user_logged_in.emit(int(match.group(1)))
            return

        match = self._patterns["logout"].search(match_line)
        if match:
            self.user_logged_out.emit(int(match.group(1)))
            return

        match = self._patterns["chdir"].search(match_line)
        if match:
            self.dir_changed.emit(match.group(1).strip())
            return

        match = self._patterns["group_load"].search(match_line)
        if match:
            count = int(match.group(1))
            block_id = int(match.group(2))
            self.group_loaded.emit(count, block_id)
            self.group_event.emit(f"Group loaded: count={count} from block={block_id}")
            self.plain_output.emit(clean_line)
            return

        match = self._patterns["group_store"].search(match_line)
        if match:
            count = int(match.group(1))
            block_id = int(match.group(2))
            self.group_stored.emit(count, block_id)
            self.group_event.emit(f"Group stored: count={count} into block={block_id}")
            self.plain_output.emit(clean_line)
            return

        match = self._patterns["group_items"].search(match_line)
        if match:
            items_str = match.group(1)
            items = [int(x) for x in items_str.split() if x.strip().isdigit()]
            self.group_items.emit(items)
            return

        match = self._patterns["cache_lru"].search(match_line)
        if match:
            count = int(match.group(1))
            items_str = match.group(2)
            items = [int(x) for x in items_str.split() if x.strip().isdigit()]
            self.cache_lru.emit(items[:count])
            self.plain_output.emit(clean_line)
            return

        match = self._patterns["used_blocks"].search(match_line)
        if match:
            start_block = int(match.group(1))
            count = int(match.group(2))
            used_count = int(match.group(3))
            items_str = match.group(4) or ""
            items = [int(x) for x in items_str.split() if x.strip().isdigit()]
            self.used_blocks.emit(start_block, count, items[:used_count])
            return

        match = self._patterns["inode_indirect1"].search(match_line)
        if match:
            inode_id = int(match.group(1))
            ptr_block = int(match.group(2))
            items_str = match.group(3) or ""
            items = [int(x) for x in items_str.split() if x.strip().isdigit()]
            self.inode_indirect1.emit(inode_id, ptr_block, items)
            return

        match = self._patterns["inode_indirect2"].search(match_line)
        if match:
            inode_id = int(match.group(1))
            ptr_block = int(match.group(2))
            items_str = match.group(3) or ""
            # Parse items that may contain colon-separated lists: "block:data1:data2"
            items = []
            for part in items_str.split():
                if ":" in part:
                    # L1 block with data pointers
                    nums = [int(x) for x in part.split(":") if x.strip().isdigit()]
                    if nums:
                        items.append(nums)
                else:
                    # Simple block number
                    if part.strip().isdigit():
                        items.append(int(part))
            self.inode_indirect2.emit(inode_id, ptr_block, items)
            return

        self.plain_output.emit(clean_line)

    def _try_parse_inode_dump(self, match_line: str) -> bool:
        start = self._inode_start_pattern.match(match_line)
        if start:
            inode_id = int(start.group(1))
            self._inode_capture = {
                "inode_id": inode_id,
                "direct": [],
                "indirect1": 0,
                "indirect2": 0,
            }
            return True

        if self._inode_capture is None:
            return False

        direct_match = self._inode_direct_pattern.match(match_line)
        if direct_match:
            idx = int(direct_match.group(1))
            blk = int(direct_match.group(2))
            direct_list = self._inode_capture["direct"]
            if len(direct_list) <= idx:
                direct_list.extend([0] * (idx + 1 - len(direct_list)))
            direct_list[idx] = blk
            return True

        kv = self._inode_kv_pattern.match(match_line)
        if kv:
            key = kv.group(1)
            val = int(kv.group(2))
            if key in ("size", "block_count", "link_count", "open_count", "indirect1", "indirect2"):
                self._inode_capture[key] = val
            return True

        if self._inode_end_pattern.match(match_line) and "inode_id" in self._inode_capture:
            info = self._inode_capture
            self._inode_capture = None
            self.inode_info.emit(info)
            return True

        return False
