"""Top-level Atlas Studio application controller."""

from __future__ import annotations

from pathlib import Path
from typing import TYPE_CHECKING

from PySide6.QtCore import QObject

from ..models.benchmark import BenchmarkDocumentModel
from ..models.documents import DocumentError, StudioSessionModel
from ..models.graph import GraphDocumentModel
from ..models.results import ResultsSessionModel
from ..models.validation import validate_document
from ..services.documents import DocumentRepository
from ..services.imports import DocumentImport, RecordStreamImport, StudioImporter
from ..services.process import AtlasProcessService
from .benchmark import BenchmarkController
from .graph import GraphController
from .results import ResultsController
from .run import RunController

if TYPE_CHECKING:
    from ..views.main_window import MainWindow


class StudioController(QObject):
    """Coordinate the Studio shell, document controllers, and one local run."""

    def __init__(self, window: MainWindow) -> None:
        super().__init__(window)
        self.window = window
        self.session = StudioSessionModel()
        self.graph = GraphController(GraphDocumentModel(), window.graph_view)
        self.benchmark = BenchmarkController(BenchmarkDocumentModel(), window.benchmark_view, self.session)
        self.results = ResultsController(ResultsSessionModel(), window.results_view)
        self.run = RunController(AtlasProcessService(self), self.results)
        self._close_after_run = False

        window.open_requested.connect(self.open_file)
        window.save_requested.connect(self.save_file)
        window.validate_requested.connect(self.validate_current)
        window.run_requested.connect(self.run_current)
        window.stop_requested.connect(self.run.stop)
        window.workspace_changed.connect(self._workspace_changed)
        window.close_requested.connect(self._close_requested)
        window.graph_view.message.connect(window.show_status)
        window.benchmark_view.message.connect(window.show_status)
        self.run.run_started.connect(self._run_started)
        self.run.run_finished.connect(self._run_finished)
        self._workspace_changed("graph")

    def open_file(self) -> None:
        path = self.window.choose_open_file()
        if path is None:
            return
        try:
            imported = StudioImporter.load(path)
            if isinstance(imported, DocumentImport):
                if imported.kind == "graph":
                    self.graph.replace(imported.document)
                else:
                    self.benchmark.replace(imported.document)
                self.window.show_workspace(imported.kind)
            elif isinstance(imported, RecordStreamImport):
                self.results.load_records(imported.records)
                self.window.show_workspace("results")
            else:
                self.results.reset("Imported")
                self.results.set_benchmark_results(imported.results)
                self.window.show_workspace("results")
            self.window.show_status(f"Opened {path}", 5_000)
        except (OSError, ValueError, DocumentError) as error:
            self.window.show_warning("Unable to open file", str(error))

    def save_file(self) -> None:
        kind = self.window.workspace_kind()
        if kind not in {"graph", "benchmark"}:
            return
        path = self.window.choose_save_file("graph.json" if kind == "graph" else "suite.json")
        if path is None:
            return
        document = self.graph.model.snapshot() if kind == "graph" else self.benchmark.model.snapshot()
        try:
            DocumentRepository.save_json(path, document)
            self.window.show_status(f"Saved {path}", 5_000)
        except (OSError, ValueError) as error:
            self.window.show_critical("Unable to save file", str(error))

    def validate_current(self) -> None:
        kind = self.window.workspace_kind()
        if kind not in {"graph", "benchmark"}:
            return
        document = self.graph.model.snapshot() if kind == "graph" else self.benchmark.model.snapshot()
        errors = validate_document(kind, document)
        if errors:
            self.window.show_warning("Validation failed", "\n".join(errors[:32]))
        else:
            self.window.show_status(f"{kind.title()} document is valid.", 5_000)

    def run_current(self) -> None:
        if self.run.active:
            self.window.show_information("Atlas is running", "Only one local Atlas run can be active.")
            return
        kind = self.window.workspace_kind()
        try:
            if kind == "graph":
                self.run.start_graph(self.graph.model.snapshot())
            elif kind == "benchmark":
                options = self.session.benchmark_options
                if not options.output_directory:
                    self.window.show_warning(
                        "Output directory required", "Choose a benchmark output directory or run parent."
                    )
                    return
                self.run.start_benchmark(
                    self.benchmark.model.snapshot(),
                    Path(options.output_directory),
                    Path(options.environment_file) if options.environment_file else None,
                    live_tracing=options.live_tracing,
                )
        except (OSError, RuntimeError) as error:
            self.results.add_diagnostic(str(error))
            self.results.set_state("failed")
            self.window.show_critical("Unable to start Atlas", str(error))

    def _workspace_changed(self, kind: str) -> None:
        if kind not in {"graph", "benchmark", "results"}:
            raise ValueError(f"unknown workspace: {kind}")
        self.session.workspace = kind
        self.window.update_action_state(self.run.active)

    def _run_started(self, _kind: str) -> None:
        self.window.show_workspace("results")
        self.window.update_action_state(True)

    def _run_finished(self, exit_code: int, state: str) -> None:
        self.window.update_action_state(False)
        self.window.show_status(f"Atlas run {state} with exit code {exit_code}", 10_000)
        if self._close_after_run:
            self._close_after_run = False
            self.window.accept_close()

    def _close_requested(self) -> None:
        if self.run.active:
            if not self.window.confirm_stop():
                return
            self._close_after_run = True
            self.run.stop()
        else:
            self.window.accept_close()
