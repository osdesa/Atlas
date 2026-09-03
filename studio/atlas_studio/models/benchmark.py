"""Canonical benchmark-suite document state."""

from __future__ import annotations

import copy
from dataclasses import dataclass
from typing import Literal

from .documents import DocumentError, DocumentModel, JsonObject


@dataclass(frozen=True)
class BenchmarkSelection:
    """Typed location selected in the benchmark case tree."""

    kind: Literal["case", "variant"]
    case_index: int
    variant_index: int | None = None


_BENCHMARK_TEMPLATE: JsonObject = {
    "schema_version": 1,
    "suite_id": "studio-suite",
    "seeds": [42],
    "warmup_runs": 1,
    "repetitions": 2,
    "worker_count": 2,
    "cases": [
        {
            "case_id": "mixed",
            "reference_variant": "direct",
            "workload": {
                "cpu": {"task_count": 4, "iterations": 10_000},
                "gpu": {"task_count": 2, "workgroups": {"x": 64, "y": 1, "z": 1}},
                "dependencies": {"shape": "independent"},
                "priorities": {"assignment": "cycle", "values": [0, 5]},
                "bursts": {"count": 1},
            },
            "variants": [
                {"variant_id": "direct", "execution": "direct"},
                {
                    "variant_id": "fifo",
                    "execution": "scheduled",
                    "policy": {"type": "fifo"},
                    "slice_workgroups": None,
                },
            ],
        }
    ],
}


def default_benchmark() -> JsonObject:
    """Return a detached default benchmark suite document."""
    return copy.deepcopy(_BENCHMARK_TEMPLATE)


class BenchmarkDocumentModel(DocumentModel):
    """Canonical benchmark-suite state and invariant-preserving commands."""

    def __init__(self, document: JsonObject | None = None) -> None:
        super().__init__("benchmark", default_benchmark() if document is None else document)

    def update_settings(self, settings: JsonObject) -> None:
        candidate = self.snapshot()
        candidate.update(copy.deepcopy(settings))
        self._commit(candidate)

    def add_case(self) -> int:
        candidate = self.snapshot()
        suffix = len(candidate["cases"]) + 1
        identifiers = {case["case_id"] for case in candidate["cases"]}
        while f"case-{suffix}" in identifiers:
            suffix += 1
        new_case = copy.deepcopy(default_benchmark()["cases"][0])
        new_case["case_id"] = f"case-{suffix}"
        candidate["cases"].append(new_case)
        self._commit(candidate)
        return len(candidate["cases"]) - 1

    def update_case(self, index: int, replacement: JsonObject) -> None:
        candidate = self.snapshot()
        self._require_index(candidate["cases"], index, "benchmark case")
        candidate["cases"][index] = copy.deepcopy(replacement)
        self._commit(candidate)

    def remove_case(self, index: int) -> None:
        candidate = self.snapshot()
        self._require_index(candidate["cases"], index, "benchmark case")
        candidate["cases"].pop(index)
        self._commit(candidate)

    def add_variant(self, case_index: int) -> int:
        candidate = self.snapshot()
        self._require_index(candidate["cases"], case_index, "benchmark case")
        variants = candidate["cases"][case_index]["variants"]
        suffix = len(variants) + 1
        identifiers = {variant["variant_id"] for variant in variants}
        while f"variant-{suffix}" in identifiers:
            suffix += 1
        variants.append(
            {
                "variant_id": f"variant-{suffix}",
                "execution": "scheduled",
                "policy": {"type": "fifo"},
                "slice_workgroups": None,
            }
        )
        self._commit(candidate)
        return len(variants) - 1

    def update_variant(self, case_index: int, variant_index: int, replacement: JsonObject) -> None:
        candidate = self.snapshot()
        self._require_index(candidate["cases"], case_index, "benchmark case")
        case = candidate["cases"][case_index]
        self._require_index(case["variants"], variant_index, "benchmark variant")
        previous = case["variants"][variant_index]["variant_id"]
        case["variants"][variant_index] = copy.deepcopy(replacement)
        if case["reference_variant"] == previous:
            case["reference_variant"] = replacement["variant_id"]
        self._commit(candidate)

    def remove_variant(self, case_index: int, variant_index: int) -> None:
        candidate = self.snapshot()
        self._require_index(candidate["cases"], case_index, "benchmark case")
        case = candidate["cases"][case_index]
        self._require_index(case["variants"], variant_index, "benchmark variant")
        identifier = case["variants"][variant_index]["variant_id"]
        if case["reference_variant"] == identifier:
            raise DocumentError("select a different reference variant before removing this variant")
        case["variants"].pop(variant_index)
        self._commit(candidate)

    @staticmethod
    def _require_index(values: list[object], index: int, description: str) -> None:
        if index < 0 or index >= len(values):
            raise DocumentError(f"unknown {description}")
