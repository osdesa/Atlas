"""Timeline rendering for validated Atlas events."""

from __future__ import annotations

from typing import Any

from PySide6.QtCore import Qt
from PySide6.QtGui import QBrush, QColor, QPen
from PySide6.QtWidgets import QGraphicsScene, QGraphicsView

from .formatting import format_nanoseconds
from .theme import DEFAULT_DIAGRAM_PALETTE, DiagramPalette


class TimelineView(QGraphicsView):
    """Render task events using one semantic diagram palette."""

    def __init__(self, palette: DiagramPalette = DEFAULT_DIAGRAM_PALETTE) -> None:
        super().__init__()
        self._palette = palette
        self.setScene(QGraphicsScene(self))
        self.setMinimumHeight(150)

    def render_events(self, events: tuple[dict[str, Any], ...]) -> None:
        scene = self.scene()
        scene.clear()
        if not events:
            return
        maximum = max(1, max(int(event.get("timestamp_ns", 0)) for event in events))
        width = 900
        scene.addLine(20, 60, width + 20, 60, QPen(QColor(self._palette.timeline_axis), 2))
        for event in events:
            x = 20 + int(event.get("timestamp_ns", 0)) / maximum * width
            colour = (
                self._palette.timeline_gpu if event.get("resource") == "gpu" else self._palette.timeline_cpu
            )
            item = scene.addEllipse(x - 4, 56, 8, 8, QPen(Qt.NoPen), QBrush(QColor(colour)))
            item.setToolTip(f"{event.get('kind')} · {format_nanoseconds(event.get('timestamp_ns'))}")
        scene.setSceneRect(0, 0, width + 40, 120)
