"""Main window layout.

Dependency: pip install PyQt5
"""
from __future__ import annotations

from typing import Optional

from PyQt5.QtCore import Qt, pyqtSignal
from PyQt5.QtWidgets import (
    QMainWindow,
    QMessageBox,
    QSplitter,
    QToolBar,
    QVBoxLayout,
    QWidget,
    QAction,
    QInputDialog,
    QDockWidget,
    QTreeWidget,
    QTreeWidgetItem,
)

from core.fs_commands import FSCommands
from ui.heatmap_panel import HeatmapPanel
from ui.terminal_panel import TerminalPanel
from ui.tree_panel import TreePanel


class MainWindow(QMainWindow):
    """Main application window."""

    command_sent = pyqtSignal(str)

    def __init__(self) -> None:
        super().__init__()
        self.setWindowTitle("MYFS Visual Terminal - OS Course Design")
        self.resize(1200, 800)
        self.setMinimumSize(900, 600)

        self.tree = TreePanel()
        self.heatmap = HeatmapPanel()
        self.terminal = TerminalPanel()

        # Store latest indirect block info for the current inode being displayed
        self._indirect1_info: Optional[tuple] = None  # (inode_id, ptr_block, [blocks])
        self._indirect2_info: Optional[tuple] = None  # (inode_id, ptr_block, [l1_blocks...])

        self.tree.command_requested.connect(self._on_command_requested)
        self.terminal.command_sent.connect(self._on_command_requested)
        self.heatmap.range_requested.connect(self._on_heatmap_range_requested)

        splitter = QSplitter(Qt.Horizontal)
        splitter.addWidget(self.tree)
        splitter.addWidget(self.heatmap)
        splitter.setStretchFactor(0, 2)
        splitter.setStretchFactor(1, 5)
        self.tree.setMinimumWidth(260)
        splitter.setChildrenCollapsible(False)
        splitter.setSizes([320, 880])

        layout = QVBoxLayout()
        layout.addWidget(splitter)
        layout.addWidget(self.terminal)

        container = QWidget()
        container.setLayout(layout)
        self.setCentralWidget(container)

        self._create_toolbar()
        self._create_demo_toolbar()
        self._create_inode_dock()

    def _create_inode_dock(self) -> None:
        self._inode_tree = QTreeWidget()
        self._inode_tree.setHeaderLabels(["Inode Index Tree"])
        self._inode_tree.setMinimumHeight(220)

        dock = QDockWidget("Inode Tree", self)
        dock.setObjectName("inode_tree_dock")
        dock.setWidget(self._inode_tree)
        dock.setAllowedAreas(Qt.RightDockWidgetArea | Qt.BottomDockWidgetArea)
        self.addDockWidget(Qt.RightDockWidgetArea, dock)
        dock.hide()

        self._inode_dock = dock

    def inode_info_received(self, info: object) -> None:
        if not isinstance(info, dict):
            return

        inode_id = info.get("inode_id")
        if inode_id is None:
            return

        self._inode_tree.clear()

        root = QTreeWidgetItem([f"inode {inode_id}"])
        size = info.get("size")
        block_count = info.get("block_count")
        link_count = info.get("link_count")
        open_count = info.get("open_count")
        
        if link_count is not None:
            root.addChild(QTreeWidgetItem([f"link_count = {link_count}"]))
        if open_count is not None:
            root.addChild(QTreeWidgetItem([f"open_count = {open_count}"]))
        if size is not None:
            root.addChild(QTreeWidgetItem([f"size = {size} bytes"]))
        if block_count is not None:
            root.addChild(QTreeWidgetItem([f"block_count = {block_count}"]))

        direct = info.get("direct") or []
        direct_node = QTreeWidgetItem([f"direct[{len(direct)}]"])
        for idx, blk in enumerate(direct):
            label = f"direct[{idx:02d}] -> {blk}" if blk else f"direct[{idx:02d}] -> HOLE"
            direct_node.addChild(QTreeWidgetItem([label]))
        root.addChild(direct_node)

        # indirect1 — show as expandable node with child blocks
        indirect1 = info.get("indirect1", 0)
        ind1_node = QTreeWidgetItem(["indirect1"])
        if indirect1 is not None and indirect1 != 0:
            ind1_node.addChild(QTreeWidgetItem([f"pointer_block = {indirect1}"]))
            # Add child items from stored indirect1 sync info
            if self._indirect1_info and self._indirect1_info[0] == inode_id:
                _, _, blocks = self._indirect1_info
                for idx, blk in enumerate(blocks):
                    ind1_node.addChild(QTreeWidgetItem([f"  [{idx:03d}] -> {blk}"]))
        else:
            ind1_node.addChild(QTreeWidgetItem(["pointer_block = NONE"]))
        root.addChild(ind1_node)

        # indirect2 — show as expandable node with child blocks
        indirect2 = info.get("indirect2", 0)
        ind2_node = QTreeWidgetItem(["indirect2"])
        if indirect2 is not None and indirect2 != 0:
            ind2_node.addChild(QTreeWidgetItem([f"pointer_block = {indirect2}"]))
            # Add child items from stored indirect2 sync info
            if self._indirect2_info and self._indirect2_info[0] == inode_id:
                _, _, blocks = self._indirect2_info
                for blk_info in blocks:
                    # blocks can be single ints or lists [l1_block, data1, data2...]
                    if isinstance(blk_info, list):
                        l1_blk = blk_info[0]
                        child = QTreeWidgetItem([f"  L1[{l1_blk}]"])
                        for j, data_blk in enumerate(blk_info[1:], 1):
                            child.addChild(QTreeWidgetItem([f"    [{j-1:03d}] -> {data_blk}"]))
                        ind2_node.addChild(child)
                    else:
                        ind2_node.addChild(QTreeWidgetItem([f"  [{blk_info}]"]))
        else:
            ind2_node.addChild(QTreeWidgetItem(["pointer_block = NONE"]))
        root.addChild(ind2_node)

        self._inode_tree.addTopLevelItem(root)
        self._inode_tree.expandAll()
        self._inode_dock.show()

    def indirect1_received(self, inode_id: int, ptr_block: int, blocks: list) -> None:
        """Store indirect1 block info for later use in inode tree display."""
        self._indirect1_info = (inode_id, ptr_block, blocks)
        # If we're currently displaying this inode, update the tree
        if self._inode_tree.topLevelItemCount() > 0:
            top_item = self._inode_tree.topLevelItem(0)
            if top_item and top_item.text(0).startswith(f"inode {inode_id}"):
                self._update_indirect1_display(inode_id, ptr_block, blocks)

    def _update_indirect1_display(self, inode_id: int, ptr_block: int, blocks: list) -> None:
        """Update the indirect1 display in the current inode tree."""
        # Find the indirect1 node in the tree
        root = self._inode_tree.topLevelItem(0)
        if not root:
            return
        
        # Find indirect1 child (should be the 3rd child after direct blocks)
        for i in range(root.childCount()):
            child = root.child(i)
            if child.text(0) == "indirect1":
                # Clear existing children
                while child.childCount() > 0:
                    child.takeChild(0)
                
                # Add pointer block
                child.addChild(QTreeWidgetItem([f"pointer_block = {ptr_block}"]))
                
                # Add block entries
                for idx, blk in enumerate(blocks):
                    child.addChild(QTreeWidgetItem([f"  [{idx:03d}] -> {blk}"]))
                break

    def indirect2_received(self, inode_id: int, ptr_block: int, blocks: list) -> None:
        """Store indirect2 block info for later use in inode tree display."""
        self._indirect2_info = (inode_id, ptr_block, blocks)
        # If we're currently displaying this inode, update the tree
        if self._inode_tree.topLevelItemCount() > 0:
            top_item = self._inode_tree.topLevelItem(0)
            if top_item and top_item.text(0).startswith(f"inode {inode_id}"):
                self._update_indirect2_display(inode_id, ptr_block, blocks)

    def _update_indirect2_display(self, inode_id: int, ptr_block: int, blocks: list) -> None:
        """Update the indirect2 display in the current inode tree."""
        # Find the indirect2 node in the tree
        root = self._inode_tree.topLevelItem(0)
        if not root:
            return
        
        # Find indirect2 child
        for i in range(root.childCount()):
            child = root.child(i)
            if child.text(0) == "indirect2":
                # Clear existing children
                while child.childCount() > 0:
                    child.takeChild(0)
                
                # Add pointer block
                child.addChild(QTreeWidgetItem([f"pointer_block = {ptr_block}"]))
                
                # Add block entries
                for blk_info in blocks:
                    if isinstance(blk_info, list):
                        l1_blk = blk_info[0]
                        l1_node = QTreeWidgetItem([f"  L1[{l1_blk}]"])
                        for j, data_blk in enumerate(blk_info[1:], 1):
                            l1_node.addChild(QTreeWidgetItem([f"    [{j-1:03d}] -> {data_blk}"]))
                        child.addChild(l1_node)
                    else:
                        child.addChild(QTreeWidgetItem([f"  [{blk_info}]"]))
                break

    def _create_toolbar(self) -> None:
        toolbar = QToolBar("Main")
        self.addToolBar(toolbar)

        action_format = QAction("Format", self)
        action_login = QAction("Login", self)
        action_logout = QAction("Logout", self)
        action_dir = QAction("Dir", self)
        action_pwd = QAction("Pwd", self)
        action_mkdir = QAction("Mkdir", self)
        action_create = QAction("Create", self)
        action_delete = QAction("Delete", self)
        action_rmdir = QAction("Rmdir", self)
        action_cd = QAction("Cd", self)
        action_open = QAction("Open", self)
        action_close = QAction("Close", self)
        action_write = QAction("Write", self)
        action_read = QAction("Read", self)
        action_batch_mkdir = QAction("Batch Mkdir", self)
        action_batch_create = QAction("Batch Create", self)
        action_statfs = QAction("Statfs", self)
        action_super = QAction("Super", self)
        action_freegroup = QAction("Free Group", self)
        action_cache = QAction("Cache", self)
        action_fsck = QAction("FSCK", self)
        action_recover = QAction("Recover", self)
        action_bmap = QAction("Bmap", self)
        action_inode = QAction("Inode", self)
        
        action_chmod = QAction("Chmod", self)
        action_chown = QAction("Chown", self)
        action_useradd = QAction("UserAdd", self)
        action_userdel = QAction("UserDel", self)
        action_su = QAction("Su", self)
        action_whoami = QAction("Whoami", self)

        action_format.triggered.connect(lambda: self._send(FSCommands.format()))
        action_login.triggered.connect(self._prompt_login)
        action_logout.triggered.connect(lambda: self._send(FSCommands.logout()))
        action_dir.triggered.connect(self._refresh_dir)
        action_pwd.triggered.connect(lambda: self._send(FSCommands.pwd()))
        action_mkdir.triggered.connect(self._prompt_mkdir)
        action_create.triggered.connect(self._prompt_create)
        action_delete.triggered.connect(self._prompt_delete)
        action_rmdir.triggered.connect(self._prompt_rmdir)
        action_cd.triggered.connect(self._prompt_cd)
        action_open.triggered.connect(self._prompt_open)
        action_close.triggered.connect(self._prompt_close)
        action_write.triggered.connect(self._prompt_write)
        action_read.triggered.connect(self._prompt_read)
        action_batch_mkdir.triggered.connect(self._prompt_batch_mkdir)
        action_batch_create.triggered.connect(self._prompt_batch_create)
        action_statfs.triggered.connect(lambda: self._send(FSCommands.statfs()))
        action_super.triggered.connect(lambda: self._send(FSCommands.super()))
        action_freegroup.triggered.connect(lambda: self._send(FSCommands.free_group()))
        action_cache.triggered.connect(lambda: self._send(FSCommands.cache()))
        action_fsck.triggered.connect(lambda: self._send(FSCommands.fsck()))
        action_recover.triggered.connect(lambda: self._send("recover\n"))
        action_bmap.triggered.connect(self._prompt_bmap)
        action_inode.triggered.connect(self._prompt_inode)
        action_chmod.triggered.connect(self._prompt_chmod)
        action_chown.triggered.connect(self._prompt_chown)
        action_useradd.triggered.connect(self._prompt_useradd)
        action_userdel.triggered.connect(self._prompt_userdel)
        action_su.triggered.connect(self._prompt_su)
        action_whoami.triggered.connect(lambda: self._send(FSCommands.whoami()))

        toolbar.addAction(action_format)
        toolbar.addAction(action_login)
        toolbar.addAction(action_logout)
        toolbar.addAction(action_dir)
        toolbar.addAction(action_pwd)
        toolbar.addAction(action_mkdir)
        toolbar.addAction(action_create)
        toolbar.addAction(action_delete)
        toolbar.addAction(action_rmdir)
        toolbar.addAction(action_cd)
        toolbar.addAction(action_open)
        toolbar.addAction(action_close)
        toolbar.addAction(action_write)
        toolbar.addAction(action_read)
        toolbar.addAction(action_batch_mkdir)
        toolbar.addAction(action_batch_create)
        toolbar.addAction(action_statfs)
        toolbar.addAction(action_super)
        toolbar.addAction(action_freegroup)
        toolbar.addAction(action_cache)
        toolbar.addAction(action_fsck)
        toolbar.addAction(action_recover)
        toolbar.addAction(action_bmap)
        toolbar.addAction(action_inode)
        
        users_toolbar = QToolBar("Users")
        self.addToolBar(users_toolbar)
        users_toolbar.addAction(action_chmod)
        users_toolbar.addAction(action_chown)
        users_toolbar.addAction(action_useradd)
        users_toolbar.addAction(action_userdel)
        users_toolbar.addAction(action_su)
        users_toolbar.addAction(action_whoami)

    def _create_demo_toolbar(self) -> None:
        toolbar = QToolBar("Algorithm Demos")
        self.addToolBar(toolbar)

        action_sync_super = QAction("Sync Superblock", self)
        action_sync_super.setToolTip("Auto read superblock and sync datablocks")
        action_demo_alloc = QAction("Demo: Auto Batch Create (Alloc 150 blocks)", self)
        action_demo_alloc.setToolTip("Batch create files to trigger Group Linked stack loading/popping")
        action_demo_free = QAction("Demo: Auto Batch Delete (Free 150 blocks)", self)
        action_demo_free.setToolTip("Batch delete files to trigger Group Linked stack pushing/storing")
        action_crash = QAction("Simulate Crash", self)
        action_crash.setToolTip("Kill without umount — simulates crash; next mount triggers journal recovery")

        action_sync_super.triggered.connect(lambda: self._send(FSCommands.statfs()))

        # Batch create: parent=/, prefix=demo_alloc_, count=15, size=40960 (10 blocks per file) -> 150 blocks total
        action_demo_alloc.triggered.connect(lambda: self._send("batch_create / demo_alloc_ 15 40960\n"))
        # Batch delete: parent=/, prefix=demo_alloc_, count=15
        action_demo_free.triggered.connect(lambda: self._send("batch_delete / demo_alloc_ 15\n"))
        action_crash.triggered.connect(lambda: self._send(FSCommands.crash()))

        toolbar.addAction(action_sync_super)
        toolbar.addAction(action_demo_alloc)
        toolbar.addAction(action_demo_free)
        toolbar.addAction(action_crash)

    def _prompt_login(self) -> None:
        username, ok = QInputDialog.getText(self, "Login", "Username:")
        if not ok or not username:
            return
        password, ok = QInputDialog.getText(self, "Login", "Password:")
        if not ok or not password:
            return
        self._send(FSCommands.login(username, password))

    def _refresh_dir(self) -> None:
        self.tree.refresh_from_fs()

    def _prompt_mkdir(self) -> None:
        path, ok = QInputDialog.getText(self, "mkdir", "Path:")
        if ok and path:
            self._send(FSCommands.mkdir(path))

    def _prompt_create(self) -> None:
        path, ok = QInputDialog.getText(self, "create", "Path:")
        if ok and path:
            self._send(FSCommands.create(path))

    def _prompt_delete(self) -> None:
        path, ok = QInputDialog.getText(self, "delete", "Path:")
        if ok and path:
            self._send(FSCommands.delete(path))

    def _prompt_rmdir(self) -> None:
        path, ok = QInputDialog.getText(self, "rmdir", "Path:")
        if ok and path:
            self._send(f"rmdir {path}\n")

    def _prompt_cd(self) -> None:
        path, ok = QInputDialog.getText(self, "cd", "Path:")
        if ok and path:
            self._send(FSCommands.cd(path))

    def _prompt_open(self) -> None:
        text, ok = QInputDialog.getText(self, "open", "Path and mode (r/w/a/rw/w+/a+):")
        if not ok or not text:
            return
        parts = text.split(maxsplit=1)
        if len(parts) != 2:
            QMessageBox.warning(self, "open", "Usage: <path> <mode>")
            return
        self._send(FSCommands.open_file(parts[0], parts[1]))

    def _prompt_close(self) -> None:
        fd, ok = QInputDialog.getInt(self, "close", "File descriptor:", min=0)
        if ok:
            self._send(FSCommands.close(fd))

    def _prompt_write(self) -> None:
        text, ok = QInputDialog.getText(self, "write", "fd and data (e.g. 1 hello | 1 -s 4096 | 1 -t hello):")
        if not ok or not text:
            return
        parts = text.split(maxsplit=1)
        if len(parts) != 2:
            QMessageBox.warning(self, "write", "Usage: <fd> <text>")
            return
        self._send(f"write {parts[0]} {parts[1]}\n")

    def _prompt_read(self) -> None:
        text, ok = QInputDialog.getText(self, "read", "fd and size (e.g. 1 64):")
        if not ok or not text:
            return
        parts = text.split(maxsplit=1)
        if len(parts) != 2:
            QMessageBox.warning(self, "read", "Usage: <fd> <size>")
            return
        self._send(FSCommands.read(int(parts[0]), int(parts[1])))

    def _prompt_batch_mkdir(self) -> None:
        default_path = self.tree.get_current_path()
        base, ok = QInputDialog.getText(self, "batch mkdir", "Parent path:", text=default_path)
        if not ok or not base:
            return
        prefix, ok = QInputDialog.getText(self, "batch mkdir", "Name prefix:")
        if not ok or not prefix:
            return
        count, ok = QInputDialog.getInt(self, "batch mkdir", "Count:", value=10, min=1)
        if not ok:
            return
        self._send(FSCommands.batch_mkdir(base, prefix, count))

    def _prompt_batch_create(self) -> None:
        default_path = self.tree.get_current_path()
        base, ok = QInputDialog.getText(self, "batch create", "Parent path:", text=default_path)
        if not ok or not base:
            return
        prefix, ok = QInputDialog.getText(self, "batch create", "Name prefix:")
        if not ok or not prefix:
            return
        count, ok = QInputDialog.getInt(self, "batch create", "Count:", value=10, min=1)
        if not ok:
            return
        size, ok = QInputDialog.getInt(self, "batch create", "Write size per file (bytes):", value=0, min=0)
        if not ok:
            return
        self._send(FSCommands.batch_create(base, prefix, count, size))

    def _prompt_bmap(self) -> None:
        inode_id, ok = QInputDialog.getInt(self, "bmap", "Inode id:", value=0, min=0)
        if not ok:
            return
        logical_block, ok = QInputDialog.getInt(self, "bmap", "Logical block:", value=0, min=0)
        if not ok:
            return
        self._send(FSCommands.bmap(inode_id, logical_block))

    def _prompt_inode(self) -> None:
        inode_id, ok = QInputDialog.getInt(self, "inode", "Inode id:", value=0, min=0)
        if not ok:
            return
        self._send(FSCommands.inode(inode_id))

    def _prompt_chmod(self) -> None:
        text, ok = QInputDialog.getText(self, "chmod", "Path and mode (octal, e.g., /file 0755):")
        if ok and text:
            parts = text.split()
            if len(parts) == 2:
                self._send(FSCommands.chmod(parts[0], parts[1]))

    def _prompt_chown(self) -> None:
        text, ok = QInputDialog.getText(self, "chown", "Path, uid, gid (e.g., /file 1000 1000):")
        if ok and text:
            parts = text.split()
            if len(parts) == 3:
                try:
                    self._send(FSCommands.chown(parts[0], int(parts[1]), int(parts[2])))
                except ValueError:
                    pass

    def _prompt_useradd(self) -> None:
        text, ok = QInputDialog.getText(self, "useradd", "Username, password, uid, gid:")
        if ok and text:
            parts = text.split()
            if len(parts) == 4:
                try:
                    self._send(FSCommands.useradd(parts[0], parts[1], int(parts[2]), int(parts[3])))
                except ValueError:
                    pass

    def _prompt_userdel(self) -> None:
        username, ok = QInputDialog.getText(self, "userdel", "Username:")
        if ok and username:
            self._send(FSCommands.userdel(username))

    def _prompt_su(self) -> None:
        username, ok = QInputDialog.getText(self, "su", "Username:")
        if not ok or not username:
            return
        password, ok = QInputDialog.getText(self, "su", "Password:")
        if not ok or not password:
            return
        self._send(FSCommands.su(username, password))

    def _on_command_requested(self, cmd: str) -> None:
        if cmd.strip() == "format":
            self.tree.reset()
            self.heatmap.reset()
            self.set_current_path("/")
        self.tree.set_last_command(cmd)
        self.command_sent.emit(cmd)

    def _on_heatmap_range_requested(self, start_block: int, count: int) -> None:
        self.command_sent.emit(FSCommands.blockmap(start_block, count))

    def _send(self, cmd: str) -> None:
        self.command_sent.emit(cmd)

    def handle_plain_output(self, line: str) -> None:
        self.terminal.append_output(line)
        self.tree.consume_dir_line(line)

    def handle_group_event(self, message: str) -> None:
        self.heatmap.log_event(message)

    def set_current_path(self, path: str) -> None:
        self.tree.set_current_path(path)
        self.tree.ensure_dir_path(path)
        self.terminal.set_current_path(path)
        self.tree.refresh_from_fs()

    def show_process_error(self, message: str) -> None:
        QMessageBox.critical(self, "Process Error", message)

    def show_process_exited(self, code: int) -> None:
        QMessageBox.warning(self, "Process Exited", f"Process exited with code {code}")

    def closeEvent(self, event) -> None:
        self.command_sent.emit(FSCommands.exit())
        super().closeEvent(event)
