# Atlas agent guide

This file applies to the entire repository. Read `README.md`,
`docs/user-guide.md`, `docs/development.md`, and `docs/task-lifecycle.md` before
changing public behavior or architecture.

## Current project

Atlas is a C++20 heterogeneous CPU/Vulkan task-graph prototype. Vulkan is a
mandatory, first-class dependency. Atlas builds directed acyclic task graphs,
executes CPU callables with synchronous or worker-pool executors, executes
declarative Vulkan compute dispatches, and schedules mixed graphs with FIFO,
work-unit round-robin, or stable static-priority policies. `atlas_bench` runs
version-one comparison suites and writes JSON Lines/CSV measurements and paired
confidence summaries.
Atlas can also emit bounded version-one JSONL execution traces from `atlas`,
and benchmark schema version two records capability-checked Vulkan timestamp
measurements when the selected compute queue supports them.
Milestone 13 canonical physical Intel and Lavapipe baselines found no stable
material trade-off or workload-dependent fixed quantum optimum. Adaptive
scheduling is therefore skipped. Milestone 15 adds deterministic generated-DAG
and large-graph validation, fault matrices, sanitizer/soak workflows, and
fail-stop Vulkan device-loss handling. Milestone 16 completes an 8,600-run
physical Intel and Lavapipe final evaluation. Milestone 17 adds the optional
PySide6 desktop studio and its strict built-in-kernel graph runner.
The Atlas library also exposes the Part A trusted native task-pack API: packs
are inspected and content-hashed before explicit loading, CPU callbacks and
declarative storage-buffer GPU tasks become existing graph payloads, and
summaries are bounded and typed. Runner and Studio pack integration is not yet
implemented.

The supported executables are:

- `atlas`: a verified CPU -> sliced Vulkan -> CPU example;
- `atlas_bench`: the suite-based benchmark harness;
- `atlas_studio_runner`: the JSONL process boundary used by the optional
  `python -m atlas_studio` desktop application.

## Compatibility and scope

Atlas is under active development and provides no backwards-compatibility
guarantees. Do not preserve deprecated APIs, configuration formats, command-line
arguments, benchmark formats, build flags, or internal abstractions solely for
compatibility with earlier revisions. When architecture changes, update current
callers and delete obsolete behavior instead of adding aliases, wrappers,
fallback paths, migration shims, or transitional overloads.

Prefer deleting obsolete functionality over retaining code that may be useful
later. Do not add speculative abstractions for future backends or features
unless the current milestone requires them.

Vulkan is mandatory. Do not add optional Vulkan builds, CPU-only application
modes, backend selection flags, or silent runtime fallbacks. Configuration must
fail when Vulkan development dependencies are absent. Executables must fail
early with a useful error when the loader, device, queue, feature, extension, or
device creation required by Atlas is unavailable.

The following remain outside the current scope unless requirements explicitly
change: repeated/runtime graph submission, graphics or presentation, multiple
Vulkan queues, true dispatch preemption, adaptive backend or policy selection,
lossless general event tracing, and allocator/pipeline-cache tuning.

## First actions

1. Run `git status --short` and inspect relevant diffs. Preserve unrelated user
   changes and never reset the worktree.
2. Use `rg` and `rg --files` for searches.
3. Read the public header, implementation, callers, and tests together before
   changing a contract.
4. Do not edit generated files below `build/`.

## Build and validation

Atlas requires CMake 3.24+, C++20, Vulkan 1.1 development files and a usable
compute device, `glslc`, `spirv-val`, Threads, and Ninja for Linux presets. The
normal workflow is:

```bash
cmake -S . -B build
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Linux development and CI presets are also supported:

```bash
cmake --preset dev-linux
cmake --build --preset dev-linux --parallel
ctest --preset dev-linux

cmake --preset ci-linux
cmake --build --preset ci-linux --parallel
ctest --preset ci-linux
```

Use a real Vulkan implementation. CI pins Mesa Lavapipe with
`VK_DRIVER_FILES`; never hard-code a local ICD path in source or CMake.

Validation must match risk:

- task, executor, scheduler, or concurrency changes: normal suite plus relevant
  ASan/UBSan and repeated TSan tests;
- Vulkan changes: normal suite against a real Vulkan implementation and the
  `atlas` executable;
- benchmark changes: benchmark contract tests and
  `benchmarks/manifests/smoke-v1.json`;
- CMake/preset changes: clean configure and build for every affected path;
- documentation-only changes: build `docs-linux` and run `git diff --check`.

Before handoff, run formatting, `git diff --check`, and searches for removed
APIs/options and for `legacy|deprecated|compat|fallback`.

## Architecture invariants

- Task handles are graph-scoped. Cross-graph handles are never valid graph
  members or dependency endpoints.
- Each task contains exactly one CPU callable, `VulkanDispatch`, or
  `SlicedVulkanDispatch`, matching its execution resource.
- Graph structure is mutable only before successful finalisation and each graph
  is intended for one scheduler execution.
- Scheduler control is single-threaded. Executors publish exactly one
  task-attributed completion for every accepted submission; only the scheduler
  applies graph state and releases dependencies.
- CPU and Vulkan capacities are independent. Busy work on one resource must not
  block ready work on the other.
- Incomplete sliced dispatches retain progress, enter `Paused`, and return to
  the Vulkan ready-set tail. Atlas does not claim to preempt an active dispatch.
- Cancellation stops new submissions and drains accepted work. Failure is
  fail-stop: preserve the first task exception and map infrastructure and policy
  failures to their explicit scheduler statuses.
- `CompletionChannel` storage is preallocated; producer publication is
  thread-safe, allocation-free, and non-throwing.
- Executor shutdown rejects new work, drains accepted work, joins workers, and
  is idempotent.
- Raw Vulkan ownership handles remain private. Buffers and pipelines belong to
  one retained runtime context and cross-runtime use is rejected.
- Native task-pack modules are explicitly trusted, use only the versioned C ABI,
  and remain loaded while prepared callables, GPU work, or result callbacks
  depend on them. GPU packs never receive raw Vulkan handles.
- Every compute module is validated for Vulkan 1.1 and reflected before pipeline
  creation; declared, reflected, and dispatch buffer bindings/access must match.
- Queue wait time is not task execution duration.

## Code and tests

Use C++20, repository formatting, target-scoped CMake, explicit standard-library
ownership, and the narrowest correct dependency visibility. Public declarations
belong under `include/atlas/<Module>/`; implementations belong under matching
`src/<Module>/` paths. Tests use Catch2: deterministic device-independent tests
go in `tests/unit/`, real Vulkan behavior in `tests/feature/`, and benchmark
contracts in `tests/benchmark/`. Prefer synchronization primitives over sleeps.

Public and private interfaces require concise Doxygen coverage, including
ownership, lifetime, validation, exception, and concurrency contracts. Keep the
Tasking, Executor, Scheduling, and Vulkan topic groups and UML inputs aligned.

## Documentation rule

User-facing documentation is part of every behavior change. Update
`docs/user-guide.md` whenever build instructions, dependencies, executables,
CLI arguments, benchmark parameters, policies, workloads, output, limitations,
or runtime behavior changes.

Review and update the User Guide after every milestone. It must describe only
the current milestone and must not accumulate historical or compatibility
documentation. Update `README.md`, development documentation, Doxygen, and UML
configuration where the same change affects them.
