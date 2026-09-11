"""Canonical explicit-graph document state."""

from __future__ import annotations

import copy

from .descriptors import BUILTINS, default_parameters, validate_parameters
from .documents import DocumentError, DocumentModel, JsonObject

_GRAPH_TEMPLATE: JsonObject = {
    "schema_version": 2,
    "packs": [],
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
            "pack_id": "atlas.builtin",
            "task_id": "cpu_burn",
            "parameters": default_parameters(BUILTINS["cpu_burn"]),
        },
        {
            "id": "gpu-1",
            "name": "GPU work",
            "resource": "gpu",
            "priority": 0,
            "pack_id": "atlas.builtin",
            "task_id": "gpu_increment",
            "parameters": default_parameters(BUILTINS["gpu_increment"]),
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
        self.catalog: dict[str, JsonObject] = {}
        super().__init__("graph", default_graph() if document is None else document)

    def descriptor(self, node: JsonObject, document: JsonObject | None = None) -> JsonObject | None:
        """Resolve metadata by exact graph provenance, retaining unresolved nodes for save."""
        if node["pack_id"] == "atlas.builtin":
            return BUILTINS.get(node["task_id"])
        document = document if document is not None else self.snapshot()
        requested = next((p for p in document["packs"] if p["pack_id"] == node["pack_id"]), None)
        pack = self.catalog.get(requested["digest"]) if requested else None
        if not pack or any(pack[key] != requested[key] for key in ("pack_id", "version")):
            return None
        return next((task for task in pack["tasks"] if task["task_id"] == node["task_id"]), None)

    def unresolved_nodes(self) -> dict[str, str]:
        """Return explicit per-node resolution errors without changing serialized data."""
        document = self.snapshot()
        return {
            node["id"]: "Missing exact task descriptor"
            for node in document["nodes"]
            if self.descriptor(node, document) is None
        }

    def parameter_errors(self, document: JsonObject) -> list[str]:
        errors = []
        for node in document["nodes"]:
            descriptor = self.descriptor(node, document)
            if descriptor:
                errors.extend(
                    f"{node['id']}: {error}" for error in validate_parameters(descriptor, node["parameters"])
                )
                if node["resource"] != descriptor["resource"]:
                    errors.append(f"{node['id']}: resource does not match descriptor")
                if node.get("slice_workgroups") and not descriptor.get("supports_slicing", False):
                    errors.append(f"{node['id']}: task does not support slicing")
        return errors

    def _commit(self, candidate: JsonObject) -> None:
        referenced = {node["pack_id"] for node in candidate["nodes"]}
        candidate["packs"] = [pack for pack in candidate["packs"] if pack["pack_id"] in referenced]
        previous = {node["id"]: node for node in self.snapshot()["nodes"]}
        changed = [node for node in candidate["nodes"] if node != previous.get(node["id"])]
        errors = self.parameter_errors({**candidate, "nodes": changed})
        if errors:
            raise DocumentError("\n".join(errors))
        super()._commit(candidate)

    def add_descriptor(self, descriptor: JsonObject, pack: JsonObject | None = None) -> str:
        """Add defaults and exact provenance in one validated transaction."""
        candidate = self.snapshot()
        suffix = len(candidate["nodes"]) + 1
        identifier = f"task-{suffix}"
        while any(node["id"] == identifier for node in candidate["nodes"]):
            suffix += 1
            identifier = f"task-{suffix}"
        if pack:
            provenance = {key: pack[key] for key in ("pack_id", "version", "digest")}
            existing = next((p for p in candidate["packs"] if p["pack_id"] == pack["pack_id"]), None)
            if existing and existing != provenance:
                raise DocumentError("A graph can reference only one digest per pack ID")
            if not existing:
                candidate["packs"].append(provenance)
        candidate["nodes"].append(
            {
                "id": identifier,
                "name": descriptor.get("name") or descriptor["task_id"],
                "priority": 0,
                "resource": descriptor["resource"],
                "pack_id": pack["pack_id"] if pack else "atlas.builtin",
                "task_id": descriptor["task_id"],
                "parameters": default_parameters(descriptor),
            }
        )
        self._commit(candidate)
        return identifier

    def update_settings(self, settings: JsonObject) -> None:
        candidate = self.snapshot()
        candidate.update(copy.deepcopy(settings))
        self._commit(candidate)

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
