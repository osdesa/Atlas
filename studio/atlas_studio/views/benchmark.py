"""Structured and JSON benchmark-suite workspace."""

from __future__ import annotations

import json
from typing import Any

from PySide6.QtCore import Qt, Signal
from PySide6.QtWidgets import (
    QCheckBox,
    QFileDialog,
    QFormLayout,
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QMessageBox,
    QPushButton,
    QScrollArea,
    QSplitter,
    QStackedWidget,
    QTabWidget,
    QTextEdit,
    QTreeWidget,
    QTreeWidgetItem,
    QVBoxLayout,
    QWidget,
)

from ..models.benchmark import BenchmarkSelection
from ..models.documents import JsonObject
from .benchmark_forms import BenchmarkCaseEditor, BenchmarkVariantEditor
from .fields import UIntEdit


class BenchmarkView(QWidget):
    """Compose focused editors and emit benchmark-suite editing intent."""

    settings_requested = Signal(dict)
    case_add_requested = Signal()
    case_update_requested = Signal(int, dict)
    case_remove_requested = Signal(int)
    variant_add_requested = Signal(int)
    variant_update_requested = Signal(int, int, dict)
    variant_remove_requested = Signal(int, int)
    document_replace_requested = Signal(str)
    selection_requested = Signal(object)
    run_options_requested = Signal(dict)
    message = Signal(str)

    def __init__(self) -> None:
        super().__init__()
        self._snapshot: JsonObject = {"cases": []}
        self._updating = False
        self._selection: BenchmarkSelection | None = None
        self.case_editor = BenchmarkCaseEditor()
        self.variant_editor = BenchmarkVariantEditor()
        self.case_editor.changed.connect(self._case_changed)
        self.case_editor.message.connect(self.message)
        self.variant_editor.changed.connect(self._variant_changed)
        self.tabs = QTabWidget()
        self.tabs.addTab(self._build_forms(), "Structured editor")
        self.tabs.addTab(self._build_json(), "Advanced JSON")
        self.tabs.currentChanged.connect(self._tab_changed)
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.addWidget(self.tabs)
        self._connect_global_fields()
        self._expose_editor_controls()

    def _build_forms(self) -> QWidget:
        page = QWidget()
        layout = QVBoxLayout(page)
        global_group = QGroupBox("Suite settings")
        global_form = QFormLayout(global_group)
        self.suite_id = QLineEdit()
        self.seeds = QLineEdit()
        self.warmups = UIntEdit(1)
        self.repetitions = UIntEdit(2)
        self.workers = UIntEdit(2, 2**32 - 1)
        for label, widget in (
            ("Suite ID", self.suite_id),
            ("Seeds (comma-separated)", self.seeds),
            ("Warmup runs", self.warmups),
            ("Repetitions", self.repetitions),
            ("Worker count", self.workers),
        ):
            global_form.addRow(label, widget)
        layout.addWidget(global_group)
        layout.addWidget(self._build_run_options())

        self.tree = QTreeWidget()
        self.tree.setHeaderLabel("Cases and variants")
        self.tree.currentItemChanged.connect(self._selection_changed)
        tree_actions = QHBoxLayout()
        for label, callback in (
            ("Add case", self.add_case),
            ("Add variant", self.add_variant),
            ("Remove", self.remove_selected),
        ):
            button = QPushButton(label)
            button.clicked.connect(callback)
            tree_actions.addWidget(button)
        left = QWidget()
        left_layout = QVBoxLayout(left)
        left_layout.addWidget(self.tree)
        left_layout.addLayout(tree_actions)
        self.editor_stack = QStackedWidget()
        self.editor_stack.addWidget(QLabel("Select a case or variant."))
        self.editor_stack.addWidget(self.case_editor)
        self.editor_stack.addWidget(self.variant_editor)
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setWidget(self.editor_stack)
        splitter = QSplitter()
        splitter.addWidget(left)
        splitter.addWidget(scroll)
        splitter.setSizes([310, 700])
        layout.addWidget(splitter, 1)
        return page

    def _build_run_options(self) -> QGroupBox:
        group = QGroupBox("Benchmark run files")
        form = QFormLayout(group)
        self.output_directory = QLineEdit()
        output_button = QPushButton("Choose…")
        output_button.clicked.connect(self._choose_output)
        output_row = QHBoxLayout()
        output_row.addWidget(self.output_directory)
        output_row.addWidget(output_button)
        self.environment_file = QLineEdit()
        environment_button = QPushButton("Choose…")
        environment_button.clicked.connect(self._choose_environment)
        environment_row = QHBoxLayout()
        environment_row.addWidget(self.environment_file)
        environment_row.addWidget(environment_button)
        self.live_tracing = QCheckBox("Show live benchmark tasks (adds measurement overhead)")
        self.live_tracing.setChecked(True)
        self.output_directory.textChanged.connect(self._run_options_changed)
        self.environment_file.textChanged.connect(self._run_options_changed)
        self.live_tracing.toggled.connect(self._run_options_changed)
        form.addRow("Output directory or run parent", output_row)
        form.addRow("Environment metadata (optional)", environment_row)
        form.addRow(self.live_tracing)
        return group

    def _build_json(self) -> QWidget:
        page = QWidget()
        layout = QVBoxLayout(page)
        self.json_editor = QTextEdit()
        self.json_editor.setAcceptRichText(False)
        self.json_editor.setLineWrapMode(QTextEdit.NoWrap)
        apply_button = QPushButton("Apply JSON to structured editor")
        apply_button.clicked.connect(
            lambda: self.document_replace_requested.emit(self.json_editor.toPlainText())
        )
        layout.addWidget(self.json_editor)
        layout.addWidget(apply_button)
        return page

    def _connect_global_fields(self) -> None:
        for signal in (
            self.suite_id.editingFinished,
            self.seeds.editingFinished,
            self.warmups.value_changed,
            self.repetitions.value_changed,
            self.workers.value_changed,
        ):
            signal.connect(self._global_changed)

    def render(self, document: JsonObject, selection: BenchmarkSelection | None = None) -> None:
        self._snapshot = document
        self._updating = True
        self.suite_id.setText(document["suite_id"])
        self.seeds.setText(", ".join(map(str, document["seeds"])))
        self.warmups.set_value(document["warmup_runs"])
        self.repetitions.set_value(document["repetitions"])
        self.workers.set_value(document["worker_count"])
        self._updating = False
        self._rebuild_tree(selection)
        self._sync_json()

    def add_case(self) -> None:
        self.case_add_requested.emit()

    def add_variant(self) -> None:
        if self._selection is not None:
            self.variant_add_requested.emit(self._selection.case_index)

    def remove_selected(self) -> None:
        if self._selection is None:
            return
        if self._selection.kind == "case":
            self.case_remove_requested.emit(self._selection.case_index)
        elif self._selection.variant_index is not None:
            self.variant_remove_requested.emit(self._selection.case_index, self._selection.variant_index)

    def _rebuild_tree(self, selected: BenchmarkSelection | None) -> None:
        self._updating = True
        self.tree.clear()
        selected_item: QTreeWidgetItem | None = None
        for case_index, case in enumerate(self._snapshot["cases"]):
            case_selection = BenchmarkSelection("case", case_index)
            case_item = QTreeWidgetItem([case["case_id"]])
            case_item.setData(0, Qt.UserRole, case_selection)
            self.tree.addTopLevelItem(case_item)
            if selected == case_selection:
                selected_item = case_item
            for variant_index, variant in enumerate(case["variants"]):
                variant_selection = BenchmarkSelection("variant", case_index, variant_index)
                variant_item = QTreeWidgetItem([variant["variant_id"]])
                variant_item.setData(0, Qt.UserRole, variant_selection)
                case_item.addChild(variant_item)
                if selected == variant_selection:
                    selected_item = variant_item
            case_item.setExpanded(True)
        self._updating = False
        if selected_item is None and self.tree.topLevelItemCount():
            selected_item = self.tree.topLevelItem(0)
        if selected_item:
            self.tree.setCurrentItem(selected_item)

    def _selection_changed(self, current: QTreeWidgetItem | None, _previous: QTreeWidgetItem | None) -> None:
        if self._updating or current is None:
            return
        selection = current.data(0, Qt.UserRole)
        if not isinstance(selection, BenchmarkSelection):
            return
        self._selection = selection
        self.selection_requested.emit(selection)
        if selection.kind == "case":
            self.case_editor.load(self._snapshot["cases"][selection.case_index])
            self.editor_stack.setCurrentIndex(1)
        elif selection.variant_index is not None:
            self.variant_editor.load(
                self._snapshot["cases"][selection.case_index]["variants"][selection.variant_index]
            )
            self.editor_stack.setCurrentIndex(2)

    def _global_changed(self, *_args: Any) -> None:
        if self._updating:
            return
        try:
            seeds = [int(value.strip()) for value in self.seeds.text().split(",") if value.strip()]
        except ValueError:
            self.message.emit("Seeds must be comma-separated unsigned integers.")
            return
        self.settings_requested.emit(
            {
                "suite_id": self.suite_id.text(),
                "seeds": seeds,
                "warmup_runs": self.warmups.value(),
                "repetitions": self.repetitions.value(),
                "worker_count": self.workers.value(),
            }
        )

    def _case_changed(self, replacement: JsonObject) -> None:
        if self._selection is not None and self._selection.kind == "case":
            self.case_update_requested.emit(self._selection.case_index, replacement)

    def _variant_changed(self, replacement: JsonObject) -> None:
        if (
            self._selection is not None
            and self._selection.kind == "variant"
            and self._selection.variant_index is not None
        ):
            self.variant_update_requested.emit(
                self._selection.case_index, self._selection.variant_index, replacement
            )

    def _choose_output(self) -> None:
        if path := QFileDialog.getExistingDirectory(self, "Choose a new or empty benchmark output directory"):
            self.output_directory.setText(path)

    def _choose_environment(self) -> None:
        path, _ = QFileDialog.getOpenFileName(
            self, "Choose environment metadata", filter="JSON files (*.json)"
        )
        if path:
            self.environment_file.setText(path)

    def _tab_changed(self, index: int) -> None:
        if index == 1:
            self._sync_json()

    def _sync_json(self) -> None:
        self.json_editor.setPlainText(json.dumps(self._snapshot, indent=2))

    def _run_options_changed(self, *_args: Any) -> None:
        self.run_options_requested.emit(
            {
                "output_directory": self.output_directory.text().strip(),
                "environment_file": self.environment_file.text().strip(),
                "live_tracing": self.live_tracing.isChecked(),
            }
        )

    def _expose_editor_controls(self) -> None:
        """Expose composed controls used by focused widget interaction tests."""
        for name in (
            "case_id",
            "reference_variant",
            "cpu_tasks",
            "cpu_iterations",
            "gpu_tasks",
            "gpu_workgroups",
            "dependency_shape",
            "layers",
            "edge_probability",
            "priority_assignment",
            "priority_values",
            "burst_count",
        ):
            setattr(self, name, getattr(self.case_editor, name))
        self.variant_id = self.variant_editor.variant_id
        self.execution = self.variant_editor.execution
        self.variant_policy = self.variant_editor.policy
        self.variant_quantum = self.variant_editor.quantum
        self.variant_slicing = self.variant_editor.slicing
        self.variant_slice = self.variant_editor.slice_dimensions

    def show_validation_error(self, text: str) -> None:
        QMessageBox.warning(self, "Invalid benchmark document", text)
