"""Shared document types and atomic model state."""

from __future__ import annotations

import copy
from dataclasses import dataclass, field
from typing import Any, Literal

DocumentKind = Literal["graph", "benchmark"]
WorkspaceKind = Literal["graph", "benchmark", "results"]
ResourceKind = Literal["cpu", "gpu"]
JsonObject = dict[str, Any]


@dataclass
class BenchmarkRunOptions:
    """Non-document inputs used for one Studio benchmark launch."""

    output_directory: str = ""
    environment_file: str = ""
    live_tracing: bool = True


@dataclass
class StudioSessionModel:
    """Application-level selection and transient run configuration."""

    workspace: WorkspaceKind = "graph"
    benchmark_options: BenchmarkRunOptions = field(default_factory=BenchmarkRunOptions)


class DocumentError(ValueError):
    """Report an edit that would leave a Studio document invalid."""


class DocumentModel:
    """Own one valid versioned document and commit replacements atomically."""

    def __init__(self, kind: DocumentKind, document: JsonObject) -> None:
        self.kind = kind
        self._document: JsonObject = {}
        self.replace(document)

    def snapshot(self) -> JsonObject:
        """Return a detached serialization snapshot."""
        return copy.deepcopy(self._document)

    def replace(self, document: JsonObject) -> None:
        """Replace the document only when the complete candidate is valid."""
        from .validation import validate_document

        candidate = copy.deepcopy(document)
        errors = validate_document(self.kind, candidate)
        if errors:
            raise DocumentError("\n".join(errors))
        self._document = candidate

    def _commit(self, candidate: JsonObject) -> None:
        self.replace(candidate)
