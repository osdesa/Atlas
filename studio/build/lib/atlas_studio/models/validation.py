"""Schema and semantic validation for Studio documents and records."""

from __future__ import annotations

import json
from collections import Counter
from pathlib import Path

from jsonschema import Draft202012Validator
from referencing import Registry, Resource

from .documents import DocumentKind, JsonObject

ROOT = Path(__file__).resolve().parents[3]
SCHEMA_DIR = ROOT / "benchmarks" / "schema"


class SchemaRegistry:
    """Load repository schemas once and construct reference-aware validators."""

    def __init__(self, directory: Path = SCHEMA_DIR) -> None:
        schemas = [json.loads(path.read_text(encoding="utf-8")) for path in directory.glob("*.schema.json")]
        self._schemas = {schema["$id"]: schema for schema in schemas if "$id" in schema}
        self._registry = Registry().with_resources(
            (identifier, Resource.from_contents(schema)) for identifier, schema in self._schemas.items()
        )
        self._validators: dict[str, Draft202012Validator] = {}

    def validator(self, name: str) -> Draft202012Validator:
        cached = self._validators.get(name)
        if cached is not None:
            return cached
        schema = next(
            (value for identifier, value in self._schemas.items() if identifier.endswith(name)), None
        )
        if schema is None:
            raise ValueError(f"unknown Atlas schema: {name}")
        validator = Draft202012Validator(schema, registry=self._registry)
        self._validators[name] = validator
        return validator


SCHEMAS = SchemaRegistry()


def validate_document(kind: DocumentKind, document: JsonObject) -> list[str]:
    """Return schema and semantic errors for a user-authored document."""
    schema_name = (
        "atlas-studio-graph-v1.schema.json" if kind == "graph" else "atlas-baseline-suite-v1.schema.json"
    )
    errors = [
        f"{'.'.join(map(str, error.absolute_path)) or '$'}: {error.message}"
        for error in sorted(
            SCHEMAS.validator(schema_name).iter_errors(document), key=lambda item: list(item.absolute_path)
        )
    ]
    if kind != "graph" or errors:
        return errors
    return _graph_semantic_errors(document)


def detect_document_kind(document: JsonObject) -> DocumentKind:
    """Identify one supported versioned document shape."""
    if document.get("schema_version") == 1 and isinstance(document.get("nodes"), list):
        return "graph"
    if document.get("schema_version") == 1 and isinstance(document.get("cases"), list):
        return "benchmark"
    raise ValueError("file is not an Atlas graph-v1 or benchmark-suite-v1 document")


def _graph_semantic_errors(document: JsonObject) -> list[str]:
    errors: list[str] = []
    nodes = document["nodes"]
    identifiers = [node["id"] for node in nodes]
    duplicates = sorted(identifier for identifier, count in Counter(identifiers).items() if count > 1)
    if duplicates:
        errors.append(f"duplicate node ids: {', '.join(duplicates)}")
    known = set(identifiers)
    adjacency: dict[str, list[str]] = {identifier: [] for identifier in known}
    indegree = {identifier: 0 for identifier in known}
    seen_edges: set[tuple[str, str]] = set()
    for index, edge in enumerate(document["edges"]):
        source, target = edge["from"], edge["to"]
        if source not in known or target not in known:
            errors.append(f"edges[{index}] references an unknown node")
            continue
        if source == target:
            errors.append(f"edges[{index}] is a self dependency")
            continue
        pair = (source, target)
        if pair in seen_edges:
            errors.append(f"edges[{index}] duplicates {source} -> {target}")
            continue
        seen_edges.add(pair)
        adjacency[source].append(target)
        indegree[target] += 1
    queue = [identifier for identifier, count in indegree.items() if count == 0]
    visited = 0
    while queue:
        source = queue.pop()
        visited += 1
        for target in adjacency[source]:
            indegree[target] -= 1
            if indegree[target] == 0:
                queue.append(target)
    if known and visited != len(known):
        errors.append("graph dependencies contain a cycle")
    if document.get("policy", {}).get("type") == "round_robin" and "quantum" not in document["policy"]:
        errors.append("round_robin policy requires quantum")
    for index, node in enumerate(nodes):
        resource = node["resource"]
        kernel = node["kernel"]
        kernel_type = kernel["type"]
        path = f"nodes[{index}]"
        if resource == "cpu" and kernel_type != "cpu_burn":
            errors.append(f"{path}: CPU resource requires cpu_burn")
        if resource == "gpu" and kernel_type not in {"gpu_increment", "vector_add"}:
            errors.append(f"{path}: GPU resource requires a Vulkan kernel")
        if resource == "cpu" and node.get("slice_workgroups") is not None:
            errors.append(f"{path}: CPU work cannot be sliced")
        required = {"cpu_burn": "iterations", "gpu_increment": "workgroups", "vector_add": "element_count"}[
            kernel_type
        ]
        if required not in kernel:
            errors.append(f"{path}: {kernel_type} requires {required}")
    return errors
