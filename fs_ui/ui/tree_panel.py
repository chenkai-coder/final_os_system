"""Directory tree panel.

Dependency: pip install PyQt5
"""
from __future__ import annotations

import re
from typing import Optional

from PyQt5.QtCore import Qt, pyqtSignal
from PyQt5.QtGui import QStandardItem, QStandardItemModel
from PyQt5.QtWidgets import (
    QInputDialog,
    QMenu,
    QTreeView,
    QVBoxLayout,
    QWidget,
)


class TreePanel(QWidget):
    """Tree view showing file and directory structure."""

    command_requested = pyqtSignal(str)

    def __init__(self) -> None:
        super().__init__()
        self._current_path = "/"
        self._last_command = ""
        self._dir_listing_active = False

        self.tree = QTreeView()
        self.model = QStandardItemModel()
        self.model.setHorizontalHeaderLabels(["File System"])  # single column
        self.root_item = QStandardItem("/")
        self.root_item.setData("dir", Qt.UserRole)
        self.model.appendRow(self.root_item)
        self.tree.setModel(self.model)
        self.tree.expandAll()
        self.tree.doubleClicked.connect(self.on_item_double_clicked)
        self.tree.setContextMenuPolicy(Qt.CustomContextMenu)
        self.tree.customContextMenuRequested.connect(self._show_context_menu)

        layout = QVBoxLayout()
        layout.addWidget(self.tree)
        self.setLayout(layout)

    def set_current_path(self, path: str) -> None:
        self._current_path = path if path else "/"

    def get_current_path(self) -> str:
        return self._current_path

    def ensure_dir_path(self, path: str) -> None:
        if not path or path == "/":
            return
        self._ensure_path(path, True)

    def set_last_command(self, cmd: str) -> None:
        self._last_command = cmd.strip()

    def reset(self) -> None:
        self._current_path = "/"
        self._last_command = ""
        self._dir_listing_active = False
        self.root_item.removeRows(0, self.root_item.rowCount())
        self.tree.expandAll()

    def update_tree(self, path: str) -> None:
        """Update tree structure based on a path string."""
        if not path or path == "/":
            return

        if self._is_delete_command(self._last_command):
            self._remove_path(path)
            return

        self._ensure_path(path, self._is_mkdir_command(self._last_command))

    def refresh_from_fs(self) -> None:
        """Request a directory listing to refresh current directory nodes."""
        self._dir_listing_active = True
        self._clear_children(self._find_item(self._current_path))
        self.command_requested.emit("dir\n")

    def consume_dir_line(self, line: str) -> None:
        """Consume a line from `dir` output and update nodes."""
        if not self._dir_listing_active:
            return

        match = re.match(r"<(DIR|FILE)>\s+\d+\s+(.+)$", line.strip())
        if not match:
            self._dir_listing_active = False
            return

        entry_type = match.group(1)
        name = match.group(2)

        parent = self._find_item(self._current_path)
        if parent is None:
            parent = self.root_item

        item = QStandardItem(name)
        item.setData("dir" if entry_type == "DIR" else "file", Qt.UserRole)
        parent.appendRow(item)
        self.tree.expand(self.model.indexFromItem(parent))

    def on_item_double_clicked(self, index) -> None:
        item = self.model.itemFromIndex(index)
        if item is None:
            return

        if item.data(Qt.UserRole) == "dir":
            path = self._build_path(item)
            self.command_requested.emit(f"cd {path}\n")

    def _show_context_menu(self, position) -> None:
        index = self.tree.indexAt(position)
        item = self.model.itemFromIndex(index) if index.isValid() else None
        target = item if item is not None else self.root_item

        menu = QMenu(self)
        action_new_dir = menu.addAction("New Folder")
        action_new_file = menu.addAction("New File")
        action_delete = menu.addAction("Delete")

        action = menu.exec_(self.tree.viewport().mapToGlobal(position))
        if action is None:
            return

        if action == action_new_dir:
            name, ok = QInputDialog.getText(self, "New Folder", "Folder name:")
            if ok and name:
                path = self._build_path(target)
                full_path = self._join_path(path, name)
                self.command_requested.emit(f"mkdir {full_path}\n")
        elif action == action_new_file:
            name, ok = QInputDialog.getText(self, "New File", "File name:")
            if ok and name:
                path = self._build_path(target)
                full_path = self._join_path(path, name)
                self.command_requested.emit(f"create {full_path}\n")
        elif action == action_delete and item is not None:
            path = self._build_path(item)
            if item.data(Qt.UserRole) == "dir":
                self.command_requested.emit(f"rmdir {path}\n")
            else:
                self.command_requested.emit(f"delete {path}\n")

    def _ensure_path(self, path: str, is_dir: bool) -> None:
        parts = [p for p in path.split("/") if p]
        current = self.root_item

        for idx, part in enumerate(parts):
            child = self._find_child(current, part)
            if child is None:
                child = QStandardItem(part)
                is_last = idx == len(parts) - 1
                node_type = "dir" if not is_last or is_dir else "file"
                child.setData(node_type, Qt.UserRole)
                current.appendRow(child)
            current = child

        self.tree.expandAll()

    def _remove_path(self, path: str) -> None:
        item = self._find_item(path)
        if item is None or item is self.root_item:
            return

        parent = item.parent() or self.root_item
        parent.removeRow(item.row())

    def _find_item(self, path: str) -> Optional[QStandardItem]:
        if path == "/":
            return self.root_item

        parts = [p for p in path.split("/") if p]
        current = self.root_item
        for part in parts:
            current = self._find_child(current, part)
            if current is None:
                return None
        return current

    @staticmethod
    def _find_child(parent: QStandardItem, name: str) -> Optional[QStandardItem]:
        for row in range(parent.rowCount()):
            child = parent.child(row)
            if child.text() == name:
                return child
        return None

    def _build_path(self, item: QStandardItem) -> str:
        parts = []
        current = item
        while current is not None and current is not self.root_item:
            parts.append(current.text())
            current = current.parent()
        parts.reverse()
        return "/" + "/".join(parts)

    @staticmethod
    def _join_path(base: str, name: str) -> str:
        if base.endswith("/"):
            return base + name
        if base == "/":
            return "/" + name
        return base + "/" + name

    @staticmethod
    def _clear_children(item: Optional[QStandardItem]) -> None:
        if item is None:
            return
        item.removeRows(0, item.rowCount())

    @staticmethod
    def _is_delete_command(cmd: str) -> bool:
        return cmd.startswith("delete ") or cmd.startswith("rm ") or cmd.startswith("rmdir ")

    @staticmethod
    def _is_mkdir_command(cmd: str) -> bool:
        return cmd.startswith("mkdir ")
