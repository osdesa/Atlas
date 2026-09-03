import json

import pytest

from atlas_studio.models import (
    BenchmarkDocumentModel,
    DocumentError,
    GraphDocumentModel,
    JsonlRecordDecoder,
    default_benchmark,
    default_graph,
    detect_document_kind,
    load_jsonl,
    parse_json,
    validate_document,
)


def test_default_documents_satisfy_their_versioned_schemas() -> None:
    assert validate_document("graph", default_graph()) == []
    assert validate_document("benchmark", default_benchmark()) == []


def test_explicit_empty_documents_are_not_replaced_with_defaults() -> None:
    with pytest.raises(DocumentError):
        GraphDocumentModel({})
    with pytest.raises(DocumentError):
        BenchmarkDocumentModel({})


def test_graph_semantics_reject_duplicates_unknown_edges_and_cycles() -> None:
    duplicate = default_graph()
    duplicate["nodes"][1]["id"] = duplicate["nodes"][0]["id"]
    assert any("duplicate node ids" in error for error in validate_document("graph", duplicate))

    unknown = default_graph()
    unknown["edges"].append({"from": "missing", "to": "gpu-1"})
    assert any("unknown node" in error for error in validate_document("graph", unknown))

    cyclic = default_graph()
    cyclic["edges"].append({"from": "gpu-1", "to": "cpu-1"})
    assert any("cycle" in error for error in validate_document("graph", cyclic))


def test_json_round_trip_detects_document_kind() -> None:
    for kind, document in (("graph", default_graph()), ("benchmark", default_benchmark())):
        parsed = parse_json(json.dumps(document))
        assert detect_document_kind(parsed) == kind
        assert parsed == document


def test_graph_model_commits_edits_atomically_and_returns_detached_snapshots() -> None:
    model = GraphDocumentModel()
    snapshot = model.snapshot()
    snapshot["graph_id"] = "detached"
    assert model.snapshot()["graph_id"] == "studio-example"

    replacement = model.snapshot()["nodes"][0]
    replacement["id"] = "renamed"
    assert model.update_task("cpu-1", replacement) == "renamed"
    assert model.snapshot()["edges"] == [{"from": "renamed", "to": "gpu-1"}]

    before = model.snapshot()
    with pytest.raises(DocumentError, match="cycle"):
        model.add_dependency("gpu-1", "renamed")
    assert model.snapshot() == before


def test_graph_model_rejects_duplicate_rename_and_removing_final_task() -> None:
    model = GraphDocumentModel()
    duplicate = model.snapshot()["nodes"][0]
    duplicate["id"] = "gpu-1"
    with pytest.raises(DocumentError, match="duplicate"):
        model.update_task("cpu-1", duplicate)
    assert [node["id"] for node in model.snapshot()["nodes"]] == ["cpu-1", "gpu-1"]

    assert model.remove_task("cpu-1") == "gpu-1"
    with pytest.raises(DocumentError):
        model.remove_task("gpu-1")
    assert [node["id"] for node in model.snapshot()["nodes"]] == ["gpu-1"]


def test_benchmark_model_tracks_reference_renames_and_guards_removal() -> None:
    model = BenchmarkDocumentModel()
    document = model.snapshot()
    direct = {**document["cases"][0]["variants"][0], "variant_id": "renamed-direct"}
    model.update_variant(0, 0, direct)
    assert model.snapshot()["cases"][0]["reference_variant"] == "renamed-direct"

    with pytest.raises(DocumentError, match="reference"):
        model.remove_variant(0, 0)

    with pytest.raises(DocumentError, match="too short"):
        model.remove_variant(0, 1)

    added = model.add_variant(0)
    model.remove_variant(0, added)
    assert len(model.snapshot()["cases"][0]["variants"]) == 2

    with pytest.raises(DocumentError, match="unknown benchmark case"):
        model.add_variant(99)
    with pytest.raises(DocumentError, match="unknown benchmark variant"):
        model.remove_variant(0, 99)


def benchmark_progress_records() -> list[dict]:
    return [
        {
            "record_type": "header",
            "benchmark_stream_version": 1,
            "suite_id": "test",
            "total_run_count": 1,
            "measured_run_count": 1,
            "trace_capacity": 16,
        },
        {
            "record_type": "run_started",
            "run_id": 0,
            "run_number": 1,
            "total_run_count": 1,
            "case_id": "case",
            "variant_id": "fifo",
            "execution": "scheduled",
            "seed": 42,
            "repetition": 0,
            "warmup": False,
            "execution_order": 0,
        },
        {
            "record_type": "task",
            "run_id": 0,
            "task_id": 1,
            "name": "cpu-0",
            "resource": "cpu",
            "priority": 0,
            "burst": 0,
            "state": "ready",
        },
        {
            "record_type": "run_finished",
            "run_id": 0,
            "run_number": 1,
            "total_run_count": 1,
            "case_id": "case",
            "variant_id": "fifo",
            "execution": "scheduled",
            "seed": 42,
            "repetition": 0,
            "warmup": False,
            "execution_order": 0,
            "status": "Success",
            "executed_task_count": 1,
            "execution_time_ns": 100,
            "control_active_ns": 10,
            "throughput_tasks_per_second": 1.0,
            "device": None,
            "timestamp_supported": False,
            "accepted_events": 0,
            "dropped_events": 0,
            "tasks": [
                {
                    "node_id": "cpu-0",
                    "task_id": 1,
                    "state": "success",
                    "execution_duration_ns": 50,
                    "completed_work_units": 1,
                    "total_work_units": 1,
                    "ready_wait_ns": 5,
                    "selection_bypass_count": 0,
                }
            ],
        },
        {
            "record_type": "footer",
            "status": "success",
            "completed_run_count": 1,
            "total_run_count": 1,
            "complete": True,
        },
    ]


def test_benchmark_progress_stream_is_versioned_and_validated(tmp_path) -> None:  # type: ignore[no-untyped-def]
    records = benchmark_progress_records()
    path = tmp_path / "progress.jsonl"
    path.write_text("\n".join(json.dumps(record) for record in records) + "\n", encoding="utf-8")
    assert load_jsonl(path) == records

    decoder = JsonlRecordDecoder()
    assert [decoder.decode(json.dumps(record).encode()) for record in records] == records


def test_incremental_decoder_rejects_unversioned_and_invalid_records() -> None:
    decoder = JsonlRecordDecoder()
    with pytest.raises(ValueError, match="unversioned"):
        decoder.decode(b'{"record_type":"event"}')

    decoder = JsonlRecordDecoder()
    decoder.decode(json.dumps(benchmark_progress_records()[0]).encode())
    with pytest.raises(ValueError, match="invalid JSONL"):
        decoder.decode(b'{"record_type":"task"}')


def test_incremental_decoder_requires_a_footer_and_rejects_trailing_records() -> None:
    records = benchmark_progress_records()
    decoder = JsonlRecordDecoder()
    decoder.decode(json.dumps(records[0]).encode())
    with pytest.raises(ValueError, match="no completion footer"):
        decoder.finish()

    decoder = JsonlRecordDecoder()
    for record in records:
        decoder.decode(json.dumps(record).encode())
    decoder.finish()
    with pytest.raises(ValueError, match="after its footer"):
        decoder.decode(json.dumps(records[-1]).encode())

    decoder = JsonlRecordDecoder()
    decoder.decode(json.dumps(records[0]).encode())
    with pytest.raises(ValueError, match="more than one header"):
        decoder.decode(json.dumps(records[0]).encode())


def test_incremental_decoder_enforces_configurable_bounds() -> None:
    header = json.dumps(benchmark_progress_records()[0]).encode()
    with pytest.raises(ValueError, match="128 MiB"):
        JsonlRecordDecoder(maximum_bytes=len(header)).decode(header)
    with pytest.raises(ValueError, match="2 MiB"):
        JsonlRecordDecoder(maximum_line_bytes=4).decode(header)
