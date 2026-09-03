"""Canonical Atlas Studio document and result models."""

from .benchmark import BenchmarkDocumentModel, BenchmarkSelection, default_benchmark
from .documents import BenchmarkRunOptions, DocumentError, StudioSessionModel
from .graph import GraphDocumentModel, default_graph
from .protocol import JsonlRecordDecoder, load_jsonl, parse_json
from .results import ResultsSessionModel, ResultsSnapshot
from .validation import detect_document_kind, validate_document

__all__ = [
    "BenchmarkDocumentModel",
    "BenchmarkRunOptions",
    "BenchmarkSelection",
    "DocumentError",
    "GraphDocumentModel",
    "JsonlRecordDecoder",
    "ResultsSessionModel",
    "ResultsSnapshot",
    "StudioSessionModel",
    "default_benchmark",
    "default_graph",
    "detect_document_kind",
    "load_jsonl",
    "parse_json",
    "validate_document",
]
