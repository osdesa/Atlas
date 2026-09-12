"""Explicit task-graph editor widgets."""

from __future__ import annotations

from typing import Any

from PySide6.QtCore import Qt, Signal
from PySide6.QtWidgets import (
    QCheckBox,
    QComboBox,
    QFormLayout,
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QPushButton,
    QScrollArea,
    QSplitter,
    QVBoxLayout,
    QWidget,
)

from ..models.descriptors import BUILTINS, default_parameters
from ..models.documents import JsonObject
from .fields import DimensionsEditor, ScalarEdit, ScalarValidator, UIntEdit
from .graph_canvas import GraphCanvas


class GraphView(QWidget):
    """Render a graph snapshot and emit graph-editing intent."""

    settings_requested = Signal(object)
    descriptor_add_requested = Signal(object, object)
    task_remove_requested = Signal(str)
    task_update_requested = Signal(str, object)
    dependency_add_requested = Signal(str, str)
    selection_requested = Signal(str)
    message = Signal(str)

    def __init__(self) -> None:
        super().__init__()
        self._snapshot: JsonObject = {"nodes": [], "edges": []}
        self.selected_id = ""
        self._updating = False
        self.node_problems: dict[str, str] = {}
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

        self.palette = QComboBox()
        controls_layout.addWidget(self.palette)
        self.pack_status = QLabel()
        self.pack_status.setWordWrap(True)
        self.pack_status.setTextFormat(Qt.PlainText)
        controls_layout.addWidget(self.pack_status)
        self.descriptor_resolver = lambda node: (
            BUILTINS.get(node["task_id"]) if node["pack_id"] == "atlas.builtin" else None
        )
        self.set_catalog({})
        actions = QHBoxLayout()
        for label, callback in (
            ("Add selected task", self._add_selected),
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
        self.kernel.addItems(list(BUILTINS))
        self.priority = UIntEdit(0, 2**32 - 1)
        for label, widget in (
            ("ID", self.node_id),
            ("Name", self.node_name),
            ("Resource", self.resource),
            ("Task type", self.kernel),
            ("Priority", self.priority),
        ):
            task_form.addRow(label, widget)
        self.parameter_group = QGroupBox("Parameters")
        self.parameter_form = QFormLayout(self.parameter_group)
        self.parameter_fields: dict[str, QWidget] = {}
        self._parameter_key: tuple[str, ...] | None = None
        self.slicing = QCheckBox("Cooperative slicing")
        self.slice_dimensions = DimensionsEditor("Slice workgroups")
        task_form.addRow(self.parameter_group)
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

    def set_catalog(self, packs: dict[str, JsonObject], statuses: dict[str, str] | None = None) -> None:
        """Render detached descriptor metadata in grouped palette entries."""
        key = tuple((digest, (statuses or {}).get(digest, "")) for digest in packs)
        if getattr(self, "_catalog_key", None) == key:
            return
        self._catalog_key = key
        self.palette.clear()
        for descriptor in BUILTINS.values():
            self.palette.addItem(
                f"Built-in · {descriptor.get('name') or descriptor['task_id']} · {descriptor['resource']}",
                (descriptor, None),
            )
        for digest, pack in packs.items():
            for descriptor in pack["tasks"]:
                status = (statuses or {}).get(digest, "")
                self.palette.addItem(
                    f"Installed Packs · {pack['pack_id']} {pack['version']} [{digest[:12]}] · "
                    f"{descriptor.get('name') or descriptor['task_id']} · {descriptor['resource']} · {status}",
                    (descriptor, pack),
                )
                self.palette.setItemData(self.palette.count() - 1, descriptor.get("description", ""), 3)

    def _add_selected(self) -> None:
        selected = self.palette.currentData()
        if selected:
            self.descriptor_add_requested.emit(*selected)

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
        self.canvas.set_document(
            {
                **self._snapshot,
                "nodes": [
                    {**node, "resolution_error": self.node_problems.get(node["id"], "")}
                    for node in self._snapshot["nodes"]
                ],
            }
        )
        self._refresh_task()

    def _refresh_task(self) -> None:
        node = next((item for item in self._snapshot["nodes"] if item["id"] == self.selected_id), None)
        if node is None:
            return
        self._updating = True
        descriptor = self.descriptor_resolver(node)
        self.node_id.setText(node["id"])
        self.node_name.setText(node.get("name", node["id"]))
        self.resource.setText(node["resource"])
        self.kernel.setEnabled(node["pack_id"] == "atlas.builtin")
        if self.kernel.findText(node["task_id"]) < 0:
            self.kernel.addItem(node["task_id"])
        self.kernel.setCurrentText(node["task_id"])
        self.priority.set_value(int(node.get("priority", 0)))
        key = (node["pack_id"], node["task_id"], str(descriptor))
        if key != self._parameter_key:
            while self.parameter_form.rowCount():
                row = self.parameter_form.takeRow(0)
                for item in (row.labelItem, row.fieldItem):
                    if item and item.widget():
                        item.widget().hide()
                        item.widget().deleteLater()
            self.parameter_fields = {}
            self._parameter_key = key
        for field in descriptor["parameters"] if descriptor else []:
            value = node["parameters"].get(field["id"], field.get("default"))
            kind = field["type"]
            widget = self.parameter_fields.get(field["id"])
            if widget is None:
                if kind == "boolean":
                    widget = QCheckBox()
                    widget.toggled.connect(self._update_task)
                elif kind == "enum":
                    widget = QComboBox()
                    widget.addItems(field["values"])
                    widget.currentTextChanged.connect(self._update_task)
                else:
                    widget = ScalarEdit()
                    if kind in {"integer", "unsigned_integer", "number"}:
                        widget.setValidator(ScalarValidator(field, widget))
                    if kind == "string":
                        widget.setMaxLength(field["max_length"])
                    widget.editingFinished.connect(self._update_task)
                self.parameter_fields[field["id"]] = widget
                self.parameter_form.addRow(field.get("name") or field["id"], widget)
            if kind == "boolean":
                widget.setChecked(bool(value))
            elif kind == "enum":
                widget.setCurrentText(str(value))
            else:
                widget.setText(str(value))
        sliced = node.get("slice_workgroups") is not None
        self.slicing.setChecked(sliced)
        self.slice_dimensions.set_dimensions(node.get("slice_workgroups") or {"x": 1, "y": 1, "z": 1})
        slicing = bool(descriptor and descriptor.get("supports_slicing", False))
        self.slicing.setVisible(slicing)
        self.slice_dimensions.setVisible(slicing and sliced)
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
        if kernel_type not in BUILTINS:
            return
        descriptor = BUILTINS[kernel_type]
        replacement: JsonObject = {
            **node,
            "resource": descriptor["resource"],
            "pack_id": "atlas.builtin",
            "task_id": kernel_type,
            "parameters": default_parameters(descriptor),
        }
        replacement.pop("slice_workgroups", None)
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
        descriptor = self.descriptor_resolver(node)
        if descriptor:
            parameters: JsonObject = {}
            try:
                for field in descriptor["parameters"]:
                    widget = self.parameter_fields[field["id"]]
                    kind = field["type"]
                    if kind == "boolean":
                        value = widget.isChecked()
                    elif kind == "enum":
                        value = widget.currentText()
                    elif kind in {"integer", "unsigned_integer"}:
                        value = int(widget.text())
                    elif kind == "number":
                        value = float(widget.text())
                    else:
                        value = widget.text()
                    parameters[field["id"]] = value
            except ValueError:
                self.message.emit("Parameter must match its declared type")
                self._refresh_task()
                return
            replacement["parameters"] = parameters
        if replacement["resource"] == "gpu":
            replacement["slice_workgroups"] = (
                self.slice_dimensions.dimensions() if self.slicing.isChecked() else None
            )
            self.slice_dimensions.setVisible(self.slicing.isChecked())
        self.task_update_requested.emit(previous, replacement)

    def _selected_node(self) -> JsonObject | None:
        return next((node for node in self._snapshot["nodes"] if node["id"] == self.selected_id), None)

    def _toggle_connecting(self, enabled: bool) -> None:
        self.canvas.set_connecting(enabled)
        self.connect_button.setText("Click dependency, then dependent" if enabled else "Connect tasks")

    def _request_remove(self) -> None:
        if self.selected_id:
            self.task_remove_requested.emit(self.selected_id)
