"""Shared built-in metadata and scalar rules for inspected task descriptors."""

from __future__ import annotations

import copy
import json
import math
from importlib.resources import files

from .documents import JsonObject

BUILTINS = {
    item["task_id"]: item
    for item in json.loads(files("atlas_studio.resources").joinpath("builtin-tasks.json").read_text())
}


def default_parameters(descriptor: JsonObject) -> JsonObject:
    """Copy defaults, initializing required fields without defaults to a valid scalar."""
    parameters = {}
    for field in descriptor["parameters"]:
        if "default" in field:
            value = copy.deepcopy(field["default"])
        elif field["type"] == "boolean":
            value = False
        elif field["type"] == "enum":
            value = field["values"][0]
        elif field["type"] == "string":
            value = ""
        else:
            value = 0
            if "minimum" in field:
                value = max(value, field["minimum"])
            if "maximum" in field:
                value = min(value, field["maximum"])
        parameters[field["id"]] = value
    return parameters


def validate_parameters(descriptor: JsonObject, parameters: JsonObject) -> list[str]:
    """Check flat scalar values and byte limits before a Studio transaction commits."""
    errors: list[str] = []
    if len(json.dumps(parameters, ensure_ascii=False).encode()) > 65536:
        errors.append("parameters exceed 64 KiB")
    expected = {field["id"] for field in descriptor["parameters"]}
    if parameters.keys() - expected:
        errors.append("unknown parameter")
    for field in descriptor["parameters"]:
        identifier = field["id"]
        if identifier not in parameters:
            if "default" not in field and field.get("required", True):
                errors.append(f"missing parameter: {identifier}")
            continue
        value = parameters[identifier]
        kind = field["type"]
        valid = True
        if kind == "boolean":
            valid = type(value) is bool
        elif kind in {"integer", "unsigned_integer"}:
            low, high = (0, 2**64 - 1) if kind == "unsigned_integer" else (-(2**63), 2**63 - 1)
            valid = type(value) is int and low <= value <= high
        elif kind == "number":
            valid = type(value) in {int, float} and math.isfinite(value)
        elif kind in {"string", "enum"}:
            valid = isinstance(value, str) and "\0" not in value
            if valid and kind == "string":
                valid = len(value.encode()) <= field["max_length"]
            if valid and kind == "enum":
                valid = value in field["values"]
        if valid and kind in {"integer", "unsigned_integer", "number"}:
            valid = field.get("minimum", value) <= value <= field.get("maximum", value)
        if not valid:
            errors.append(f"invalid parameter: {identifier}")
    return errors
