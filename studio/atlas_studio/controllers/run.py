"""Controller for Atlas process execution and protocol handling."""

from __future__ import annotations

from collections import deque
from pathlib import Path
from time import monotonic

from PySide6.QtCore import QObject, QTimer, Signal

from ..models.documents import JsonObject
from ..services.process import AtlasProcessService
from .results import ResultsController

_RECORD_SLICE_SECONDS = 0.005
_RECORD_SLICE_LIMIT = 256


class RunController(QObject):
    """Coordinate run requests, time-sliced results, and process lifecycle."""

    run_started = Signal(str)
    run_finished = Signal(int, str)

    def __init__(self, service: AtlasProcessService, results: ResultsController) -> None:
        super().__init__(service)
        self.service = service
        self.results = results
        self._pending_records: deque[JsonObject] = deque()
        self._pending_benchmark_results: JsonObject | None = None
        self._pending_finish: tuple[int, str] | None = None
        self._record_timer = QTimer(self)
        self._record_timer.setSingleShot(True)
        self._record_timer.timeout.connect(self._drain_records)
        service.records_received.connect(self._records_received)
        service.benchmark_results_received.connect(self._benchmark_results_received)
        service.diagnostic_received.connect(results.add_diagnostic)
        service.state_changed.connect(results.set_state)
        service.run_started.connect(self.run_started)
        service.run_finished.connect(self._finished)

    @property
    def active(self) -> bool:
        return self.service.active

    def start_graph(self, document: JsonObject) -> None:
        self._prepare()
        self.service.start_graph(document)

    def start_benchmark(
        self,
        document: JsonObject,
        output_directory: Path,
        environment_file: Path | None,
        *,
        live_tracing: bool,
    ) -> None:
        self._prepare()
        self.service.start_benchmark(document, output_directory, environment_file, live_tracing=live_tracing)

    def stop(self) -> None:
        self.service.stop()

    def _prepare(self) -> None:
        self.results.reset("Starting")
        self._record_timer.stop()
        self._pending_records.clear()
        self._pending_benchmark_results = None
        self._pending_finish = None

    def _records_received(self, records: object) -> None:
        assert isinstance(records, tuple)
        self._pending_records.extend(records)
        if self._pending_records and not self._record_timer.isActive():
            self._record_timer.start(0)

    def _benchmark_results_received(self, results: object) -> None:
        assert isinstance(results, dict)
        self._pending_benchmark_results = results

    def _drain_records(self) -> None:
        deadline = monotonic() + _RECORD_SLICE_SECONDS
        consumed = 0
        while self._pending_records and consumed < _RECORD_SLICE_LIMIT and monotonic() < deadline:
            self.results.receive_record(self._pending_records.popleft())
            consumed += 1
        if self._pending_records:
            self._record_timer.start(0)
        elif self._pending_finish is not None:
            exit_code, state = self._pending_finish
            self._pending_finish = None
            self._emit_finished(exit_code, state)

    def _finished(self, exit_code: int, state: str) -> None:
        if self._pending_records:
            self._pending_finish = (exit_code, state)
            if not self._record_timer.isActive():
                self._record_timer.start(0)
            return
        self._emit_finished(exit_code, state)

    def _emit_finished(self, exit_code: int, state: str) -> None:
        if self._pending_benchmark_results is not None:
            self.results.set_benchmark_results(self._pending_benchmark_results)
            self._pending_benchmark_results = None
        self.results.set_state(state)
        self.results.render()
        self.run_finished.emit(exit_code, state)
