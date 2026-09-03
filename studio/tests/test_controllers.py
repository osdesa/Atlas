from pathlib import Path

from atlas_studio.controllers import ResultsController, RunController
from atlas_studio.models import default_benchmark, default_graph
from atlas_studio.models.results import ResultsSessionModel
from atlas_studio.services.process import AtlasProcessService
from atlas_studio.views.results import ResultsView


def build_run_controller(qtbot):  # type: ignore[no-untyped-def]
    view = ResultsView()
    qtbot.addWidget(view)
    model = ResultsSessionModel()
    results = ResultsController(model, view)
    service = AtlasProcessService()
    return RunController(service, results), model


def test_run_controller_fails_malformed_versioned_stdout(qtbot, tmp_path: Path, monkeypatch) -> None:  # type: ignore[no-untyped-def]
    runner = tmp_path / "bad_runner.py"
    runner.write_text(
        '#!/usr/bin/env python3\nprint(\'{"record_type":"event"}\', flush=True)\n', encoding="utf-8"
    )
    runner.chmod(0o755)
    monkeypatch.setenv("ATLAS_STUDIO_RUNNER", str(runner))
    controller, model = build_run_controller(qtbot)

    with qtbot.waitSignal(controller.run_finished, timeout=5_000) as finished:
        controller.start_graph(default_graph())

    assert finished.args[1] == "failed"
    assert any("unversioned JSONL" in diagnostic for diagnostic in model.diagnostics)


def test_non_live_benchmark_stdout_remains_diagnostic_text(qtbot, tmp_path: Path, monkeypatch) -> None:  # type: ignore[no-untyped-def]
    runner = tmp_path / "plain_benchmark.py"
    runner.write_text(
        "#!/usr/bin/env python3\n"
        "import pathlib,sys\n"
        "arguments=sys.argv[1:]\n"
        "pathlib.Path(arguments[arguments.index('--output-dir')+1]).mkdir(parents=True, exist_ok=True)\n"
        "print('benchmark complete', flush=True)\n",
        encoding="utf-8",
    )
    runner.chmod(0o755)
    monkeypatch.setenv("ATLAS_BENCH", str(runner))
    controller, model = build_run_controller(qtbot)

    with qtbot.waitSignal(controller.run_finished, timeout=5_000) as finished:
        controller.start_benchmark(default_benchmark(), tmp_path / "results", None, live_tracing=False)

    assert finished.args[1] == "complete"
    assert any("benchmark complete" in diagnostic for diagnostic in model.diagnostics)
