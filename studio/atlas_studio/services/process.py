"""Threaded Qt process supervision for Atlas Studio."""

from __future__ import annotations

import json
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

from PySide6.QtCore import QObject, QProcess, QProcessEnvironment, QThread, QTimer, Signal, Slot

from ..models.documents import JsonObject
from ..models.protocol import MAX_JSONL_LINE_BYTES, JsonlRecordDecoder
from ..models.validation import ROOT
from .imports import load_benchmark_results
from .launch import discover_executable, prepare_benchmark_output_directory
from .streams import BoundedLineBuffer


@dataclass(frozen=True)
class _RunRequest:
    kind: str
    document: JsonObject
    output_directory: Path | None = None
    environment_file: Path | None = None
    live_tracing: bool = True


class _AtlasProcessWorker(QObject):
    """Own one Atlas process and all blocking protocol work on a worker thread."""

    records_received = Signal(object)
    diagnostic_received = Signal(str)
    state_changed = Signal(str)
    run_started = Signal(str)
    output_directory_selected = Signal(object)
    benchmark_results_received = Signal(object)
    completed = Signal(int, str)

    def __init__(self, request: _RunRequest) -> None:
        super().__init__()
        self._request = request
        self._process: QProcess | None = None
        self._terminate_timer: QTimer | None = None
        self._kill_timer: QTimer | None = None
        self._stdout = BoundedLineBuffer(MAX_JSONL_LINE_BYTES)
        self._stderr = BoundedLineBuffer(MAX_JSONL_LINE_BYTES)
        self._temporary: tempfile.TemporaryDirectory[str] | None = None
        self._control_path: Path | None = None
        self._output_directory: Path | None = None
        self._decoder = JsonlRecordDecoder() if request.kind == "graph" or request.live_tracing else None
        self._stopping = False
        self._output_failed = False
        self._complete = False

    @Slot()
    def start(self) -> None:
        """Prepare and start the configured run from the worker thread."""
        try:
            executable, arguments = self._prepare_launch()
            self._process = QProcess(self)
            self._process.setProcessChannelMode(QProcess.SeparateChannels)
            self._process.setProcessEnvironment(QProcessEnvironment.systemEnvironment())
            self._process.setWorkingDirectory(str(ROOT))
            self._process.readyReadStandardOutput.connect(self._read_stdout)
            self._process.readyReadStandardError.connect(self._read_stderr)
            self._process.started.connect(self._started)
            self._process.finished.connect(self._finished)
            self._process.errorOccurred.connect(self._process_error)
            self._terminate_timer = self._timer(self._terminate_if_running)
            self._kill_timer = self._timer(self._kill_if_running)
            if executable.suffix.casefold() == ".py":
                self._process.setProgram(sys.executable)
                self._process.setArguments([str(executable), *arguments])
            else:
                self._process.setProgram(str(executable))
                self._process.setArguments(arguments)
            self.state_changed.emit("starting")
            self._process.start()
        except Exception as error:
            self.diagnostic_received.emit(str(error))
            self._finish_run(-1, "failed")

    @Slot()
    def stop(self) -> None:
        """Request cooperative cancellation, then bounded process termination."""
        if self._complete or self._stopping:
            return
        self._stopping = True
        self.state_changed.emit("cancelling")
        process = self._process
        if process is None or process.state() == QProcess.NotRunning:
            return
        if self._request.kind == "graph" and self._control_path is not None:
            try:
                self._control_path.write_text("terminate\n", encoding="utf-8")
                assert self._terminate_timer is not None
                self._terminate_timer.start(10_000)
            except OSError as error:
                self.diagnostic_received.emit(f"unable to request cooperative cancellation: {error}")
                process.terminate()
                assert self._kill_timer is not None
                self._kill_timer.start(5_000)
        else:
            process.terminate()
            assert self._kill_timer is not None
            self._kill_timer.start(5_000)

    def _prepare_launch(self) -> tuple[Path, list[str]]:
        self._temporary = tempfile.TemporaryDirectory(prefix="atlas-studio-")
        directory = Path(self._temporary.name)
        if self._request.kind == "graph":
            executable = discover_executable(
                "ATLAS_STUDIO_RUNNER", "apps/atlas_studio_runner/atlas_studio_runner"
            )
            if executable is None:
                raise FileNotFoundError(
                    "atlas_studio_runner was not found; build Atlas or set ATLAS_STUDIO_RUNNER"
                )
            config_path = directory / "graph.json"
            config_path.write_text(json.dumps(self._request.document, indent=2), encoding="utf-8")
            self._control_path = directory / "cancel"
            return executable, ["--config", str(config_path), "--control", str(self._control_path)]

        executable = discover_executable("ATLAS_BENCH", "apps/atlas_bench/atlas_bench")
        if executable is None:
            raise FileNotFoundError("atlas_bench was not found; build Atlas or set ATLAS_BENCH")
        assert self._request.output_directory is not None
        selected = self._request.output_directory
        self._output_directory = prepare_benchmark_output_directory(selected)
        self.output_directory_selected.emit(self._output_directory)
        if self._output_directory != selected:
            self.diagnostic_received.emit(
                f"output directory contains files; writing this run to {self._output_directory}"
            )
        suite_path = directory / "suite.json"
        suite_path.write_text(json.dumps(self._request.document, indent=2), encoding="utf-8")
        arguments = ["--suite", str(suite_path), "--output-dir", str(self._output_directory)]
        if self._request.environment_file is not None:
            arguments.extend(["--environment-file", str(self._request.environment_file)])
        if self._request.live_tracing:
            arguments.append("--studio-progress-jsonl")
        else:
            self.diagnostic_received.emit("live benchmark task tracing is disabled for this run")
        return executable, arguments

    def _timer(self, callback: object) -> QTimer:
        timer = QTimer(self)
        timer.setSingleShot(True)
        timer.timeout.connect(callback)  # type: ignore[arg-type]
        return timer

    @Slot()
    def _started(self) -> None:
        self.state_changed.emit("running")
        self.run_started.emit(self._request.kind)

    @Slot()
    def _read_stdout(self) -> None:
        assert self._process is not None
        try:
            lines = self._stdout.feed(bytes(self._process.readAllStandardOutput()))
            self._publish_stdout(lines)
        except ValueError as error:
            self._fail_output(str(error))

    @Slot()
    def _read_stderr(self) -> None:
        assert self._process is not None
        try:
            lines = self._stderr.feed(bytes(self._process.readAllStandardError()))
        except ValueError as error:
            self.diagnostic_received.emit(str(error))
            self._process.kill()
            return
        for line in lines:
            if line:
                self.diagnostic_received.emit(line.decode("utf-8", errors="replace"))

    def _publish_stdout(self, lines: list[bytes]) -> None:
        records: list[JsonObject] = []
        for line in lines:
            if not line:
                continue
            if self._decoder is None:
                self.diagnostic_received.emit(line.decode("utf-8", errors="replace"))
                continue
            try:
                records.append(self._decoder.decode(line))
            except ValueError as error:
                if records:
                    self.records_received.emit(tuple(records))
                self._fail_output(str(error))
                return
        if records:
            self.records_received.emit(tuple(records))

    @Slot(int, QProcess.ExitStatus)
    def _finished(self, exit_code: int, _status: QProcess.ExitStatus) -> None:
        self._read_stdout()
        self._read_stderr()
        remainder = self._stdout.finish()
        if remainder:
            self._publish_stdout([remainder])
        diagnostic = self._stderr.finish()
        if diagnostic:
            self.diagnostic_received.emit(diagnostic.decode("utf-8", errors="replace"))
        state = (
            "failed"
            if self._output_failed
            else ("cancelled" if self._stopping else ("complete" if exit_code == 0 else "failed"))
        )
        if self._decoder is not None and state == "complete":
            try:
                self._decoder.finish()
            except ValueError as error:
                self.diagnostic_received.emit(str(error))
                state = "failed"
        self._load_results()
        self._finish_run(exit_code, state)

    def _load_results(self) -> None:
        if self._request.kind != "benchmark" or self._output_directory is None:
            return
        try:
            self.benchmark_results_received.emit(load_benchmark_results(self._output_directory))
        except (OSError, ValueError, json.JSONDecodeError) as error:
            self.diagnostic_received.emit(f"unable to load benchmark results: {error}")

    @Slot(QProcess.ProcessError)
    def _process_error(self, error: QProcess.ProcessError) -> None:
        assert self._process is not None
        self.diagnostic_received.emit(self._process.errorString())
        if error == QProcess.FailedToStart:
            self._finish_run(-1, "failed")

    @Slot()
    def _terminate_if_running(self) -> None:
        assert self._process is not None
        if self._process.state() != QProcess.NotRunning:
            self.diagnostic_received.emit("cooperative cancellation timed out; terminating runner")
            self._process.terminate()
            assert self._kill_timer is not None
            self._kill_timer.start(2_000)

    @Slot()
    def _kill_if_running(self) -> None:
        assert self._process is not None
        if self._process.state() != QProcess.NotRunning:
            self.diagnostic_received.emit("runner did not terminate; killing process")
            self._process.kill()

    def _fail_output(self, message: str) -> None:
        if self._output_failed:
            return
        self._output_failed = True
        self.diagnostic_received.emit(message)
        assert self._process is not None
        self._process.kill()

    def _finish_run(self, exit_code: int, state: str) -> None:
        if self._complete:
            return
        self._complete = True
        if self._terminate_timer is not None:
            self._terminate_timer.stop()
        if self._kill_timer is not None:
            self._kill_timer.stop()
        self._temporary = None
        self.state_changed.emit(state)
        self.completed.emit(exit_code, state)


class AtlasProcessService(QObject):
    """Run one Atlas child process on a dedicated Qt worker thread."""

    records_received = Signal(object)
    benchmark_results_received = Signal(object)
    diagnostic_received = Signal(str)
    state_changed = Signal(str)
    run_started = Signal(str)
    run_finished = Signal(int, str)

    _stop_requested = Signal()

    def __init__(self, parent: QObject | None = None) -> None:
        super().__init__(parent)
        self._thread: QThread | None = None
        self._worker: _AtlasProcessWorker | None = None
        self._active = False
        self._output_directory: Path | None = None
        self._pending_completion: tuple[int, str] | None = None

    @property
    def active(self) -> bool:
        return self._active

    @property
    def output_directory(self) -> Path | None:
        return self._output_directory

    def start_graph(self, document: JsonObject) -> None:
        self._launch(_RunRequest("graph", document))

    def start_benchmark(
        self,
        suite: JsonObject,
        output_directory: Path,
        environment_file: Path | None = None,
        *,
        live_tracing: bool = True,
    ) -> None:
        self._launch(_RunRequest("benchmark", suite, output_directory, environment_file, live_tracing))

    def stop(self) -> None:
        if self._active:
            self._stop_requested.emit()

    def _launch(self, request: _RunRequest) -> None:
        if self._active or self._thread is not None:
            raise RuntimeError("one Atlas run is already active")
        self._active = True
        self._output_directory = None
        self._pending_completion = None
        thread = QThread(self)
        thread.setObjectName("AtlasProcessThread")
        worker = _AtlasProcessWorker(request)
        worker.moveToThread(thread)
        thread.started.connect(worker.start)
        self._stop_requested.connect(worker.stop)
        worker.records_received.connect(self.records_received)
        worker.diagnostic_received.connect(self.diagnostic_received)
        worker.state_changed.connect(self.state_changed)
        worker.run_started.connect(self.run_started)
        worker.output_directory_selected.connect(self._select_output_directory)
        worker.benchmark_results_received.connect(self.benchmark_results_received)
        worker.completed.connect(worker.deleteLater)
        worker.completed.connect(self._worker_completed)
        thread.finished.connect(self._thread_finished)
        self._thread = thread
        self._worker = worker
        thread.start()

    @Slot(object)
    def _select_output_directory(self, output_directory: object) -> None:
        self._output_directory = output_directory if isinstance(output_directory, Path) else None

    @Slot(int, str)
    def _worker_completed(self, exit_code: int, state: str) -> None:
        self._pending_completion = (exit_code, state)
        assert self._thread is not None
        self._thread.quit()

    @Slot()
    def _thread_finished(self) -> None:
        completion = self._pending_completion or (-1, "failed")
        thread = self._thread
        self._worker = None
        self._thread = None
        self._active = False
        self._pending_completion = None
        if thread is not None:
            thread.deleteLater()
        self.run_finished.emit(*completion)
