"""Interactive graphics scene for explicit task graphs."""

from __future__ import annotations

from collections.abc import Callable
from typing import Any

from PySide6.QtCore import QPointF, Qt, Signal
from PySide6.QtGui import QBrush, QColor, QPainter, QPen, QPolygonF
from PySide6.QtWidgets import (
    QGraphicsItem,
    QGraphicsLineItem,
    QGraphicsPolygonItem,
    QGraphicsRectItem,
    QGraphicsScene,
    QGraphicsSimpleTextItem,
    QGraphicsView,
)

from ..models.documents import JsonObject
from .theme import DEFAULT_DIAGRAM_PALETTE, DiagramPalette


class TaskItem(QGraphicsRectItem):
    def __init__(
        self, identifier: str, label: str, resource: str, moved: Callable[[], None], palette: DiagramPalette
    ) -> None:
        super().__init__(0, 0, 185, 62)
        self.identifier = identifier
        self._moved = moved
        self.setFlags(
            QGraphicsItem.ItemIsMovable
            | QGraphicsItem.ItemIsSelectable
            | QGraphicsItem.ItemSendsGeometryChanges
        )
        self.setBrush(QBrush(QColor(palette.cpu_node if resource == "cpu" else palette.gpu_node)))
        self.setPen(QPen(QColor(palette.node_border), 1.2))
        text = QGraphicsSimpleTextItem(label, self)
        text.setBrush(QBrush(QColor(palette.node_text)))
        text.setPos(10, 9)

    def itemChange(self, change: QGraphicsItem.GraphicsItemChange, value: Any) -> Any:
        result = super().itemChange(change, value)
        if change == QGraphicsItem.ItemPositionHasChanged and self.scene() is not None:
            self._moved()
        return result


class GraphCanvas(QGraphicsView):
    node_selected = Signal(str)
    edge_requested = Signal(str, str)

    def __init__(self, palette: DiagramPalette = DEFAULT_DIAGRAM_PALETTE) -> None:
        super().__init__()
        self._palette = palette
        self.setScene(QGraphicsScene(self))
        self.setRenderHint(QPainter.Antialiasing)
        self.setDragMode(QGraphicsView.RubberBandDrag)
        self.connecting = False
        self.connection_source: str | None = None
        self.items_by_id: dict[str, TaskItem] = {}
        self.positions: dict[str, QPointF] = {}
        self.edge_pairs: list[tuple[str, str]] = []
        self.edge_graphics: list[QGraphicsItem] = []

    def set_connecting(self, enabled: bool) -> None:
        self.connecting = enabled
        self.connection_source = None

    def set_document(self, document: JsonObject) -> None:
        for identifier, item in self.items_by_id.items():
            self.positions[identifier] = item.pos()
        self.edge_graphics.clear()
        self.scene().clear()
        self.items_by_id.clear()
        for index, node in enumerate(document["nodes"]):
            label = f"{node.get('name', node['id'])}\n{node['resource'].upper()} · {node['kernel']['type']}"
            item = TaskItem(node["id"], label, node["resource"], self._redraw_edges, self._palette)
            item.setPos(self.positions.get(node["id"], QPointF((index % 3) * 235, (index // 3) * 115)))
            self.scene().addItem(item)
            self.items_by_id[node["id"]] = item
        self.edge_pairs = [(edge["from"], edge["to"]) for edge in document["edges"]]
        self._redraw_edges()
        self.scene().setSceneRect(self.scene().itemsBoundingRect().adjusted(-80, -80, 80, 80))

    def select_node(self, identifier: str) -> None:
        item = self.items_by_id.get(identifier)
        if item:
            item.setSelected(True)
            self.ensureVisible(item)

    def mousePressEvent(self, event: Any) -> None:
        item = self.itemAt(event.position().toPoint())
        while item is not None and not isinstance(item, TaskItem):
            item = item.parentItem()
        if isinstance(item, TaskItem):
            self.node_selected.emit(item.identifier)
            if self.connecting:
                if self.connection_source is None:
                    self.connection_source = item.identifier
                else:
                    self.edge_requested.emit(self.connection_source, item.identifier)
                    self.connection_source = None
                event.accept()
                return
        super().mousePressEvent(event)

    def _draw_edge(self, source_id: str, target_id: str) -> None:
        source = self.items_by_id.get(source_id)
        target = self.items_by_id.get(target_id)
        if source is None or target is None:
            return
        start = source.sceneBoundingRect().center()
        target_rect = target.sceneBoundingRect()
        target_center = target_rect.center()
        direction = start - target_center
        length = max(1.0, (direction.x() ** 2 + direction.y() ** 2) ** 0.5)
        unit = QPointF(direction.x() / length, direction.y() / length)
        perpendicular = QPointF(-unit.y(), unit.x())
        horizontal = target_rect.width() / (2 * abs(unit.x())) if unit.x() else float("inf")
        vertical = target_rect.height() / (2 * abs(unit.y())) if unit.y() else float("inf")
        tip = target_center + unit * min(horizontal, vertical)
        line = QGraphicsLineItem(start.x(), start.y(), tip.x(), tip.y())
        line.setPen(QPen(QColor(self._palette.edge), 1.5))
        line.setZValue(-2)
        self.scene().addItem(line)
        self.edge_graphics.append(line)
        arrow = QPolygonF([tip, tip + unit * 14 + perpendicular * 6, tip + unit * 14 - perpendicular * 6])
        head = QGraphicsPolygonItem(arrow)
        head.setBrush(QBrush(QColor(self._palette.edge)))
        head.setPen(QPen(Qt.NoPen))
        head.setZValue(-1)
        self.scene().addItem(head)
        self.edge_graphics.append(head)

    def _redraw_edges(self) -> None:
        for item in self.edge_graphics:
            if item.scene() is self.scene():
                self.scene().removeItem(item)
        self.edge_graphics.clear()
        for source, target in self.edge_pairs:
            self._draw_edge(source, target)
