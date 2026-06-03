"""Subprocess manager for the MYFS core.

Dependency: pip install PyQt5
"""
from __future__ import annotations

import subprocess
import threading
from typing import Optional

from PyQt5.QtCore import QObject, pyqtSignal

from config import FS_CORE_PATH


class FSProcess(QObject):
    """Launch and communicate with the MYFS core process."""

    line_received = pyqtSignal(str)
    process_exited = pyqtSignal(int)
    process_error = pyqtSignal(str)

    def __init__(self) -> None:
        super().__init__()
        self.process: Optional[subprocess.Popen] = None
        self._read_thread: Optional[threading.Thread] = None
        self._running = False

    def start(self) -> None:
        """Start the fs_core process and the read loop."""
        if self.process is not None:
            return

        try:
            self.process = subprocess.Popen(
                [FS_CORE_PATH],
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                encoding="utf-8",
                errors="replace",
                bufsize=1,
            )
        except FileNotFoundError:
            self.process_error.emit(f"Executable not found: {FS_CORE_PATH}")
            self.process = None
            return
        except OSError as exc:
            self.process_error.emit(f"Failed to start process: {exc}")
            self.process = None
            return

        self._running = True
        self._read_thread = threading.Thread(target=self._read_loop, daemon=True)
        self._read_thread.start()

    def send_command(self, cmd: str) -> None:
        """Send a command to the subprocess."""
        if self.process is None or self.process.stdin is None:
            self.process_error.emit("Process not running")
            return

        try:
            self.process.stdin.write(cmd)
            self.process.stdin.flush()
        except OSError as exc:
            self.process_error.emit(f"Failed to send command: {exc}")

    def _read_loop(self) -> None:
        """Read stdout in a background thread and emit signals."""
        assert self.process is not None
        assert self.process.stdout is not None

        while True:
            line = self.process.stdout.readline()
            if line == "":
                if self.process.poll() is not None:
                    break
                continue
            self.line_received.emit(line.rstrip("\n"))

        exit_code = self.process.poll()
        if exit_code is None:
            exit_code = 0
        self.process_exited.emit(exit_code)

    def terminate(self) -> None:
        """Terminate the subprocess."""
        if self.process is None:
            return

        try:
            if self.process.stdin is not None:
                self.process.stdin.write("exit\n")
                self.process.stdin.flush()
        except OSError:
            pass

        try:
            self.process.wait(timeout=60.0)
        except subprocess.TimeoutExpired:
            self.process.kill()
            try:
                self.process.wait(timeout=2.0)
            except subprocess.TimeoutExpired:
                pass

        self._running = False
        self.process = None

    def restart(self) -> None:
        """Restart the subprocess (e.g. after a simulated crash)."""
        self.terminate()
        self.start()
