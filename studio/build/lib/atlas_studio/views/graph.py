"""Explicit task-graph editor widgets."""

from __future__ import annotations

from typing import Any

from PySide6.QtCore import Signal
from PySide6.QtWidgets import (
    QCheckBox,
    QComboBox,
    QFormLayout,
    QGroupBox,
    QHBoxLayout,
    QLineEdit,
    QPushButton,
    QScrollArea,
    QSplitter,
    QVBoxLayout,
    QWidget,
)

from ..models.documents import JsonObject
from .fields import DimensionsEditor, UIntEdit
from .graph_canvas import GraphCanvas


class GraphView(QWidget):
    """Render a graph snapshot and emit graph-editing intent."""

    settings_requested = Signal(dict)
    task_add_requested = Signal(str)
    task_remove_requested = Signal(str)
    task_update_requested = Signal(str, dict)
    dependency_add_requested = Signal(str, str)
    selection_requested = Signal(str)
    message = Signal(str)

    def __init__(self) -> None:
        super().__init__()
        self._snapshot: JsonObject = {"nodes": [], "edges": []}
        self.selected_id = ""
        self._updating = False
        self.canvas = GraphCanvas()
        self.canvas.node_selected.connect(self.selection_requested)
        self.canvas.edge_requested.connect(self.dependency_add_requested)

        controls = QWidget()
        controls_layout = QVBoxLayout(controls)
        graph_group = QGroupBox("Graph settings")
        graph_form = QFormLayout(graph_group)
        self.graph_id = QLineEdit()
        self.seed = UIntEdit()
        self.cpu_mode = QComboBox()
        self.cpu_mode.addItems(["synchronous", "worker_pool"])
        self.worker_count = UIntEdit(1, 2**32 - 1)
        self.policy = QComboBox()
        self.policy.addItems(["fifo", "round_robin", "static_priority"])
        self.quantum = UIntEdit(1)
        self.validation = QCheckBox()
        self.trace_enabled = QCheckBox()
        self.trace_capacity = UIntEdit(65_536, 1_000_000)
        for label, widget in (
            ("Graph ID", self.graph_id),
            ("Seed", self.seed),
            ("CPU executor", self.cpu_mode),
            ("Worker count", self.worker_count),
            ("Policy", self.policy),
            ("Quantum", self.quantum),
            ("Vulkan validation", self.validation),
            ("Trace enabled", self.trace_enabled),
            ("Trace capacity", self.trace_capacity),
        ):
            graph_form.addRow(label, widget)
        controls_layout.addWidget(graph_group)

        actions = QHBoxLayout()
        for label, callback in (
            ("Add CPU", lambda: self.task_add_requested.emit("cpu")),
            ("Add GPU", lambda: self.task_add_requested.emit("gpu")),
            ("Remove", self._request_remove),
        ):
            button = QPushButton(label)
            button.clicked.connect(callback)
            actions.addWidget(button)
        controls_layout.addLayout(actions)
        self.connect_button = QPushButton("Connect tasks")
        self.connect_button.setCheckable(True)
        self.connect_button.toggled.connect(self._toggle_connecting)
        controls_layout.addWidget(self.connect_button)

        task_group = QGroupBox("Selected task")
        task_form = QFormLayout(task_group)
        self.node_id = QLineEdit()
        self.node_name = QLineEdit()
        self.resource = QLineEdit()
        self.resource.setReadOnly(True)
        self.kernel = QComboBox()
        self.kernel.addItems(["cpu_burn", "gpu_increment", "vector_add"])
        self.priority = UIntEdit(0, 2**32 - 1)
        self.iterations = UIntEdit(1)
        self.element_count = UIntEdit(256)
        self.left_value = QLineEdit("4")
        self.right_value = QLineEdit("7")
        for label, widget in (
            ("ID", self.node_id),
            ("Name", self.node_name),
            ("Resource", self.resource),
            ("Kernel", self.kernel),
            ("Priority", self.priority),
            ("Iterations", self.iterations),
            ("Element count", self.element_count),
            ("Left value", self.left_value),
            ("Right value", self.right_value),
        ):
            task_form.addRow(label, widget)
        self.workgroups = DimensionsEditor("Workgroups")
        self.slicing = QCheckBox("Cooperative slicing")
        self.slice_dimensions = DimensionsEditor("Slice workgroups")
        task_form.addRow(self.workgroups)
        task_form.addRow(self.slicing)
        task_form.addRow(self.slice_dimensions)
        controls_layout.addWidget(task_group)
        controls_layout.addStretch()

        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setWidget(controls)
        splitter = QSplitter()
        splitter.addWidget(self.canvas)
        splitter.addWidget(scroll)
        splitter.setSizes([850, 360])
        layout = QHBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.addWidget(splitter)
        self._connect_fields()

    def render(self, document: JsonObject, selected_id: str | None = None) -> None:
        self._snapshot = document
        known = {node["id"] for node in document["nodes"]}
        requested = selected_id if selected_id in known else None
        self.selected_id = requested or (document["nodes"][0]["id"] if document["nodes"] else "")
        self._refresh()

    def _connect_fields(self) -> None:
        self.graph_id.editingFinished.connect(self._update_global)
        self.seed.value_changed.connect(self._update_global)
        self.cpu_mode.currentTextChanged.connect(self._update_global)
        self.worker_count.value_changed.connect(self._update_global)
        self.policy.currentTextChanged.connect(self._update_global)
        self.quantum.value_changed.connect(self._update_global)
        self.validation.toggled.connect(self._update_global)
        self.trace_enabled.toggled.connect(self._update_global)
        self.trace_capacity.value_changed.connect(self._update_global)
        self.node_id.editingFinished.connect(self._update_task)
        self.node_name.editingFinished.connect(self._update_task)
        self.kernel.currentTextChanged.connect(self._kernel_changed)
        self.priority.value_changed.connect(self._update_task)
        self.iterations.value_changed.connect(self._update_task)
        self.element_count.value_changed.connect(self._update_task)
        self.left_value.editingFinished.connect(self._update_task)
        self.right_value.editingFinished.connect(self._update_task)
        self.workgroups.changed.connect(self._update_task)
        self.slicing.toggled.connect(self._update_task)
        self.slice_dimensions.changed.connect(self._update_task)

    def _refresh(self) -> None:
        self._updating = True
        self.graph_id.setText(str(self._snapshot.get("graph_id", "")))
        self.seed.set_value(int(self._snapshot.get("seed", 1)))
        self.cpu_mode.setCurrentText(self._snapshot.get("cpu_executor", {}).get("mode", "synchronous"))
        self.worker_count.set_value(int(self._snapshot.get("cpu_executor", {}).get("worker_count", 1)))
        self.policy.setCurrentText(self._snapshot.get("policy", {}).get("type", "fifo"))
        self.quantum.set_value(int(self._snapshot.get("policy", {}).get("quantum", 1)))
        self.quantum.setVisible(self.policy.currentText() == "round_robin")
        self.validation.setChecked(bool(self._snapshot.get("runtime", {}).get("validation", False)))
        self.trace_enabled.setChecked(bool(self._snapshot.get("trace", {}).get("enabled", True)))
        self.trace_capacity.set_value(int(self._snapshot.get("trace", {}).get("capacity", 65_536)))
        self._updating = False
        self.canvas.set_document(self._snapshot)
        self._refresh_task()

    def _refresh_task(self) -> None:
        node = next((item for item in self._snapshot["nodes"] if item["id"] == self.selected_id), None)
        if node is None:
            return
        self._updating = True
        kernel = node["kernel"]
        self.node_id.setText(node["id"])
        self.node_name.setText(node.get("name", node["id"]))
        self.resource.setText(node["resource"])
        self.kernel.setCurrentText(kernel["type"])
        self.priority.set_value(int(node.get("priority", 0)))
        self.iterations.set_value(int(kernel.get("iterations", 1)))
        self.element_count.set_value(int(kernel.get("element_count", 256)))
        self.left_value.setText(str(kernel.get("left_value", 4.0)))
        self.right_value.setText(str(kernel.get("right_value", 7.0)))
        self.workgroups.set_dimensions(kernel.get("workgroups", {"x": 1, "y": 1, "z": 1}))
        sliced = node.get("slice_workgroups") is not None
        self.slicing.setChecked(sliced)
        self.slice_dimensions.set_dimensions(node.get("slice_workgroups") or {"x": 1, "y": 1, "z": 1})
        self._set_kernel_visibility(kernel["type"])
        self._updating = False
        self.canvas.select_node(self.selected_id)

    def _update_global(self, *_args: Any) -> None:
        if self._updating:
            return
        policy: JsonObject = {"type": self.policy.currentText()}
        if self.policy.currentText() == "round_robin":
            policy["quantum"] = self.quantum.value()
        self.settings_requested.emit(
            {
                "graph_id": self.graph_id.text(),
                "seed": self.seed.value(),
                "cpu_executor": {
                    "mode": self.cpu_mode.currentText(),
                    "worker_count": self.worker_count.value(),
                },
                "policy": policy,
                "runtime": {"validation": self.validation.isChecked()},
                "trace": {"enabled": self.trace_enabled.isChecked(), "capacity": self.trace_capacity.value()},
            }
        )
        self.quantum.setVisible(self.policy.currentText() == "round_robin")

    def _kernel_changed(self, kernel_type: str) -> None:
        if self._updating:
            return
        node = self._selected_node()
        if node is None:
            return
        if kernel_type == "cpu_burn":
            replacement: JsonObject = {
                **node,
                "resource": "cpu",
                "kernel": {"type": kernel_type, "iterations": 10_000},
            }
            replacement.pop("slice_workgroups", None)
        elif kernel_type == "gpu_increment":
            replacement = {
                **node,
                "resource": "gpu",
                "kernel": {"type": kernel_type, "workgroups": {"x": 64, "y": 1, "z": 1}},
                "slice_workgroups": None,
            }
        else:
            replacement = {
                **node,
                "resource": "gpu",
                "kernel": {"type": kernel_type, "element_count": 256, "left_value": 4.0, "right_value": 7.0},
                "slice_workgroups": None,
            }
        self.task_update_requested.emit(self.selected_id, replacement)

    def _update_task(self, *_args: Any) -> None:
        if self._updating:
            return
        node = self._selected_node()
        if node is None:
            return
        previous = node["id"]
        identifier = self.node_id.text()
        replacement = {
            **node,
            "id": identifier,
            "name": self.node_name.text(),
            "priority": self.priority.value(),
        }
        kernel_type = node["kernel"]["type"]
        if kernel_type == "cpu_burn":
            replacement["kernel"] = {"type": kernel_type, "iterations": self.iterations.value()}
            replacement.pop("slice_workgroups", None)
        elif kernel_type == "gpu_increment":
            replacement["kernel"] = {"type": kernel_type, "workgroups": self.workgroups.dimensions()}
        else:
            try:
                left, right = float(self.left_value.text()), float(self.right_value.text())
            except ValueError:
                self.message.emit("vector values must be numbers")
                return
            replacement["kernel"] = {
                "type": kernel_type,
                "element_count": self.element_count.value(),
                "left_value": left,
                "right_value": right,
            }
        if replacement["resource"] == "gpu":
            replacement["slice_workgroups"] = (
                self.slice_dimensions.dimensions() if self.slicing.isChecked() else None
            )
            self.slice_dimensions.setVisible(self.slicing.isChecked())
        self.task_update_requested.emit(previous, replacement)

    def _selected_node(self) -> JsonObject | None:
        return next((node for node in self._snapshot["nodes"] if node["id"] == self.selected_id), None)

    def _set_kernel_visibility(self, kernel_type: str) -> None:
        self.iterations.setVisible(kernel_type == "cpu_burn")
        self.workgroups.setVisible(kernel_type == "gpu_increment")
        vector = kernel_type == "vector_add"
        self.element_count.setVisible(vector)
        self.left_value.setVisible(vector)
        self.right_value.setVisible(vector)
        gpu = kernel_type != "cpu_burn"
        self.slicing.setVisible(gpu)
        self.slice_dimensions.setVisible(gpu and self.slicing.isChecked())

    def _toggle_connecting(self, enabled: bool) -> None:
        self.canvas.set_connecting(enabled)
        self.connect_button.setText("Click dependency, then dependent" if enabled else "Connect tasks")

    def _request_remove(self) -> None:
        if self.selected_id:
            self.task_remove_requested.emit(self.selected_id)
