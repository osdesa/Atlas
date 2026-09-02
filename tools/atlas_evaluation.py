#!/usr/bin/env python3
"""Run, validate, analyze, package, and verify Atlas evaluation studies."""

from __future__ import annotations

import argparse
import csv
import datetime as dt
import hashlib
import json
import math
import os
import pathlib
import platform
import shutil
import subprocess
import sys
import tarfile
import urllib.request
import urllib.parse
from collections import defaultdict
from dataclasses import dataclass
from typing import Any, Iterable


class EvaluationError(RuntimeError):
    """Raised when an evaluation contract is invalid."""


METRICS: dict[str, tuple[str, ...]] = {
    "completion_time_us": ("result", "completion_time_us"),
    "throughput_tasks_per_second": ("metrics", "throughput_tasks_per_second"),
    "response_mean_us": ("metrics", "response_latency", "mean_us"),
    "response_p95_us": ("metrics", "response_latency", "p95_us"),
    "ready_wait_mean_us": ("metrics", "ready_wait", "mean_us"),
    "ready_wait_p95_us": ("metrics", "ready_wait", "p95_us"),
    "selection_bypass_mean": ("metrics", "selection_bypass_mean"),
    "selection_bypass_max": ("metrics", "selection_bypass_max"),
    "control_active_fraction": ("result", "control_active_fraction"),
    "immediate_slice_switch_mean_us": ("metrics", "immediate_slice_switch_mean_us"),
    "cpu_busy_fraction": ("metrics", "cpu_busy_fraction"),
    "gpu_host_busy_fraction": ("metrics", "gpu_host_busy_fraction"),
    "gpu_timestamp_busy_fraction": ("metrics", "gpu_timestamp_busy_fraction"),
    "cpu_jain_fairness": ("metrics", "cpu_jain_fairness"),
    "gpu_jain_fairness": ("metrics", "gpu_jain_fairness"),
}

METRIC_DIRECTIONS = {
    "completion_time_us": "lower_is_better",
    "throughput_tasks_per_second": "higher_is_better",
    "response_mean_us": "lower_is_better",
    "response_p95_us": "lower_is_better",
    "ready_wait_mean_us": "lower_is_better",
    "ready_wait_p95_us": "lower_is_better",
    "selection_bypass_mean": "lower_is_better",
    "selection_bypass_max": "lower_is_better",
    "control_active_fraction": "lower_is_better",
    "immediate_slice_switch_mean_us": "descriptive",
    "cpu_busy_fraction": "descriptive",
    "gpu_host_busy_fraction": "descriptive",
    "gpu_timestamp_busy_fraction": "descriptive",
    "cpu_jain_fairness": "higher_is_better",
    "gpu_jain_fairness": "higher_is_better",
}

RESULT_FILES = (
    "suite.resolved.json",
    "environment.resolved.json",
    "runs.jsonl",
    "runs.csv",
    "tasks.csv",
    "comparisons.json",
    "comparisons.csv",
)

# The evaluation tools consume files from local users and release assets.  Keep
# those inputs deliberately bounded so malformed or hostile data cannot turn a
# validation run into an unbounded memory, disk, or network operation.
MAX_JSON_BYTES = 8 * 1024 * 1024
MAX_JSONL_BYTES = 256 * 1024 * 1024
MAX_JSONL_LINE_BYTES = 2 * 1024 * 1024
MAX_ARCHIVE_BYTES = 512 * 1024 * 1024
MAX_ARCHIVE_MEMBERS = 10_000
MAX_ARCHIVE_UNPACKED_BYTES = 1024 * 1024 * 1024
ALLOWED_ARTIFACT_HOSTS = frozenset({"github.com", "objects.githubusercontent.com", "release-assets.githubusercontent.com"})


def _load_json(path: pathlib.Path) -> Any:
    try:
        if path.stat().st_size > MAX_JSON_BYTES:
            raise EvaluationError(f"JSON input exceeds {MAX_JSON_BYTES} byte limit: {path}")
        with path.open(encoding="utf-8") as stream:
            return json.load(stream)
    except (OSError, json.JSONDecodeError) as error:
        raise EvaluationError(f"Unable to read JSON {path}: {error}") from error


def _write_json(path: pathlib.Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="\n") as stream:
        json.dump(value, stream, indent=2, sort_keys=True, allow_nan=False)
        stream.write("\n")


def _reject_unknown(value: dict[str, Any], allowed: set[str], path: str) -> None:
    unknown = sorted(set(value) - allowed)
    if unknown:
        raise EvaluationError(f"{path} contains unknown field '{unknown[0]}'")


def _require_object(value: Any, path: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise EvaluationError(f"{path} must be an object")
    return value


def _required(value: dict[str, Any], key: str, path: str) -> Any:
    if key not in value:
        raise EvaluationError(f"{path} is missing required field '{key}'")
    return value[key]


def _non_empty_string(value: Any, path: str) -> str:
    if not isinstance(value, str) or not value:
        raise EvaluationError(f"{path} must be a non-empty string")
    return value


def _positive_integer(value: Any, path: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or value <= 0:
        raise EvaluationError(f"{path} must be a positive integer")
    return value


def _sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _require_regular_file(path: pathlib.Path, description: str, maximum_bytes: int) -> None:
    """Require a regular bounded input file before parsing it."""
    try:
        status = path.stat()
    except OSError as error:
        raise EvaluationError(f"Unable to access {description}: {path}: {error}") from error
    if not path.is_file() or path.is_symlink():
        raise EvaluationError(f"{description} must be a regular file: {path}")
    if status.st_size > maximum_bytes:
        raise EvaluationError(f"{description} exceeds {maximum_bytes} byte limit: {path}")


@dataclass(frozen=True)
class Study:
    path: pathlib.Path
    value: dict[str, Any]
    suite_path: pathlib.Path

    @property
    def study_id(self) -> str:
        return self.value["study_id"]

    @property
    def environments(self) -> tuple[str, ...]:
        return tuple(self.value["expected_environments"])

    @property
    def resamples(self) -> int:
        return self.value["analysis"]["bootstrap_resamples"]

    @property
    def materiality(self) -> float:
        return float(self.value["analysis"]["practical_effect_percent"])


def load_study(path: pathlib.Path) -> Study:
    root = _require_object(_load_json(path), "study")
    _reject_unknown(
        root,
        {
            "schema_version",
            "study_id",
            "suite_path",
            "suite_sha256",
            "expected_environments",
            "analysis",
            "questions",
        },
        "study",
    )
    if _required(root, "schema_version", "study") != 1:
        raise EvaluationError("Only evaluation study schema_version 1 is supported")
    _non_empty_string(_required(root, "study_id", "study"), "study.study_id")
    suite_relative = _non_empty_string(_required(root, "suite_path", "study"), "study.suite_path")
    suite_path = (path.parent / suite_relative).resolve()
    expected_digest = _non_empty_string(_required(root, "suite_sha256", "study"), "study.suite_sha256")
    if len(expected_digest) != 64 or _sha256(suite_path) != expected_digest:
        raise EvaluationError("study.suite_sha256 does not match the selected suite")

    environments = _required(root, "expected_environments", "study")
    if not isinstance(environments, list) or not environments:
        raise EvaluationError("study.expected_environments must be a non-empty array")
    parsed_environments = [_non_empty_string(item, "study.expected_environments") for item in environments]
    if len(set(parsed_environments)) != len(parsed_environments):
        raise EvaluationError("study.expected_environments must be unique")

    analysis = _require_object(_required(root, "analysis", "study"), "study.analysis")
    _reject_unknown(analysis, {"confidence_level", "bootstrap_resamples", "practical_effect_percent"}, "study.analysis")
    if _required(analysis, "confidence_level", "study.analysis") != 0.95:
        raise EvaluationError("study.analysis.confidence_level must be 0.95")
    _positive_integer(_required(analysis, "bootstrap_resamples", "study.analysis"), "study.analysis.bootstrap_resamples")
    materiality = _required(analysis, "practical_effect_percent", "study.analysis")
    if isinstance(materiality, bool) or not isinstance(materiality, (int, float)) or materiality <= 0:
        raise EvaluationError("study.analysis.practical_effect_percent must be positive")

    suite = _require_object(_load_json(suite_path), "suite")
    case_variants = {
        item["case_id"]: {variant["variant_id"] for variant in item["variants"]}
        for item in _required(suite, "cases", "suite")
    }
    questions = _required(root, "questions", "study")
    if not isinstance(questions, list) or not questions:
        raise EvaluationError("study.questions must be a non-empty array")
    question_ids: set[str] = set()
    contrast_ids: set[str] = set()
    absolute_ids: set[str] = set()
    for question_index, question_value in enumerate(questions):
        qpath = f"study.questions[{question_index}]"
        question = _require_object(question_value, qpath)
        _reject_unknown(question, {"question_id", "title", "contrasts", "absolute_groups"}, qpath)
        question_id = _non_empty_string(_required(question, "question_id", qpath), f"{qpath}.question_id")
        _non_empty_string(_required(question, "title", qpath), f"{qpath}.title")
        if question_id in question_ids:
            raise EvaluationError("study question IDs must be unique")
        question_ids.add(question_id)
        contrasts = _required(question, "contrasts", qpath)
        absolute_groups = _required(question, "absolute_groups", qpath)
        if not isinstance(contrasts, list) or not isinstance(absolute_groups, list) or not (contrasts or absolute_groups):
            raise EvaluationError(f"{qpath} must declare at least one contrast or absolute group")
        for contrast_index, contrast_value in enumerate(contrasts):
            cpath = f"{qpath}.contrasts[{contrast_index}]"
            contrast = _require_object(contrast_value, cpath)
            _reject_unknown(contrast, {"contrast_id", "case_id", "reference_variant", "candidate_variant", "metrics"}, cpath)
            contrast_id = _non_empty_string(_required(contrast, "contrast_id", cpath), f"{cpath}.contrast_id")
            case_id = _non_empty_string(_required(contrast, "case_id", cpath), f"{cpath}.case_id")
            reference = _non_empty_string(_required(contrast, "reference_variant", cpath), f"{cpath}.reference_variant")
            candidate = _non_empty_string(_required(contrast, "candidate_variant", cpath), f"{cpath}.candidate_variant")
            if contrast_id in contrast_ids or case_id not in case_variants:
                raise EvaluationError(f"{cpath} has a duplicate ID or unknown case")
            if reference == candidate or reference not in case_variants[case_id] or candidate not in case_variants[case_id]:
                raise EvaluationError(f"{cpath} must name two distinct variants in its case")
            contrast_ids.add(contrast_id)
            _validate_metrics(_required(contrast, "metrics", cpath), f"{cpath}.metrics")
        for group_index, group_value in enumerate(absolute_groups):
            gpath = f"{qpath}.absolute_groups[{group_index}]"
            group = _require_object(group_value, gpath)
            _reject_unknown(group, {"group_id", "case_id", "variants", "metrics"}, gpath)
            group_id = _non_empty_string(_required(group, "group_id", gpath), f"{gpath}.group_id")
            case_id = _non_empty_string(_required(group, "case_id", gpath), f"{gpath}.case_id")
            variants = _required(group, "variants", gpath)
            if group_id in absolute_ids or case_id not in case_variants or not isinstance(variants, list) or not variants:
                raise EvaluationError(f"{gpath} has an invalid ID, case, or variants array")
            if any(variant not in case_variants[case_id] for variant in variants) or len(set(variants)) != len(variants):
                raise EvaluationError(f"{gpath}.variants must be unique variants in its case")
            absolute_ids.add(group_id)
            _validate_metrics(_required(group, "metrics", gpath), f"{gpath}.metrics")
    return Study(path.resolve(), root, suite_path)


def _validate_metrics(value: Any, path: str) -> None:
    if not isinstance(value, list) or not value:
        raise EvaluationError(f"{path} must be a non-empty array")
    if len(set(value)) != len(value) or any(item not in METRICS for item in value):
        raise EvaluationError(f"{path} contains duplicate or unsupported metrics")


@dataclass
class ResultSet:
    environment_id: str
    revision: str
    root: pathlib.Path
    runs: dict[tuple[str, str, int, int], dict[str, Any]]


def _metric_value(run: dict[str, Any], metric: str) -> float | None:
    value: Any = run
    for component in METRICS[metric]:
        if not isinstance(value, dict) or component not in value:
            raise EvaluationError(f"Run record is missing metric path for {metric}")
        value = value[component]
    if value is None:
        return None
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        raise EvaluationError(f"Metric {metric} must be finite or null")
    return float(value)


def load_results(study: Study, root: pathlib.Path) -> ResultSet:
    root = root.resolve()
    for name in RESULT_FILES:
        if not (root / name).is_file():
            raise EvaluationError(f"Result directory is missing {name}: {root}")
    suite = _load_json(root / "suite.resolved.json")
    canonical_suite = _load_json(study.suite_path)
    if suite != canonical_suite:
        raise EvaluationError(f"Resolved suite does not match the study suite: {root}")
    environment = _require_object(_load_json(root / "environment.resolved.json"), "environment")
    revision = _non_empty_string(_required(environment, "git_revision", "environment"), "environment.git_revision")
    user_metadata = _require_object(_required(environment, "user_metadata", "environment"), "environment.user_metadata")
    environment_id = _non_empty_string(
        _required(user_metadata, "environment_id", "environment.user_metadata"),
        "environment.user_metadata.environment_id",
    )
    if environment_id not in study.environments:
        raise EvaluationError(f"Unexpected evaluation environment: {environment_id}")

    planned: dict[tuple[str, str, int, int], bool] = {}
    gpu_cases: set[str] = set()
    expected_tasks: dict[str, int] = {}
    for case in canonical_suite["cases"]:
        case_id = case["case_id"]
        cpu_count = case["workload"]["cpu"]["task_count"]
        gpu_count = case["workload"]["gpu"]["task_count"]
        expected_tasks[case_id] = cpu_count + gpu_count
        if gpu_count:
            gpu_cases.add(case_id)
        for variant in case["variants"]:
            for seed in canonical_suite["seeds"]:
                for repetition in range(canonical_suite["repetitions"]):
                    planned[(case_id, variant["variant_id"], seed, repetition)] = True

    runs: dict[tuple[str, str, int, int], dict[str, Any]] = {}
    try:
        runs_path = root / "runs.jsonl"
        _require_regular_file(runs_path, "runs.jsonl", MAX_JSONL_BYTES)
        with (root / "runs.jsonl").open(encoding="utf-8") as stream:
            for line_number, line in enumerate(stream, 1):
                if len(line.encode("utf-8")) > MAX_JSONL_LINE_BYTES:
                    raise EvaluationError(f"runs.jsonl record exceeds {MAX_JSONL_LINE_BYTES} byte limit at line {line_number}")
                if not line.strip():
                    raise EvaluationError(f"runs.jsonl contains a blank record at line {line_number}")
                run = _require_object(json.loads(line), f"runs.jsonl line {line_number}")
                if run.get("run_schema_version") != 2:
                    raise EvaluationError("Only baseline run schema version 2 is supported")
                key = (run.get("case_id"), run.get("variant_id"), run.get("seed"), run.get("repetition"))
                if key not in planned:
                    raise EvaluationError(f"Unexpected run key at line {line_number}: {key}")
                if key in runs:
                    raise EvaluationError(f"Duplicate run key at line {line_number}: {key}")
                if run.get("suite_id") != canonical_suite["suite_id"] or run.get("result", {}).get("status") != "Success":
                    raise EvaluationError(f"Run at line {line_number} has the wrong suite or a failed status")
                if run.get("environment") != environment:
                    raise EvaluationError(f"Run at line {line_number} has inconsistent environment metadata")
                tasks = run.get("tasks")
                if not isinstance(tasks, list) or len(tasks) != expected_tasks[key[0]]:
                    raise EvaluationError(f"Run at line {line_number} has an unexpected task count")
                if any(task.get("state") != "Success" for task in tasks if isinstance(task, dict)) or any(
                    not isinstance(task, dict) for task in tasks
                ):
                    raise EvaluationError(f"Run at line {line_number} contains a non-successful task")
                if key[0] in gpu_cases:
                    if run.get("metrics", {}).get("gpu_timestamp_supported") is not True:
                        raise EvaluationError(f"GPU timestamps are unavailable at line {line_number}")
                    if _metric_value(run, "gpu_timestamp_busy_fraction") is None:
                        raise EvaluationError(f"GPU timestamp utilization is missing at line {line_number}")
                for metric in METRICS:
                    _metric_value(run, metric)
                runs[key] = run
    except (OSError, json.JSONDecodeError) as error:
        raise EvaluationError(f"Unable to parse {root / 'runs.jsonl'}: {error}") from error
    if set(runs) != set(planned):
        missing = sorted(set(planned) - set(runs))
        raise EvaluationError(f"Result directory is incomplete; first missing run is {missing[0]}")
    return ResultSet(environment_id, revision, root, runs)


class StableRandom:
    """Small fixed SplitMix64 generator for cross-version deterministic sampling."""

    def __init__(self, seed: int):
        self.state = seed & ((1 << 64) - 1)

    def next(self) -> int:
        mask = (1 << 64) - 1
        self.state = (self.state + 0x9E3779B97F4A7C15) & mask
        value = self.state
        value = ((value ^ (value >> 30)) * 0xBF58476D1CE4E5B9) & mask
        value = ((value ^ (value >> 27)) * 0x94D049BB133111EB) & mask
        return value ^ (value >> 31)

    def index(self, size: int) -> int:
        if size <= 0:
            raise EvaluationError("Cannot sample an empty population")
        return self.next() % size


def _stable_seed(label: str) -> int:
    value = 14695981039346656037
    for byte in label.encode("utf-8"):
        value ^= byte
        value = (value * 1099511628211) & ((1 << 64) - 1)
    return value


def _mean(values: Iterable[float]) -> float:
    materialized = list(values)
    return math.fsum(materialized) / len(materialized)


def _bootstrap(values: list[tuple[int, float]], resamples: int, label: str) -> dict[str, Any]:
    if not values:
        return {"sample_count": 0, "mean": None, "confidence_interval": {"lower": None, "upper": None}}
    point = _mean(value for _, value in values)
    groups: dict[int, list[float]] = defaultdict(list)
    for seed, value in values:
        groups[seed].append(value)
    if len(groups) < 2:
        return {"sample_count": len(values), "mean": point, "confidence_interval": {"lower": None, "upper": None}}
    populations = [groups[seed] for seed in sorted(groups)]
    random = StableRandom(_stable_seed(label))
    samples: list[float] = []
    for _ in range(resamples):
        selected: list[float] = []
        for _ in populations:
            population = populations[random.index(len(populations))]
            selected.extend(population[random.index(len(population))] for _ in population)
        samples.append(_mean(selected))
    samples.sort()
    lower = samples[math.floor(0.025 * (resamples - 1))]
    upper = samples[math.ceil(0.975 * (resamples - 1))]
    return {"sample_count": len(values), "mean": point, "confidence_interval": {"lower": lower, "upper": upper}}


def _absolute(result: ResultSet, case_id: str, variant_id: str, metric: str, study: Study, label: str) -> dict[str, Any]:
    values: list[tuple[int, float]] = []
    for (case, variant, seed, _), run in result.runs.items():
        if case == case_id and variant == variant_id:
            value = _metric_value(run, metric)
            if value is not None:
                values.append((seed, value))
    summary = _bootstrap(values, study.resamples, label)
    return {"metric": metric, "direction": METRIC_DIRECTIONS[metric], **summary}


def _contrast(result: ResultSet, spec: dict[str, Any], metric: str, study: Study, question_id: str) -> dict[str, Any]:
    case_id = spec["case_id"]
    reference_id = spec["reference_variant"]
    candidate_id = spec["candidate_variant"]
    reference: dict[tuple[int, int], float] = {}
    candidate: dict[tuple[int, int], float] = {}
    for (case, variant, seed, repetition), run in result.runs.items():
        if case != case_id or variant not in (reference_id, candidate_id):
            continue
        value = _metric_value(run, metric)
        if value is not None:
            target = reference if variant == reference_id else candidate
            target[(seed, repetition)] = value
    paired_keys = sorted(set(reference) & set(candidate))
    differences = [(seed, candidate[(seed, repetition)] - reference[(seed, repetition)]) for seed, repetition in paired_keys]
    percentages = [
        (seed, 100.0 * (candidate[(seed, repetition)] - reference[(seed, repetition)]) / reference[(seed, repetition)])
        for seed, repetition in paired_keys
        if reference[(seed, repetition)] != 0.0
    ]
    label = f"{study.study_id}/{result.environment_id}/{question_id}/{spec['contrast_id']}/{metric}"
    difference = _bootstrap(differences, study.resamples, label + "/difference")
    percentage = _bootstrap(percentages, study.resamples, label + "/percent")
    interval = percentage["confidence_interval"]
    stable = interval["lower"] is not None and (interval["lower"] > 0.0 or interval["upper"] < 0.0)
    material = percentage["mean"] is not None and abs(percentage["mean"]) >= study.materiality
    direction = METRIC_DIRECTIONS[metric]
    beneficial: bool | None = None
    if stable and material and direction != "descriptive":
        beneficial = percentage["mean"] < 0.0 if direction == "lower_is_better" else percentage["mean"] > 0.0
    return {
        "metric": metric,
        "direction": direction,
        "paired_sample_count": difference["sample_count"],
        "mean_difference": difference["mean"],
        "difference_confidence_interval": difference["confidence_interval"],
        "percent_sample_count": percentage["sample_count"],
        "mean_percent_change": percentage["mean"],
        "percent_confidence_interval": percentage["confidence_interval"],
        "stable": stable,
        "material": material,
        "beneficial": beneficial,
    }


def _cross_environment(metrics: list[dict[str, Any]]) -> str:
    material = [item for item in metrics if item["stable"] and item["material"]]
    if not material:
        return "inconclusive"
    signs = {item["mean_percent_change"] > 0.0 for item in material}
    if len(signs) > 1:
        return "divergent"
    if len(material) == len(metrics):
        return "replicated"
    return "environment_specific"


def calculate(study: Study, result_sets: list[ResultSet]) -> dict[str, Any]:
    if len(result_sets) != len(study.environments) or {item.environment_id for item in result_sets} != set(study.environments):
        raise EvaluationError("Analysis requires exactly the study's declared environments")
    revisions = {item.revision for item in result_sets}
    if len(revisions) != 1:
        raise EvaluationError("All evaluation environments must use the same source revision")
    ordered = sorted(result_sets, key=lambda item: study.environments.index(item.environment_id))
    questions_output: list[dict[str, Any]] = []
    for question in study.value["questions"]:
        contrasts_output: list[dict[str, Any]] = []
        for contrast in question["contrasts"]:
            metrics_by_environment: dict[str, list[dict[str, Any]]] = {}
            for result in ordered:
                metrics_by_environment[result.environment_id] = [
                    _contrast(result, contrast, metric, study, question["question_id"]) for metric in contrast["metrics"]
                ]
            cross = []
            for metric in contrast["metrics"]:
                selected = [
                    next(item for item in metrics_by_environment[result.environment_id] if item["metric"] == metric)
                    for result in ordered
                ]
                cross.append({"metric": metric, "classification": _cross_environment(selected)})
            contrasts_output.append(
                {
                    "contrast_id": contrast["contrast_id"],
                    "case_id": contrast["case_id"],
                    "reference_variant": contrast["reference_variant"],
                    "candidate_variant": contrast["candidate_variant"],
                    "environments": metrics_by_environment,
                    "cross_environment": cross,
                }
            )
        absolute_output: list[dict[str, Any]] = []
        for group in question["absolute_groups"]:
            environment_values: dict[str, list[dict[str, Any]]] = {}
            for result in ordered:
                summaries = []
                for variant in group["variants"]:
                    for metric in group["metrics"]:
                        label = f"{study.study_id}/{result.environment_id}/{question['question_id']}/{group['group_id']}/{variant}/{metric}"
                        summaries.append(
                            {
                                "variant_id": variant,
                                **_absolute(result, group["case_id"], variant, metric, study, label),
                            }
                        )
                environment_values[result.environment_id] = summaries
            absolute_output.append(
                {"group_id": group["group_id"], "case_id": group["case_id"], "environments": environment_values}
            )
        questions_output.append(
            {
                "question_id": question["question_id"],
                "title": question["title"],
                "contrasts": contrasts_output,
                "absolute_groups": absolute_output,
            }
        )
    return {
        "evaluation_schema_version": 1,
        "study_id": study.study_id,
        "suite_sha256": study.value["suite_sha256"],
        "source_revision": next(iter(revisions)),
        "environments": [item.environment_id for item in ordered],
        "analysis": study.value["analysis"],
        "questions": questions_output,
    }


def _format_number(value: float | None) -> str:
    return "" if value is None else f"{value:.6g}"


def _write_contrast_csv(path: pathlib.Path, evaluation: dict[str, Any]) -> None:
    with path.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.writer(stream, lineterminator="\n")
        writer.writerow(
            [
                "evaluation_schema_version",
                "study_id",
                "question_id",
                "contrast_id",
                "case_id",
                "reference_variant",
                "candidate_variant",
                "environment_id",
                "metric",
                "direction",
                "paired_sample_count",
                "mean_difference",
                "difference_ci_lower",
                "difference_ci_upper",
                "mean_percent_change",
                "percent_ci_lower",
                "percent_ci_upper",
                "stable",
                "material",
                "beneficial",
                "cross_environment",
            ]
        )
        for question in evaluation["questions"]:
            for contrast in question["contrasts"]:
                cross = {item["metric"]: item["classification"] for item in contrast["cross_environment"]}
                for environment, metrics in contrast["environments"].items():
                    for metric in metrics:
                        writer.writerow(
                            [
                                1,
                                evaluation["study_id"],
                                question["question_id"],
                                contrast["contrast_id"],
                                contrast["case_id"],
                                contrast["reference_variant"],
                                contrast["candidate_variant"],
                                environment,
                                metric["metric"],
                                metric["direction"],
                                metric["paired_sample_count"],
                                _format_number(metric["mean_difference"]),
                                _format_number(metric["difference_confidence_interval"]["lower"]),
                                _format_number(metric["difference_confidence_interval"]["upper"]),
                                _format_number(metric["mean_percent_change"]),
                                _format_number(metric["percent_confidence_interval"]["lower"]),
                                _format_number(metric["percent_confidence_interval"]["upper"]),
                                str(metric["stable"]).lower(),
                                str(metric["material"]).lower(),
                                "" if metric["beneficial"] is None else str(metric["beneficial"]).lower(),
                                cross[metric["metric"]],
                            ]
                        )


def _write_question_markdown(path: pathlib.Path, question: dict[str, Any]) -> None:
    lines = [f"# {question['question_id']}: {question['title']}", ""]
    for contrast in question["contrasts"]:
        lines.extend(
            [
                f"## {contrast['contrast_id']}",
                "",
                f"`{contrast['candidate_variant']}` relative to `{contrast['reference_variant']}` in `{contrast['case_id']}`.",
                "",
                "| Environment | Metric | Mean change | 95% CI | Stable | Material | Cross-environment |",
                "| --- | --- | ---: | ---: | --- | --- | --- |",
            ]
        )
        cross = {item["metric"]: item["classification"] for item in contrast["cross_environment"]}
        for environment, metrics in contrast["environments"].items():
            for metric in metrics:
                interval = metric["percent_confidence_interval"]
                lines.append(
                    f"| {environment} | `{metric['metric']}` | {_format_number(metric['mean_percent_change'])}% | "
                    f"[{_format_number(interval['lower'])}, {_format_number(interval['upper'])}] | "
                    f"{str(metric['stable']).lower()} | {str(metric['material']).lower()} | {cross[metric['metric']]} |"
                )
        lines.append("")
    for group in question["absolute_groups"]:
        lines.extend(
            [
                f"## {group['group_id']}",
                "",
                "| Environment | Variant | Metric | Mean | 95% CI |",
                "| --- | --- | --- | ---: | ---: |",
            ]
        )
        for environment, summaries in group["environments"].items():
            for summary in summaries:
                interval = summary["confidence_interval"]
                lines.append(
                    f"| {environment} | `{summary['variant_id']}` | `{summary['metric']}` | {_format_number(summary['mean'])} | "
                    f"[{_format_number(interval['lower'])}, {_format_number(interval['upper'])}] |"
                )
        lines.append("")
    path.write_text("\n".join(lines), encoding="utf-8")


def _escape_xml(value: str) -> str:
    return value.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;").replace('"', "&quot;")


def _write_question_svg(path: pathlib.Path, question: dict[str, Any]) -> None:
    rows: list[tuple[str, float, float, float, bool, bool]] = []
    for contrast in question["contrasts"]:
        for environment, metrics in contrast["environments"].items():
            for metric in metrics:
                mean = metric["mean_percent_change"]
                interval = metric["percent_confidence_interval"]
                if mean is not None and interval["lower"] is not None:
                    label = f"{environment} · {contrast['contrast_id']} · {metric['metric']}"
                    rows.append((label, interval["lower"], mean, interval["upper"], metric["stable"], metric["material"]))
    width = 1200
    row_height = 28
    height = max(140, 90 + row_height * len(rows))
    minimum = min([row[1] for row in rows] + [-5.0])
    maximum = max([row[3] for row in rows] + [5.0])
    padding = max(1.0, 0.05 * (maximum - minimum))
    minimum -= padding
    maximum += padding
    plot_left, plot_right = 540, 1170
    scale = lambda value: plot_left + (value - minimum) * (plot_right - plot_left) / (maximum - minimum)
    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
        '<rect width="100%" height="100%" fill="#ffffff"/>',
        f'<text x="24" y="32" font-family="sans-serif" font-size="20" fill="#0f172a">{_escape_xml(question["title"])}</text>',
        '<text x="24" y="56" font-family="sans-serif" font-size="12" fill="#475569">Paired mean percent change with 95% confidence interval</text>',
        f'<line x1="{scale(0):.2f}" y1="70" x2="{scale(0):.2f}" y2="{height - 28}" stroke="#94a3b8" stroke-width="1"/>',
    ]
    for index, (label, lower, mean, upper, stable, material) in enumerate(rows):
        y = 88 + index * row_height
        color = "#0369a1" if stable and material else "#64748b"
        parts.extend(
            [
                f'<text x="24" y="{y + 4}" font-family="sans-serif" font-size="11" fill="#0f172a">{_escape_xml(label)}</text>',
                f'<line x1="{scale(lower):.2f}" y1="{y}" x2="{scale(upper):.2f}" y2="{y}" stroke="{color}" stroke-width="2"/>',
                f'<circle cx="{scale(mean):.2f}" cy="{y}" r="4" fill="{color}"/>',
            ]
        )
    parts.append(f'<text x="{plot_left}" y="{height - 8}" font-family="sans-serif" font-size="11">{minimum:.3g}%</text>')
    parts.append(f'<text x="{plot_right - 34}" y="{height - 8}" font-family="sans-serif" font-size="11">{maximum:.3g}%</text>')
    parts.append("</svg>\n")
    path.write_text("\n".join(parts), encoding="utf-8")


def write_analysis(output: pathlib.Path, evaluation: dict[str, Any]) -> None:
    if output.exists():
        raise EvaluationError(f"Analysis output already exists: {output}")
    (output / "tables").mkdir(parents=True)
    (output / "plots").mkdir()
    _write_json(output / "evaluation.json", evaluation)
    _write_contrast_csv(output / "contrasts.csv", evaluation)
    for question in evaluation["questions"]:
        name = question["question_id"].lower()
        _write_question_markdown(output / "tables" / f"{name}.md", question)
        _write_question_svg(output / "plots" / f"{name}.svg", question)


def analyze_command(arguments: argparse.Namespace) -> None:
    study = load_study(arguments.study)
    result_sets = [load_results(study, path) for path in arguments.results]
    write_analysis(arguments.output_dir, calculate(study, result_sets))
    print(f"Wrote {study.study_id} analysis to {arguments.output_dir}")


def _run_checked(command: list[str], cwd: pathlib.Path, environment: dict[str, str], log: pathlib.Path) -> None:
    rendered = " ".join(command)
    print(f"+ {rendered}", flush=True)
    completed = subprocess.run(command, cwd=cwd, env=environment, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    with log.open("a", encoding="utf-8", newline="\n") as stream:
        stream.write(f"$ {rendered}\n{completed.stdout}\n")
    sys.stdout.write(completed.stdout)
    if completed.returncode:
        raise EvaluationError(f"Command failed with exit code {completed.returncode}: {rendered}")


def _command_output(command: list[str], environment: dict[str, str]) -> str:
    try:
        return subprocess.run(command, env=environment, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=True).stdout
    except (OSError, subprocess.CalledProcessError) as error:
        return f"unavailable: {error}\n"


def _tar_filter(member: tarfile.TarInfo) -> tarfile.TarInfo:
    member.uid = 0
    member.gid = 0
    member.uname = ""
    member.gname = ""
    member.mtime = 0
    return member


def _create_bundle(
    output: pathlib.Path,
    results: pathlib.Path,
    capture: pathlib.Path,
    study: Study,
    environment_id: str,
    revision: str,
) -> pathlib.Path:
    bundle = output / "bundle"
    (bundle / "results").mkdir(parents=True)
    shutil.copytree(capture, bundle / "capture")
    for name in RESULT_FILES:
        shutil.copy2(results / name, bundle / "results" / name)
    files = sorted(path for path in bundle.rglob("*") if path.is_file())
    manifest = {
        "bundle_schema_version": 1,
        "study_id": study.study_id,
        "environment_id": environment_id,
        "source_revision": revision,
        "suite_sha256": study.value["suite_sha256"],
        "files": [
            {"path": path.relative_to(bundle).as_posix(), "size_bytes": path.stat().st_size, "sha256": _sha256(path)} for path in files
        ],
    }
    _write_json(bundle / "bundle-manifest.json", manifest)
    archive = output / f"atlas-final-evaluation-v1-{environment_id}.tar.gz"
    with tarfile.open(archive, "w:gz", format=tarfile.PAX_FORMAT) as tar:
        tar.add(bundle, arcname="bundle", filter=_tar_filter)
    print(f"Created {archive} ({archive.stat().st_size} bytes, sha256={_sha256(archive)})")
    return archive


def run_command(arguments: argparse.Namespace) -> None:
    study = load_study(arguments.study)
    repository = pathlib.Path(__file__).resolve().parent.parent
    output = arguments.output_dir.resolve()
    if output.exists():
        raise EvaluationError(f"Evaluation output already exists: {output}")
    status = subprocess.run(
        ["git", "status", "--porcelain"], cwd=repository, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True
    ).stdout
    if status:
        raise EvaluationError("Evaluation collection requires a clean Git worktree")
    icd = arguments.icd.resolve()
    if not icd.is_absolute() or not icd.is_file():
        raise EvaluationError("--icd must name an existing absolute ICD manifest")
    metadata = _require_object(_load_json(arguments.environment_file), "environment")
    environment_id = _non_empty_string(_required(metadata, "environment_id", "environment"), "environment.environment_id")
    if metadata.get("schema_version") != 1 or environment_id not in study.environments:
        raise EvaluationError("Environment metadata does not match the study")

    build = output / "build"
    results = output / "results"
    capture = output / "capture"
    capture.mkdir(parents=True)
    log = capture / "commands.log"
    environment = dict(os.environ)
    environment["VK_DRIVER_FILES"] = str(icd)
    jobs = str(arguments.jobs) if arguments.jobs else str(os.cpu_count() or 1)
    commands = [
        ["cmake", "--preset", "ci-linux", "-B", str(build), "-DATLAS_ENABLE_PROFILING=ON"],
        ["cmake", "--build", str(build), "--parallel", jobs],
        ["ctest", "--test-dir", str(build), "--output-on-failure"],
        [str(build / "apps" / "atlas" / "atlas")],
        [
            str(build / "apps" / "atlas_bench" / "atlas_bench"),
            "--suite",
            str(study.suite_path),
            "--environment-file",
            str(arguments.environment_file.resolve()),
            "--output-dir",
            str(results),
        ],
    ]
    for command in commands:
        _run_checked(command, repository, environment, log)
    revision = _command_output(["git", "rev-parse", "--short=12", "HEAD"], environment).strip()
    capture_value = {
        "capture_schema_version": 1,
        "captured_at_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
        "source_revision": revision,
        "environment_id": environment_id,
        "icd_filename": icd.name,
        "icd_sha256": _sha256(icd),
        "platform": platform.platform(),
        "python": platform.python_version(),
        "cmake": _command_output(["cmake", "--version"], environment).splitlines()[0],
        "compiler": _command_output([os.environ.get("CXX", "c++"), "--version"], environment).splitlines()[0],
    }
    _write_json(capture / "environment-capture.json", capture_value)
    (capture / "vulkaninfo.txt").write_text(_command_output(["vulkaninfo", "--summary"], environment), encoding="utf-8")
    command_text = log.read_text(encoding="utf-8")
    for local, replacement in ((str(repository), "<repository>"), (str(output), "<output>"), (str(icd), "<icd>")):
        command_text = command_text.replace(local, replacement)
    log.write_text(command_text, encoding="utf-8", newline="\n")
    _create_bundle(output, results, capture, study, environment_id, revision)


def _safe_extract(archive: pathlib.Path, destination: pathlib.Path) -> None:
    """Extract a checked regular archive without links, devices, or overwrite paths."""
    _require_regular_file(archive, "archive", MAX_ARCHIVE_BYTES)
    with tarfile.open(archive, "r:gz") as tar:
        root = destination.resolve()
        members = tar.getmembers()
        if len(members) > MAX_ARCHIVE_MEMBERS:
            raise EvaluationError(f"Archive contains more than {MAX_ARCHIVE_MEMBERS} members")
        unpacked_bytes = 0
        seen: set[pathlib.PurePosixPath] = set()
        for member in members:
            relative = pathlib.PurePosixPath(member.name)
            if relative.is_absolute() or ".." in relative.parts or not relative.parts:
                raise EvaluationError(f"Archive contains an unsafe path: {member.name}")
            if (relative in seen or not (member.isfile() or member.isdir()) or member.issym() or member.islnk() or member.isdev() or
                    member.isfifo()):
                raise EvaluationError(f"Archive contains an unsafe member: {member.name}")
            seen.add(relative)
            if member.isfile():
                unpacked_bytes += member.size
                if unpacked_bytes > MAX_ARCHIVE_UNPACKED_BYTES:
                    raise EvaluationError(f"Archive expands beyond {MAX_ARCHIVE_UNPACKED_BYTES} byte limit")
            target = (destination / member.name).resolve()
            if root == target or root not in target.parents or target.exists():
                raise EvaluationError(f"Archive contains an unsafe path: {member.name}")
        # Extract only the member kinds accepted above.  This avoids tarfile's
        # platform-dependent metadata handling and works with the documented
        # Python 3.10 minimum as well as newer extraction-filter APIs.
        for member in sorted((item for item in members if item.isdir()), key=lambda item: len(pathlib.PurePosixPath(item.name).parts)):
            (destination / member.name).mkdir(parents=True, exist_ok=True)
        for member in members:
            if member.isdir():
                continue
            target = destination / member.name
            target.parent.mkdir(parents=True, exist_ok=True)
            source = tar.extractfile(member)
            if source is None:
                raise EvaluationError(f"Archive member cannot be read: {member.name}")
            with source, target.open("xb") as stream:
                shutil.copyfileobj(source, stream, length=1024 * 1024)


def _artifact_url(value: Any) -> str:
    """Validate an immutable HTTPS release-asset URL before network access."""
    url = _non_empty_string(value, "artifact.url")
    parsed = urllib.parse.urlparse(url)
    if parsed.scheme != "https" or parsed.username or parsed.password or parsed.port or parsed.hostname not in ALLOWED_ARTIFACT_HOSTS:
        raise EvaluationError("Artifact URLs must use HTTPS and an allowlisted release host")
    if not parsed.path or pathlib.PurePosixPath(parsed.path).name in {"", ".", ".."}:
        raise EvaluationError("Artifact URL must name an archive file")
    return url


def _download_artifact(url: str, destination: pathlib.Path, expected_size: int) -> None:
    """Stream a release asset with a strict byte limit and final-host check."""
    if expected_size > MAX_ARCHIVE_BYTES:
        raise EvaluationError(f"Artifact exceeds {MAX_ARCHIVE_BYTES} byte limit")
    request = urllib.request.Request(url, headers={"User-Agent": "Atlas-evaluation/1"})
    try:
        with urllib.request.urlopen(request, timeout=30) as response, destination.open("xb") as stream:
            final = urllib.parse.urlparse(response.geturl())
            if final.scheme != "https" or final.hostname not in ALLOWED_ARTIFACT_HOSTS:
                raise EvaluationError("Artifact redirect left the HTTPS release-host allowlist")
            declared_length = response.headers.get("Content-Length")
            if declared_length is not None and int(declared_length) != expected_size:
                raise EvaluationError("Artifact Content-Length does not match its index")
            written = 0
            while block := response.read(1024 * 1024):
                written += len(block)
                if written > expected_size or written > MAX_ARCHIVE_BYTES:
                    raise EvaluationError("Artifact download exceeds its declared size")
                stream.write(block)
    except (OSError, ValueError) as error:
        raise EvaluationError(f"Unable to download {url}: {error}") from error


def _validate_bundle(bundle: pathlib.Path, study: Study, artifact: dict[str, Any]) -> pathlib.Path:
    manifest = _require_object(_load_json(bundle / "bundle-manifest.json"), "bundle")
    _reject_unknown(
        manifest,
        {"bundle_schema_version", "study_id", "environment_id", "source_revision", "suite_sha256", "files"},
        "bundle",
    )
    if (
        manifest.get("bundle_schema_version") != 1
        or manifest.get("study_id") != study.study_id
        or manifest.get("environment_id") != artifact["environment_id"]
        or manifest.get("source_revision") != artifact["source_revision"]
        or manifest.get("suite_sha256") != study.value["suite_sha256"]
    ):
        raise EvaluationError("Extracted bundle provenance does not match its artifact index")
    files = manifest.get("files")
    if not isinstance(files, list) or not files:
        raise EvaluationError("Bundle manifest must contain file entries")
    declared: set[str] = set()
    for entry_value in files:
        entry = _require_object(entry_value, "bundle.files")
        _reject_unknown(entry, {"path", "size_bytes", "sha256"}, "bundle.files")
        relative = pathlib.PurePosixPath(_non_empty_string(entry.get("path"), "bundle.files.path"))
        if relative.is_absolute() or ".." in relative.parts or relative.as_posix() in declared:
            raise EvaluationError("Bundle manifest contains an unsafe or duplicate path")
        declared.add(relative.as_posix())
        path = bundle.joinpath(*relative.parts)
        if (
            not path.is_file()
            or path.stat().st_size != _positive_integer(entry.get("size_bytes"), "bundle.files.size_bytes")
            or _sha256(path) != entry.get("sha256")
        ):
            raise EvaluationError(f"Bundle file failed verification: {relative.as_posix()}")
    actual = {
        path.relative_to(bundle).as_posix()
        for path in bundle.rglob("*")
        if path.is_file() and path.name != "bundle-manifest.json"
    }
    if actual != declared:
        raise EvaluationError("Bundle contents do not exactly match its manifest")
    return bundle / "results"


def _load_artifact_index(path: pathlib.Path, study: Study) -> list[dict[str, Any]]:
    root = _require_object(_load_json(path), "artifact_index")
    _reject_unknown(root, {"artifact_index_schema_version", "study_id", "release_tag", "artifacts"}, "artifact_index")
    if root.get("artifact_index_schema_version") != 1 or root.get("study_id") != study.study_id:
        raise EvaluationError("Artifact index version or study ID is invalid")
    _non_empty_string(root.get("release_tag"), "artifact_index.release_tag")
    artifacts = root.get("artifacts")
    if not isinstance(artifacts, list) or len(artifacts) != len(study.environments):
        raise EvaluationError("Artifact index must contain one artifact per environment")
    seen: set[str] = set()
    for index, item_value in enumerate(artifacts):
        item = _require_object(item_value, f"artifact_index.artifacts[{index}]")
        _reject_unknown(item, {"environment_id", "url", "size_bytes", "sha256", "source_revision", "suite_sha256"}, "artifact")
        environment_id = _non_empty_string(item.get("environment_id"), "artifact.environment_id")
        if environment_id not in study.environments or environment_id in seen:
            raise EvaluationError("Artifact environments must exactly match the study")
        seen.add(environment_id)
        item["url"] = _artifact_url(item.get("url"))
        _positive_integer(item.get("size_bytes"), "artifact.size_bytes")
        digest = _non_empty_string(item.get("sha256"), "artifact.sha256")
        if len(digest) != 64 or item.get("suite_sha256") != study.value["suite_sha256"]:
            raise EvaluationError("Artifact digest or suite digest is invalid")
        _non_empty_string(item.get("source_revision"), "artifact.source_revision")
    if seen != set(study.environments) or len({item["source_revision"] for item in artifacts}) != 1:
        raise EvaluationError("Artifact environments or source revisions do not match")
    return artifacts


def verify_command(arguments: argparse.Namespace) -> None:
    study = load_study(arguments.study)
    artifacts = _load_artifact_index(arguments.artifact_index, study)
    output = arguments.output_dir.resolve()
    if output.exists():
        raise EvaluationError(f"Verification output already exists: {output}")
    downloads = output / "downloads"
    extracted = output / "extracted"
    downloads.mkdir(parents=True)
    extracted.mkdir()
    result_paths = []
    for artifact in artifacts:
        archive = downloads / pathlib.PurePosixPath(urllib.parse.urlparse(artifact["url"]).path).name
        try:
            _download_artifact(artifact["url"], archive, artifact["size_bytes"])
        except EvaluationError:
            archive.unlink(missing_ok=True)
            raise
        if archive.stat().st_size != artifact["size_bytes"] or _sha256(archive) != artifact["sha256"]:
            raise EvaluationError(f"Published artifact failed verification: {archive.name}")
        destination = extracted / artifact["environment_id"]
        destination.mkdir()
        _safe_extract(archive, destination)
        result_paths.append(_validate_bundle(destination / "bundle", study, artifact))
    result_sets = [load_results(study, path) for path in result_paths]
    indexed_revisions = {item["source_revision"] for item in artifacts}
    if {item.revision for item in result_sets} != indexed_revisions:
        raise EvaluationError("Artifact index revision does not match the embedded benchmark revision")
    write_analysis(output / "analysis", calculate(study, result_sets))
    print(f"Verified and analyzed {len(artifacts)} published evaluation bundles")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    analyze = subparsers.add_parser("analyze", help="validate complete result sets and generate final outputs")
    analyze.add_argument("--study", type=pathlib.Path, required=True)
    analyze.add_argument("--results", type=pathlib.Path, required=True, action="append")
    analyze.add_argument("--output-dir", type=pathlib.Path, required=True)
    analyze.set_defaults(handler=analyze_command)

    run = subparsers.add_parser("run", help="clean-build and collect one study environment")
    run.add_argument("--study", type=pathlib.Path, required=True)
    run.add_argument("--environment-file", type=pathlib.Path, required=True)
    run.add_argument("--icd", type=pathlib.Path, required=True)
    run.add_argument("--output-dir", type=pathlib.Path, required=True)
    run.add_argument("--jobs", type=int)
    run.set_defaults(handler=run_command)

    verify = subparsers.add_parser("verify", help="download, verify, and analyze published raw bundles")
    verify.add_argument("--study", type=pathlib.Path, required=True)
    verify.add_argument("--artifact-index", type=pathlib.Path, required=True)
    verify.add_argument("--output-dir", type=pathlib.Path, required=True)
    verify.set_defaults(handler=verify_command)
    return parser


def main() -> int:
    try:
        arguments = build_parser().parse_args()
        if getattr(arguments, "jobs", None) is not None and arguments.jobs <= 0:
            raise EvaluationError("--jobs must be positive")
        arguments.handler(arguments)
        return 0
    except (EvaluationError, OSError, subprocess.CalledProcessError) as error:
        print(f"atlas_evaluation: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
