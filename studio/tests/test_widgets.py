from atlas_studio.controllers import BenchmarkController, GraphController, ResultsController, StudioController
from atlas_studio.models import (
    BenchmarkDocumentModel,
    GraphDocumentModel,
    StudioSessionModel,
    default_benchmark,
    validate_document,
)
from atlas_studio.models.results import ResultsSessionModel
from atlas_studio.views import BenchmarkView, GraphView, MainWindow, ResultsView


def commit_uint(control, value: int) -> None:  # type: ignore[no-untyped-def]
    control.setText(str(value))
    control.editingFinished.emit()


def commit_text(control, value: str) -> None:  # type: ignore[no-untyped-def]
    control.setText(value)
    control.editingFinished.emit()


def commit_dimensions(control, *, x: int, y: int, z: int) -> None:  # type: ignore[no-untyped-def]
    for axis, value in (("x", x), ("y", y), ("z", z)):
        commit_uint(control.edits[axis], value)


def test_main_window_constructs_passive_workspaces_and_disables_result_actions(qtbot) -> None:
    window = MainWindow()
    qtbot.addWidget(window)
    controller = StudioController(window)
    assert window.tabs.count() == 3
    assert [window.tabs.tabText(index) for index in range(3)] == ["Task Graph", "Benchmarks", "Results"]

    window.show_workspace("results")
    assert window.workspace_kind() == "results"
    assert not window.actions["save"].isEnabled()
    assert not window.actions["validate"].isEnabled()
    assert not window.actions["run"].isEnabled()
    assert controller.session.workspace == "results"


def test_graph_controller_adds_tasks_and_rejects_a_cycle_atomically(qtbot) -> None:
    view = GraphView()
    qtbot.addWidget(view)
    model = GraphDocumentModel()
    GraphController(model, view)
    messages: list[str] = []
    view.message.connect(messages.append)

    view.task_add_requested.emit("cpu")
    assert len(model.snapshot()["nodes"]) == 3
    before = model.snapshot()
    view.dependency_add_requested.emit("gpu-1", "cpu-1")
    assert model.snapshot() == before
    assert any("cycle" in message for message in messages)


def test_graph_controller_exposes_every_runner_parameter(qtbot) -> None:
    view = GraphView()
    qtbot.addWidget(view)
    model = GraphDocumentModel()
    GraphController(model, view)

    commit_text(view.graph_id, "configured-graph")
    commit_uint(view.seed, 91)
    view.cpu_mode.setCurrentText("synchronous")
    commit_uint(view.worker_count, 6)
    view.policy.setCurrentText("round_robin")
    commit_uint(view.quantum, 13)
    view.validation.setChecked(True)
    view.trace_enabled.setChecked(False)
    commit_uint(view.trace_capacity, 4096)

    view.selection_requested.emit("cpu-1")
    commit_text(view.node_name, "configured CPU")
    commit_uint(view.priority, 17)
    commit_uint(view.iterations, 7654)

    view.selection_requested.emit("gpu-1")
    commit_text(view.node_name, "configured GPU")
    commit_uint(view.priority, 23)
    commit_dimensions(view.workgroups, x=7, y=3, z=2)
    view.slicing.setChecked(True)
    commit_dimensions(view.slice_dimensions, x=2, y=1, z=1)

    document = model.snapshot()
    assert document["graph_id"] == "configured-graph"
    assert document["seed"] == 91
    assert document["cpu_executor"] == {"mode": "synchronous", "worker_count": 6}
    assert document["policy"] == {"type": "round_robin", "quantum": 13}
    assert document["runtime"] == {"validation": True}
    assert document["trace"] == {"enabled": False, "capacity": 4096}
    assert document["nodes"][0]["kernel"] == {"type": "cpu_burn", "iterations": 7654}
    assert document["nodes"][1]["kernel"] == {"type": "gpu_increment", "workgroups": {"x": 7, "y": 3, "z": 2}}
    assert document["nodes"][1]["slice_workgroups"] == {"x": 2, "y": 1, "z": 1}
    assert validate_document("graph", document) == []


def test_graph_controller_reverts_a_duplicate_identifier(qtbot) -> None:
    view = GraphView()
    qtbot.addWidget(view)
    model = GraphDocumentModel()
    GraphController(model, view)
    commit_text(view.node_id, "gpu-1")
    assert [node["id"] for node in model.snapshot()["nodes"]] == ["cpu-1", "gpu-1"]
    assert view.node_id.text() == "cpu-1"


def test_benchmark_controller_preserves_and_edits_the_suite(qtbot) -> None:
    view = BenchmarkView()
    qtbot.addWidget(view)
    model = BenchmarkDocumentModel()
    BenchmarkController(model, view, StudioSessionModel())
    assert model.snapshot() == default_benchmark()

    commit_text(view.suite_id, "configured-suite")
    commit_text(view.seeds, "2, 3, 5")
    commit_uint(view.warmups, 3)
    commit_uint(view.repetitions, 7)
    commit_uint(view.workers, 9)

    view.tree.setCurrentItem(view.tree.topLevelItem(0))
    commit_text(view.case_id, "configured-case")
    commit_uint(view.cpu_tasks, 11)
    commit_uint(view.cpu_iterations, 2222)
    commit_uint(view.gpu_tasks, 4)
    commit_dimensions(view.gpu_workgroups, x=8, y=4, z=2)
    view.dependency_shape.setCurrentText("random")
    view.edge_probability.setValue(0.375)
    view.priority_assignment.setCurrentText("random")
    commit_text(view.priority_values, "1, 8, 21")
    commit_uint(view.burst_count, 5)

    scheduled = view.tree.topLevelItem(0).child(1)
    view.tree.setCurrentItem(scheduled)
    commit_text(view.variant_id, "configured-scheduled")
    view.variant_policy.setCurrentText("round_robin")
    commit_uint(view.variant_quantum, 16)
    view.variant_slicing.setChecked(True)
    commit_dimensions(view.variant_slice, x=4, y=2, z=1)

    document = model.snapshot()
    assert document["suite_id"] == "configured-suite"
    assert document["seeds"] == [2, 3, 5]
    case = document["cases"][0]
    assert case["case_id"] == "configured-case"
    assert case["workload"]["dependencies"] == {"shape": "random", "edge_probability": 0.375}
    assert case["variants"][1] == {
        "variant_id": "configured-scheduled",
        "execution": "scheduled",
        "policy": {"type": "round_robin", "quantum": 16},
        "slice_workgroups": {"x": 4, "y": 2, "z": 1},
    }
    assert validate_document("benchmark", document) == []


def test_benchmark_controller_adds_a_valid_unique_variant(qtbot) -> None:
    view = BenchmarkView()
    qtbot.addWidget(view)
    model = BenchmarkDocumentModel()
    BenchmarkController(model, view, StudioSessionModel())
    view.tree.setCurrentItem(view.tree.topLevelItem(0))
    view.variant_add_requested.emit(0)
    assert model.snapshot()["cases"][0]["variants"][-1] == {
        "variant_id": "variant-3",
        "execution": "scheduled",
        "policy": {"type": "fifo"},
        "slice_workgroups": None,
    }


def benchmark_record(run_id: int, record_type: str, **values) -> dict:
    context = {
        "run_id": run_id,
        "run_number": run_id + 1,
        "total_run_count": 22,
        "case_id": "case",
        "variant_id": "fifo",
        "execution": "scheduled",
        "seed": 42,
        "repetition": run_id,
        "warmup": False,
        "execution_order": 0,
    }
    return {"record_type": record_type, **context, **values}


def test_results_controller_retains_last_twenty_benchmark_runs(qtbot) -> None:
    view = ResultsView()
    qtbot.addWidget(view)
    model = ResultsSessionModel()
    controller = ResultsController(model, view)
    controller.receive_record(
        {
            "record_type": "header",
            "benchmark_stream_version": 1,
            "suite_id": "suite",
            "total_run_count": 22,
            "measured_run_count": 22,
            "trace_capacity": 100,
        }
    )
    for run_id in range(22):
        controller.receive_record(benchmark_record(run_id, "run_started"))
        controller.receive_record(
            benchmark_record(
                run_id,
                "task",
                task_id=1,
                name=f"task-{run_id}",
                resource="cpu",
                priority=0,
                burst=0,
                state="ready",
            )
        )
        controller.receive_record(
            benchmark_record(
                run_id,
                "run_finished",
                status="Success",
                executed_task_count=1,
                execution_time_ns=100,
                control_active_ns=10,
                throughput_tasks_per_second=1.0,
                device=None,
                timestamp_supported=False,
                accepted_events=0,
                dropped_events=0,
                tasks=[
                    {
                        "node_id": f"task-{run_id}",
                        "task_id": 1,
                        "state": "success",
                        "execution_duration_ns": 5,
                        "completed_work_units": 1,
                        "total_work_units": 1,
                        "ready_wait_ns": 1,
                        "selection_bypass_count": 0,
                    }
                ],
            )
        )

    assert len(model.history) == 20
    assert view.run_selector.count() == 21
    assert view.benchmark_runs.rowCount() == 22
    assert view.tasks.item(0, 0).text() == "task-21"
    view.run_selector.setCurrentIndex(view.run_selector.count() - 1)
    assert view.tasks.item(0, 0).text() == "task-2"
