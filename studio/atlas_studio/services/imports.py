"""Typed import boundary for Studio documents and result artifacts."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import TypeAlias

from ..models.documents import DocumentKind, JsonObject
from ..models.protocol import load_jsonl, parse_json
from ..models.validation import SCHEMAS, detect_document_kind, validate_document


@dataclass(frozen=True)
class DocumentImport:
    kind: DocumentKind
    document: JsonObject


@dataclass(frozen=True)
class RecordStreamImport:
    records: list[JsonObject]


@dataclass(frozen=True)
class BenchmarkResultImport:
    results: JsonObject


StudioImport: TypeAlias = DocumentImport | RecordStreamImport | BenchmarkResultImport


class StudioImporter:
    """Load, identify, and validate one user-selected Atlas file."""

    @staticmethod
    def load(path: Path) -> StudioImport:
        if path.suffix.lower() == ".jsonl":
            records = load_jsonl(path)
            if records[0].get("run_schema_version") == 2:
                return BenchmarkResultImport(
                    {"directory": str(path.parent), "runs": records, "comparisons": None}
                )
            return RecordStreamImport(records)

        document = parse_json(path.read_text(encoding="utf-8"))
        try:
            kind = detect_document_kind(document)
        except ValueError:
            return BenchmarkResultImport(load_result_document(path, document))
        errors = validate_document(kind, document)
        if errors:
            raise ValueError("\n".join(errors))
        return DocumentImport(kind, document)


def load_result_document(path: Path, document: JsonObject | None = None) -> JsonObject:
    """Load one versioned benchmark run or comparison summary for display."""
    value = document if document is not None else parse_json(path.read_text(encoding="utf-8"))
    if value.get("summary_schema_version") == 2:
        schema_name = "atlas-baseline-summary-v2.schema.json"
        result: JsonObject = {"directory": str(path.parent), "runs": [], "comparisons": value}
    elif value.get("run_schema_version") == 2:
        schema_name = "atlas-baseline-run-v2.schema.json"
        result = {"directory": str(path.parent), "runs": [value], "comparisons": None}
    else:
        raise ValueError("file is not a versioned Atlas benchmark run or comparison summary")
    errors = list(SCHEMAS.validator(schema_name).iter_errors(value))
    if errors:
        raise ValueError(errors[0].message)
    return result


def load_benchmark_results(directory: Path) -> JsonObject:
    """Load and validate result artifacts produced by one benchmark run."""
    result: JsonObject = {"directory": str(directory), "runs": [], "comparisons": None}
    runs_path = directory / "runs.jsonl"
    if runs_path.is_file():
        result["runs"] = load_jsonl(runs_path)
    comparisons_path = directory / "comparisons.json"
    if comparisons_path.is_file():
        result["comparisons"] = load_result_document(comparisons_path)["comparisons"]
    return result
