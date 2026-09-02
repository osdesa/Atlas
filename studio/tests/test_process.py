import re
import sys
from pathlib import Path

import pytest

from atlas_studio.models import default_benchmark
from atlas_studio.services import AtlasProcessService, prepare_benchmark_output_directory
from atlas_studio.services.streams import BoundedLineBuffer


def test_benchmark_output_uses_new_or_empty_directory_directly(tmp_path: Path) -> None:
    new_directory = tmp_path / "new"
    assert prepare_benchmark_output_directory(new_directory) == new_directory
    assert not new_directory.exists()

    empty_directory = tmp_path / "empty"
    empty_directory.mkdir()
    assert prepare_benchmark_output_directory(empty_directory) == empty_directory


def test_benchmark_output_creates_timestamped_child_in_nonempty_directory(tmp_path: Path) -> None:
    (tmp_path / "existing-result.json").write_text("{}\n", encoding="utf-8")
    output_directory = prepare_benchmark_output_directory(tmp_path)
    assert output_directory.parent == tmp_path
    assert re.fullmatch(r"testRun-\d{8}-\d{6}", output_directory.name)
    assert output_directory.is_dir()
    assert (tmp_path / "existing-result.json").is_file()


def test_benchmark_output_rejects_a_file_path(tmp_path: Path) -> None:
    output_file = tmp_path / "result.json"
    output_file.write_text("{}\n", encoding="utf-8")
    try:
        prepare_benchmark_output_directory(output_file)
        raise AssertionError("expected non-directory output rejection")
    except NotADirectoryError as error:
        assert str(output_file) in str(error)


def test_benchmark_service_passes_timestamped_child_to_atlas_bench(
    qtbot, tmp_path: Path, monkeypatch
) -> None:  # type: ignore[no-untyped-def]
    runner = tmp_path / "fake_atlas_bench.py"
    runner.write_text(
        "#!/usr/bin/env python3\n"
        "import pathlib,sys\n"
        "arguments = sys.argv[1:]\n"
        "output = pathlib.Path(arguments[arguments.index('--output-dir') + 1])\n"
        "(output / 'received.txt').write_text(str(output), encoding='utf-8')\n",
        encoding="utf-8",
    )
    runner.chmod(0o755)
    monkeypatch.setenv("ATLAS_BENCH", str(runner))
    service = AtlasProcessService()
    diagnostics: list[str] = []
    service.diagnostic_received.connect(diagnostics.append)

    with qtbot.waitSignal(service.run_finished, timeout=5_000):
        service.start_benchmark(default_benchmark(), tmp_path)

    output_directory = service.output_directory
    assert output_directory is not None
    assert output_directory.parent == tmp_path
    assert (output_directory / "received.txt").read_text(encoding="utf-8") == str(output_directory)
    assert any(str(output_directory) in diagnostic for diagnostic in diagnostics)
    assert service.process.arguments()[-1] == "--studio-progress-jsonl"


def test_process_service_publishes_raw_stdout_and_stderr(qtbot, tmp_path: Path) -> None:
    script = tmp_path / "fake_runner.py"
    script.write_text(
        "import sys\n"
        'print(\'{"record_type":"header"}\', flush=True)\n'
        "print('diagnostic text', file=sys.stderr, flush=True)\n",
        encoding="utf-8",
    )
    service = AtlasProcessService()
    lines: list[bytes] = []
    diagnostics: list[str] = []
    service.stdout_line.connect(lines.append)
    service.diagnostic_received.connect(diagnostics.append)
    service._prepare("graph")
    with qtbot.waitSignal(service.run_finished, timeout=5_000):
        service._start(Path(sys.executable), [str(script)])
    assert lines == [b'{"record_type":"header"}']
    assert any("diagnostic text" in diagnostic for diagnostic in diagnostics)


def test_process_service_enforces_one_active_run(qtbot, tmp_path: Path) -> None:
    script = tmp_path / "waiting.py"
    script.write_text("import time\ntime.sleep(2)\n", encoding="utf-8")
    service = AtlasProcessService()
    service._prepare("graph")
    service._start(Path(sys.executable), [str(script)])
    try:
        assert service.active
        try:
            service._prepare("benchmark")
            raise AssertionError("expected one-run guard")
        except RuntimeError as error:
            assert "one Atlas run" in str(error)
    finally:
        service.process.kill()
        service.process.waitForFinished(3_000)


def test_bounded_line_buffer_frames_fragments_and_rejects_oversize_lines() -> None:
    buffer = BoundedLineBuffer(5)
    assert buffer.feed(b"one\ntw") == [b"one"]
    assert buffer.feed(b"o\r\n") == [b"two"]
    assert buffer.finish() == b""

    with pytest.raises(ValueError, match="unterminated"):
        BoundedLineBuffer(3).feed(b"four")
