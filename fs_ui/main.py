"""MYFS Visual Terminal - OS Course Design GUI.

Dependency: pip install PyQt5
Run: python main.py

Disk image: disk.img is located in the same directory as this script
(fs_ui/disk.img). The C++ backend uses the relative path "disk.img",
which resolves correctly because the subprocess inherits the Python
working directory.
"""
from __future__ import annotations

import sys
import os
from pathlib import Path

from PyQt5.QtWidgets import QApplication

from core.fs_process import FSProcess
from core.sync_parser import SyncParser
from ui.main_window import MainWindow


def main() -> None:
    # Change working directory to fs_ui/ so that the C++ subprocess
    # resolves its relative "disk.img" path to the same file.
    os.chdir(str(Path(__file__).resolve().parent))

    app = QApplication(sys.argv)
    app.setStyle("Fusion")

    fs_process = FSProcess()
    sync_parser = SyncParser()
    main_window = MainWindow()

    fs_process.line_received.connect(sync_parser.parse_line)
    fs_process.process_exited.connect(main_window.show_process_exited)
    fs_process.process_error.connect(main_window.show_process_error)

    sync_parser.plain_output.connect(main_window.handle_plain_output)
    sync_parser.block_allocated.connect(main_window.heatmap.allocate_block)
    sync_parser.block_freed.connect(main_window.heatmap.free_block)
    sync_parser.tree_updated.connect(main_window.tree.update_tree)
    sync_parser.dir_changed.connect(main_window.set_current_path)
    sync_parser.group_event.connect(main_window.handle_group_event)
    sync_parser.group_loaded.connect(main_window.heatmap.group_loaded)
    sync_parser.group_stored.connect(main_window.heatmap.group_stored)
    sync_parser.group_items.connect(main_window.heatmap.group_items_received)
    sync_parser.cache_lru.connect(main_window.heatmap.cache_lru_received)
    sync_parser.super_info.connect(main_window.heatmap.configure_layout)
    sync_parser.inode_info.connect(main_window.inode_info_received)
    sync_parser.used_blocks.connect(main_window.heatmap.used_blocks_received)
    sync_parser.inode_indirect1.connect(main_window.indirect1_received)
    sync_parser.inode_indirect2.connect(main_window.indirect2_received)

    main_window.command_sent.connect(fs_process.send_command)

    fs_process.start()

    main_window.show()

    exit_code = app.exec_()

    fs_process.terminate()
    sys.exit(exit_code)


if __name__ == "__main__":
    main()
