"""Atlas Studio application composition root."""

from __future__ import annotations

import sys

from PySide6.QtWidgets import QApplication

from .controllers.studio import StudioController
from .views.main_window import MainWindow
from .views.theme import load_stylesheet


def main() -> int:
    app = QApplication(sys.argv)
    app.setApplicationName("Atlas Studio")
    app.setOrganizationName("Atlas")
    app.setStyle("Fusion")
    app.setStyleSheet(load_stylesheet())
    window = MainWindow()
    window.controller = StudioController(window)
    window.show()
    return app.exec()
