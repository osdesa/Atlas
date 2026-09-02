"""Atlas Studio PySide6 desktop entry point and shell view."""

from __future__ import annotations

from pathlib import Path

from PySide6.QtCore import QSettings, Signal
from PySide6.QtGui import QAction, QCloseEvent, QKeySequence
from PySide6.QtWidgets import QFileDialog, QMainWindow, QMessageBox, QTabWidget, QToolBar

from .benchmark import BenchmarkView
from .graph import GraphView
from .results import ResultsView


class MainWindow(QMainWindow):
    """Passive application shell for the Studio MVC controllers."""

    open_requested = Signal()
    save_requested = Signal()
    validate_requested = Signal()
    run_requested = Signal()
    stop_requested = Signal()
    workspace_changed = Signal(str)
    close_requested = Signal()

    def __init__(self) -> None:
        super().__init__()
        self.setWindowTitle("Atlas Studio")
        self.resize(1280, 820)
        self._allow_close = False
        self.settings = QSettings("Atlas", "Atlas Studio")
        self.graph_view = GraphView()
        self.benchmark_view = BenchmarkView()
        self.results_view = ResultsView()
        self.tabs = QTabWidget()
        self.tabs.addTab(self.graph_view, "Task Graph")
        self.tabs.addTab(self.benchmark_view, "Benchmarks")
        self.tabs.addTab(self.results_view, "Results")
        self.tabs.currentChanged.connect(lambda _index: self.workspace_changed.emit(self.workspace_kind()))
        self.setCentralWidget(self.tabs)
        self.actions: dict[str, QAction] = {}
        self._build_toolbar()
        geometry = self.settings.value("geometry")
        if geometry:
            self.restoreGeometry(geometry)
        self.statusBar().showMessage("Ready")

    def _build_toolbar(self) -> None:
        toolbar = QToolBar("Studio actions")
        toolbar.setMovable(False)
        self.addToolBar(toolbar)
        definitions = [
            ("open", "Open", QKeySequence.Open, self.open_requested),
            ("save", "Save", QKeySequence.Save, self.save_requested),
            ("validate", "Validate", QKeySequence("Ctrl+Shift+V"), self.validate_requested),
            ("run", "Run", QKeySequence("Ctrl+R"), self.run_requested),
            ("stop", "Stop", QKeySequence("Ctrl+."), self.stop_requested),
        ]
        for name, text, shortcut, signal in definitions:
            action = QAction(text, self)
            action.setShortcut(shortcut)
            action.triggered.connect(signal)
            toolbar.addAction(action)
            self.actions[name] = action
        self.update_action_state(False)

    def workspace_kind(self) -> str:
        current = self.tabs.currentWidget()
        if current is self.benchmark_view:
            return "benchmark"
        if current is self.results_view:
            return "results"
        return "graph"

    def show_workspace(self, kind: str) -> None:
        target = {"graph": self.graph_view, "benchmark": self.benchmark_view, "results": self.results_view}[
            kind
        ]
        self.tabs.setCurrentWidget(target)

    def update_action_state(self, run_active: bool) -> None:
        editable = self.workspace_kind() in {"graph", "benchmark"}
        self.actions["open"].setEnabled(not run_active)
        for name in ("save", "validate", "run"):
            self.actions[name].setEnabled(editable and not run_active)
        self.actions["stop"].setEnabled(run_active)
        self.graph_view.setEnabled(not run_active)
        self.benchmark_view.setEnabled(not run_active)

    def choose_open_file(self) -> Path | None:
        path, _ = QFileDialog.getOpenFileName(
            self, "Open Atlas document", filter="Atlas files (*.json *.jsonl);;All files (*)"
        )
        return Path(path) if path else None

    def choose_save_file(self, suggested: str) -> Path | None:
        path, _ = QFileDialog.getSaveFileName(self, "Save Atlas document", suggested, "JSON files (*.json)")
        return Path(path) if path else None

    def show_status(self, text: str, timeout: int = 5_000) -> None:
        self.statusBar().showMessage(text, timeout)

    def show_information(self, title: str, text: str) -> None:
        QMessageBox.information(self, title, text)

    def show_warning(self, title: str, text: str) -> None:
        QMessageBox.warning(self, title, text)

    def show_critical(self, title: str, text: str) -> None:
        QMessageBox.critical(self, title, text)

    def confirm_stop(self) -> bool:
        answer = QMessageBox.question(
            self,
            "Stop active run?",
            "Atlas is still running. Stop it and close the Studio?",
            QMessageBox.Yes | QMessageBox.No,
            QMessageBox.No,
        )
        return answer == QMessageBox.Yes

    def accept_close(self) -> None:
        self._allow_close = True
        self.close()

    def closeEvent(self, event: QCloseEvent) -> None:
        if self._allow_close:
            self.settings.setValue("geometry", self.saveGeometry())
            event.accept()
            return
        event.ignore()
        self.close_requested.emit()
