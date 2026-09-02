#!/usr/bin/env python3
"""Validate, summarize, and render Atlas trace JSON Lines files."""

from __future__ import annotations

import argparse
import collections
import html
import json
import pathlib
import sys
from typing import Any, TypeAlias


TraceRecord: TypeAlias = dict[str, Any]
TraceRecords: TypeAlias = list[TraceRecord]
TaskLane: TypeAlias = tuple[Any, Any, str]


EVENT_KINDS = {
    "scheduler_started",
    "scheduler_finished",
    "task_ready",
    "policy_decision",
    "task_selected",
    "task_resumed",
    "submission_requested",
    "submission_accepted",
    "submission_rejected",
    "backend_started",
    "backend_finished",
    "completion_observed",
    "task_paused",
    "cancellation_requested",
    "cancellation_applied",
    "task_succeeded",
    "task_failed",
    "policy_failed",
    "infrastructure_failed",
}
EVENT_SOURCES = {"scheduler", "cpu_executor", "vulkan_executor"}
TASK_STATES = {
    "unknown",
    "blocked",
    "ready",
    "running",
    "paused",
    "success",
    "failure",
    "cancelled",
}

EXPECTED_HEADER: TraceRecord = {
    "record_type": "header",
    "trace_schema_version": 1,
    "clock": "steady_nanoseconds",
}

REQUIRED_EVENT_FIELDS = {
    "record_type",
    "sequence",
    "timestamp_ns",
    "kind",
    "source",
    "priority",
    "previous_state",
    "state",
    "host_duration_ns",
}

OPTIONAL_EVENT_FIELDS = {
    "graph_id",
    "task_id",
    "resource",
    "work_unit_index",
    "worker_index",
    "ready_count",
    "selected_index",
    "device_duration_ns",
}

ALLOWED_EVENT_FIELDS = REQUIRED_EVENT_FIELDS | OPTIONAL_EVENT_FIELDS

NON_NEGATIVE_INTEGER_FIELDS = (
    "sequence",
    "timestamp_ns",
    "priority",
    "host_duration_ns",
    "graph_id",
    "task_id",
    "work_unit_index",
    "worker_index",
    "ready_count",
    "selected_index",
    "device_duration_ns",
)

EXPECTED_FOOTER_FIELDS = {
    "record_type",
    "status",
    "accepted_events",
    "dropped_events",
    "complete",
}

TIMELINE_WIDTH = 1200
TIMELINE_LABEL_WIDTH = 180
TIMELINE_ROW_HEIGHT = 28
TIMELINE_RIGHT_MARGIN = 20
TIMELINE_LANE_TOP = 38
TIMELINE_AXIS_Y = 25
TIMELINE_MIN_HEIGHT = 80
MAX_TRACE_BYTES = 128 * 1024 * 1024
MAX_TRACE_LINE_BYTES = 2 * 1024 * 1024
MAX_TRACE_RECORDS = 1_000_000


class TraceError(ValueError):
    """A trace violates the version-one contract."""


def read_records(path: pathlib.Path) -> TraceRecords:
    """Read and decode all JSON records from a trace file."""
    if not path.is_file() or path.is_symlink():
        raise TraceError(f"trace must be a regular file: {path}")
    if path.stat().st_size > MAX_TRACE_BYTES:
        raise TraceError(f"trace exceeds {MAX_TRACE_BYTES} byte limit")
    records: TraceRecords = []

    with path.open(encoding="utf-8") as stream:
        for line_number, line in enumerate(stream, 1):
            if line_number > MAX_TRACE_RECORDS:
                raise TraceError(f"trace exceeds {MAX_TRACE_RECORDS} record limit")
            if len(line.encode("utf-8")) > MAX_TRACE_LINE_BYTES:
                raise TraceError(f"line {line_number}: record exceeds {MAX_TRACE_LINE_BYTES} byte limit")
            records.append(parse_record(line, line_number))

    return records


def parse_record(line: str, line_number: int) -> TraceRecord:
    """Parse one JSON Lines record and ensure it is an object."""
    try:
        value = json.loads(line)
    except json.JSONDecodeError as error:
        raise TraceError(
            f"line {line_number}: invalid JSON: {error.msg}"
        ) from error

    if not isinstance(value, dict):
        raise TraceError(f"line {line_number}: record must be an object")

    return value


def validate_header(records: TraceRecords) -> None:
    """Validate the version-one trace header."""
    if not records or records[0] != EXPECTED_HEADER:
        raise TraceError("first record must be the exact version-one header")


def has_footer(records: TraceRecords) -> bool:
    """Return whether the final record is a footer."""
    return bool(records) and records[-1].get("record_type") == "footer"


def validate_footer(footer: TraceRecord, event_count: int) -> None:
    """Validate the completion footer."""
    if set(footer) != EXPECTED_FOOTER_FIELDS or footer.get("complete") is not True:
        raise TraceError("invalid completion footer")

    if not isinstance(footer["status"], str) or not footer["status"]:
        raise TraceError("footer status must be a non-empty string")

    for field in ("accepted_events", "dropped_events"):
        validate_non_negative_integer(footer[field], field, 0)

    if footer["accepted_events"] != event_count:
        raise TraceError(
            "footer accepted_events does not match serialized event count"
        )


def get_event_records(
    records: TraceRecords,
    allow_incomplete: bool,
) -> TraceRecords:
    """Return the event portion of a trace and validate its footer."""
    if not has_footer(records):
        if not allow_incomplete:
            raise TraceError(
                "trace has no completion footer; "
                "pass --allow-incomplete to inspect it"
            )
        return records[1:]

    event_records = records[1:-1]
    validate_footer(records[-1], len(event_records))
    return event_records


def validate_event_shape(event: TraceRecord, line_number: int) -> None:
    """Validate the required and allowed fields of an event."""
    if (
        not REQUIRED_EVENT_FIELDS.issubset(event)
        or event.get("record_type") != "event"
    ):
        raise TraceError(f"line {line_number}: invalid event shape")

    unknown_fields = set(event) - ALLOWED_EVENT_FIELDS
    if unknown_fields:
        raise TraceError(
            f"line {line_number}: unknown event field(s): "
            f"{sorted(unknown_fields)}"
        )


def validate_event_kind(event: TraceRecord, line_number: int) -> None:
    """Validate an event kind."""
    if event["kind"] not in EVENT_KINDS:
        raise TraceError(
            f"line {line_number}: unknown event kind {event['kind']!r}"
        )


def validate_non_negative_integer(
    value: Any,
    field: str,
    line_number: int,
) -> None:
    """Validate a field containing a non-negative integer."""
    if not isinstance(value, int) or isinstance(value, bool) or value < 0:
        raise TraceError(
            f"line {line_number}: {field} must be a non-negative integer"
        )


def validate_integer_fields(event: TraceRecord, line_number: int) -> None:
    """Validate all present non-negative integer event fields."""
    for field in NON_NEGATIVE_INTEGER_FIELDS:
        if field in event:
            validate_non_negative_integer(event[field], field, line_number)


def validate_sequence(
    event: TraceRecord,
    line_number: int,
    sequences: set[int],
) -> None:
    """Validate that an event sequence number has not already appeared."""
    sequence = event["sequence"]

    if sequence in sequences:
        raise TraceError(
            f"line {line_number}: duplicate sequence {sequence}"
        )

    sequences.add(sequence)


def validate_task_identifiers(event: TraceRecord, line_number: int) -> None:
    """Ensure graph_id and task_id are either both present or both absent."""
    if ("graph_id" in event) != ("task_id" in event):
        raise TraceError(
            f"line {line_number}: graph_id and task_id must appear together"
        )
    if event.get("task_id") == 0:
        raise TraceError(f"line {line_number}: task_id must be positive")


def validate_event_values(event: TraceRecord, line_number: int) -> None:
    """Validate enum-like event fields."""
    if event["source"] not in EVENT_SOURCES:
        raise TraceError(
            f"line {line_number}: unknown event source {event['source']!r}"
        )
    if (
        event["previous_state"] not in TASK_STATES
        or event["state"] not in TASK_STATES
    ):
        raise TraceError(f"line {line_number}: invalid task state")
    if "resource" in event and event["resource"] not in {"cpu", "gpu"}:
        raise TraceError(f"line {line_number}: invalid resource")


def validate_event(
    event: TraceRecord,
    line_number: int,
    sequences: set[int],
) -> None:
    """Validate one trace event."""
    validate_event_shape(event, line_number)
    validate_event_kind(event, line_number)
    validate_event_values(event, line_number)
    validate_integer_fields(event, line_number)
    validate_sequence(event, line_number, sequences)
    validate_task_identifiers(event, line_number)


def validate_events(event_records: TraceRecords) -> None:
    """Validate all trace events."""
    sequences: set[int] = set()

    for line_number, event in enumerate(event_records, 2):
        validate_event(event, line_number, sequences)


def load_trace(
    path: pathlib.Path,
    allow_incomplete: bool = False,
) -> TraceRecords:
    """Load and validate an Atlas trace file."""
    records = read_records(path)

    validate_header(records)

    event_records = get_event_records(records, allow_incomplete)
    validate_events(event_records)

    return records


def events(records: TraceRecords) -> TraceRecords:
    """Return only event records from a trace."""
    return [
        record
        for record in records
        if record.get("record_type") == "event"
    ]


def count_field_values(
    trace_events: TraceRecords,
    field: str,
) -> dict[Any, int]:
    """Count and sort the values of an event field."""
    counts = collections.Counter(event[field] for event in trace_events)
    return dict(sorted(counts.items()))


def task_count(trace_events: TraceRecords) -> int:
    """Return the number of distinct graph/task pairs."""
    tasks = {
        (event.get("graph_id"), event.get("task_id"))
        for event in trace_events
        if "task_id" in event
    }
    return len(tasks)


def gpu_device_duration(trace_events: TraceRecords) -> int | None:
    """Return the total recorded device duration, if any exists."""
    durations = [
        event["device_duration_ns"]
        for event in trace_events
        if "device_duration_ns" in event
    ]

    return sum(durations) if durations else None


def trace_footer(records: TraceRecords) -> TraceRecord | None:
    """Return the trace footer if one is present."""
    return records[-1] if has_footer(records) else None


def summarize(records: TraceRecords) -> dict[str, Any]:
    """Create a summary of a validated trace."""
    trace_events = events(records)
    footer = trace_footer(records)

    timestamps = [
        event["timestamp_ns"]
        for event in trace_events
    ]

    return {
        "event_count": len(trace_events),
        "dropped_event_count": (
            footer["dropped_events"] if footer else None
        ),
        "status": footer["status"] if footer else "incomplete",
        "elapsed_ns": max(timestamps, default=0),
        "event_kinds": count_field_values(trace_events, "kind"),
        "sources": count_field_values(trace_events, "source"),
        "task_count": task_count(trace_events),
        "gpu_device_duration_ns": gpu_device_duration(trace_events),
    }


def finished_backend_events(records: TraceRecords) -> TraceRecords:
    """Return backend-finished events associated with tasks."""
    return [
        event
        for event in events(records)
        if event["kind"] == "backend_finished"
        and "task_id" in event
    ]


def event_lane(event: TraceRecord) -> TaskLane:
    """Return the timeline lane identifying an event."""
    return (
        event["graph_id"],
        event["task_id"],
        event.get("resource", "unknown"),
    )


def timeline_lanes(finished: TraceRecords) -> list[TaskLane]:
    """Return sorted unique timeline lanes."""
    return sorted({event_lane(event) for event in finished})


def timeline_height(lane_count: int) -> int:
    """Calculate SVG height for a number of timeline lanes."""
    return max(
        TIMELINE_MIN_HEIGHT,
        45 + TIMELINE_ROW_HEIGHT * lane_count,
    )


def timeline_lane_y(index: int) -> int:
    """Return the vertical SVG position for a timeline lane."""
    return TIMELINE_LANE_TOP + index * TIMELINE_ROW_HEIGHT


def render_svg_header(height: int) -> list[str]:
    """Render the initial SVG elements."""
    return [
        (
            f'<svg xmlns="http://www.w3.org/2000/svg" '
            f'width="{TIMELINE_WIDTH}" height="{height}" '
            f'viewBox="0 0 {TIMELINE_WIDTH} {height}">'
        ),
        (
            "<style>"
            "text{font:12px sans-serif}"
            ".cpu{fill:#4c78a8}"
            ".gpu{fill:#f58518}"
            ".axis{stroke:#777;stroke-width:1}"
            "</style>"
        ),
        (
            f'<line class="axis" '
            f'x1="{TIMELINE_LABEL_WIDTH}" '
            f'y1="{TIMELINE_AXIS_Y}" '
            f'x2="{TIMELINE_WIDTH - TIMELINE_RIGHT_MARGIN}" '
            f'y2="{TIMELINE_AXIS_Y}"/>'
        ),
    ]


def render_lane_labels(
    lane_indices: dict[TaskLane, int],
) -> list[str]:
    """Render labels for each timeline lane."""
    output: list[str] = []

    for lane, index in lane_indices.items():
        y = timeline_lane_y(index)
        output.append(
            f'<text x="8" y="{y + 12}">'
            f"graph {lane[0]} / task {lane[1]}"
            "</text>"
        )

    return output


def render_event_bar(
    event: TraceRecord,
    lane_indices: dict[TaskLane, int],
    end: int,
) -> str:
    """Render one backend-finished event as an SVG rectangle."""
    lane = event_lane(event)
    duration = event.get("host_duration_ns", 0)
    start = max(0, event["timestamp_ns"] - duration)

    plot_width = (
        TIMELINE_WIDTH
        - TIMELINE_LABEL_WIDTH
        - TIMELINE_RIGHT_MARGIN
    )

    x = TIMELINE_LABEL_WIDTH + plot_width * start / end
    bar_width = max(1.0, plot_width * duration / end)
    y = timeline_lane_y(lane_indices[lane])

    title = html.escape(
        f"{event['resource']} task {event['task_id']}: {duration} ns"
    )

    return (
        f'<rect class="{event.get("resource", "cpu")}" '
        f'x="{x:.2f}" '
        f'y="{y}" '
        f'width="{bar_width:.2f}" '
        f'height="16">'
        f"<title>{title}</title>"
        "</rect>"
    )


def render_timeline(records: TraceRecords, path: pathlib.Path) -> None:
    """Render backend execution events as an SVG timeline."""
    if path.exists():
        raise TraceError(f"timeline output already exists: {path}")
    if path.parent.exists() and not path.parent.is_dir():
        raise TraceError(f"timeline output parent is not a directory: {path.parent}")
    finished = finished_backend_events(records)
    lanes = timeline_lanes(finished)
    lane_indices = {
        lane: index
        for index, lane in enumerate(lanes)
    }

    end = max(
        (event["timestamp_ns"] for event in finished),
        default=1,
    )

    output = render_svg_header(timeline_height(len(lanes)))
    output.extend(render_lane_labels(lane_indices))

    for event in finished:
        output.append(
            render_event_bar(event, lane_indices, end)
        )

    output.append("</svg>\n")
    path.write_text("\n".join(output), encoding="utf-8")


def create_argument_parser() -> argparse.ArgumentParser:
    """Create the Atlas trace command-line argument parser."""
    parser = argparse.ArgumentParser(prog="atlas_trace")

    parser.add_argument(
        "command",
        choices=("validate", "summary", "timeline"),
    )
    parser.add_argument(
        "trace",
        type=pathlib.Path,
    )
    parser.add_argument(
        "--allow-incomplete",
        action="store_true",
    )
    parser.add_argument(
        "--output",
        type=pathlib.Path,
        help="timeline SVG output",
    )

    return parser


def run_command(
    args: argparse.Namespace,
    parser: argparse.ArgumentParser,
    records: TraceRecords,
) -> None:
    """Execute the requested Atlas trace command."""
    if args.command == "validate":
        print(f"valid Atlas trace: {len(events(records))} event(s)")
        return

    if args.command == "summary":
        print(json.dumps(summarize(records), indent=2, sort_keys=True))
        return

    if args.output is None:
        parser.error("timeline requires --output")

    render_timeline(records, args.output)


def main(argv: list[str] | None = None) -> int:
    """Run the Atlas trace command-line utility."""
    parser = create_argument_parser()
    args = parser.parse_args(argv)

    try:
        records = load_trace(args.trace, args.allow_incomplete)
        run_command(args, parser, records)
    except (OSError, TraceError) as error:
        print(f"atlas_trace: {error}", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
