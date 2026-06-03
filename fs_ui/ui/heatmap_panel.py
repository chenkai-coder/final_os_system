"""Disk heatmap panel.

Dependency: pip install PyQt5
"""
from __future__ import annotations

from typing import List, Optional

from PyQt5.QtCore import Qt, QTimer, pyqtSignal
from PyQt5.QtWidgets import (
    QLabel,
    QGridLayout,
    QHBoxLayout,
    QVBoxLayout,
    QWidget,
    QTextEdit,
    QPushButton,
    QComboBox,
    QSizePolicy,
)

from config import (
    COLOR_ALLOCATED,
    COLOR_FREE,
    COLOR_METADATA,
    COLOR_SELECTED,
    DATA_BLOCK_COUNT,
    DATA_START_BLOCK,
    HEATMAP_COLS,
    HEATMAP_PAGE_STEP,
    FREE_GROUP_SIZE,
    GROUP_STACK_VISIBLE,
    GROUP_ANIMATION_MS,
)


class GroupStackPanel(QWidget):
    """Visualize the free-group stack for grouped linked allocation."""

    def __init__(
        self,
        max_group_size: int,
        visible_count: int,
        animation_ms: int,
    ) -> None:
        super().__init__()
        self._max_group_size = max(1, max_group_size)
        self._visible_count = max(4, visible_count)
        self._animation_ms = max(60, animation_ms)
        self._group_count = self._max_group_size
        self._head_block: Optional[int] = None
        self._stack_values: List[str] = ["?"] * self._group_count
        self._skip_next_alloc_block: Optional[int] = None
        self._skip_next_free_block: Optional[int] = None

        self._title = QLabel("Free Group Stack")
        self._stats_label = QLabel("")
        self._head_label = QLabel("")
        self._op_label = QLabel("")

        self._item_labels: List[QLabel] = []
        stack_layout = QVBoxLayout()
        stack_layout.setSpacing(3)

        for _ in range(self._visible_count):
            label = QLabel("")
            label.setFixedHeight(18)
            label.setAlignment(Qt.AlignCenter)
            label.setStyleSheet(self._stack_style("#1f2937"))
            self._item_labels.append(label)
            stack_layout.addWidget(label)

        self._overflow_label = QLabel("")
        self._overflow_label.setAlignment(Qt.AlignCenter)
        stack_layout.addWidget(self._overflow_label)

        layout = QVBoxLayout()
        layout.addWidget(self._title)
        layout.addWidget(self._stats_label)
        layout.addWidget(self._head_label)
        layout.addWidget(self._op_label)
        layout.addLayout(stack_layout)
        layout.addStretch(1)

        self.setLayout(layout)
        self._render_stack()

    def reset(self) -> None:
        self._group_count = self._max_group_size
        self._head_block = None
        self._stack_values = ["?"] * self._group_count
        self._skip_next_alloc_block = None
        self._skip_next_free_block = None
        self._set_op("RESET: group stack initialized")
        self._render_stack()

    def configure(self, max_group_size: int) -> None:
        self._max_group_size = max(1, max_group_size)
        if self._group_count > self._max_group_size:
            self._group_count = self._max_group_size
        self._sync_stack_length()
        self._render_stack()

    def group_loaded(self, count: int, head_block: int) -> None:
        self._group_count = max(0, min(count, self._max_group_size))
        self._head_block = head_block
        self._stack_values = ["?"] * self._group_count
        self._skip_next_alloc_block = head_block
        self._set_op(f"LOAD: head block {head_block} -> {count} slots")
        self._render_stack()
        self._flash_head("#38bdf8")
        self._animate_fill()

    def group_stored(self, count: int, head_block: int) -> None:
        self._group_count = 1
        self._head_block = head_block
        self._stack_values = [str(head_block)]
        self._skip_next_free_block = head_block
        self._set_op(f"STORE: {count} slots -> head block {head_block}")
        self._render_stack()
        self._flash_head("#f59e0b")

    def set_group_items(self, items: List[int]) -> None:
        if not items:
            return
        self._group_count = len(items)
        self._stack_values = [str(x) for x in items]
        if len(items) > 1:
            self._head_block = items[0]  # Or whatever the logical head is
        self._sync_stack_length()
        self._render_stack()

    def on_alloc(self, block_id: int) -> None:
        if self._skip_next_alloc_block == block_id:
            self._skip_next_alloc_block = None
            self._set_op(f"ALLOC: head block {block_id}")
            self._flash_head("#f97316")
            return

        if self._group_count <= 0:
            self._set_op(f"ALLOC: block {block_id}")
            return

        self._group_count -= 1
        self._sync_stack_length()

        if self._stack_values:
            self._stack_values[0] = str(block_id)
            self._set_op(f"POP: block {block_id}")
            self._render_stack()
            self._flash_label(self._item_labels[0], "#f97316")
            QTimer.singleShot(self._animation_ms, self._pop_top)
            return

        self._set_op(f"POP: block {block_id}")
        self._render_stack()

    def on_free(self, block_id: int) -> None:
        if self._skip_next_free_block == block_id:
            self._skip_next_free_block = None
            self._set_op(f"FREE: head block {block_id}")
            return

        if self._group_count >= self._max_group_size:
            self._set_op(f"FREE: block {block_id} (group full)")
            return

        self._group_count += 1
        self._stack_values.insert(0, str(block_id))
        self._sync_stack_length()
        self._set_op(f"PUSH: block {block_id}")
        self._render_stack()
        self._flash_label(self._item_labels[0], "#22c55e")

    def _set_op(self, text: str) -> None:
        self._op_label.setText(text)

    def _sync_stack_length(self) -> None:
        if self._group_count < len(self._stack_values):
            self._stack_values = self._stack_values[: self._group_count]
        elif self._group_count > len(self._stack_values):
            missing = self._group_count - len(self._stack_values)
            self._stack_values.extend(["?"] * missing)

    def _pop_top(self) -> None:
        if self._stack_values:
            self._stack_values.pop(0)
        self._render_stack()

    def _render_stack(self) -> None:
        self._sync_stack_length()

        self._stats_label.setText(
            "Group size: {} / {}".format(self._group_count, self._max_group_size)
        )

        if self._head_block is None:
            self._head_label.setText("Head pointer: -")
        elif self._group_count == 1:
            self._head_label.setText("Head pointer: {}".format(self._head_block))
        else:
            self._head_label.setText("Last head: {}".format(self._head_block))

        display = self._stack_values[: self._visible_count]
        for idx, label in enumerate(self._item_labels):
            if idx < len(display):
                if idx == 0 and self._group_count == 1:
                    prefix = "HEAD"
                elif idx == 0:
                    prefix = "TOP"
                else:
                    prefix = "{:02d}".format(idx + 1)
                label.setText("{} | {}".format(prefix, display[idx]))
            else:
                label.setText("")
            label.setStyleSheet(self._stack_style("#1f2937"))

        extra = self._group_count - self._visible_count
        self._overflow_label.setText("+{} more".format(extra) if extra > 0 else "")

    def _animate_fill(self) -> None:
        steps = min(self._group_count, self._visible_count)
        for idx in range(steps):
            delay = self._animation_ms * idx
            QTimer.singleShot(delay, lambda i=idx: self._flash_label(self._item_labels[i], "#38bdf8"))

    def _flash_head(self, color: str) -> None:
        self._flash_label(self._head_label, color)

    def _flash_label(self, label: QLabel, color: str) -> None:
        base = label.styleSheet()
        label.setStyleSheet(self._stack_style(color))
        QTimer.singleShot(self._animation_ms, lambda: label.setStyleSheet(base))

    @staticmethod
    def _stack_style(color: str) -> str:
        return (
            "background-color: {} ;"
            "border: 1px solid #111111;"
            "color: #e2e8f0;"
        ).format(color)


class CachePanel(QWidget):
    """Visualize the LRU block cache."""

    def __init__(self) -> None:
        super().__init__()
        self._title = QLabel("LRU Cache (Block IDs)")
        self._stats_label = QLabel("Cache Items: 0")
        
        self._items: List[QLabel] = []
        stack_layout = QVBoxLayout()
        stack_layout.setSpacing(3)
        
        # We display top 8 items from LRU
        self._visible_count = 8
        
        for _ in range(self._visible_count):
            label = QLabel("")
            label.setFixedHeight(18)
            label.setAlignment(Qt.AlignCenter)
            label.setStyleSheet(self._stack_style("#1f2937"))
            self._items.append(label)
            stack_layout.addWidget(label)
            
        self._overflow_label = QLabel("")
        self._overflow_label.setAlignment(Qt.AlignCenter)
        stack_layout.addWidget(self._overflow_label)

        layout = QVBoxLayout()
        layout.addWidget(self._title)
        layout.addWidget(self._stats_label)
        layout.addLayout(stack_layout)
        layout.addStretch(1)

        self.setLayout(layout)

    def update_lru(self, lru_blocks: List[int]) -> None:
        self._stats_label.setText(f"Cache Items: {len(lru_blocks)}")
        
        display = lru_blocks[:self._visible_count]
        for idx, label in enumerate(self._items):
            if idx < len(display):
                prefix = "LRU HEAD" if idx == 0 else f"{idx + 1:02d}"
                label.setText(f"{prefix} | Block {display[idx]}")
                color = "#3b82f6" if idx == 0 else "#1f2937"
                label.setStyleSheet(self._stack_style(color))
            else:
                label.setText("")
                label.setStyleSheet(self._stack_style("#1f2937"))
                
        extra = len(lru_blocks) - self._visible_count
        self._overflow_label.setText(f"+{extra} more" if extra > 0 else "")

    @staticmethod
    def _stack_style(color: str) -> str:
        return (
            "background-color: {} ;"
            "border: 1px solid #111111;"
            "color: #e2e8f0;"
        ).format(color)


class HeatmapPanel(QWidget):
    """Render a 32x16 block heatmap for data blocks."""

    range_requested = pyqtSignal(int, int)

    def __init__(self) -> None:
        super().__init__()
        self.cells: List[QLabel] = []
        self.data_block_start = DATA_START_BLOCK
        self.data_block_total = DATA_BLOCK_COUNT
        self.visible_count = DATA_BLOCK_COUNT
        self.window_start = 0
        self.allocated_count = 0
        self.block_states: List[bool] = [False] * self.data_block_total

        self.trace = QTextEdit()
        self.trace.setReadOnly(True)
        self.trace.setPlaceholderText("Allocation trace...")
        self.trace.setFixedHeight(140)

        self.stats_label = QLabel("Allocated: 0 / {} (0.00%)".format(self.data_block_total))
        self.stats_label.setAlignment(Qt.AlignLeft | Qt.AlignVCenter)

        self.range_label = QLabel("")
        
        self.page_combo = QComboBox()
        self.page_combo.currentIndexChanged.connect(self._on_page_selected)
        
        self.prev_button = QPushButton("Prev Page")
        self.next_button = QPushButton("Next Page")
        self.prev_button.clicked.connect(lambda: self._shift_page(-1))
        self.next_button.clicked.connect(lambda: self._shift_page(1))

        legend = QHBoxLayout()
        legend.addWidget(self._legend_item(COLOR_FREE, "Free"))
        legend.addWidget(self._legend_item(COLOR_ALLOCATED, "Allocated"))
        legend.addStretch(1)

        grid = QGridLayout()
        grid.setSpacing(1)

        for idx in range(self.visible_count):
            row = idx // HEATMAP_COLS
            col = idx % HEATMAP_COLS
            label = QLabel("")
            label.setFixedSize(16, 16)
            label.setStyleSheet(self._cell_style(COLOR_FREE))
            label.setToolTip(self._tooltip_text(idx))
            label.setAlignment(Qt.AlignCenter)
            grid.addWidget(label, row, col)
            self.cells.append(label)

        self.group_stack = GroupStackPanel(
            FREE_GROUP_SIZE,
            GROUP_STACK_VISIBLE,
            GROUP_ANIMATION_MS,
        )

        self.cache_panel = CachePanel()

        layout = QVBoxLayout()
        layout.addWidget(self.stats_label)
        layout.addLayout(legend)
        range_row = QHBoxLayout()
        range_row.addWidget(self.prev_button)
        range_row.addWidget(self.page_combo)
        range_row.addWidget(self.next_button)
        range_row.addWidget(self.range_label)
        range_row.addStretch(1)
        layout.addLayout(range_row)
        
        grid_row = QHBoxLayout()
        
        # Grid container to restrict expansion
        grid_container = QWidget()
        grid_container.setLayout(grid)
        grid_container.setSizePolicy(QSizePolicy.Fixed, QSizePolicy.Fixed)
        grid_row.addWidget(grid_container)
        
        side_panel_row = QHBoxLayout()
        self.group_stack.setMinimumWidth(220)
        self.cache_panel.setMinimumWidth(220)
        side_panel_row.addWidget(self.group_stack)
        side_panel_row.addWidget(self.cache_panel)
        side_panel_row.addStretch(1)
        
        grid_row.addLayout(side_panel_row)
        grid_row.addStretch(1)
        
        layout.addLayout(grid_row)
        layout.addWidget(self.trace)
        layout.addStretch(1)

        self.setLayout(layout)
        self._update_slider_range()
        self._refresh_visible_cells()

    def _legend_item(self, color: str, text: str) -> QWidget:
        container = QWidget()
        layout = QHBoxLayout(container)
        layout.setContentsMargins(0, 0, 0, 0)
        swatch = QLabel("")
        swatch.setFixedSize(14, 14)
        swatch.setStyleSheet(self._cell_style(color))
        label = QLabel(text)
        layout.addWidget(swatch)
        layout.addWidget(label)
        return container

    def _cell_style(self, color: str) -> str:
        return (
            "background-color: {} ;"
            "border: 1px solid #111111;"
        ).format(color)

    def _tooltip_text(self, visible_index: int) -> str:
        relative_index = self.window_start + visible_index
        if relative_index >= self.data_block_total:
            return "Out of range"
        absolute_index = self.data_block_start + relative_index
        state = "Free" if not self.block_states[relative_index] else "Allocated"
        return "Block #{} (relative #{}) [{}]".format(absolute_index, relative_index, state)

    def _update_cell(self, index: int) -> None:
        relative_index = self.window_start + index
        if relative_index >= self.data_block_total:
            self.cells[index].setStyleSheet(self._cell_style(COLOR_METADATA))
            self.cells[index].setToolTip("Out of range")
            return
        color = COLOR_ALLOCATED if self.block_states[relative_index] else COLOR_FREE
        self.cells[index].setStyleSheet(self._cell_style(color))
        self.cells[index].setToolTip(self._tooltip_text(index))

    def _update_slider_range(self) -> None:
        if self.data_block_total <= 0:
            return
            
        total_pages = (self.data_block_total + self.visible_count - 1) // self.visible_count
        
        self.page_combo.blockSignals(True)
        self.page_combo.clear()
        for i in range(total_pages):
            start = i * self.visible_count
            end = min(start + self.visible_count - 1, self.data_block_total - 1)
            self.page_combo.addItem(f"Page {i + 1} (Rel {start}-{end})", i)
            
        current_page = self.window_start // self.visible_count
        if current_page >= total_pages:
            current_page = total_pages - 1
            self.window_start = current_page * self.visible_count
            
        self.page_combo.setCurrentIndex(current_page)
        self.page_combo.blockSignals(False)
        self._update_range_label()

    def _update_range_label(self) -> None:
        if self.data_block_total <= 0:
            self.range_label.setText("No data blocks")
            return
        start_abs = self.data_block_start + self.window_start
        end_rel = min(self.window_start + self.visible_count, self.data_block_total)
        end_abs = self.data_block_start + end_rel - 1
        self.range_label.setText("{} - {}".format(start_abs, end_abs))

    def _refresh_visible_cells(self) -> None:
        for idx in range(self.visible_count):
            self._update_cell(idx)

    def _on_page_selected(self, index: int) -> None:
        if index < 0:
            return
        self.window_start = index * self.visible_count
        self._refresh_visible_cells()
        self._update_range_label()
        start_abs = self.data_block_start + self.window_start
        count = min(self.visible_count, max(0, self.data_block_total - self.window_start))
        if count > 0:
            self.range_requested.emit(start_abs, count)

    def _shift_page(self, delta: int) -> None:
        if self.data_block_total <= 0:
            return
        total_pages = (self.data_block_total + self.visible_count - 1) // self.visible_count
        current_page = self.page_combo.currentIndex()
        new_page = current_page + delta
        if 0 <= new_page < total_pages:
            self.page_combo.setCurrentIndex(new_page)

    def allocate_block(self, relative_block_no: int) -> None:
        if 0 <= relative_block_no < self.data_block_total:
            if self.block_states[relative_block_no]:
                return
            self.block_states[relative_block_no] = True
            self.allocated_count += 1
            
            # Auto-scroll jumping window if allocation falls outside
            if relative_block_no < self.window_start or relative_block_no >= self.window_start + self.visible_count:
                new_page = relative_block_no // self.visible_count
                self.page_combo.setCurrentIndex(new_page)
                
            if self.window_start <= relative_block_no < self.window_start + self.visible_count:
                self._update_cell(relative_block_no - self.window_start)
                
            self.update_stats()
            absolute_block_no = self.data_block_start + relative_block_no
            self.log_event(f"ALLOC block #{absolute_block_no} (rel {relative_block_no})")
            self.group_stack.on_alloc(absolute_block_no)

    def free_block(self, relative_block_no: int) -> None:
        if 0 <= relative_block_no < self.data_block_total:
            if not self.block_states[relative_block_no]:
                return
            self.block_states[relative_block_no] = False
            if self.allocated_count > 0:
                self.allocated_count -= 1
            if self.window_start <= relative_block_no < self.window_start + self.visible_count:
                self._update_cell(relative_block_no - self.window_start)
            self.update_stats()
            absolute_block_no = self.data_block_start + relative_block_no
            self.log_event(f"FREE block #{absolute_block_no} (rel {relative_block_no})")
            self.group_stack.on_free(absolute_block_no)

    def update_stats(self) -> None:
        allocated = self.allocated_count
        total = self.data_block_total
        percent = (allocated / total) * 100.0 if total else 0.0
        self.stats_label.setText(
            "Allocated: {} / {} ({:.2f}%)".format(allocated, total, percent)
        )

    def reset(self) -> None:
        for idx in range(self.data_block_total):
            self.block_states[idx] = False
        self.allocated_count = 0
        self._refresh_visible_cells()
        self._update_slider_range()
        self.update_stats()
        self.trace.clear()
        self.group_stack.reset()

    def configure_layout(
        self,
        data_start: int,
        data_blocks: int,
        free_group_size: int,
        block_size: int,
        total_blocks: int,
    ) -> None:
        if data_blocks <= 0:
            return
            
        if self.data_block_start != data_start or self.data_block_total != data_blocks:
            old_states = self.block_states

            self.data_block_start = data_start
            self.data_block_total = data_blocks
            self.window_start = 0

            new_states: List[bool] = [False] * self.data_block_total
            copy_len = min(len(old_states), len(new_states))
            if copy_len > 0:
                new_states[:copy_len] = old_states[:copy_len]
            self.block_states = new_states
            self.allocated_count = sum(1 for x in self.block_states if x)
            
        self.group_stack.configure(free_group_size)
        self._update_slider_range()
        self._refresh_visible_cells()
        self.update_stats()

        # Always request the full blockmap after (re)configuring layout,
        # so the heatmap reflects the actual on-disk state — not just
        # incremental BLOCK_ALLOC/FREE signals received since startup.
        # Use a short timer to let the core process finish its mount sequence.
        start_abs = self.data_block_start + self.window_start
        count = min(self.visible_count, max(0, self.data_block_total - self.window_start))
        if count > 0:
            QTimer.singleShot(200, lambda s=start_abs, c=count: self.range_requested.emit(s, c))

    def used_blocks_received(self, start_block: int, count: int, used_blocks: List[int]) -> None:
        start_rel = start_block - self.data_block_start
        if start_rel < 0 or start_rel >= self.data_block_total:
            return

        max_count = min(count, self.data_block_total - start_rel)
        for i in range(max_count):
            self.block_states[start_rel + i] = False

        for blk in used_blocks:
            rel = blk - self.data_block_start
            if 0 <= rel < self.data_block_total:
                self.block_states[rel] = True

        self.allocated_count = sum(1 for x in self.block_states if x)
        self._refresh_visible_cells()
        self.update_stats()

    def group_loaded(self, count: int, head_block: int) -> None:
        self.group_stack.group_loaded(count, head_block)

    def group_stored(self, count: int, head_block: int) -> None:
        self.group_stack.group_stored(count, head_block)

    def group_items_received(self, items: List[int]) -> None:
        self.group_stack.set_group_items(items)

    def cache_lru_received(self, lru_blocks: List[int]) -> None:
        self.cache_panel.update_lru(lru_blocks)

    def log_event(self, message: str) -> None:
        if not message:
            return
        self.trace.append(message)
