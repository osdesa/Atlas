"""Presentation-only value formatting."""

from __future__ import annotations

from typing import Any


def format_nanoseconds(value: Any) -> str:
    try:
        amount = int(value)
    except (TypeError, ValueError):
        return "—"
    if amount >= 1_000_000_000:
        return f"{amount / 1_000_000_000:.2f} s"
    if amount >= 1_000_000:
        return f"{amount / 1_000_000:.2f} ms"
    if amount >= 1_000:
        return f"{amount / 1_000:.2f} µs"
    return f"{amount} ns"
