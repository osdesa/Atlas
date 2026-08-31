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
Lines/CSV output, environment metadata, and paired confidence intervals.
Graphics, presentation, multiple Vulkan queues, runtime graph mutation,
repeated graph execution, and true CPU/GPU preemption are not implemented.

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

## Running Atlas

### `atlas`

`atlas` runs the current representative workflow: a CPU preparation task,
cooperatively sliced Vulkan vector addition, and a dependent CPU verification
task.

```bash
./build/apps/atlas/atlas
```

It accepts no command-line arguments. Success prints the number of completed
Vulkan work units and the selected device name. Any graph, scheduling, Vulkan,
or verification failure returns a non-zero exit status and prints an error.

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
| `--help` | Optional | Flag | Prints usage and exits | `--help` |

Unknown, repeated, or inconsistent arguments are errors. Suites are the only
supported benchmark input.

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
  response, fairness, and Vulkan device metadata;
- `tasks.csv` with per-task resource, priority, state, execution, ready-wait,
  response, bypass, and work-unit measurements;
- `comparisons.json` and `comparisons.csv` with absolute metrics and paired
  confidence comparisons.

Warmups are not written. Output paths must not already exist unless
`--overwrite` is supplied. Timing varies by device and system load; smoke tests
check contracts and successful execution, not elapsed-time thresholds.

## Common failures

- **Vulkan unavailable or incompatible:** install a Vulkan 1.1 loader and a
  compute-capable driver. On headless Linux, Mesa Lavapipe is suitable. Use
  `VK_DRIVER_FILES` only to select an installed ICD; do not point Atlas at a
  non-existent or incompatible manifest.
- **No suitable device/queue/feature:** use a device with compute support and
  the Vulkan capabilities reported by the startup error.
- **Validation layer unavailable:** install the Khronos validation layer when
  running validation-enabled tests.
- **Invalid suite:** correct the exact missing, unknown, out-of-range, or
  inconsistent JSON field named in the error.
- **Unsupported workload or parameters:** use only the policies, dependency
  shapes, dimensions, and execution modes documented above.
- **Existing output:** choose an empty directory or explicitly pass
  `--overwrite`.
