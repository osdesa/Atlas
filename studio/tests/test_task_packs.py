"""Part B manifest, runner preflight, native execution, and stream contracts."""

import copy
import json
import subprocess
from pathlib import Path

import pytest

from atlas_studio.models import JsonlRecordDecoder, default_graph, validate_document
from atlas_studio.models.validation import ROOT, SCHEMAS
from atlas_studio.services.launch import discover_executable


@pytest.fixture
def executables():
    runner = discover_executable("ATLAS_STUDIO_RUNNER", "apps/atlas_studio_runner/atlas_studio_runner")
    probe = discover_executable("ATLAS_TASK_PACK_CONTRACT", "tests/atlas_task_pack_contract")
    if runner is None or probe is None:
        pytest.skip("build Atlas with tests to run native pack contracts")
    return runner, probe


@pytest.fixture
def pack(tmp_path, executables):
    directory = tmp_path / "pack"
    result = subprocess.run(
        [str(executables[1]), "fixture", str(directory)], capture_output=True, text=True, check=True
    )
    return directory, result.stdout.strip()


def run_graph(tmp_path, executables, document, directories=()):
    path = tmp_path / "graph.json"
    path.write_text(json.dumps(document))
    command = [str(executables[0]), "--config", str(path), "--control", str(tmp_path / "cancel")]
    for directory in directories:
        command.extend(["--task-pack", str(directory)])
    result = subprocess.run(command, capture_output=True, timeout=30)
    decoder = JsonlRecordDecoder()
    records = [decoder.decode(line) for line in result.stdout.splitlines()]
    decoder.finish()
    return result, records


def custom_graph(digest, task="cpu_success", resource="cpu"):
    graph = default_graph()
    graph["packs"] = [{"pack_id": "test.pack", "version": "1.0.0", "digest": digest}]
    graph["nodes"] = [
        {
            "id": "custom",
            "name": "Custom task",
            "resource": resource,
            "priority": 0,
            "pack_id": "test.pack",
            "task_id": task,
            "parameters": {},
        }
    ]
    graph["edges"] = []
    return graph


@pytest.mark.parametrize(
    "task,resource,expected",
    [
        ("cpu_success", "cpu", {"value": 42}),
        ("gpu_vector", "gpu", {"ok": True}),
    ],
)
def test_native_runner_executes_exact_pack_and_emits_bounded_summary(
    tmp_path, executables, pack, task, resource, expected
):
    directory, digest = pack
    graph = custom_graph(digest, task, resource)
    if resource == "gpu":
        graph["nodes"][0]["slice_workgroups"] = {"x": 1, "y": 1, "z": 1}
    assert validate_document("graph", graph) == []
    result, records = run_graph(tmp_path, executables, graph, [directory])
    assert result.returncode == 0, result.stderr.decode()
    assert records[0]["packs"] == graph["packs"]
    assert any(record.get("summary") == expected for record in records)
    assert records[-1]["status"] == "success"
    task_record = next(record for record in records if record["record_type"] == "task")
    assert task_record["pack_id"] == "test.pack"
    assert task_record["pack_task_id"] == task
    assert directory.exists()


def test_all_builtins_execute_through_summary_protocol(tmp_path, executables):
    graph = json.loads((ROOT / "studio/examples/all-kernels-graph-v2.json").read_text())
    result, records = run_graph(tmp_path, executables, graph)
    assert result.returncode == 0, result.stderr.decode()
    assert records[0]["packs"] == []
    assert len([record for record in records if record["record_type"] == "task_summary"]) == 3


@pytest.mark.parametrize(
    "failure",
    [
        "missing",
        "digest",
        "version",
        "task",
        "resource",
        "parameter",
        "slice",
        "cycle",
        "overflow",
        "negative",
        "fractional",
        "cpu_slice",
        "unknown_dimension",
        "v1",
    ],
)
def test_runner_preflight_errors_emit_no_execution_records(tmp_path, executables, pack, failure):
    directory, digest = pack
    graph = custom_graph(digest)
    directories = [directory]
    if failure == "missing":
        directories = []
    elif failure in {"digest", "version"}:
        graph["packs"][0][failure] = "0" * 64 if failure == "digest" else "changed"
    elif failure == "task":
        graph["nodes"][0]["task_id"] = "absent"
    elif failure == "resource":
        graph["nodes"][0]["resource"] = "gpu"
    elif failure == "parameter":
        graph["nodes"][0]["parameters"] = {"amount": 0}
    elif failure in {"slice", "cpu_slice", "unknown_dimension"}:
        graph["nodes"][0]["slice_workgroups"] = {"x": 1, "y": 1, "z": 1}
        if failure == "unknown_dimension":
            graph["nodes"][0]["slice_workgroups"]["extra"] = 1
    elif failure == "cycle":
        graph["edges"] = [{"from": "custom", "to": "custom"}]
    elif failure in {"overflow", "negative", "fractional"}:
        graph["nodes"][0]["priority"] = {"overflow": 2**32, "negative": -1, "fractional": 1.5}[failure]
    elif failure == "v1":
        graph["schema_version"] = 1
    result, records = run_graph(tmp_path, executables, graph, directories)
    assert result.returncode != 0
    assert len(records) == 1
    assert records[0]["record_type"] == "error"
    assert records[0]["phase"] == "preflight"


def test_callback_failure_preserves_scheduler_result(tmp_path, executables, pack):
    directory, digest = pack
    result, records = run_graph(tmp_path, executables, custom_graph(digest, "cpu_error"), [directory])
    assert result.returncode != 0
    assert next(record for record in records if record["record_type"] == "result")["status"] == "TaskFailed"
    assert records[-1]["status"] == "failed"


def test_mixed_graph_and_unreferenced_pack_arguments(tmp_path, executables, pack):
    directory, digest = pack
    graph = custom_graph(digest)
    gpu = {**graph["nodes"][0], "id": "gpu", "resource": "gpu", "task_id": "gpu_vector"}
    graph["nodes"].append(gpu)
    graph["nodes"].append(default_graph()["nodes"][0])
    graph["edges"] = [{"from": "custom", "to": "gpu"}, {"from": "gpu", "to": "cpu-1"}]
    result, records = run_graph(tmp_path, executables, graph, [directory, directory])
    assert result.returncode == 0, result.stderr.decode()
    assert len([record for record in records if record["record_type"] == "task_summary"]) == 3
    # A supplied but unreferenced pack is inspected, never loaded or reported as executed provenance.
    manifest_path = directory / "manifest.json"
    manifest = json.loads(manifest_path.read_text())
    manifest["tasks"][0]["task_id"] = "native_metadata_mismatch"
    manifest_path.write_text(json.dumps(manifest))
    result, records = run_graph(tmp_path, executables, default_graph(), [directory])
    assert result.returncode == 0
    assert records[0]["packs"] == []


def test_missing_host_binary_is_a_preflight_error(tmp_path, executables, pack):
    directory, _digest = pack
    path = directory / "manifest.json"
    manifest = json.loads(path.read_text())
    binary = manifest["platforms"][0]
    binary["architecture"] = "aarch64" if binary["architecture"] == "x86_64" else "x86_64"
    path.write_text(json.dumps(manifest))
    digest = subprocess.check_output([str(executables[1]), "inspect", str(directory)], text=True).strip()
    result, records = run_graph(tmp_path, executables, custom_graph(digest), [directory])
    assert result.returncode != 0
    assert len(records) == 1 and records[0]["phase"] == "preflight"


@pytest.mark.parametrize("failure", ["preparation", "summary"])
def test_preparation_and_summary_errors_keep_their_protocol_phase(tmp_path, executables, pack, failure):
    directory, _digest = pack
    manifest_path = directory / "manifest.json"
    manifest = json.loads(manifest_path.read_text())
    if failure == "preparation":
        # Valid manifest and digest, but native GPU preparation disagrees with the requested interface.
        manifest["shaders"][0]["storage_buffers"][0]["access"] = "read_write"
        task, resource = "gpu_vector", "gpu"
    else:
        manifest["tasks"][0]["summaries"][0]["maximum"] = 10
        task, resource = "cpu_success", "cpu"
    manifest_path.write_text(json.dumps(manifest))
    digest = subprocess.check_output([str(executables[1]), "inspect", str(directory)], text=True).strip()
    result, records = run_graph(tmp_path, executables, custom_graph(digest, task, resource), [directory])
    assert result.returncode != 0
    error = next(record for record in records if record["record_type"] == "error")
    if failure == "preparation":
        assert len(records) == 1 and error["phase"] == "preflight"
    else:
        assert error["phase"] == "summary"
        assert next(record for record in records if record["record_type"] == "result")["status"] == "Success"
        assert records[-1]["status"] == "failed"


def test_cancellation_drains_and_emits_complete_footer(tmp_path, executables, pack):
    directory, digest = pack
    graph = custom_graph(digest)
    graph["nodes"][0]["parameters"] = {"amount": 5}
    (tmp_path / "cancel").touch()
    result, records = run_graph(tmp_path, executables, graph, [directory])
    assert records[-1]["complete"] is True
    status = next(record for record in records if record["record_type"] == "result")["status"]
    assert status == "Cancelled"
    assert result.returncode != 0


def test_manifest_schema_matches_library_structural_contract(tmp_path, executables, pack):
    directory, _digest = pack
    original = json.loads((directory / "manifest.json").read_text())
    cases = [(original, True)]
    for key, value in [
        ("schema_version", 2),
        ("abi_version", 2),
        ("extra", True),
        ("pack_id", "bad/id"),
        ("platforms", []),
        ("tasks", []),
    ]:
        candidate = copy.deepcopy(original)
        candidate[key] = value
        cases.append((candidate, False))
    for field in [
        {"id": "flag", "type": "boolean", "default": True},
        {"id": "signed", "type": "integer", "minimum": -10, "default": -2},
        {"id": "number", "type": "number", "default": 1.25},
        {"id": "text", "type": "string", "max_length": 20, "default": "hello"},
        {"id": "choice", "type": "enum", "values": ["a", "b"], "default": "a"},
    ]:
        candidate = copy.deepcopy(original)
        candidate["tasks"][0]["parameters"] = [field]
        cases.append((candidate, True))
        invalid = copy.deepcopy(candidate)
        invalid["tasks"][0]["parameters"][0]["default"] = []
        cases.append((invalid, False))
    for mutation in [
        "cpu_shader",
        "cpu_slicing",
        "wrong_extension",
        "traversal",
        "bad_access",
        "unknown_platform",
        "inapplicable_bounds",
        "optional_without_default",
    ]:
        candidate = copy.deepcopy(original)
        if mutation == "cpu_shader":
            candidate["tasks"][0]["shader_id"] = "vector_add"
        elif mutation == "cpu_slicing":
            candidate["tasks"][0]["supports_slicing"] = False
        elif mutation == "wrong_extension":
            candidate["shaders"][0]["path"] = "shaders/vector_add.txt"
        elif mutation == "traversal":
            candidate["platforms"][0]["library"] = "../outside.so"
        elif mutation == "bad_access":
            candidate["shaders"][0]["storage_buffers"][0]["access"] = "write"
        elif mutation == "unknown_platform":
            candidate["platforms"][0]["platform"] = "macos"
        elif mutation == "inapplicable_bounds":
            candidate["tasks"][0]["parameters"] = [{"id": "x", "type": "boolean", "minimum": 0}]
        else:
            candidate["tasks"][0]["parameters"] = [{"id": "x", "type": "boolean", "required": False}]
        cases.append((candidate, False))
    validator = SCHEMAS.validator("atlas-task-pack-v1.schema.json")
    for document, expected in cases:
        (directory / "manifest.json").write_text(json.dumps(document))
        inspected = subprocess.run([str(executables[1]), "inspect", str(directory)], capture_output=True)
        assert validator.is_valid(document) == expected, document
        assert (inspected.returncode == 0) == expected, inspected.stderr


def test_incomplete_native_crash_stream_is_rejected():
    decoder = JsonlRecordDecoder()
    decoder.decode(b'{"record_type":"header","studio_schema_version":2,"trace_schema_version":1,"packs":[]}')
    with pytest.raises(ValueError, match="completion footer"):
        decoder.finish()


@pytest.mark.parametrize(
    "name", ["builtin-only", "cpu-pack", "gpu-pack", "missing-pack", "wrong-digest", "incomplete-crash"]
)
def test_checked_stream_fixtures(name):
    decoder = JsonlRecordDecoder()
    for line in (Path(__file__).parent / "fixtures" / f"{name}-v2.jsonl").read_bytes().splitlines():
        decoder.decode(line)
    if name == "incomplete-crash":
        with pytest.raises(ValueError, match="completion footer"):
            decoder.finish()
    else:
        decoder.finish()
