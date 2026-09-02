"""Focused structured editors for benchmark cases and variants."""

from __future__ import annotations

import copy
from typing import Any

from PySide6.QtCore import Signal
from PySide6.QtWidgets import QCheckBox, QComboBox, QDoubleSpinBox, QFormLayout, QLineEdit, QWidget

from ..models.documents import JsonObject
from .fields import DimensionsEditor, UIntEdit


class BenchmarkCaseEditor(QWidget):
    """Map one benchmark case between controls and a schema-shaped value."""

    changed = Signal(dict)
    message = Signal(str)

    def __init__(self) -> None:
        super().__init__()
        self._case: JsonObject = {}
        self._updating = False
        form = QFormLayout(self)
        self.case_id = QLineEdit()
        self.reference_variant = QLineEdit()
        self.cpu_tasks = UIntEdit()
        self.cpu_iterations = UIntEdit()
        self.gpu_tasks = UIntEdit()
        self.gpu_workgroups = DimensionsEditor("GPU workgroups")
        self.dependency_shape = QComboBox()
        self.dependency_shape.addItems(["independent", "chain", "layered", "random"])
        self.layers = UIntEdit(1)
        self.edge_probability = QDoubleSpinBox()
        self.edge_probability.setRange(0, 1)
        self.edge_probability.setDecimals(6)
        self.edge_probability.setSingleStep(0.05)
        self.priority_assignment = QComboBox()
        self.priority_assignment.addItems(["cycle", "random"])
        self.priority_values = QLineEdit()
        self.burst_count = UIntEdit(1)
        for label, control in (
            ("Case ID", self.case_id),
            ("Reference variant", self.reference_variant),
            ("CPU task count", self.cpu_tasks),
            ("CPU iterations", self.cpu_iterations),
            ("GPU task count", self.gpu_tasks),
            ("Dependencies", self.dependency_shape),
            ("Layers", self.layers),
            ("Edge probability", self.edge_probability),
            ("Priority assignment", self.priority_assignment),
            ("Priority values", self.priority_values),
            ("Burst count", self.burst_count),
        ):
            form.addRow(label, control)
        form.addRow(self.gpu_workgroups)
        signals = [
            self.case_id.editingFinished,
            self.reference_variant.editingFinished,
            self.cpu_tasks.value_changed,
            self.cpu_iterations.value_changed,
            self.gpu_tasks.value_changed,
            self.gpu_workgroups.changed,
            self.dependency_shape.currentTextChanged,
            self.layers.value_changed,
            self.edge_probability.valueChanged,
            self.priority_assignment.currentTextChanged,
            self.priority_values.editingFinished,
            self.burst_count.value_changed,
        ]
        for signal in signals:
            signal.connect(self._emit_candidate)

    def load(self, case: JsonObject) -> None:
        self._case = copy.deepcopy(case)
        workload = case["workload"]
        dependency = workload["dependencies"]
        self._updating = True
        self.case_id.setText(case["case_id"])
        self.reference_variant.setText(case["reference_variant"])
        self.cpu_tasks.set_value(workload["cpu"]["task_count"])
        self.cpu_iterations.set_value(workload["cpu"]["iterations"])
        self.gpu_tasks.set_value(workload["gpu"]["task_count"])
        self.gpu_workgroups.set_dimensions(workload["gpu"]["workgroups"])
        self.dependency_shape.setCurrentText(dependency["shape"])
        self.layers.set_value(dependency.get("layers", 1))
        self.edge_probability.setValue(dependency.get("edge_probability", 0.1))
        self.priority_assignment.setCurrentText(workload["priorities"]["assignment"])
        self.priority_values.setText(", ".join(map(str, workload["priorities"]["values"])))
        self.burst_count.set_value(workload["bursts"]["count"])
        self._update_visibility()
        self._updating = False

    def _emit_candidate(self, *_args: Any) -> None:
        if self._updating:
            return
        try:
            priorities = [
                int(value.strip()) for value in self.priority_values.text().split(",") if value.strip()
            ]
        except ValueError:
            self.message.emit("Priority values must be comma-separated unsigned integers.")
            return
        dependency: JsonObject = {"shape": self.dependency_shape.currentText()}
        if dependency["shape"] == "layered":
            dependency["layers"] = self.layers.value()
        if dependency["shape"] == "random":
            dependency["edge_probability"] = self.edge_probability.value()
        replacement = {
            **self._case,
            "case_id": self.case_id.text(),
            "reference_variant": self.reference_variant.text(),
            "workload": {
                "cpu": {"task_count": self.cpu_tasks.value(), "iterations": self.cpu_iterations.value()},
                "gpu": {"task_count": self.gpu_tasks.value(), "workgroups": self.gpu_workgroups.dimensions()},
                "dependencies": dependency,
                "priorities": {"assignment": self.priority_assignment.currentText(), "values": priorities},
                "bursts": {"count": self.burst_count.value()},
            },
        }
        self._update_visibility()
        self.changed.emit(replacement)

    def _update_visibility(self) -> None:
        self.layers.setVisible(self.dependency_shape.currentText() == "layered")
        self.edge_probability.setVisible(self.dependency_shape.currentText() == "random")


class BenchmarkVariantEditor(QWidget):
    """Map one benchmark variant between controls and a schema-shaped value."""

    changed = Signal(dict)

    def __init__(self) -> None:
        super().__init__()
        self._updating = False
        form = QFormLayout(self)
        self.variant_id = QLineEdit()
        self.execution = QComboBox()
        self.execution.addItems(["direct", "scheduled"])
        self.policy = QComboBox()
        self.policy.addItems(["fifo", "round_robin", "static_priority"])
        self.quantum = UIntEdit(1)
        self.slicing = QCheckBox("Cooperative slicing")
        self.slice_dimensions = DimensionsEditor("Slice workgroups")
        form.addRow("Variant ID", self.variant_id)
        form.addRow("Execution", self.execution)
        form.addRow("Policy", self.policy)
        form.addRow("Quantum", self.quantum)
        form.addRow(self.slicing)
        form.addRow(self.slice_dimensions)
        for signal in (
            self.variant_id.editingFinished,
            self.execution.currentTextChanged,
            self.policy.currentTextChanged,
            self.quantum.value_changed,
            self.slicing.toggled,
            self.slice_dimensions.changed,
        ):
            signal.connect(self._emit_candidate)

    def load(self, variant: JsonObject) -> None:
        self._updating = True
        self.variant_id.setText(variant["variant_id"])
        self.execution.setCurrentText(variant["execution"])
        self.policy.setCurrentText(variant.get("policy", {}).get("type", "fifo"))
        self.quantum.set_value(variant.get("policy", {}).get("quantum", 1))
        sliced = variant.get("slice_workgroups") is not None
        self.slicing.setChecked(sliced)
        self.slice_dimensions.set_dimensions(variant.get("slice_workgroups") or {"x": 1, "y": 1, "z": 1})
        self._update_visibility()
        self._updating = False

    def _emit_candidate(self, *_args: Any) -> None:
        if self._updating:
            return
        variant: JsonObject = {
            "variant_id": self.variant_id.text(),
            "execution": self.execution.currentText(),
        }
        if variant["execution"] == "scheduled":
            policy: JsonObject = {"type": self.policy.currentText()}
            if policy["type"] == "round_robin":
                policy["quantum"] = self.quantum.value()
            variant.update(
                {
                    "policy": policy,
                    "slice_workgroups": self.slice_dimensions.dimensions()
                    if self.slicing.isChecked()
                    else None,
                }
            )
        self._update_visibility()
        self.changed.emit(variant)

    def _update_visibility(self) -> None:
        scheduled = self.execution.currentText() == "scheduled"
        self.policy.setVisible(scheduled)
        self.quantum.setVisible(scheduled and self.policy.currentText() == "round_robin")
        self.slicing.setVisible(scheduled)
        self.slice_dimensions.setVisible(scheduled and self.slicing.isChecked())
