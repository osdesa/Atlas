"""Bounded parsing and validation for Atlas JSON and JSONL protocols."""

from __future__ import annotations

import json
from pathlib import Path

from jsonschema import Draft202012Validator

from .documents import JsonObject
from .validation import SCHEMAS

MAX_JSONL_BYTES = 128 * 1024 * 1024
MAX_JSONL_LINE_BYTES = 16 * 1024 * 1024
MAX_JSONL_RECORDS = 1_000_000


def parse_json(text: str) -> JsonObject:
    value = json.loads(text)
    if not isinstance(value, dict):
        raise ValueError("document must be a JSON object")
    return value


class JsonlRecordDecoder:
    """Validate one bounded, versioned JSONL stream incrementally."""

    def __init__(
        self,
        *,
        maximum_bytes: int = MAX_JSONL_BYTES,
        maximum_line_bytes: int = MAX_JSONL_LINE_BYTES,
        maximum_records: int = MAX_JSONL_RECORDS,
    ) -> None:
        self._validator: Draft202012Validator | None = None
        self._headered = False
        self._finished = False
        self._preflight_error = False
        self._studio = False
        self._tasks: dict[int, str] = {}
        self._node_ids: set[str] = set()
        self._summaries: set[int] = set()
        self._result = False
        self._error = False
        self._maximum_bytes = maximum_bytes
        self._maximum_line_bytes = maximum_line_bytes
        self._maximum_records = maximum_records
        self.record_count = 0
        self.byte_count = 0

    def decode(self, line: bytes) -> JsonObject:
        if self._finished:
            raise ValueError("JSONL stream contains a record after its footer")
        if len(line) > self._maximum_line_bytes:
            raise ValueError("JSONL record exceeds its byte limit")
        self.byte_count += len(line) + 1
        if self.byte_count > self._maximum_bytes:
            raise ValueError("JSONL stream exceeds 128 MiB")
        try:
            value = json.loads(line.decode("utf-8"), parse_constant=_invalid_constant)
        except (UnicodeDecodeError, json.JSONDecodeError) as error:
            raise ValueError(f"invalid JSONL record: {error}") from error
        if not isinstance(value, dict):
            raise ValueError("JSONL record is not an object")
        if self._validator is None:
            schema_name, self._headered = _stream_schema(value)
            self._validator = SCHEMAS.validator(schema_name)
            self._studio = schema_name == "atlas-studio-run-v2.schema.json"
            self._preflight_error = value.get("record_type") == "error"
            if self._preflight_error and value.get("phase") != "preflight":
                raise ValueError("execution error requires a stream header")
        elif self._headered and value.get("record_type") == "header":
            raise ValueError("JSONL stream contains more than one header")
        errors = list(self._validator.iter_errors(value))
        if errors:
            raise ValueError(f"invalid JSONL record: {errors[0].message}")
        if not self._studio and len(line) > 2 * 1024 * 1024:
            raise ValueError("non-Studio JSONL record exceeds 2 MiB")
        if self._studio:
            kind = value["record_type"]
            if kind == "task":
                if value["task_id"] in self._tasks or value["node_id"] in self._node_ids:
                    raise ValueError("duplicate task identity in Studio stream")
                self._tasks[value["task_id"]] = value["node_id"]
                self._node_ids.add(value["node_id"])
            elif kind == "task_summary":
                task_id = value["task_id"]
                if self._tasks.get(task_id) != value["node_id"] or task_id in self._summaries:
                    raise ValueError("unknown or duplicate task summary")
                self._summaries.add(task_id)
            elif kind == "result":
                if self._result:
                    raise ValueError("duplicate Studio result")
                self._result = True
            elif kind == "error":
                self._error = True
            elif kind == "footer" and not (self._result or self._error):
                raise ValueError("Studio footer requires a result or error")
        self.record_count += 1
        if self.record_count > self._maximum_records:
            raise ValueError("JSONL stream exceeds one million records")
        if self._headered and value.get("record_type") == "footer":
            self._finished = True
        if self._preflight_error:
            self._finished = True
        if (
            value.get("record_type") == "task_summary"
            and len(json.dumps(value["summary"], ensure_ascii=False, separators=(",", ":")).encode()) > 65536
        ):
            raise ValueError("task summary exceeds 64 KiB")
        return value

    def finish(self) -> None:
        """Reject an empty or incomplete headered stream."""
        if self.record_count == 0:
            raise ValueError("JSONL stream is empty")
        if self._headered and not self._finished:
            raise ValueError("JSONL stream has no completion footer")


def load_jsonl(path: Path) -> list[JsonObject]:
    """Stream one bounded JSONL file through the incremental decoder."""
    if path.stat().st_size > MAX_JSONL_BYTES:
        raise ValueError("JSONL import exceeds 128 MiB")
    decoder = JsonlRecordDecoder()
    records: list[JsonObject] = []
    with path.open("rb") as stream:
        for line_number, raw_line in enumerate(stream, 1):
            line = raw_line.rstrip(b"\r\n")
            if not line:
                continue
            try:
                records.append(decoder.decode(line))
            except ValueError as error:
                raise ValueError(f"line {line_number}: {error}") from error
    decoder.finish()
    return records


def _invalid_constant(value: str):
    raise ValueError(f"non-finite JSON number: {value}")


def _stream_schema(first: JsonObject) -> tuple[str, bool]:
    if first.get("record_type") == "header" and first.get("studio_schema_version") == 2:
        return "atlas-studio-run-v2.schema.json", True
    if first.get("record_type") == "header" and first.get("benchmark_stream_version") == 1:
        return "atlas-studio-benchmark-progress-v1.schema.json", True
    if first.get("record_type") == "error" and first.get("studio_schema_version") == 2:
        return "atlas-studio-run-v2.schema.json", False
    if "studio_schema_version" in first:
        raise ValueError("unsupported Studio stream version")
    if first.get("record_type") == "header" and first.get("trace_schema_version") == 1:
        return "atlas-trace-v1.schema.json", True
    if first.get("run_schema_version") == 2:
        return "atlas-baseline-run-v2.schema.json", False
    raise ValueError("unsupported or unversioned JSONL stream")
