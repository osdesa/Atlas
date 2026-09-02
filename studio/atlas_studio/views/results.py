"""Live and imported Atlas result views."""

from __future__ import annotations

import json

from PySide6.QtCore import Qt, Signal
from PySide6.QtWidgets import (
    QComboBox,
    QFormLayout,
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QPlainTextEdit,
    QProgressBar,
    QSplitter,
    QTableWidget,
    QTableWidgetItem,
    QTabWidget,
    QVBoxLayout,
    QWidget,
)

from ..models.results import ResultsSnapshot
from .formatting import format_nanoseconds
from .timeline import TimelineView


class ResultsView(QWidget):
    """Render immutable result-session snapshots."""

    run_selection_requested = Signal(object)

    def __init__(self) -> None:
        super().__init__()
        self._updating_selector = False

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

        self.tasks = QTableWidget(0, 9)
        self.tasks.setHorizontalHeaderLabels(
            [
                "Task",
                "Resource",
                "State",
                "Execution",
                "Response",
                "Ready wait",
                "Device",
                "Work units",
                "Bypasses",
            ]
        )
        self.tasks.setSortingEnabled(False)
        self.tasks.horizontalHeader().setStretchLastSection(True)

        self.timeline = TimelineView()
        self.event_log = QPlainTextEdit()
        self.event_log.setReadOnly(True)
        self.event_log.setMaximumBlockCount(100_000)
        self.diagnostics = QPlainTextEdit()
        self.diagnostics.setReadOnly(True)
        self.diagnostics.setMaximumBlockCount(20_000)
        self.benchmark_runs = QTableWidget(0, 8)
        self.benchmark_runs.setHorizontalHeaderLabels(
            ["Case", "Variant", "Execution", "Seed", "Repetition", "Status", "Completion µs", "Throughput"]
        )
        self.comparisons = QPlainTextEdit()
        self.comparisons.setReadOnly(True)

        live_split = QSplitter(Qt.Vertical)
        live_split.addWidget(self.tasks)
        live_split.addWidget(self.timeline)
        live_split.setSizes([360, 160])
        tabs = QTabWidget()
        tabs.addTab(live_split, "Tasks and timeline")
        tabs.addTab(self.event_log, "Event stream")
        tabs.addTab(self.diagnostics, "Diagnostics")
        benchmark_split = QSplitter(Qt.Vertical)
        benchmark_split.addWidget(self.benchmark_runs)
        benchmark_split.addWidget(self.comparisons)
        tabs.addTab(benchmark_split, "Benchmark results")
        top = QSplitter()
        top.addWidget(summary)
        top.addWidget(tabs)
        top.setSizes([260, 900])
        controls = QHBoxLayout()
        controls.addWidget(self.run_context, 1)
        controls.addWidget(self.run_selector)
        controls.addWidget(self.suite_progress)
        layout = QVBoxLayout(self)
        layout.addLayout(controls)
        layout.addWidget(top)

    def _selected_run_changed(self, _index: int) -> None:
        if not self._updating_selector:
            self.run_selection_requested.emit(self.run_selector.currentData())

    def render(self, snapshot: ResultsSnapshot) -> None:
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

        self.tasks.setRowCount(len(snapshot.tasks))
        for row, task in enumerate(snapshot.tasks):
            completed, total = task.get("completed_work_units", 0), task.get("total_work_units", 0)
            task_values = [
                task.get("name", task.get("node_id", task.get("task_id"))),
                task.get("resource", "—"),
                task.get("state", "unknown"),
                format_nanoseconds(task.get("execution_duration_ns")),
                format_nanoseconds(task.get("response_duration_ns")),
                format_nanoseconds(task.get("ready_wait_ns")),
                format_nanoseconds(task.get("device_execution_duration_ns")),
                f"{completed}/{total}",
                task.get("selection_bypass_count", 0),
            ]
            for column, value in enumerate(task_values):
                self.tasks.setItem(row, column, QTableWidgetItem(str(value)))

        self.event_log.setPlainText(
            "\n".join(json.dumps(record, separators=(",", ":")) for record in snapshot.records)
        )
        self.diagnostics.setPlainText("\n".join(snapshot.diagnostics))
        self.benchmark_runs.setRowCount(len(snapshot.benchmark_runs))
        for row, run in enumerate(snapshot.benchmark_runs):
            run_values = [
                run.case_id,
                run.variant_id,
                run.execution,
                run.seed,
                run.repetition,
                run.status,
                run.completion_us,
                run.throughput,
            ]
            for column, value in enumerate(run_values):
                self.benchmark_runs.setItem(
                    row, column, QTableWidgetItem(str(value if value is not None else "—"))
                )
        self.comparisons.setPlainText(
            json.dumps(snapshot.comparisons, indent=2)
            if snapshot.comparisons
            else "No comparison summary was produced."
        )

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
        self.timeline.render_events(snapshot.events)
