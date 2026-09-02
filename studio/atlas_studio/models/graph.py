"""Canonical explicit-graph document state."""

from __future__ import annotations

import copy

from .documents import DocumentError, DocumentModel, JsonObject, ResourceKind

_GRAPH_TEMPLATE: JsonObject = {
    "schema_version": 1,
    "graph_id": "studio-example",
    "seed": 42,
    "runtime": {"validation": False},
    "cpu_executor": {"mode": "worker_pool", "worker_count": 2},
    "policy": {"type": "fifo"},
    "trace": {"enabled": True, "capacity": 65_536},
    "nodes": [
        {
            "id": "cpu-1",
            "name": "CPU work",
            "resource": "cpu",
            "priority": 0,
            "kernel": {"type": "cpu_burn", "iterations": 100_000},
        },
        {
            "id": "gpu-1",
            "name": "GPU work",
            "resource": "gpu",
            "priority": 0,
            "kernel": {"type": "gpu_increment", "workgroups": {"x": 64, "y": 1, "z": 1}},
            "slice_workgroups": None,
        },
    ],
    "edges": [{"from": "cpu-1", "to": "gpu-1"}],
}


def default_graph() -> JsonObject:
    """Return a detached default explicit graph document."""
    return copy.deepcopy(_GRAPH_TEMPLATE)


class GraphDocumentModel(DocumentModel):
    """Canonical explicit-graph state and invariant-preserving commands."""

    def __init__(self, document: JsonObject | None = None) -> None:
        super().__init__("graph", default_graph() if document is None else document)

    def update_settings(self, settings: JsonObject) -> None:
        candidate = self.snapshot()
        candidate.update(copy.deepcopy(settings))
        self._commit(candidate)

    def add_task(self, resource: ResourceKind) -> str:
        candidate = self.snapshot()
        suffix = len(candidate["nodes"]) + 1
        while any(node["id"] == f"{resource}-{suffix}" for node in candidate["nodes"]):
            suffix += 1
        identifier = f"{resource}-{suffix}"
        if resource == "cpu":
            node: JsonObject = {
                "id": identifier,
                "name": identifier,
                "resource": "cpu",
                "priority": 0,
                "kernel": {"type": "cpu_burn", "iterations": 10_000},
            }
        else:
            node = {
                "id": identifier,
                "name": identifier,
                "resource": "gpu",
                "priority": 0,
                "kernel": {"type": "gpu_increment", "workgroups": {"x": 64, "y": 1, "z": 1}},
                "slice_workgroups": None,
            }
        candidate["nodes"].append(node)
        self._commit(candidate)
        return identifier

    def remove_task(self, identifier: str) -> str:
        candidate = self.snapshot()
        if not any(node["id"] == identifier for node in candidate["nodes"]):
            raise DocumentError(f"unknown task: {identifier}")
        candidate["nodes"] = [node for node in candidate["nodes"] if node["id"] != identifier]
        candidate["edges"] = [edge for edge in candidate["edges"] if identifier not in edge.values()]
        self._commit(candidate)
        return str(candidate["nodes"][0]["id"])

    def update_task(self, identifier: str, replacement: JsonObject) -> str:
        candidate = self.snapshot()
        index = next((i for i, node in enumerate(candidate["nodes"]) if node["id"] == identifier), None)
        if index is None:
            raise DocumentError(f"unknown task: {identifier}")
        new_identifier = str(replacement.get("id", ""))
        if new_identifier != identifier:
            for edge in candidate["edges"]:
                if edge["from"] == identifier:
                    edge["from"] = new_identifier
                if edge["to"] == identifier:
                    edge["to"] = new_identifier
        candidate["nodes"][index] = copy.deepcopy(replacement)
        self._commit(candidate)
        return new_identifier

    def add_dependency(self, source: str, target: str) -> None:
        candidate = self.snapshot()
        candidate["edges"].append({"from": source, "to": target})
        self._commit(candidate)
