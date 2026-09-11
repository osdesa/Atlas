"""Task-pack manifest, runner, Studio, and delivery acceptance contracts."""

import copy
import json
import os
import shutil
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
        if os.environ.get("ATLAS_REQUIRE_NATIVE_TESTS") == "1":
            pytest.fail("Required native pack runner/contract executable is missing")
        pytest.skip("build Atlas with tests to run native pack contracts")
    return runner, probe


@pytest.fixture
def pack(tmp_path, executables):
    directory = tmp_path / "pack"
    result = subprocess.run(
        [str(executables[1]), "fixture", str(directory)], capture_output=True, text=True, check=True
    )
    return directory, result.stdout.strip()


def run_graph(tmp_path, executables, document, directories=(), *, fault=None):
    path = tmp_path / "graph.json"
    path.write_text(json.dumps(document))
    command = [str(executables[0]), "--config", str(path), "--control", str(tmp_path / "cancel")]
    for directory in directories:
        command.extend(["--task-pack", str(directory)])
    environment = os.environ.copy()
    environment.pop("ATLAS_TEST_PACK_FAULT", None)
    if fault is not None:
        environment["ATLAS_TEST_PACK_FAULT"] = fault
    result = subprocess.run(command, capture_output=True, timeout=30, env=environment)
    decoder = JsonlRecordDecoder()
    records = [decoder.decode(line) for line in result.stdout.splitlines()]
    decoder.finish()
    return result, records


@pytest.mark.parametrize(
    "fault",
    [
        "api_null",
        "abi_version",
        "api_size",
        "common_size",
        "task_count",
        "describe_null",
        "cpu_size",
        "cpu_prepare_null",
        "cpu_execute_null",
        "cpu_destroy_null",
        "gpu_size",
        "gpu_prepare_null",
        "gpu_summary_null",
        "gpu_destroy_null",
        "metadata_status",
        "metadata_size",
        "metadata_resource",
        "metadata_duplicate",
        "metadata_oversize",
        "cpu_null_context",
        "cpu_prepare_status",
    ],
)
def test_native_abi_faults_fail_before_execution(tmp_path, executables, pack, fault):
    directory, digest = pack
    result, records = run_graph(tmp_path, executables, custom_graph(digest), [directory], fault=fault)
    assert result.returncode != 0
    assert len(records) == 1
    assert records[0]["record_type"] == "error" and records[0]["phase"] == "preflight"
    assert len(records[0]["message"].encode()) <= 4096


@pytest.mark.parametrize(
    "fault",
    [
        "gpu_prepare_status",
        "gpu_preparation_size",
        "gpu_buffer_count",
        "gpu_buffers_pointer",
        "gpu_buffer_size",
        "gpu_zero_allocation",
        "gpu_excess_allocation",
        "gpu_binding",
        "gpu_access",
        "gpu_readback_flag",
        "gpu_missing_readback",
        "gpu_readonly_readback",
        "gpu_reserved",
        "gpu_writer",
        "gpu_partial_initialization",
        "gpu_zero_workgroups",
        "gpu_device_workgroups",
    ],
)
def test_native_gpu_preparation_faults_fail_before_execution(tmp_path, executables, pack, fault):
    directory, digest = pack
    result, records = run_graph(
        tmp_path, executables, custom_graph(digest, "gpu_vector", "gpu"), [directory], fault=fault
    )
    assert result.returncode != 0
    assert len(records) == 1 and records[0]["phase"] == "preflight"


@pytest.mark.parametrize(
    "fault,task,resource,status,phase",
    [
        ("cpu_execute_status", "cpu_success", "cpu", "TaskFailed", None),
        ("cpu_summary_oversize", "cpu_success", "cpu", "TaskFailed", None),
        ("cpu_error_oversize", "cpu_success", "cpu", "TaskFailed", None),
        ("cpu_summary_invalid", "cpu_success", "cpu", "Success", "summary"),
        ("gpu_summary_status", "gpu_vector", "gpu", "Success", "summary"),
        ("gpu_summary_error", "gpu_vector", "gpu", "Success", "summary"),
        ("gpu_summary_invalid", "gpu_vector", "gpu", "Success", "summary"),
        ("gpu_summary_oversize", "gpu_vector", "gpu", "Success", "summary"),
    ],
)
def test_native_execution_faults_preserve_measurements_and_footer(
    tmp_path, executables, pack, fault, task, resource, status, phase
):
    directory, digest = pack
    graph = custom_graph(digest, task, resource)
    result, records = run_graph(tmp_path, executables, graph, [directory], fault=fault)
    assert result.returncode != 0
    assert records[0]["packs"] == graph["packs"]
    assert next(record for record in records if record["record_type"] == "result")["status"] == status
    assert not any(record["record_type"] == "task_summary" for record in records)
    errors = [record for record in records if record["record_type"] == "error"]
    assert [error["phase"] for error in errors] == ([] if phase is None else [phase])
    assert records[-1]["record_type"] == "footer" and records[-1]["complete"]
    assert records[-1]["status"] == "failed"


def test_native_process_exit_leaves_an_incomplete_stream(tmp_path, executables, pack):
    directory, digest = pack
    path = tmp_path / "graph.json"
    path.write_text(json.dumps(custom_graph(digest)))
    result = subprocess.run(
        [
            str(executables[0]),
            "--config",
            str(path),
            "--control",
            str(tmp_path / "cancel"),
            "--task-pack",
            str(directory),
        ],
        env={**os.environ, "ATLAS_TEST_PACK_FAULT": "native_exit"},
        capture_output=True,
        timeout=30,
    )
    assert result.returncode == 73
    decoder = JsonlRecordDecoder()
    records = [decoder.decode(line) for line in result.stdout.splitlines()]
    assert records[0]["packs"] == custom_graph(digest)["packs"]
    assert not any(record["record_type"] in {"result", "footer"} for record in records)
    with pytest.raises(ValueError, match="completion footer"):
        decoder.finish()


@pytest.mark.parametrize("changed", ["manifest", "library", "shader"])
def test_digest_is_location_independent_and_covers_every_referenced_asset(
    tmp_path, pack, executables, changed
):
    directory, digest = pack
    relocated = tmp_path / "relocated"
    shutil.copytree(directory, relocated)
    command = [str(executables[1]), "inspect", str(relocated)]
    assert subprocess.check_output(command, text=True).strip() == digest
    manifest = json.loads((relocated / "manifest.json").read_text())
    path = {
        "manifest": relocated / "manifest.json",
        "library": relocated / manifest["platforms"][0]["library"],
        "shader": relocated / manifest["shaders"][0]["path"],
    }[changed]
    with path.open("ab") as stream:
        stream.write(b" ")
    assert subprocess.check_output(command, text=True).strip() != digest


if hasattr(os, "mkfifo"):

    def test_inspection_rejects_posix_special_files_without_opening_them(pack, executables):
        directory, _ = pack
        os.mkfifo(directory / "pipe")
        result = subprocess.run(
            [str(executables[1]), "inspect", str(directory)], capture_output=True, timeout=5
        )
        assert result.returncode != 0 and not result.stdout


@pytest.mark.parametrize(
    "invalid", ["unknown_task", "wrong_node", "duplicate", "byte_bound", "nested", "nonfinite"]
)
def test_summary_decoder_rejects_misattribution_and_unbounded_values(invalid):
    records = [
        json.loads(line)
        for line in (Path(__file__).parent / "fixtures/cpu-pack-v2.jsonl").read_text().splitlines()
    ]
    decoder = JsonlRecordDecoder()
    for record in records[:2]:
        decoder.decode(json.dumps(record).encode())
    summary = records[2]
    if invalid == "unknown_task":
        summary["task_id"] = 2
    elif invalid == "wrong_node":
        summary["node_id"] = "other"
    elif invalid == "duplicate":
        decoder.decode(json.dumps(summary).encode())
    elif invalid == "byte_bound":
        # Each string meets the schema's character bound; UTF-8 aggregate bytes do not.
        summary["summary"] = {str(index): "界" * 4096 for index in range(6)}
    elif invalid == "nested":
        summary["summary"] = {"value": {"nested": 1}}
    else:
        summary["summary"] = {"value": float("inf")}
    with pytest.raises(ValueError):
        decoder.decode(json.dumps(summary, ensure_ascii=False).encode())


@pytest.mark.parametrize(
    "damage",
    ["missing_library", "directory_library", "manifest_size", "shader_size", "pack_size", "file_count"],
)
def test_inspection_rejects_filesystem_bounds(pack, executables, damage):
    directory, _ = pack
    manifest = json.loads((directory / "manifest.json").read_text())
    library = directory / manifest["platforms"][0]["library"]
    if damage in {"missing_library", "directory_library"}:
        library.unlink()
        if damage == "directory_library":
            library.mkdir()
    elif damage == "file_count":
        for index in range(129):
            (directory / f"extra-{index}").touch()
    else:
        path, size = {
            "manifest_size": (directory / "manifest.json", 1024 * 1024 + 1),
            "shader_size": (directory / "shaders/vector_add.spv", 16 * 1024 * 1024 + 1),
            "pack_size": (directory / "extra", 256 * 1024 * 1024 + 1),
        }[damage]
        with path.open("wb") as stream:
            stream.truncate(size)
    result = subprocess.run([str(executables[1]), "inspect", str(directory)], capture_output=True, timeout=30)
    assert result.returncode != 0 and not result.stdout


@pytest.mark.parametrize(
    "case,diagnostic",
    [
        ("missing", "declared bindings"),
        ("extra", "declared bindings"),
        ("set", "set zero"),
        ("uniform", "not a storage buffer"),
        ("array", "non-arrayed"),
        ("access", "declared bindings"),
        ("push", "push constants"),
        ("specialization", "specialization constants"),
        ("image", "images and samplers"),
        ("vertex", "GLCompute"),
    ],
)
def test_runner_rejects_valid_spirv_outside_storage_buffer_contract(
    tmp_path, executables, pack, case, diagnostic
):
    compiler = shutil.which("glslc")
    assert compiler is not None, "Native pack validation requires the Vulkan glslc build dependency"
    directory, _ = pack
    declarations = (
        "layout(set=0,binding=0) readonly buffer A { float value; } a;\n"
        "layout(set=0,binding=1) readonly buffer B { float value; } b;\n"
        "layout(set=0,binding=2) writeonly buffer C { float value; } c;\n"
    )
    expression = "a.value + b.value"
    if case == "missing":
        declarations = declarations.replace(
            "layout(set=0,binding=1) readonly buffer B { float value; } b;\n", ""
        )
        expression = "a.value"
    elif case == "extra":
        declarations += "layout(set=0,binding=3) readonly buffer D { float value; } d;\n"
        expression += " + d.value"
    elif case == "set":
        declarations = declarations.replace("set=0,binding=0", "set=1,binding=0")
    elif case == "uniform":
        declarations = declarations.replace("readonly buffer A", "uniform A")
    elif case == "array":
        declarations = declarations.replace("} a;", "} a[2];")
        expression = "a[0].value + b.value"
    elif case == "access":
        declarations = declarations.replace("readonly buffer A", "buffer A")
    elif case == "push":
        declarations += "layout(push_constant) uniform Push { float value; } p;\n"
        expression += " + p.value"
    elif case == "specialization":
        declarations += "layout(constant_id=0) const float extra = 1.0;\n"
        expression += " + extra"
    elif case == "image":
        declarations += "layout(set=0,binding=3) uniform sampler2D tex;\n"
        expression += " + texture(tex, vec2(0)).x"
    stage = "vertex" if case == "vertex" else "compute"
    source = tmp_path / "shader.glsl"
    source.write_text(
        "#version 450\n"
        + ("" if case == "vertex" else "layout(local_size_x=1) in;\n")
        + declarations
        + "void main() { c.value = "
        + expression
        + "; }\n"
    )
    output = directory / "shaders/vector_add.spv"
    subprocess.run(
        [compiler, "--target-env=vulkan1.1", f"-fshader-stage={stage}", str(source), "-o", str(output)],
        capture_output=True,
        check=True,
        timeout=30,
    )
    digest = subprocess.check_output([str(executables[1]), "inspect", str(directory)], text=True).strip()
    result, records = run_graph(tmp_path, executables, custom_graph(digest, "gpu_vector", "gpu"), [directory])
    assert result.returncode != 0
    assert len(records) == 1 and records[0]["phase"] == "preflight"
    assert diagnostic in records[0]["message"]


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


@pytest.fixture
def store(tmp_path):
    from PySide6.QtCore import QSettings

    from atlas_studio.services.packs import PackStore

    return PackStore(tmp_path / "store", QSettings(str(tmp_path / "trust.ini"), QSettings.IniFormat))


def test_studio_safe_import_trust_revocation_and_active_removal(pack, store):
    from atlas_studio.services.packs import PackStore

    directory, digest = pack
    metadata = store.import_directory(directory)
    assert metadata["digest"] == digest
    assert metadata["tasks"][0]["parameters"]
    graph = custom_graph(digest)
    assert "Untrusted" in store.problems(graph)[0]
    with pytest.raises(ValueError, match="Untrusted"):
        store.launch_directories(graph)
    store.set_trusted(digest, True)
    from PySide6.QtCore import QSettings

    reopened = PackStore(store.root, QSettings(store.settings.fileName(), QSettings.IniFormat))
    reopened.refresh()
    assert reopened.trusted(digest)
    assert store.launch_directories(graph) == (store.root / digest,)
    with pytest.raises(ValueError, match="active launch"):
        store.remove(digest)
    store.set_trusted(digest, False)
    assert not reopened.trusted(digest)
    with pytest.raises(ValueError, match="Untrusted"):
        store.launch_directories(graph)
    store.active.clear()
    store.remove(digest)
    assert "Missing" in store.problems(graph)[0]


def test_studio_inspection_never_loads_native_library(pack, store):
    directory, _ = pack
    path = directory / "manifest.json"
    manifest = json.loads(path.read_text())
    # Deliberately unloadable bytes are inspectable data and cannot execute.
    (directory / manifest["platforms"][0]["library"]).write_bytes(b"not a native module")
    metadata = store.import_directory(directory)
    assert metadata["tasks"]
    assert not store.trusted(metadata["digest"])


def test_studio_changed_content_requires_new_trust_and_detects_store_damage(pack, store):
    directory, digest = pack
    store.import_directory(directory)
    store.set_trusted(digest, True)
    path = directory / "manifest.json"
    manifest = json.loads(path.read_text())
    manifest["description"] = "Changed content"
    path.write_text(json.dumps(manifest))
    changed = store.import_directory(directory)
    assert changed["digest"] != digest
    assert not store.trusted(changed["digest"])
    (store.root / digest / "manifest.json").write_text(path.read_text())
    store.refresh()
    assert digest not in store.installed
    assert "changed" in store.errors[0]


def test_studio_custom_transaction_and_unresolved_save(pack, store):
    from atlas_studio.models.documents import DocumentError
    from atlas_studio.models.graph import GraphDocumentModel

    directory, digest = pack
    metadata = store.import_directory(directory)
    model = GraphDocumentModel()
    model.catalog = store.installed
    descriptor = next(task for task in metadata["tasks"] if task["task_id"] == "cpu_success")
    identifier = model.add_descriptor(descriptor, metadata)
    before = model.snapshot()
    node = copy.deepcopy(before["nodes"][-1])
    node["parameters"] = {"amount": 0}
    with pytest.raises(DocumentError, match="invalid parameter"):
        model.update_task(identifier, node)
    assert model.snapshot() == before
    unresolved = GraphDocumentModel(before)
    assert identifier in unresolved.unresolved_nodes()
    assert unresolved.snapshot() == before
    model.remove_task(identifier)
    assert model.snapshot()["packs"] == []


def test_studio_import_rejects_symlinks(pack, store):

    directory, _ = pack
    link = directory / "unsafe"
    try:
        link.symlink_to(directory / "manifest.json")
    except OSError as error:
        if os.environ.get("ATLAS_REQUIRE_NATIVE_TESTS") == "1":
            pytest.fail(f"Required symlink coverage unavailable: {error}")
        pytest.skip("symlinks unavailable")
    with pytest.raises(ValueError):
        store.import_directory(directory)
    link.unlink()


def test_studio_import_rejects_copy_digest_changes(pack, store, monkeypatch):
    from atlas_studio.services import packs

    directory, _ = pack
    real_inspect = packs.inspect_pack
    calls = 0

    def changing_inspect(path):
        nonlocal calls
        result = real_inspect(path)
        calls += 1
        if calls == 2:
            result["digest"] = "0" * 64
        return result

    monkeypatch.setattr(packs, "inspect_pack", changing_inspect)
    with pytest.raises(ValueError, match="changed during import"):
        store.import_directory(directory)
    assert not store.installed


def test_studio_custom_palette_and_summary_widgets(qtbot, pack, store):
    from PySide6.QtWidgets import QLineEdit

    from atlas_studio.controllers.graph import GraphController
    from atlas_studio.models.graph import GraphDocumentModel
    from atlas_studio.models.results import ResultsSessionModel
    from atlas_studio.views.graph import GraphView
    from atlas_studio.views.results import ResultsView

    metadata = store.import_directory(pack[0])
    model = GraphDocumentModel()
    model.catalog = store.installed
    view = GraphView()
    qtbot.addWidget(view)
    controller = GraphController(model, view)
    view.set_catalog(store.installed)
    view.palette.setCurrentIndex(3)
    view._add_selected()
    assert model.snapshot()["nodes"][-1]["pack_id"] == metadata["pack_id"]
    assert isinstance(view.parameter_fields["amount"], QLineEdit)
    assert view.parameter_fields["amount"].validator() is not None
    assert not view.slicing.isVisible()
    assert controller.selected_id == model.snapshot()["nodes"][-1]["id"]
    results = ResultsSessionModel()
    for line in (Path(__file__).parent / "fixtures/cpu-pack-v2.jsonl").read_bytes().splitlines():
        results.consume(json.loads(line))
    result_view = ResultsView()
    qtbot.addWidget(result_view)
    result_view.render(results.snapshot())
    result_view._show_task_summary(result_view.task_model.index(0, 0))
    assert result_view.task_summaries.topLevelItemCount() == 1


@pytest.mark.parametrize("task,resource", [("cpu_success", "cpu"), ("gpu_vector", "gpu")])
def test_studio_launches_exact_trusted_pack_through_worker(qtbot, pack, store, monkeypatch, task, resource):
    from PySide6 import QtCore

    from atlas_studio.services.process import AtlasProcessService

    metadata = store.import_directory(pack[0])
    digest = metadata["digest"]
    store.set_trusted(digest, True)
    document = custom_graph(digest, task, resource)
    if resource == "gpu":
        document["nodes"][0]["slice_workgroups"] = {"x": 1, "y": 1, "z": 1}
    directories = store.launch_directories(document)
    monkeypatch.setattr(QtCore, "QSettings", lambda *_args: store.settings)
    service = AtlasProcessService()
    records = []
    service.records_received.connect(lambda batch: records.extend(batch))
    with qtbot.waitSignal(service.run_finished, timeout=10000) as finished:
        service.start_graph(document, directories)
    assert finished.args == [0, "complete"]
    assert records[0]["packs"] == document["packs"]
    assert any(record["record_type"] == "task_summary" for record in records)
    store.set_trusted(digest, False)
    diagnostics = []
    service.diagnostic_received.connect(diagnostics.append)
    with qtbot.waitSignal(service.run_finished, timeout=10000) as finished:
        service.start_graph(document, directories)
    assert finished.args[1] == "failed"
    assert any("revoked" in error for error in diagnostics)


def test_scalar_forms_cover_all_types_and_required_without_defaults(qtbot):
    from atlas_studio.controllers.graph import GraphController
    from atlas_studio.models.descriptors import default_parameters, validate_parameters
    from atlas_studio.models.graph import GraphDocumentModel
    from atlas_studio.views.graph import GraphView

    descriptor = {
        "task_id": "scalars",
        "resource": "gpu",
        "supports_slicing": True,
        "parameters": [
            {"id": "flag", "type": "boolean"},
            {"id": "signed", "type": "integer", "minimum": -(2**63), "maximum": 2**63 - 1},
            {"id": "unsigned", "type": "unsigned_integer", "maximum": 2**64 - 1},
            {"id": "number", "type": "number", "minimum": -0.5, "maximum": 1.5},
            {"id": "text", "type": "string", "max_length": 4},
            {"id": "choice", "type": "enum", "values": ["first", "second"]},
        ],
    }
    metadata = {"pack_id": "scalar.pack", "digest": "a" * 64, "version": "1", "tasks": [descriptor]}
    model = GraphDocumentModel()
    model.catalog = {metadata["digest"]: metadata}
    assert not validate_parameters(descriptor, default_parameters(descriptor))
    identifier = model.add_descriptor(descriptor, metadata)
    view = GraphView()
    view.set_catalog(model.catalog)
    qtbot.addWidget(view)
    controller = GraphController(model, view)
    controller._select(identifier)
    view.parameter_fields["flag"].setChecked(True)
    view.parameter_fields["choice"].setCurrentText("second")
    view.parameter_fields["signed"].setText(str(-(2**63)))
    view.parameter_fields["unsigned"].setText(str(2**64 - 1))
    view.parameter_fields["number"].setText("1.25")
    view.parameter_fields["text"].setText("ok")
    view._update_task()
    before = model.snapshot()
    parameters = before["nodes"][-1]["parameters"]
    assert parameters["signed"] == -(2**63)
    assert parameters["unsigned"] == 2**64 - 1
    assert parameters["flag"] and parameters["choice"] == "second"
    view.parameter_fields["text"].setText("界界")
    view._update_task()
    assert model.snapshot() == before
    assert view.parameter_fields["text"].text() == "ok"


def test_missing_and_untrusted_graphs_disable_run_and_mark_nodes(qtbot, pack, store, monkeypatch):
    from atlas_studio.controllers import studio
    from atlas_studio.views.main_window import MainWindow

    metadata = store.import_directory(pack[0])
    monkeypatch.setattr(studio, "PackStore", lambda: store)
    window = MainWindow()
    qtbot.addWidget(window)
    controller = studio.StudioController(window)
    qtbot.waitUntil(lambda: controller.pack_jobs.thread is None)
    document = custom_graph(metadata["digest"])
    controller.graph.replace(document)
    assert not window.actions["run"].isEnabled()
    assert "Untrusted" in window.graph_view.node_problems["custom"]
    store.set_trusted(metadata["digest"], True)
    controller.graph.render()
    assert window.actions["run"].isEnabled()
    document["packs"][0]["version"] = "missing-version"
    controller.graph.replace(document)
    assert not window.actions["run"].isEnabled()
    assert "missing-version" in window.graph_view.pack_status.text()
    assert controller.graph.model.snapshot() == document


def test_pack_inspection_worker_is_off_gui_thread(qtbot):
    from PySide6.QtCore import QThread

    from atlas_studio.controllers.packs import PackJobs

    gui_thread = QThread.currentThread()
    observed = []
    jobs = PackJobs()
    with qtbot.waitSignal(jobs.finished, timeout=5000):
        jobs.start(lambda: observed.append(QThread.currentThread() is gui_thread))
    assert observed == [False]
    assert jobs.thread is None


def test_nonzero_incomplete_stream_reports_possible_native_crash(qtbot, tmp_path, monkeypatch):
    from atlas_studio.services.process import AtlasProcessService

    runner = tmp_path / "crash.py"
    runner.write_text(
        "import sys\n"
        'print(\'{"record_type":"header","studio_schema_version":2,"trace_schema_version":1,"packs":[]}\')\n'
        "sys.exit(7)\n"
    )
    monkeypatch.setenv("ATLAS_STUDIO_RUNNER", str(runner))
    service = AtlasProcessService()
    diagnostics = []
    service.diagnostic_received.connect(diagnostics.append)
    with qtbot.waitSignal(service.run_finished, timeout=5000) as finished:
        service.start_graph(default_graph())
    assert finished.args == [7, "failed"]
    assert any("possible native crash" in diagnostic for diagnostic in diagnostics)


def test_unavailable_pack_remains_inspectable_but_cannot_launch(pack, store):
    directory, _ = pack
    path = directory / "manifest.json"
    manifest = json.loads(path.read_text())
    binary = manifest["platforms"][0]
    binary["architecture"] = "aarch64" if binary["architecture"] == "x86_64" else "x86_64"
    path.write_text(json.dumps(manifest))
    metadata = store.import_directory(directory)
    store.set_trusted(metadata["digest"], True)
    assert metadata["tasks"]
    assert not store.available(metadata)
    with pytest.raises(ValueError, match="Unavailable"):
        store.launch_directories(custom_graph(metadata["digest"]))
    assert not store.active


def test_repeated_pack_jobs_finish_after_worker_destruction(qtbot):
    from threading import Event

    from PySide6.QtCore import Qt

    from atlas_studio.controllers.packs import PackJobs

    jobs = PackJobs()
    for _ in range(50):
        destroyed = Event()
        release = Event()
        with qtbot.waitSignal(jobs.finished, timeout=5000):
            jobs.start(lambda release=release: release.wait(5))
            jobs.worker.destroyed.connect(destroyed.set, Qt.DirectConnection)
            release.set()
        assert destroyed.is_set()
        assert jobs.worker is None and jobs.thread is None


@pytest.mark.parametrize("sliced", [False, True], ids=["ordinary", "sliced"])
def test_desktop_pack_delivery_workflow(qtbot, tmp_path, pack, store, monkeypatch, sliced):
    """Exercise the desktop boundary with actual native CPU and Vulkan execution."""
    from PySide6 import QtCore
    from PySide6.QtWidgets import QApplication, QMessageBox, QPushButton

    from atlas_studio.controllers import studio
    from atlas_studio.services.packs import TRUST_WARNING
    from atlas_studio.views.main_window import MainWindow

    monkeypatch.setattr(studio, "PackStore", lambda: store)
    monkeypatch.setattr(QtCore, "QSettings", lambda *_args: store.settings)
    window = MainWindow()
    qtbot.addWidget(window)
    controller = studio.StudioController(window)
    window.show()
    qtbot.waitUntil(lambda: controller.pack_jobs.thread is None)
    with qtbot.waitSignal(controller.pack_jobs.finished, timeout=10000) as imported:
        controller.pack_manager.import_requested.emit(str(pack[0]))
    assert imported.args == [""]
    digest = pack[1]
    assert digest in store.installed and not store.trusted(digest)

    # Exercise both responses to the real modal warning, including its safe default.
    for answer in (QMessageBox.No, QMessageBox.Yes):
        warnings = []

        def respond(answer=answer, warnings=warnings):
            dialog = QApplication.activeModalWidget()
            warnings.append(
                (dialog.text(), dialog.standardButton(dialog.defaultButton()), dialog.textFormat())
            )
            dialog.done(answer)

        QtCore.QTimer.singleShot(0, respond)
        controller.pack_manager.trust_requested.emit(digest, True)
        assert warnings == [(TRUST_WARNING + "\n\nSHA-256: " + digest, QMessageBox.No, QtCore.Qt.PlainText)]
        assert store.trusted(digest) == (answer == QMessageBox.Yes)

    view = window.graph_view
    add = next(button for button in view.findChildren(QPushButton) if button.text() == "Add selected task")
    for task_id in ("cpu_success", "gpu_vector"):
        index = next(
            index
            for index in range(view.palette.count())
            if view.palette.itemData(index)[0]["task_id"] == task_id
        )
        view.palette.setCurrentIndex(index)
        qtbot.mouseClick(add, QtCore.Qt.LeftButton)
        if task_id == "cpu_success":
            view.parameter_fields["amount"].setText("7")
            view.parameter_fields["amount"].editingFinished.emit()
        else:
            view.slicing.setChecked(sliced)

    document = controller.graph.model.snapshot()
    assert document["nodes"][-2]["parameters"] == {"amount": 7}
    assert bool(document["nodes"][-1].get("slice_workgroups")) == sliced
    path = tmp_path / "saved.json"
    monkeypatch.setattr(window, "choose_save_file", lambda *_args: path)
    window.actions["save"].trigger()
    assert json.loads(path.read_text()) == document
    monkeypatch.setattr(window, "choose_open_file", lambda: path)
    window.actions["open"].trigger()
    assert controller.graph.model.snapshot() == document
    assert window.actions["run"].isEnabled()
    with qtbot.waitSignal(controller.run.run_finished, timeout=30000) as finished:
        window.actions["run"].trigger()
    assert finished.args == [0, "complete"]
    assert not store.active
    snapshot = controller.results.model.snapshot()
    assert snapshot.records[0]["packs"] == document["packs"]
    custom = [task for task in snapshot.tasks if task["pack_id"] == "test.pack"]
    assert {task["pack_task_id"]: task["summary"] for task in custom} == {
        "cpu_success": {"value": 42},
        "gpu_vector": {"ok": True},
    }
    controller.results.render()
    results = window.results_view
    index = results.task_model.index(len(snapshot.tasks) - 1, 0)
    results.tasks.scrollTo(index)
    qtbot.mouseClick(
        results.tasks.viewport(), QtCore.Qt.LeftButton, pos=results.tasks.visualRect(index).center()
    )
    summary = results.task_summaries.topLevelItem(0)
    assert summary is not None
    assert summary.child(0).text(1) == "true"
    assert summary.child(1).text(0) == "Raw JSON"

    window.show_workspace("graph")
    controller.pack_manager.trust_requested.emit(digest, False)
    assert not window.actions["run"].isEnabled()
    assert "Untrusted" in view.pack_status.text()
    controller.pack_manager.remove_requested.emit(digest)
    assert not window.actions["run"].isEnabled()
    assert "Missing" in view.pack_status.text()
    window.actions["save"].trigger()
    assert json.loads(path.read_text()) == document
