"""Controller for live and imported result presentation."""

from __future__ import annotations

from PySide6.QtCore import QObject, QTimer

from ..models.documents import JsonObject
from ..models.results import ResultsSessionModel
from ..views.results import ResultsView


class ResultsController(QObject):
    """Reduce result events and schedule bounded view refreshes."""

    def __init__(self, model: ResultsSessionModel, view: ResultsView) -> None:
        super().__init__(view)
        self.model = model
        self.view = view
        self._render_timer = QTimer(self)
        self._render_timer.setSingleShot(True)
        self._render_timer.timeout.connect(self.render)
        view.run_selection_requested.connect(self.select_run)
        self.render()

    def reset(self, status: str = "Ready") -> None:
        self._render_timer.stop()
        self.model.reset(status)
        self.render()

    def receive_record(self, record: JsonObject) -> None:
        self.model.consume(record)
        if record.get("record_type") in {
            "header",
            "run_started",
            "run_finished",
            "result",
            "footer",
            "error",
        }:
            self.render()
        elif not self._render_timer.isActive():
            self._render_timer.start(50)

    def add_diagnostic(self, text: str) -> None:
        self.model.add_diagnostic(text)
        self.render()

    def set_state(self, state: str) -> None:
        self.model.set_state(state)
        self.render()

    def set_benchmark_results(self, results: JsonObject) -> None:
        self.model.set_benchmark_results(results)
        self.render()

    def load_records(self, records: list[JsonObject]) -> None:
        self.reset("Imported")
        for record in records:
            self.model.consume(record)
        self.render()

    def select_run(self, run_id: object) -> None:
        self.model.select_run(int(run_id) if run_id is not None else None)
        self.render()

    def render(self) -> None:
        self._render_timer.stop()
        self.view.render(self.model.snapshot())
