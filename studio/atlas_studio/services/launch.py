"""Pure executable discovery and output-directory launch policy."""

from __future__ import annotations

import os
from datetime import datetime
from pathlib import Path

from ..models.validation import ROOT


def discover_executable(environment_name: str, relative_path: str) -> Path | None:
    """Resolve an executable override or a normal Atlas build output."""
    override = os.environ.get(environment_name)
    if override:
        path = Path(override).expanduser()
        return path if path.is_file() else None
    suffix = ".exe" if os.name == "nt" else ""
    direct = ROOT / "build" / relative_path
    candidates = [direct.with_suffix(suffix)] if suffix else [direct]
    candidates.extend(sorted(ROOT.glob(f"build*/{relative_path}{suffix}")))
    candidates.extend(sorted(ROOT.glob(f"build/*/{relative_path}{suffix}")))
    return next((path for path in candidates if path.is_file()), None)


def prepare_benchmark_output_directory(selected: Path) -> Path:
    """Return a safe output directory, creating a run child when needed."""
    if not selected.exists():
        return selected
    if not selected.is_dir():
        raise NotADirectoryError(f"benchmark output path is not a directory: {selected}")
    if not any(selected.iterdir()):
        return selected

    timestamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    candidate = selected / f"testRun-{timestamp}"
    suffix = 2
    while candidate.exists():
        candidate = selected / f"testRun-{timestamp}-{suffix}"
        suffix += 1
    candidate.mkdir()
    return candidate
