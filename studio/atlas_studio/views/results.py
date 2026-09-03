"""Live and imported Atlas result views."""

from __future__ import annotations

import json
from typing import Any

from PySide6.QtCore import QAbstractTableModel, QModelIndex, Qt, Signal
from PySide6.QtWidgets import (
    QComboBox,
    QFormLayout,
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QPlainTextEdit,
    QProgressBar,
    QSplitter,
    QTableView,
    QTabWidget,
    QVBoxLayout,
    QWidget,
)

from ..models.documents import JsonObject
from ..models.results import BenchmarkRunRow, ResultsSnapshot
from .formatting import format_nanoseconds
from .timeline import TimelineView

_ROOT_INDEX = QModelIndex()


class _TaskTableModel(QAbstractTableModel):
    """Virtual table model for the bounded live task projection."""

    HEADERS = (
        "Task",
        "Resource",
        "State",
        "Execution",
        "Response",
        "Ready wait",
        "Device",
        "Work units",
        "Bypasses",
    )

    def __init__(self) -> None:
        super().__init__()
        self._rows: tuple[JsonObject, ...] = ()

    def rowCount(self, parent: QModelIndex = _ROOT_INDEX) -> int:
        return 0 if parent.isValid() else len(self._rows)

    def columnCount(self, parent: QModelIndex = _ROOT_INDEX) -> int:
        return 0 if parent.isValid() else len(self.HEADERS)

    def data(self, index: QModelIndex, role: int = Qt.DisplayRole) -> Any:
        if role != Qt.DisplayRole or not index.isValid():
            return None
        task = self._rows[index.row()]
        completed, total = task.get("completed_work_units", 0), task.get("total_work_units", 0)
        values = (
            task.get("name", task.get("node_id", task.get("task_id"))),
            task.get("resource", "—"),
            task.get("state", "unknown"),
            format_nanoseconds(task.get("execution_duration_ns")),
            format_nanoseconds(task.get("response_duration_ns")),
            format_nanoseconds(task.get("ready_wait_ns")),
            format_nanoseconds(task.get("device_execution_duration_ns")),
            f"{completed}/{total}",
            task.get("selection_bypass_count", 0),
        )
        return str(values[index.column()])

    def headerData(self, section: int, orientation: Qt.Orientation, role: int = Qt.DisplayRole) -> Any:
        if role == Qt.DisplayRole and orientation == Qt.Horizontal:
            return self.HEADERS[section]
        return None

    def set_rows(self, rows: tuple[JsonObject, ...]) -> None:
        if rows == self._rows:
            return
        self.beginResetModel()
        self._rows = rows
        self.endResetModel()


class _BenchmarkRunsTableModel(QAbstractTableModel):
    """Virtual table model that remains cheap for thousands of benchmark runs."""

    HEADERS = (
        "Case",
        "Variant",
        "Execution",
        "Seed",
        "Repetition",
        "Status",
        "Completion µs",
        "Throughput",
    )

    def __init__(self) -> None:
        super().__init__()
        self._rows: tuple[BenchmarkRunRow, ...] = ()

    def rowCount(self, parent: QModelIndex = _ROOT_INDEX) -> int:
        return 0 if parent.isValid() else len(self._rows)

    def columnCount(self, parent: QModelIndex = _ROOT_INDEX) -> int:
        return 0 if parent.isValid() else len(self.HEADERS)

    def data(self, index: QModelIndex, role: int = Qt.DisplayRole) -> Any:
        if role != Qt.DisplayRole or not index.isValid():
            return None
        run = self._rows[index.row()]
        values = (
            run.case_id,
            run.variant_id,
            run.execution,
            run.seed,
            run.repetition,
            run.status,
            run.completion_us,
            run.throughput,
        )
        value = values[index.column()]
        return str(value if value is not None else "—")

    def headerData(self, section: int, orientation: Qt.Orientation, role: int = Qt.DisplayRole) -> Any:
        if role == Qt.DisplayRole and orientation == Qt.Horizontal:
            return self.HEADERS[section]
        return None

    def set_rows(self, rows: tuple[BenchmarkRunRow, ...]) -> None:
        if rows == self._rows:
            return
        self.beginResetModel()
        self._rows = rows
        self.endResetModel()


class ResultsView(QWidget):
    """Render immutable result-session snapshots."""

    run_selection_requested = Signal(object)

    def __init__(self) -> None:
        super().__init__()
        self._updating_selector = False
        self._snapshot: ResultsSnapshot | None = None
        self._rendered_records: tuple[JsonObject, ...] = ()
        self._rendered_diagnostics: tuple[str, ...] = ()
        self._rendered_comparisons: object = object()

        summary = QGroupBox("Run summary")
        summary_form = QFormLayout(summary)
        self.summary_labels = {
            name: QLabel("—")
            for name in ("Status", "Executed tasks", "Execution", "Scheduler active", "Device", "Trace drops")
        }
        for name, label in self.summary_labels.items():
            summary_form.addRow(name, label)

        self.run_context = QLabel("Explicit graph run")
        self.run_selector = QComboBox()
        self.run_selector.addItem("Live/current run", None)
        self.run_selector.currentIndexChanged.connect(self._selected_run_changed)
        self.suite_progress = QProgressBar()
        self.suite_progress.setMinimum(0)
        self.suite_progress.setValue(0)
        self.run_context.setVisible(False)
        self.run_selector.setVisible(False)
        self.suite_progress.setVisible(False)
        self.display_limit = QLabel()
        self.display_limit.setVisible(False)

        self.tasks = QTableView()
        self.task_model = _TaskTableModel()
        self.tasks.setModel(self.task_model)
        self.tasks.horizontalHeader().setStretchLastSection(True)

        self.timeline = TimelineView()
        self.event_log = QPlainTextEdit()
        self.event_log.setReadOnly(True)
        self.event_log.setMaximumBlockCount(100_000)
        self.diagnostics = QPlainTextEdit()
        self.diagnostics.setReadOnly(True)
        self.diagnostics.setMaximumBlockCount(20_000)
        self.benchmark_runs = QTableView()
        self.benchmark_run_model = _BenchmarkRunsTableModel()
        self.benchmark_runs.setModel(self.benchmark_run_model)
        self.comparisons = QPlainTextEdit()
        self.comparisons.setReadOnly(True)

        live_split = QSplitter(Qt.Vertical)
        live_split.addWidget(self.tasks)
        live_split.addWidget(self.timeline)
        live_split.setSizes([360, 160])
        self.tabs = QTabWidget()
        self.tabs.addTab(live_split, "Tasks and timeline")
        self.tabs.addTab(self.event_log, "Event stream")
        self.tabs.addTab(self.diagnostics, "Diagnostics")
        benchmark_split = QSplitter(Qt.Vertical)
        benchmark_split.addWidget(self.benchmark_runs)
        benchmark_split.addWidget(self.comparisons)
        self.tabs.addTab(benchmark_split, "Benchmark results")
        self.tabs.currentChanged.connect(self._render_selected_tab)
        top = QSplitter()
        top.addWidget(summary)
        top.addWidget(self.tabs)
        top.setSizes([260, 900])
        controls = QHBoxLayout()
        controls.addWidget(self.run_context, 1)
        controls.addWidget(self.run_selector)
        controls.addWidget(self.suite_progress)
        layout = QVBoxLayout(self)
        layout.addLayout(controls)
        layout.addWidget(self.display_limit)
        layout.addWidget(top)

    def _selected_run_changed(self, _index: int) -> None:
        if not self._updating_selector:
            self.run_selection_requested.emit(self.run_selector.currentData())

    def render(self, snapshot: ResultsSnapshot) -> None:
        self._snapshot = snapshot
        summary = snapshot.summary
        values = {
            "Status": summary.get("status"),
            "Executed tasks": summary.get("executed_tasks"),
            "Execution": format_nanoseconds(summary.get("execution_ns")),
            "Scheduler active": format_nanoseconds(summary.get("scheduler_active_ns")),
            "Device": summary.get("device"),
            "Trace drops": summary.get("trace_drops"),
        }
        for name, value in values.items():
            self.summary_labels[name].setText(str(value if value is not None else "—"))

        self.run_context.setVisible(snapshot.benchmark_mode)
        self.run_selector.setVisible(snapshot.benchmark_mode)
        self.suite_progress.setVisible(snapshot.benchmark_mode)
        self.run_context.setText(snapshot.run_context)
        self.suite_progress.setMaximum(snapshot.progress_maximum)
        self.suite_progress.setValue(snapshot.progress_value)
        self._updating_selector = True
        self.run_selector.clear()
        for label, run_id in snapshot.run_choices:
            self.run_selector.addItem(label, run_id)
        index = self.run_selector.findData(snapshot.selected_run_id)
        self.run_selector.setCurrentIndex(index if index >= 0 else 0)
        self._updating_selector = False
        limits = []
        if len(snapshot.tasks) < snapshot.total_task_count:
            limits.append(f"first {len(snapshot.tasks):,} of {snapshot.total_task_count:,} tasks")
        if len(snapshot.events) < snapshot.total_event_count:
            limits.append(f"latest {len(snapshot.events):,} of {snapshot.total_event_count:,} events")
        if len(snapshot.records) < snapshot.total_record_count:
            limits.append(f"latest {len(snapshot.records):,} of {snapshot.total_record_count:,} records")
        self.display_limit.setText("Live display is limited to " + ", ".join(limits) + ".")
        self.display_limit.setVisible(bool(limits))
        self._render_selected_tab(self.tabs.currentIndex())

    def _render_selected_tab(self, index: int) -> None:
        snapshot = self._snapshot
        if snapshot is None:
            return
        if index == 0:
            self.task_model.set_rows(snapshot.tasks)
            self.timeline.render_events(snapshot.events)
        elif index == 1 and snapshot.records != self._rendered_records:
            self.event_log.setPlainText(
                "\n".join(json.dumps(record, separators=(",", ":")) for record in snapshot.records)
            )
            self._rendered_records = snapshot.records
        elif index == 2 and snapshot.diagnostics != self._rendered_diagnostics:
            self.diagnostics.setPlainText("\n".join(snapshot.diagnostics))
            self._rendered_diagnostics = snapshot.diagnostics
        elif index == 3:
            self.benchmark_run_model.set_rows(snapshot.benchmark_runs)
            if snapshot.comparisons != self._rendered_comparisons:
                self.comparisons.setPlainText(
                    json.dumps(snapshot.comparisons, indent=2)
                    if snapshot.comparisons
                    else "No comparison summary was produced."
                )
                self._rendered_comparisons = snapshot.comparisons
