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

The runner accepts graph v2 with exact custom-pack provenance and repeated
`--task-pack` arguments, and loads only digest-verified private snapshots. Studio
uses shared descriptors for built-in parameter forms and reads run v2 summaries.
The Task Packs manager imports directories into Qt's per-user application-data
store by verified digest, persists explicit digest trust in QSettings, and supports
revocation/removal. The palette and scalar forms use inspected descriptors.
Missing/untrusted/platform-unavailable tasks remain saveable with exact provenance
and disable Run. Select a result task for expandable plain-text summaries and raw
JSON. Python never loads native packs. Pack import/inspection uses a dedicated
worker; graph launch rechecks trust and the runner verifies private snapshots.
See the User Guide for the runner CLI and native-code trust boundary.
Studio bounds each JSONL record to 16 MiB, each task summary to 64 KiB, and
each complete stream to 128 MiB/one million records.

## Architecture

The desktop application uses MVC boundaries:

- `atlas_studio/models/` owns valid documents, run options, and bounded
  result-session state without depending on widgets. Graph, benchmark,
  validation, and JSONL protocol concerns live in separate modules;
- `atlas_studio/controllers/` applies editing intent, coordinates file and process
  services, time-slices validated live records, and supplies detached
  presentation state;
- `atlas_studio/views/` contains composed form editors, graph/timeline canvases,
  a packaged QSS theme, and widgets that render controller snapshots;
- `atlas_studio/services/` isolates imports and launch policy. A dedicated
  per-run `QThread` owns bounded process-line framing, schema validation,
  `QProcess`, and run-related filesystem I/O so those operations cannot block
  the GUI thread; and
- `app.py` is the composition root that wires these layers together.

Live refreshes are coalesced and large tables are virtualized. To keep a dense
trace responsive, the visual projection shows at most the first 5,000 tasks,
latest 500 timeline events, and latest 2,000 stream records, with an on-screen
notice when a limit applies. The bounded model history and final benchmark
artifacts remain available independently of these display limits.

Run the headless Studio tests with:

```bash
QT_QPA_PLATFORM=offscreen python3 -m pytest
```


The existing Studio CI matrix builds native contracts on Ubuntu and Windows x64,
selects real Mesa Lavapipe through a discovered `VK_DRIVER_FILES` manifest, and
runs the full PySide6 suite with `QT_QPA_PLATFORM=offscreen`. Windows uses MSVC,
the Vulkan SDK, and vcpkg SPIRV-Tools, then runs all C++ tests and `atlas` before
Studio tests. Native fixtures are actual shared libraries (`.dll` on Windows).
CI sets `ATLAS_REQUIRE_NATIVE_TESTS=1`: missing runner/probe binaries and missing
symlink privileges fail instead of skipping required coverage. Windows hosts
must permit symlink creation (developer mode or symlink privileges).
Set `ATLAS_STUDIO_RUNNER` and `ATLAS_TASK_PACK_CONTRACT` to the matching build's
executables when reproducing these tests locally. The manual robustness workflow
also runs the full Studio suite against ASan/UBSan native executables and packs,
with leak detection enabled. JUnit and CTest logs are uploaded as CI artifacts.

Formatting and static readability checks use Ruff:

```bash
ruff format --check atlas_studio tests
ruff check atlas_studio tests
```
