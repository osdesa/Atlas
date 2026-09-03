# Atlas User Guide

## What Atlas does

Atlas is a C++20 CPU/Vulkan task-graph prototype. The current milestone builds
and finalises a directed acyclic graph, executes CPU callables through a
synchronous or fixed-size worker pool, executes Vulkan compute dispatches, and
schedules both resource classes independently. FIFO, cooperative work-unit
round-robin, and stable static-priority selection are implemented. Sliced
Vulkan tasks yield only between completed dispatch units; Atlas does not
preempt an active Vulkan dispatch.

The current milestone also provides a comparison-suite benchmark harness with
deterministic generated workloads, direct-versus-scheduled variants, JSON
Lines/CSV output, environment metadata, paired confidence intervals, and
capability-checked Vulkan device timing. Optional bounded execution tracing
records scheduler, policy, executor, state-transition, and cancellation events
for validation, summaries, and SVG timelines.
Graphics, presentation, multiple Vulkan queues, runtime graph mutation,
repeated graph execution, and true CPU/GPU preemption are not implemented.

Vulkan device loss is permanent for a runtime context. Atlas stops new graph
submissions, drains already accepted CPU and Vulkan work, reports
`ExecutorUnavailable`, and preserves an attributable `VulkanError`. Resources
remain safely destructible, but the runtime and its executors cannot be reused;
Atlas does not recreate the device or select a fallback.

## Requirements

Atlas requires:

- CMake 3.24 or newer;
- a C++20 compiler;
- Threads and Vulkan development headers/loader;
- a Vulkan 1.1-capable physical device with a compute queue and dispatch-base
  support required by Atlas;
- `glslc` and `spirv-val` for checked shader compilation;
- Ninja when using the Linux presets.

Vulkan is mandatory. There is no CPU-only build or runtime fallback. On Ubuntu:

```bash
sudo apt-get install build-essential cmake ninja-build libvulkan-dev \
  mesa-vulkan-drivers vulkan-validationlayers glslc spirv-tools
```

## Building Atlas

Use the normal CMake workflow:

```bash
cmake -S . -B build
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

On Linux, the equivalent development preset is:

```bash
cmake --preset dev-linux
cmake --build --preset dev-linux --parallel
ctest --preset dev-linux
```

The first test-enabled configuration may download Catch2 and nlohmann/json.
Configuration fails if Vulkan or the shader tools are unavailable.

Profiling is compiled in by default. Configure with
`-DATLAS_ENABLE_PROFILING=OFF` when measuring a build that must contain no event
clock reads, trace publication, or Vulkan timestamp queries. Such a build still
provides the trace API, but `TraceSession::emit` is inert and `atlas --trace`
fails with an explicit diagnostic.

## Running Atlas

### Local Atlas Studio

The optional PySide6 desktop studio edits explicit DAGs and the complete
benchmark-suite input contract. Build Atlas first, then install and launch the
source-based application:

```bash
cd studio
python3 -m venv .venv
. .venv/bin/activate
python3 -m pip install -r requirements.lock
python3 -m pip install --no-deps -e .
python3 -m atlas_studio
```

The application launches `atlas_studio_runner` and `atlas_bench` directly and
supervises one local run at a time. Widgets and presentation remain on Qt's GUI
thread. Each run receives a dedicated worker thread for process launch and
control, temporary and result-file I/O, stream framing, JSON decoding, and
schema validation. Validated records cross back in batches and are applied in
bounded time slices so a complex benchmark or dense live trace does not
monopolize GUI event handling. Atlas execution remains isolated in the
supervised C++ child process. Large tables use virtual models, hidden result
tabs are not rebuilt, and live rendering is limited to the first 5,000 tasks,
latest 500 timeline events, and latest 2,000 stream records. The Studio shows
when one of these presentation limits is active; validated result state and
benchmark artifacts retain their existing bounds. Set `ATLAS_STUDIO_RUNNER` or
`ATLAS_BENCH` when the executables are outside the normal build tree. The
application accepts only versioned built-in-kernel documents and does not
accept arbitrary C++, shaders, task-level live control, or runtime graph
mutation. The graph contract is
`benchmarks/schema/atlas-studio-graph-v1.schema.json`; live runner output is the
versioned `atlas-studio-run-v1` JSONL stream.

Task Studio exposes CPU burn and Vulkan increment/vector-add kernels, task
metadata, explicit edges, worker capacity, policies, slicing, seeds, Vulkan
validation, and trace settings. Benchmark Studio provides structured controls
and synchronized JSON for every field in the existing
`atlas-baseline-suite-v1` contract, optional environment metadata, and a new or
empty output directory. If the selected benchmark directory already contains
files, the Studio preserves them and creates a `testRun-YYYYMMDD-HHMMSS` child
directory for the new run. Results show live task state, timelines, events,
diagnostics, final measurements, and imported versioned traces or benchmark
artifacts.

Structured edits are committed atomically: an edit that would create an
invalid graph or suite is rejected and the last valid value is restored. Save,
Validate, and Run apply only to the Task Graph and Benchmarks workspaces and
are disabled while Results is selected. Live machine-readable runner records
are schema-checked before they are displayed. Malformed, oversized, mixed, or
incomplete streams fail the affected run and are reported in Diagnostics;
imported headered JSONL streams likewise require one terminal completion footer.

Studio enables live benchmark task tracing by default. Tasks and Timeline show
the active warmup or measured execution and retain the latest 20 measured runs
in a selector; warmups are displayed live but are not retained. This
instrumentation adds observer overhead, so clear **Show live benchmark tasks**
for a timing-focused GUI run. The aggregate result files remain the
authoritative output in either mode.

### `atlas`

`atlas` runs the current representative workflow: a CPU preparation task,
cooperatively sliced Vulkan vector addition, and a dependent CPU verification
task.

```bash
./build/apps/atlas/atlas [--trace build/atlas-trace.jsonl]
```

`--trace` optionally writes a version-one JSON Lines execution trace. The path
must not already exist. Success prints the number of completed
Vulkan work units and the selected device name. Any graph, scheduling, Vulkan,
or verification failure returns a non-zero exit status and prints an error.

Trace producers publish fixed-size events to a preallocated bounded buffer and
never perform file I/O. Publication is best-effort: events can be dropped when
the buffer is full or contended. The completion footer reports accepted and
dropped counts. A missing footer means the producing process did not finish the
trace; analysis rejects this by default.

Validate, summarize, or render a trace with the dependency-free Python tool:

```bash
python3 tools/atlas_trace.py validate build/atlas-trace.jsonl
python3 tools/atlas_trace.py summary build/atlas-trace.jsonl
python3 tools/atlas_trace.py timeline build/atlas-trace.jsonl \
  --output build/atlas-timeline.svg
```

Trace inputs are bounded to 128 MiB, one million records, and 2 MiB per
record. Timeline output is intentionally no-overwrite: choose a new path or
move an earlier SVG before rendering again.

Pass `--allow-incomplete` only when intentionally inspecting a trace without a
footer. The checked record contract is
`benchmarks/schema/atlas-trace-v1.schema.json`. Timestamps use one monotonic
host-clock origin per session. Sequence numbers are unique but records from
concurrent producers need not arrive in sequence order.

### `atlas_bench`

`atlas_bench` validates or executes a version-one comparison suite:

```bash
./build/apps/atlas_bench/atlas_bench \
  --suite benchmarks/manifests/smoke-v1.json \
  --output-dir build/results
```

| Name | Requirement | Allowed value/default | Effect | Example |
|---|---|---|---|---|
| `--suite` | Required | Path to a version-one suite; no default | Selects the experiment suite | `--suite benchmarks/manifests/smoke-v1.json` |
| `--output-dir` | Required unless validating | Directory path; no default | Writes resolved inputs, runs, tasks, and summaries | `--output-dir build/results` |
| `--environment-file` | Optional | Path to strict environment JSON; absent by default | Adds user-supplied machine metadata | `--environment-file host.json` |
| `--validate-only` | Optional | Flag; off by default | Validates JSON without executing or writing results | `--validate-only` |
| `--overwrite` | Optional | Flag; off by default | Permits replacing existing result files | `--overwrite` |
| `--studio-progress-jsonl` | Optional | Flag; off by default | Emits the version-one Studio benchmark progress stream on stdout; requires profiling | `--studio-progress-jsonl` |
| `--help` | Optional | Flag | Prints usage and exits | `--help` |

Unknown, repeated, or inconsistent arguments are errors. Suites are the only
supported benchmark input. `--studio-progress-jsonl` is invalid with
`--validate-only`. In progress mode stdout is machine-readable JSONL and stderr
is reserved for diagnostics; without it, the existing human-readable completion
message is unchanged. The checked stream contract is
`benchmarks/schema/atlas-studio-benchmark-progress-v1.schema.json`.

## Benchmark suite format

Every field is required unless stated otherwise. Unknown fields are rejected.
See `benchmarks/manifests/smoke-v1.json` for the canonical small suite and
`baseline-canonical-v1.json` for the full current matrix.

Top-level fields:

| Field | Type and allowed values | Meaning |
|---|---|---|
| `schema_version` | unsigned integer, exactly `1` | Suite format version |
| `suite_id` | non-empty string | Stable suite identifier |
| `seeds` | non-empty array of unsigned integers | Deterministic workload/input seeds |
| `warmup_runs` | unsigned integer, `>= 0` | Unrecorded runs per seed |
| `repetitions` | unsigned integer, `>= 1` | Recorded paired trials per seed |
| `worker_count` | unsigned integer, `>= 1` | CPU worker-pool capacity |
| `cases` | non-empty array | Distinct logical workloads |

Each case contains `case_id` (non-empty string), `workload`,
`reference_variant` (the ID used for paired comparisons), and a non-empty
`variants` array with unique IDs including the reference.

Workload fields:

| Field | Type and allowed values | Meaning |
|---|---|---|
| `cpu.task_count` | unsigned integer | Number of CPU tasks |
| `cpu.iterations` | unsigned integer; positive when CPU tasks exist | Deterministic CPU kernel work per task |
| `gpu.task_count` | unsigned integer | Number of Vulkan tasks |
| `gpu.workgroups.x/y/z` | positive unsigned 32-bit integers | Logical dispatch dimensions |
| `dependencies.shape` | `independent`, `chain`, `layered`, or `random` | Generated DAG topology |
| `dependencies.layers` | positive integer, only for `layered` | Layer count |
| `dependencies.edge_probability` | number in `[0,1]`, only for `random` | Candidate edge probability |
| `priorities.assignment` | `cycle` or `random` | Priority assignment method |
| `priorities.values` | non-empty array of unsigned 32-bit integers | Available stable priorities; smaller values run first |
| `bursts.count` | integer from `1` through total task count | Dependency-driven activation groups |

At least one CPU or GPU task is required and task-count addition must not
overflow.

Variant fields:

- `variant_id`: non-empty unique string.
- `execution`: `direct` or `scheduled`. Direct variants accept no policy or
  slicing fields.
- `policy`: required for scheduled variants. `type` is `fifo`,
  `static_priority`, or `round_robin`; round-robin alone requires a positive
  `quantum`.
- `slice_workgroups`: required for scheduled variants; either `null` for one
  ordinary dispatch per GPU task or positive `x/y/z` dimensions for cooperative
  slicing.

Optional environment JSON uses `schema_version: 1` and a non-empty
`environment_id`. It may also contain `cpu_model`, `physical_memory_bytes`,
`os_version`, `gpu_driver`, `power_profile`, and `notes`.

## Expected output

A successful `atlas_bench` run prints the number of recorded runs and writes:

- `suite.resolved.json` and `environment.resolved.json`;
- `runs.jsonl` and `runs.csv` with scheduler status, timings, utilization,
  response, fairness, Vulkan device metadata, timestamp capability, and both
  host-side and device-side GPU busy fractions;
- `tasks.csv` with per-task resource, priority, state, execution, ready-wait,
  response, bypass, work-unit, and optional device-execution measurements;
- `comparisons.json` and `comparisons.csv` with absolute metrics and paired
  confidence comparisons.

Warmups are not written. Output paths must not already exist unless
`--overwrite` is supplied. Timing varies by device and system load; smoke tests
check contracts and successful execution, not elapsed-time thresholds.
Run and summary outputs use schema version 2. GPU device duration is null when
the selected compute queue reports no timestamp bits or profiling was compiled
out. `gpu_timestamp_supported` states the capability and
`gpu_timestamp_busy_fraction` is present only when every GPU task in the run
has device timing. Host execution duration remains distinct from queue/device
execution duration. Atlas retains host measurements internally at nanosecond
resolution and converts them explicitly for the result contract's `_us`
fields.

## Evaluation conclusions

Milestone 13 ran the full canonical suite on a physical Intel Iris Xe device
and Mesa Lavapipe: 4,300 measured trials completed successfully in each
environment. The research questions, fixed 5% materiality threshold, 95%
confidence rules, environment descriptions, results, and adaptive-scheduling
no-go decision are recorded in
[Milestone 13 baseline evaluation](milestone-13-evaluation.md).

Every stable material same-slice quantum comparison favored quantum 1. Static
priority materially worsened p95 response while its fairness changes remained
below the practical threshold or inconclusive.

The final Milestone 16 rerun produced another 4,300 successful trials in each
environment. It confirms FIFO and quantum-one round-robin as defensible
defaults, finds no general case for static priority, and measures substantial
fine-slicing cost. Two narrow quantum effects replicate, but they do not define
a portable online decision rule. Atlas therefore does not implement an
adaptive scheduling policy. Methods, limitations, checked tables and plots,
and immutable raw-data links are in the
[Milestone 16 final evaluation](milestone-16-evaluation.md).

## Final evaluation tooling

Milestone 16 keeps benchmark collection and research analysis separate. The
version-one study at `benchmarks/evaluation/final-v1/study.json` predeclares the
canonical suite digest, environments, research contrasts, 95% confidence
method, and 5% practical-effect threshold. The benchmark's existing suite v1
and run/summary v2 formats are unchanged.

From a clean Git checkout, collect one complete environment with an explicitly
selected installed ICD:

```bash
python3 tools/atlas_evaluation.py run \
  --study benchmarks/evaluation/final-v1/study.json \
  --environment-file benchmarks/environments/milestone-16-intel-xe.json \
  --icd /absolute/path/to/intel_icd.json \
  --output-dir build/final-intel
```

The command performs a clean release configure/build, runs all tests and the
`atlas` example, executes the canonical suite, captures allowlisted provenance,
and creates a compressed raw-data bundle. It does not delete or overwrite an
existing output directory. Repeat with the Lavapipe environment and analyze
the two `results` directories with `atlas_evaluation.py analyze`. The analysis
emits version-one JSON, CSV, Markdown tables, and dependency-free SVG plots.
Published-bundle verification accepts only bounded HTTPS release assets from
the Atlas GitHub release hosts and rejects archives with links, special files,
path traversal, duplicate paths, or excessive expansion.

Python 3.10 or newer is required only for evaluation and trace tooling. If
`vulkaninfo` is installed, its summary is captured; otherwise the bundle records
that it was unavailable while retaining the Vulkan metadata resolved by Atlas.
See `benchmarks/evaluation/README.md` for the complete bundle layout and
publication verification command.

## Coverage, fuzzing, and security validation

The coverage preset instruments Atlas and its tests with gcov-compatible
flags:

```bash
cmake --preset coverage-linux
cmake --build --preset coverage-linux --parallel
ctest --preset coverage-linux
```

The Clang fuzz preset builds bounded ASan/UBSan libFuzzer harnesses. For
example:

```bash
cmake --preset fuzz-linux
cmake --build --preset fuzz-linux --parallel
./build/fuzz-linux/tests/fuzz/TaskGraph_fuzz -runs=50000 -max_len=4096
```

CI captures project coverage, and the security workflow runs CodeQL plus the
bounded graph fuzz regression. Benchmark provenance appends `-dirty` to the
embedded Git revision whenever tracked or untracked source-tree changes are
present at configure time.

The published study can be downloaded, hash-checked, structurally validated,
and reanalyzed in one command:

```bash
python3 tools/atlas_evaluation.py verify \
  --study benchmarks/evaluation/final-v1/study.json \
  --artifact-index benchmarks/evaluation/final-v1/artifacts.json \
  --output-dir build/final-verification
```

## Common failures

- **Vulkan unavailable or incompatible:** install a Vulkan 1.1 loader and a
  compute-capable driver. On headless Linux, Mesa Lavapipe is suitable. Use
  `VK_DRIVER_FILES` only to select an installed ICD; do not point Atlas at a
  non-existent or incompatible manifest.
- **No suitable device/queue/feature:** use a device with compute support and
  the Vulkan capabilities reported by the startup error.
- **Device lost during execution:** the affected runtime context is permanently
  unavailable. Destroy the runtime and its resources, diagnose the driver or
  workload failure, and start a new process or explicitly construct a new
  runtime; Atlas performs no automatic recovery.
- **Validation layer unavailable:** install the Khronos validation layer when
  running validation-enabled tests.
- **Invalid suite:** correct the exact missing, unknown, out-of-range, or
  inconsistent JSON field named in the error.
- **Unsupported workload or parameters:** use only the policies, dependency
  shapes, dimensions, and execution modes documented above.
- **Existing output:** choose an empty directory or explicitly pass
  `--overwrite`.
