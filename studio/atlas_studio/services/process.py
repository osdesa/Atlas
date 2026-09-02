"""Qt process supervision for Atlas Studio."""

from __future__ import annotations

import json
import tempfile
from pathlib import Path

from PySide6.QtCore import QObject, QProcess, QProcessEnvironment, QTimer, Signal

from ..models.documents import JsonObject
from ..models.protocol import MAX_JSONL_LINE_BYTES
from ..models.validation import ROOT
from .launch import discover_executable, prepare_benchmark_output_directory
from .streams import BoundedLineBuffer


class AtlasProcessService(QObject):
    """Own one Atlas child process and expose bounded raw process events."""

    stdout_line = Signal(bytes)
    diagnostic_received = Signal(str)
    state_changed = Signal(str)
    run_started = Signal(str)
    run_finished = Signal(int, str)

    def __init__(self, parent: QObject | None = None) -> None:
        super().__init__(parent)
        self.process = QProcess(self)
        self.process.setProcessChannelMode(QProcess.SeparateChannels)
        self.process.readyReadStandardOutput.connect(self._read_stdout)
        self.process.readyReadStandardError.connect(self._read_stderr)
        self.process.finished.connect(self._finished)
        self.process.errorOccurred.connect(self._process_error)
        self.process.setProcessEnvironment(QProcessEnvironment.systemEnvironment())
        self._terminate_timer = QTimer(self)
        self._terminate_timer.setSingleShot(True)
        self._terminate_timer.timeout.connect(self._terminate_if_running)
        self._kill_timer = QTimer(self)
        self._kill_timer.setSingleShot(True)
        self._kill_timer.timeout.connect(self._kill_if_running)
        self._stdout = BoundedLineBuffer(MAX_JSONL_LINE_BYTES)
        self._stderr = BoundedLineBuffer(MAX_JSONL_LINE_BYTES)
        self._temporary: tempfile.TemporaryDirectory[str] | None = None
        self._control_path: Path | None = None
        self._output_directory: Path | None = None
        self._kind: str | None = None
        self._stopping = False
        self._output_failed = False

    @property
    def active(self) -> bool:
        return self.process.state() != QProcess.NotRunning

    @property
    def output_directory(self) -> Path | None:
        return self._output_directory

    def start_graph(self, document: JsonObject) -> None:
        runner = discover_executable("ATLAS_STUDIO_RUNNER", "apps/atlas_studio_runner/atlas_studio_runner")
        if runner is None:
            raise FileNotFoundError(
                "atlas_studio_runner was not found; build Atlas or set ATLAS_STUDIO_RUNNER"
            )
        self._prepare("graph")
        self._temporary = tempfile.TemporaryDirectory(prefix="atlas-studio-")
        directory = Path(self._temporary.name)
        config_path = directory / "graph.json"
        config_path.write_text(json.dumps(document, indent=2), encoding="utf-8")
        self._control_path = directory / "cancel"
        try:
            self._start(runner, ["--config", str(config_path), "--control", str(self._control_path)])
        except BaseException:
            self._discard_failed_start()
            raise

    def start_benchmark(
        self,
        suite: JsonObject,
        output_directory: Path,
        environment_file: Path | None = None,
        *,
        live_tracing: bool = True,
    ) -> None:
        runner = discover_executable("ATLAS_BENCH", "apps/atlas_bench/atlas_bench")
        if runner is None:
            raise FileNotFoundError("atlas_bench was not found; build Atlas or set ATLAS_BENCH")
        self._prepare("benchmark")
        selected_output_directory = output_directory
        output_directory = prepare_benchmark_output_directory(selected_output_directory)
        if output_directory != selected_output_directory:
            self.diagnostic_received.emit(
                f"output directory contains files; writing this run to {output_directory}"
            )
        self._temporary = tempfile.TemporaryDirectory(prefix="atlas-studio-")
        suite_path = Path(self._temporary.name) / "suite.json"
        suite_path.write_text(json.dumps(suite, indent=2), encoding="utf-8")
        self._output_directory = output_directory
        arguments = ["--suite", str(suite_path), "--output-dir", str(output_directory)]
        if environment_file is not None:
            arguments.extend(["--environment-file", str(environment_file)])
        if live_tracing:
            arguments.append("--studio-progress-jsonl")
        else:
            self.diagnostic_received.emit("live benchmark task tracing is disabled for this run")
        try:
            self._start(runner, arguments)
        except BaseException:
            self._discard_failed_start()
            raise

    def stop(self) -> None:
        if not self.active or self._stopping:
            return
        self._stopping = True
        self.state_changed.emit("cancelling")
        if self._kind == "graph" and self._control_path is not None:
            try:
                self._control_path.write_text("terminate\n", encoding="utf-8")
                self._terminate_timer.start(10_000)
            except OSError as error:
                self.diagnostic_received.emit(f"unable to request cooperative cancellation: {error}")
                self.process.terminate()
                self._kill_timer.start(5_000)
        else:
            self.process.terminate()
            self._kill_timer.start(5_000)

    def fail_output(self, message: str) -> None:
        """Fail the active run after a process-protocol violation."""
        self._fail_output(message)

    def _prepare(self, kind: str) -> None:
        if self.active:
            raise RuntimeError("one Atlas run is already active")
        self._stdout.clear()
        self._stderr.clear()
        self._control_path = None
        self._output_directory = None
        self._kind = kind
        self._stopping = False
        self._output_failed = False
        self._terminate_timer.stop()
        self._kill_timer.stop()

    def _start(self, executable: Path, arguments: list[str]) -> None:
        self.process.setWorkingDirectory(str(ROOT))
        self.process.setProgram(str(executable))
        self.process.setArguments(arguments)
        self.process.start()
        if not self.process.waitForStarted(3_000):
            raise RuntimeError(self.process.errorString())
        self.state_changed.emit("running")
        self.run_started.emit(self._kind or "unknown")

    def _read_stdout(self) -> None:
        try:
            lines = self._stdout.feed(bytes(self.process.readAllStandardOutput()))
        except ValueError as error:
            self._fail_output(str(error))
            return
        for line in lines:
            if not line:
                continue
            self.stdout_line.emit(line)
            if self._output_failed:
                return

    def _read_stderr(self) -> None:
        try:
            lines = self._stderr.feed(bytes(self.process.readAllStandardError()))
        except ValueError as error:
            self.diagnostic_received.emit(str(error))
            self.process.kill()
            return
        for line in lines:
            if line:
                self.diagnostic_received.emit(line.decode("utf-8", errors="replace"))

    def _finished(self, exit_code: int, _status: QProcess.ExitStatus) -> None:
        self._read_stdout()
        self._read_stderr()
        remainder = self._stdout.finish()
        if remainder:
            self.stdout_line.emit(remainder)
        diagnostic = self._stderr.finish()
        if diagnostic:
            self.diagnostic_received.emit(diagnostic.decode("utf-8", errors="replace"))
        state = (
            "failed"
            if self._output_failed
            else ("cancelled" if self._stopping else ("complete" if exit_code == 0 else "failed"))
        )
        self._terminate_timer.stop()
        self._kill_timer.stop()
        self.state_changed.emit(state)
        self.run_finished.emit(exit_code, state)
        self._temporary = None
        self._stopping = False

    def _terminate_if_running(self) -> None:
        if self.active:
            self.diagnostic_received.emit("cooperative cancellation timed out; terminating runner")
            self.process.terminate()
            self._kill_timer.start(2_000)

    def _kill_if_running(self) -> None:
        if self.active:
            self.diagnostic_received.emit("runner did not terminate; killing process")
            self.process.kill()

    def _process_error(self, _error: QProcess.ProcessError) -> None:
        self.diagnostic_received.emit(self.process.errorString())

    def _discard_failed_start(self) -> None:
        self._temporary = None
        self._control_path = None
        self._output_directory = None
        self._kind = None
        self._stopping = False

    def _fail_output(self, message: str) -> None:
        if self._output_failed:
            return
        self._output_failed = True
        self.diagnostic_received.emit(message)
        self.process.kill()
