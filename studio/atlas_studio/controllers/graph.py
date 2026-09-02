"""Controller for explicit graph authoring."""

from __future__ import annotations

from collections.abc import Callable

from PySide6.QtCore import QObject

from ..models.documents import DocumentError, JsonObject
from ..models.graph import GraphDocumentModel
from ..views.graph import GraphView


class GraphController(QObject):
    """Translate graph-view intent into atomic graph-model commands."""

    def __init__(self, model: GraphDocumentModel, view: GraphView) -> None:
        super().__init__(view)
        self.model = model
        self.view = view
        self.selected_id = model.snapshot()["nodes"][0]["id"]
        view.settings_requested.connect(lambda value: self._change(lambda: model.update_settings(value)))
        view.task_add_requested.connect(self._add_task)
        view.task_remove_requested.connect(self._remove_task)
        view.task_update_requested.connect(self._update_task)
        view.dependency_add_requested.connect(
            lambda source, target: self._change(lambda: model.add_dependency(source, target))
        )
        view.selection_requested.connect(self._select)
        self.render()

    def replace(self, document: JsonObject) -> None:
        self.model.replace(document)
        self.selected_id = document["nodes"][0]["id"]
        self.render()

    def render(self) -> None:
        self.view.render(self.model.snapshot(), self.selected_id)

    def _select(self, identifier: str) -> None:
        if any(node["id"] == identifier for node in self.model.snapshot()["nodes"]):
            self.selected_id = identifier
            self.render()

    def _add_task(self, resource: str) -> None:
        if resource not in {"cpu", "gpu"}:
            self.view.message.emit(f"unknown task resource: {resource}")
            return
        self._change_selection(lambda: self.model.add_task(resource))

    def _remove_task(self, identifier: str) -> None:
        self._change_selection(lambda: self.model.remove_task(identifier))

    def _update_task(self, identifier: str, replacement: JsonObject) -> None:
        self._change_selection(lambda: self.model.update_task(identifier, replacement))

    def _change_selection(self, command: Callable[[], str]) -> None:
        try:
            self.selected_id = command()
        except (DocumentError, KeyError, IndexError) as error:
            self.view.message.emit(str(error))
        self.render()

    def _change(self, command: Callable[[], None]) -> None:
        try:
            command()
        except (DocumentError, KeyError, IndexError) as error:
            self.view.message.emit(str(error))
        self.render()
