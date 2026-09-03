# Atlas Studio

Atlas Studio is an optional PySide6 desktop application for authoring explicit
Atlas task graphs and benchmark-suite v1 documents. It stays in this repository
so the runner, schemas, shaders, and benchmark contracts change together. The
Python project does not alter the normal CMake build.

## Run locally

Build Atlas first, then install and start the source-based desktop application:

```bash
cd studio
python3 -m venv .venv
. .venv/bin/activate
python3 -m pip install -r requirements.lock
python3 -m pip install --no-deps -e .
python3 -m atlas_studio
```

Task Graph provides a visual DAG canvas and typed controls for every explicit
graph field. Benchmarks provides complete structured suite-v1 forms and a
synchronized advanced JSON editor. Results displays live JSONL events, task
measurements, timelines, diagnostics, and benchmark artifacts.

Benchmark runs enable live task tracing by default. The current warmup or
measured execution appears in Tasks and Timeline, and the latest 20 measured
runs remain selectable. Disable **Show live benchmark tasks** for a lower-
overhead timing run; command-line benchmark execution remains uninstrumented
unless `--studio-progress-jsonl` is supplied explicitly.

Set `ATLAS_STUDIO_RUNNER` or `ATLAS_BENCH` to override executable discovery.
User documents are validated against the versioned schemas and semantic graph
rules before a process is launched. A new or empty benchmark output directory
is used directly. When the selected directory already contains files, the
Studio preserves them and writes the run to a new
`testRun-YYYYMMDD-HHMMSS` child directory.

## Architecture

The desktop application uses MVC boundaries:

- `atlas_studio/models/` owns valid documents, run options, and bounded
  result-session state without depending on widgets. Graph, benchmark,
  validation, and JSONL protocol concerns live in separate modules;
- `atlas_studio/controllers/` applies editing intent, coordinates file and process
  services, validates live JSONL, and supplies detached presentation state;
- `atlas_studio/views/` contains composed form editors, graph/timeline canvases,
  a packaged QSS theme, and widgets that render controller snapshots;
- `atlas_studio/services/` isolates imports, launch policy, bounded process-line
  framing, `QProcess`, and filesystem behavior; and
- `app.py` is the composition root that wires these layers together.

Run the headless Studio tests with:

```bash
QT_QPA_PLATFORM=offscreen python3 -m pytest
```

Formatting and static readability checks use Ruff:

```bash
ruff format --check atlas_studio tests
ruff check atlas_studio tests
```
