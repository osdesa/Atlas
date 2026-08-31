# Milestone 10: reproducible benchmarking framework

## Implementation status

Milestone 10 is implemented. Atlas provides an opt-in `atlas_bench` runner that
rebuilds a fresh graph for every warmup and measured repetition, executes
deterministic generated CPU/Vulkan workloads, and exports versioned JSON Lines
and normalized CSV results. The framework measures scheduler and task behavior;
it does not select a universally best policy or enforce performance thresholds.

## Build and invocation

CPU benchmark builds are available on Windows and Linux. The Linux benchmark
preset additionally compiles and validates the benchmark shader and enables
real Vulkan workloads:

```bash
cmake --preset benchmark-linux
cmake --build --preset benchmark-linux --parallel
ctest --preset benchmark-linux

./build/benchmark-linux/apps/atlas_bench/atlas_bench \
  --manifest benchmarks/manifests/mixed-sliced-smoke-v1.json \
  --output-dir build/benchmark-linux/results
```

Windows uses `benchmark-windows` and builds CPU workloads by default. A CPU-only
build rejects manifests requesting GPU tasks with a clear error. Use
`--validate-only` to parse without executing and `--overwrite` to replace only
the four known files in an existing result directory.

`ATLAS_BUILD_BENCHMARKS` controls the target and defaults off.
`ATLAS_BENCHMARK_ENABLE_VULKAN` defaults off and requires the benchmark option,
`glslc`, and `spirv-val`. The JSON parser is nlohmann/json 3.12.0 fetched from a
release archive with a pinned SHA-256 and linked only to benchmark targets.

## Manifest version 1

Every manifest contains `schema_version: 1`, a non-empty experiment ID, one or
more seeds, warmup and measured repetition counts, a positive worker count, one
built-in scheduling policy, and a generated workload. Unknown fields and
unsupported versions are rejected.

The workload controls:

- CPU task count and deterministic integer-kernel iterations;
- GPU task count, logical 3D workgroups, and optional 3D slice geometry;
- independent, chain, layered, or seeded random acyclic dependencies;
- cyclic or seeded-random assignment from explicit static priority values; and
- a positive number of dependency-driven readiness bursts.

Tasks are shuffled reproducibly by resource, then partitioned into contiguous
bursts. Dependency shapes are applied inside each burst. Roots of each later
burst depend on the first task in the preceding burst, so arrival remains an
already-declared `Blocked -> Ready` transition. No graph is mutated during
execution.

The checked schemas are under `benchmarks/schema/`, and smoke manifests are
under `benchmarks/manifests/`. A seed describes one topology, resource order,
priority assignment, and input dataset. Warmups and repetitions for that seed
reconstruct the same workload with a new graph identity.

## Measurement semantics

`TaskExecutionInfo::responseDuration` begins at the first scheduler-observed
ready entry and ends at logical success, task failure, or effective
cancellation. It includes ready wait, executor queueing, payload execution, and
sliced resumptions, but excludes dependency-blocked time before first readiness.
Tasks that never become ready or terminal retain an empty value.

`SchedulerResult::schedulerActiveDuration` accumulates host time spent parsing,
selecting, applying completions, releasing dependencies, and updating lifecycle
state. Executor submission calls and blocking completion waits are excluded.
It is a scalar control-work measurement, can overlap backend execution, and
must not be subtracted from graph completion time.

Immediate slice-switch duration is sampled only when an incomplete GPU task is
the next GPU task selected. An intervening GPU selection invalidates that
sample. A single sliced GPU task therefore provides the cleanest slice-switch
measurement.

Per-run output includes:

- graph completion time and successful-task throughput;
- per-task execution, ready-wait, response, bypass, and work-unit values;
- response and ready-wait mean, nearest-rank p50/p95, and maximum;
- scheduler-active time and makespan fraction;
- immediate slice-switch count, total, and mean;
- CPU busy fraction as summed CPU execution divided by worker capacity-time;
- capacity-one host-observed GPU busy fraction;
- per-resource Jain fairness over `executionDuration / responseDuration`; and
- explicit null GPU timestamp utilization with a false support flag.

Zero or unavailable denominators produce JSON `null` and an empty CSV cell.
GPU host duration still includes command preparation, queue submission, and
fence waiting; it is not device timestamp time.

## Output contract

Each successful invocation creates:

- `manifest.resolved.json`, with all defaults and policy-specific fields made
  explicit;
- `runs.jsonl`, containing one nested schema-version-one object per measured
  repetition;
- `runs.csv`, containing one flat graph-level row per repetition; and
- `tasks.csv`, containing one row per task and repetition.

Warmups are validated but not exported. A scheduler failure in a measured run
is exported with partial task state before the process returns failure. Setup,
graph generation, buffer reset/download, correctness validation, and result
serialization occur outside `SchedulerResult::executionTime`.

## Deferred scope

Milestone 10 does not add direct CPU/Vulkan baselines, confidence intervals,
plots, stored evaluation results, adaptive policy selection, performance gates,
general event tracing, or Vulkan timestamp queries. Baselines belong to
Milestone 11; event streams, timestamp-query utilization, and visualisation
remain Milestone 12 work.
