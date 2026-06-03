"""Terminal panel for output and command input.

Dependency: pip install PyQt5
"""
from __future__ import annotations

import html
from typing import List

from PyQt5.QtCore import Qt, pyqtSignal
from PyQt5.QtWidgets import (
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QPushButton,
    QTextEdit,
    QVBoxLayout,
    QWidget,
)


class CommandLineEdit(QLineEdit):
    """Line edit with command history navigation."""

    history_up = pyqtSignal()
    history_down = pyqtSignal()

    def keyPressEvent(self, event) -> None:
        if event.key() == Qt.Key_Up:
            self.history_up.emit()
            return
        if event.key() == Qt.Key_Down:
            self.history_down.emit()
            return
        super().keyPressEvent(event)


class TerminalPanel(QWidget):
    """Terminal output and input widgets."""

    command_sent = pyqtSignal(str)

    def __init__(self) -> None:
        super().__init__()
        self._history: List[str] = []
        self._history_index = -1
        self._current_path = "/"

        self.output = QTextEdit()
        self.output.setReadOnly(True)
        self.output.setStyleSheet(
            "background-color: #f8fafc; color: #111111; font-family: monospace; font-weight: bold;"
        )

        self.prompt_label = QLabel("[/]$")
        self.input = CommandLineEdit()
        self.send_button = QPushButton("Send")

        self.input.returnPressed.connect(self.send_command)
        self.send_button.clicked.connect(self.send_command)
        self.input.history_up.connect(self._history_prev)
        self.input.history_down.connect(self._history_next)

        input_row = QHBoxLayout()
        input_row.addWidget(self.prompt_label)
        input_row.addWidget(self.input)
        input_row.addWidget(self.send_button)

        layout = QVBoxLayout()
        layout.addWidget(self.output)
        layout.addLayout(input_row)

        self.setLayout(layout)
        self._update_prompt()

    def set_current_path(self, path: str) -> None:
        self._current_path = path if path else "/"
        self._update_prompt()

    def append_output(self, text: str) -> None:
        """Append a line of output with basic coloring."""
        safe_text = html.escape(text)
        color = "#111827"  # Dark gray/black for better visibility
        lower = text.lower()

        if "[err]" in lower or "error" in lower or "failed" in lower:
            color = "#ef4444"
        elif "[ok]" in lower or "[pass]" in lower:
            color = "#22c55e"

        self.output.append(f"<span style='color:{color}'>%s</span>" % safe_text)
        self.output.moveCursor(self.output.textCursor().End)

    def send_command(self) -> None:
        """Send the command in the input field."""
        text = self.input.text().strip()
        if not text:
            return

        self._history.append(text)
        self._history_index = len(self._history)
        self.input.clear()

        self.append_output(f"{self.prompt_label.text()} {text}")
        self.command_sent.emit(text + "\n")

    def _history_prev(self) -> None:
        if not self._history:
            return
        self._history_index = max(0, self._history_index - 1)
        self.input.setText(self._history[self._history_index])

    def _history_next(self) -> None:
        if not self._history:
            return
        self._history_index = min(len(self._history), self._history_index + 1)
        if self._history_index == len(self._history):
            self.input.clear()
        else:
            self.input.setText(self._history[self._history_index])

    def _update_prompt(self) -> None:
        self.prompt_label.setText(f"[{self._current_path}]$")
