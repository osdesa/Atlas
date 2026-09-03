"""Controller for benchmark-suite authoring."""

from __future__ import annotations

import json
from collections.abc import Callable

from PySide6.QtCore import QObject

from ..models.benchmark import BenchmarkDocumentModel, BenchmarkSelection
from ..models.documents import DocumentError, JsonObject, StudioSessionModel
from ..views.benchmark import BenchmarkView


class BenchmarkController(QObject):
    """Translate benchmark-view intent into atomic benchmark-model commands."""

    def __init__(
        self, model: BenchmarkDocumentModel, view: BenchmarkView, session: StudioSessionModel
    ) -> None:
        super().__init__(view)
        self.model = model
        self.view = view
        self.session = session
        self.selection: BenchmarkSelection | None = BenchmarkSelection("case", 0)
        view.settings_requested.connect(lambda value: self._change(lambda: model.update_settings(value)))
        view.case_add_requested.connect(self._add_case)
        view.case_update_requested.connect(
            lambda index, value: self._change(lambda: model.update_case(index, value))
        )
        view.case_remove_requested.connect(lambda index: self._change(lambda: model.remove_case(index), None))
        view.variant_add_requested.connect(self._add_variant)
        view.variant_update_requested.connect(
            lambda case_index, variant_index, value: self._change(
                lambda: model.update_variant(case_index, variant_index, value)
            )
        )
        view.variant_remove_requested.connect(
            lambda case_index, variant_index: self._change(
                lambda: model.remove_variant(case_index, variant_index), None
            )
        )
        view.document_replace_requested.connect(self._replace_json)
        view.selection_requested.connect(self._selection_changed)
        view.run_options_requested.connect(self._run_options_changed)
        self.render()

    def replace(self, document: JsonObject) -> None:
        self.model.replace(document)
        self.selection = BenchmarkSelection("case", 0)
        self.render()

    def render(self) -> None:
        self.view.render(self.model.snapshot(), self.selection)

    def _selection_changed(self, selection: object) -> None:
        if isinstance(selection, BenchmarkSelection):
            self.selection = selection

    def _run_options_changed(self, values: JsonObject) -> None:
        self.session.benchmark_options.output_directory = str(values["output_directory"])
        self.session.benchmark_options.environment_file = str(values["environment_file"])
        self.session.benchmark_options.live_tracing = bool(values["live_tracing"])

    def _add_case(self) -> None:
        index = self.model.add_case()
        self.selection = BenchmarkSelection("case", index)
        self.render()

    def _add_variant(self, case_index: int) -> None:
        index = self.model.add_variant(case_index)
        self.selection = BenchmarkSelection("variant", case_index, index)
        self.render()

    def _replace_json(self, text: str) -> None:
        try:
            value = json.loads(text)
            if not isinstance(value, dict):
                raise DocumentError("document must be a JSON object")
            self.replace(value)
            self.view.message.emit("Benchmark JSON applied.")
        except (json.JSONDecodeError, DocumentError) as error:
            self.view.show_validation_error(str(error))
            self.render()

    def _change(
        self, command: Callable[[], None], selection: BenchmarkSelection | None | object = ...
    ) -> None:
        try:
            command()
            if selection is not ...:
                self.selection = selection if isinstance(selection, BenchmarkSelection) else None
        except (DocumentError, KeyError, IndexError) as error:
            self.view.message.emit(str(error))
        self.render()
