"""Reusable typed form fields."""

from __future__ import annotations

from PySide6.QtCore import QRegularExpression, Signal
from PySide6.QtGui import QRegularExpressionValidator, QValidator
from PySide6.QtWidgets import QFormLayout, QGroupBox, QHBoxLayout, QLineEdit

from ..models.documents import JsonObject


class UIntEdit(QLineEdit):
    value_changed = Signal(object)

    def __init__(self, value: int = 0, maximum: int = 2**64 - 1) -> None:
        super().__init__(str(value))
        self.maximum = maximum
        self.setValidator(QRegularExpressionValidator(QRegularExpression(r"[0-9]+"), self))
        self.editingFinished.connect(self._commit)

    def value(self) -> int:
        return min(int(self.text() or "0"), self.maximum)

    def set_value(self, value: int) -> None:
        self.setText(str(value))

    def _commit(self) -> None:
        value = self.value()
        self.setText(str(value))
        self.value_changed.emit(value)


class DimensionsEditor(QGroupBox):
    changed = Signal(dict)

    def __init__(self, title: str) -> None:
        super().__init__(title)
        layout = QHBoxLayout(self)
        self.edits: dict[str, UIntEdit] = {}
        for axis in ("x", "y", "z"):
            edit = UIntEdit(1, 2**32 - 1)
            edit.value_changed.connect(self._emit)
            self.edits[axis] = edit
            form = QFormLayout()
            form.addRow(axis.upper(), edit)
            layout.addLayout(form)

    def set_dimensions(self, value: JsonObject) -> None:
        for axis, edit in self.edits.items():
            edit.set_value(int(value.get(axis, 1)))

    def dimensions(self) -> JsonObject:
        return {axis: edit.value() for axis, edit in self.edits.items()}

    def _emit(self, _value: int) -> None:
        self.changed.emit(self.dimensions())


class ScalarValidator(QValidator):
    """Validate exact 64-bit integers and finite numeric bounds without float rounding."""

    def __init__(self, field, parent=None):
        super().__init__(parent)
        self.field = field

    def validate(self, text, position):
        from ..models.descriptors import validate_parameters

        try:
            value = float(text) if self.field["type"] == "number" else int(text)
            errors = validate_parameters({"parameters": [self.field]}, {self.field["id"]: value})
            state = QValidator.State.Acceptable if not errors else QValidator.State.Intermediate
        except ValueError:
            state = QValidator.State.Intermediate
        return state, text, position


class ScalarEdit(QLineEdit):
    """Commit invalid numeric text on focus loss so the controller restores valid state."""

    def focusOutEvent(self, event):
        valid = self.hasAcceptableInput()
        super().focusOutEvent(event)
        if not valid:
            self.editingFinished.emit()
