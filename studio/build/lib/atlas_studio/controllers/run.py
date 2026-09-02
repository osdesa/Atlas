"""Controller for Atlas process execution and protocol handling."""

from __future__ import annotations

import json
from pathlib import Path

from PySide6.QtCore import QObject, Signal

from ..models.documents import JsonObject
from ..models.protocol import JsonlRecordDecoder
from ..services.imports import load_benchmark_results
from ..services.process import AtlasProcessService
from .results import ResultsController


class RunController(QObject):
    """Coordinate run requests, protocol decoding, results, and process lifecycle."""

    run_started = Signal(str)
    run_finished = Signal(int, str)

    def __init__(self, service: AtlasProcessService, results: ResultsController) -> None:
        super().__init__(service)
        self.service = service
        self.results = results
        self._decoder: JsonlRecordDecoder | None = None
        self._machine_output = True
        self._kind: str | None = None
        service.stdout_line.connect(self._stdout_line)
        service.diagnostic_received.connect(results.add_diagnostic)
        service.state_changed.connect(results.set_state)
        service.run_started.connect(self.run_started)
        service.run_finished.connect(self._finished)

    @property
    def active(self) -> bool:
        return self.service.active

    def start_graph(self, document: JsonObject) -> None:
        self._prepare("graph", True)
        self.service.start_graph(document)

    def start_benchmark(
        self,
        document: JsonObject,
        output_directory: Path,
        environment_file: Path | None,
        *,
        live_tracing: bool,
    ) -> None:
        self._prepare("benchmark", live_tracing)
        self.service.start_benchmark(document, output_directory, environment_file, live_tracing=live_tracing)

    def stop(self) -> None:
        self.service.stop()

    def _prepare(self, kind: str, machine_output: bool) -> None:
        self.results.reset("Starting")
        self._kind = kind
        self._machine_output = machine_output
        self._decoder = JsonlRecordDecoder() if machine_output else None

    def _stdout_line(self, line: bytes) -> None:
        if not self._machine_output:
            self.results.add_diagnostic(line.decode("utf-8", errors="replace"))
            return
        try:
            assert self._decoder is not None
            self.results.receive_record(self._decoder.decode(line))
        except ValueError as error:
            self.service.fail_output(str(error))

    def _finished(self, exit_code: int, state: str) -> None:
        if self._decoder is not None and state == "complete":
            try:
                self._decoder.finish()
            except ValueError as error:
                self.results.add_diagnostic(str(error))
                state = "failed"
        if self._kind == "benchmark" and self.service.output_directory is not None:
            try:
                self.results.set_benchmark_results(load_benchmark_results(self.service.output_directory))
            except (OSError, ValueError, json.JSONDecodeError) as error:
                self.results.add_diagnostic(f"unable to load benchmark results: {error}")
        self.results.set_state(state)
        self.run_finished.emit(exit_code, state)
