"""UI-independent live and imported result-session state."""

from __future__ import annotations

from collections import deque
from copy import deepcopy
from dataclasses import dataclass

from .documents import JsonObject

MAX_DISPLAY_RECORDS = 100_000
MAX_RETAINED_RUNS = 20
MAX_PRESENTED_TASKS = 5_000
MAX_PRESENTED_EVENTS = 500
MAX_PRESENTED_RECORDS = 2_000


@dataclass(frozen=True)
class ResultsSnapshot:
    """Detached presentation state consumed by the results view."""

    summary: JsonObject
    tasks: tuple[JsonObject, ...]
    events: tuple[JsonObject, ...]
    records: tuple[JsonObject, ...]
    diagnostics: tuple[str, ...]
    benchmark_runs: tuple[BenchmarkRunRow, ...]
    comparisons: JsonObject | None
    benchmark_mode: bool
    run_context: str
    run_choices: tuple[tuple[str, int | None], ...]
    selected_run_id: int | None
    progress_value: int
    progress_maximum: int
    total_task_count: int
    total_event_count: int
    total_record_count: int


@dataclass(frozen=True)
class BenchmarkRunRow:
    """Typed presentation row for one measured benchmark run."""

    case_id: object
    variant_id: object
    execution: object
    seed: object
    repetition: object
    status: object
    completion_us: object
    throughput: object


@dataclass(frozen=True)
class RetainedRun:
    """Bounded trace history for one measured benchmark run."""

    run_id: int
    label: str
    records: tuple[JsonObject, ...]


class ResultsSessionModel:
    """Reduce runner records into bounded canonical result presentation state."""

    def __init__(self) -> None:
        self.reset()

    def reset(self, status: str = "Ready") -> None:
        self.summary: JsonObject = {
            "status": status,
            "executed_tasks": None,
            "execution_ns": None,
            "scheduler_active_ns": None,
            "device": None,
            "trace_drops": None,
        }
        self.tasks: dict[int, JsonObject] = {}
        self.events: deque[JsonObject] = deque(maxlen=MAX_DISPLAY_RECORDS)
        self.records: deque[JsonObject] = deque(maxlen=MAX_DISPLAY_RECORDS)
        self.diagnostics: deque[str] = deque(maxlen=20_000)
        self.benchmark_runs: list[BenchmarkRunRow] = []
        self.comparisons: JsonObject | None = None
        self.benchmark_mode = False
        self.current_records: deque[JsonObject] = deque(maxlen=MAX_DISPLAY_RECORDS)
        self.history: list[RetainedRun] = []
        self.current_context: JsonObject | None = None
        self.live_context = ""
        self.selected_run_id: int | None = None
        self.progress_value = 0
        self.progress_maximum = 0

    def set_state(self, state: str) -> None:
        self.summary["status"] = state.title()

    def add_diagnostic(self, text: str) -> None:
        stripped = text.rstrip()
        if stripped:
            self.diagnostics.append(stripped)

    def consume(self, record: JsonObject) -> None:
        """Take ownership of one validated graph or benchmark stream record."""
        if record.get("record_type") == "header" and record.get("benchmark_stream_version") == 1:
            self._begin_benchmark(record)
            return
        if self.benchmark_mode:
            self._consume_benchmark(record)
            return
        self._apply_visual(record)

    def select_run(self, run_id: int | None) -> None:
        self.selected_run_id = run_id
        records = (
            self.current_records
            if run_id is None
            else next((item.records for item in self.history if item.run_id == run_id), ())
        )
        self._clear_visual()
        for record in records:
            if record.get("record_type") in {"task", "event", "run_finished"}:
                self._apply_visual(record)

    def set_benchmark_results(self, results: JsonObject) -> None:
        self.benchmark_runs = []
        for run in results.get("runs", []):
            result = run.get("result", {})
            metrics = run.get("metrics", {})
            self.benchmark_runs.append(
                BenchmarkRunRow(
                    case_id=run.get("case_id"),
                    variant_id=run.get("variant_id"),
                    execution=run.get("execution"),
                    seed=run.get("seed"),
                    repetition=run.get("repetition"),
                    status=result.get("status"),
                    completion_us=result.get("completion_time_us"),
                    throughput=metrics.get("throughput_tasks_per_second"),
                )
            )
        self.comparisons = deepcopy(results.get("comparisons"))
        self.summary["executed_tasks"] = len(self.benchmark_runs)
        self.summary["status"] = "Complete"

    def snapshot(self) -> ResultsSnapshot:
        choices: list[tuple[str, int | None]] = [("Live/current run", None)]
        choices.extend((item.label, item.run_id) for item in reversed(self.history))
        context = self.live_context
        if self.selected_run_id is not None:
            label = next(
                (label for label, run_id in choices if run_id == self.selected_run_id), "selected run"
            )
            context = f"Viewing {label}"
        tasks = list(self.tasks.values())
        events = list(self.events)
        records = list(self.records)
        return ResultsSnapshot(
            summary=deepcopy(self.summary),
            tasks=tuple(deepcopy(tasks[:MAX_PRESENTED_TASKS])),
            events=tuple(deepcopy(events[-MAX_PRESENTED_EVENTS:])),
            records=tuple(deepcopy(records[-MAX_PRESENTED_RECORDS:])),
            diagnostics=tuple(self.diagnostics),
            benchmark_runs=tuple(self.benchmark_runs),
            comparisons=deepcopy(self.comparisons),
            benchmark_mode=self.benchmark_mode,
            run_context=context,
            run_choices=tuple(choices),
            selected_run_id=self.selected_run_id,
            progress_value=self.progress_value,
            progress_maximum=self.progress_maximum,
            total_task_count=len(tasks),
            total_event_count=len(events),
            total_record_count=len(records),
        )

    def _begin_benchmark(self, record: JsonObject) -> None:
        self.benchmark_mode = True
        self.current_records = deque(maxlen=MAX_DISPLAY_RECORDS)
        self.history = []
        self.current_context = None
        self.progress_maximum = int(record.get("total_run_count", 1))
        self.progress_value = 0
        self.live_context = f"{record.get('suite_id', 'Benchmark')} · waiting for first run"

    def _consume_benchmark(self, record: JsonObject) -> None:
        record_type = record.get("record_type")
        if record_type == "run_started":
            self.current_context = record
            self.current_records = deque([record], maxlen=MAX_DISPLAY_RECORDS)
            phase = "warmup" if record.get("warmup") else f"repetition {int(record.get('repetition', 0)) + 1}"
            self.live_context = (
                f"Run {record.get('run_number')}/{record.get('total_run_count')} · "
                f"{record.get('case_id')}/{record.get('variant_id')} · seed {record.get('seed')} · {phase}"
            )
            self.progress_value = max(0, int(record.get("run_number", 1)) - 1)
            if self.selected_run_id is None:
                self._clear_visual()
            return
        if record_type in {"task", "event", "run_finished"}:
            self.current_records.append(record)
            if self.selected_run_id is None:
                self._apply_visual(record)
            if record_type == "run_finished":
                self.progress_value = int(record.get("run_number", 0))
                if not record.get("warmup", False):
                    self._append_benchmark_row(record)
                    self.history.append(
                        RetainedRun(
                            run_id=int(record["run_id"]),
                            label=self._run_label(record),
                            records=tuple(self.current_records),
                        )
                    )
                    self.history = self.history[-MAX_RETAINED_RUNS:]
            return
        if record_type == "error":
            self.add_diagnostic(str(record.get("message", "benchmark failed")))
        elif record_type == "footer":
            self.summary["status"] = str(record.get("status", "complete")).title()
            self.progress_value = int(record.get("completed_run_count", 0))

    def _apply_visual(self, record: JsonObject) -> None:
        self.records.append(record)
        record_type = record.get("record_type")
        if record_type == "task":
            task_id = int(record["task_id"])
            task = dict(record)
            task.setdefault("state", "unknown")
            self.tasks[task_id] = task
        elif record_type == "event":
            self.events.append(record)
            if "task_id" in record:
                task_id = int(record["task_id"])
                task = self.tasks.setdefault(task_id, {"task_id": task_id})
                state = str(record.get("state", "unknown"))
                if state != "unknown":
                    task["state"] = state
        elif record_type in {"result", "run_finished"}:
            self._apply_result(record)
            if record_type == "run_finished":
                self.summary["trace_drops"] = record.get("dropped_events", 0)
        elif record_type == "footer":
            self.summary["trace_drops"] = record.get("dropped_events", 0)
            if str(self.summary.get("status", "")).lower() in {"running", "ready", "—"}:
                self.summary["status"] = str(record.get("status", "complete"))
        elif record_type == "error":
            self.add_diagnostic(str(record.get("message", "runner failed")))

    def _apply_result(self, record: JsonObject) -> None:
        self.summary.update(
            {
                "status": record.get("status", "—"),
                "executed_tasks": record.get("executed_task_count"),
                "execution_ns": record.get("execution_time_ns"),
                "scheduler_active_ns": record.get("scheduler_active_ns", record.get("control_active_ns")),
                "device": record.get("device"),
            }
        )
        for incoming in record.get("tasks", []):
            task_id = int(incoming["task_id"])
            task = self.tasks.setdefault(task_id, {"task_id": task_id})
            task.update(incoming)

    def _append_benchmark_row(self, record: JsonObject) -> None:
        self.benchmark_runs.append(
            BenchmarkRunRow(
                case_id=record.get("case_id"),
                variant_id=record.get("variant_id"),
                execution=record.get("execution"),
                seed=record.get("seed"),
                repetition=record.get("repetition"),
                status=record.get("status"),
                completion_us=int(record.get("execution_time_ns", 0)) / 1000,
                throughput=record.get("throughput_tasks_per_second"),
            )
        )

    def _clear_visual(self) -> None:
        self.tasks.clear()
        self.events.clear()
        self.records.clear()
        self.summary.update(
            {
                "executed_tasks": None,
                "execution_ns": None,
                "scheduler_active_ns": None,
                "device": None,
                "trace_drops": None,
            }
        )

    @staticmethod
    def _run_label(record: JsonObject) -> str:
        return (
            f"{record.get('run_number')} · {record.get('case_id')}/{record.get('variant_id')} · "
            f"seed {record.get('seed')} · rep {int(record.get('repetition', 0)) + 1}"
        )
