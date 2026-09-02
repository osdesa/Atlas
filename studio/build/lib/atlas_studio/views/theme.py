"""Packaged widget stylesheet and semantic diagram colours."""

from __future__ import annotations

from dataclasses import dataclass
from importlib.resources import files


@dataclass(frozen=True)
class DiagramPalette:
    cpu_node: str = "#284c6c"
    gpu_node: str = "#533d70"
    node_border: str = "#7fa8cd"
    node_text: str = "#f0f4f8"
    edge: str = "#91a9c0"
    timeline_axis: str = "#71879d"
    timeline_cpu: str = "#6ca9e8"
    timeline_gpu: str = "#bb83ec"


DEFAULT_DIAGRAM_PALETTE = DiagramPalette()


def load_stylesheet() -> str:
    """Load the installed dark Qt stylesheet."""
    return files("atlas_studio.resources").joinpath("dark.qss").read_text(encoding="utf-8")
