# Development guide

## Repository layout

The repository separates tasking, scheduling, CPU execution, and Vulkan compute:

- `include/atlas/`: public Atlas headers
- `src/`: compiled library implementation
- `apps/`: CPU, standalone Vulkan, and mixed CPU/Vulkan examples
- `tests/`: Catch2/CTest unit and feature tests
- `cmake/`: target-scoped warnings, sanitizers, and static-analysis helpers
- `.github/workflows/`: Windows and Linux continuous integration

## Targets

- `atlas` is a compiled static library. `Atlas::Atlas` is its namespaced alias.
- `atlas_cli` runs the same 17-task pipeline through both CPU executors.
- `atlas_vulkan_example` and `atlas_mixed_example` are built by the Vulkan
  integration preset and verify standalone and graph-integrated compute.
- `atlas_all_example` runs `atlas_cli`, `atlas_vulkan_example`, and
  `atlas_mixed_example` in sequence, stopping when one fails.
- `atlas_unit_tests` and `atlas_feature_tests` link to `Atlas::Atlas` and Catch2.
  `catch_discover_tests` registers their test cases with CTest.

The Vulkan SDK is discovered with `find_package(Vulkan REQUIRED)` and is a
public link dependency because `VulkanError` exposes `VkResult`. Integration
builds additionally require `glslc` and `spirv-val`; normal Windows and Linux
builds remain device-independent.

The executor module defines a backend-neutral CPU submission/completion contract,
a `SynchronousCpuExecutor`, and a fixed-size `WorkerpoolExecutor`. The synchronous
implementation executes on the submitting thread. The worker-pool implementation
owns a FIFO work queue, runs independent callables concurrently, captures every
callable exception, and drains accepted work before joining during shutdown.
Both implementations return task-attributed completions containing callable
duration and any captured exception. `KahnScheduler` remains backend-neutral:
capacity one preserves synchronous FIFO behavior, while a worker pool keeps
independent ready tasks in flight up to its worker count.

`VulkanRuntime` owns a Vulkan 1.1 compute-only instance, device, queue, and command pool.
It creates opaque persistent device-local buffers and compute pipelines;
blocking uploads/downloads use internal staging allocations. `VulkanDispatch`
contains a pipeline, exact storage-buffer bindings with access declarations,
and non-zero workgroup dimensions. Pipelines opt into dispatch-base execution.
`SlicedVulkanDispatch` retains one logical dispatch and divides it into maximum
X/Y/Z extents, enumerated with X advancing fastest and partial edge units where
needed. Invalid SPIR-V, binding sets, runtime identity, ranges, slice geometry,
or device limits are rejected before queue submission.

`VulkanExecutor` owns one worker and reports capacity one. It drains accepted
dispatches on shutdown and returns the same task-attributed completion shape as
CPU executors. Vulkan API failures after acceptance are captured as dispatch
exceptions rather than escaping the worker.

The CPU-only `KahnScheduler` constructor preserves the original completion path.
The mixed constructor borrows both executors, maintains separate stable ready
sets and in-flight counts, and consumes CPU/GPU outcomes through one
preallocated `CompletionChannel`. Existing constructors install FIFO. Policy-
aware overloads clone one backend-neutral `SchedulingPolicy` independently for
CPU and GPU. A completion must match its handle, resource, running state, and
exact work-unit index before it can update progress. An incomplete sliced task
becomes `Paused` and returns to the GPU ready-set tail. Dependencies are released
only after its final unit succeeds.

`WorkerpoolExecutor` requires a non-zero fixed worker count. Public executor
calls must be serialized, although accepted callables run concurrently. Its FIFO
work queue is protected by a mutex and condition variable; a separate protected
completion queue returns results in completion order. Shutdown rejects new work,
drains queued and running work, retains produced completions, joins every worker,
and is idempotent. A callable must not invoke lifecycle operations on its own
executor.

## Current task and execution model

`TaskGraph` owns task definitions and exposes `shared_ptr<const Task>` views.
Identity, variant-backed CPU/ordinary Vulkan/sliced Vulkan work, and options are
immutable. `TaskExecutionInfo` groups the logically mutable lifecycle state,
captured exception, accumulated payload and ready-wait durations, selection
bypass count, and completed/total work-unit counts so the scheduler control
thread can update them through that otherwise read-only view. Retaining a
returned `shared_ptr` extends the task object's lifetime beyond the graph, so
callers must not infer a strict non-owning lifetime from the graph-owned
description.

`addCpuTask()` and `addGpuTask()` make the work type authoritative and reject
resource metadata disagreement. `addTask()` remains a CPU compatibility alias.
Lower numeric priorities represent higher priority. `FifoSchedulingPolicy`
selects the oldest candidate. `RoundRobinSchedulingPolicy` retains an incomplete
logical task for at most its configured work-unit quantum; quantum one matches
FIFO rotation. `StaticPrioritySchedulingPolicy` selects the lowest numeric value
and preserves FIFO ties. Policies receive only task handles, priorities, and
stable enqueue order. They never receive task payloads, executor objects, or
Vulkan types.

`KahnScheduler` delegates ready-set measurement to a private accounting helper.
The first interval starts when scheduler parsing enqueues a root, not when graph
finalisation first marks it `Ready`. Dependency completion and incomplete slice
resumption start later intervals. Selection closes the chosen interval and
increments every other valid same-resource candidate still ready or paused.
Cancellation closes the affected interval, and scheduler termination closes all
intervals left open by fail-stop behavior. Ready-wait excludes blocked time,
executor queueing, and payload execution. Bypass counts expose strict-priority
starvation without changing immutable priorities or adding aging.

Tasks are `Unknown` during graph construction. Successful finalisation assigns
`Ready` to roots and `Blocked` to tasks with dependencies. `KahnScheduler` owns
subsequent state changes and does not call a shared transition validator:

```text
Unknown --successful graph finalisation--> Ready or Blocked
Blocked --final dependency succeeds------> Ready
Ready   --selected by scheduler-----------> Running
Running --backend work succeeds----------> Success
Running --backend work fails-------------> Failure
Running --missing/mismatched completion---> Failure (ExecutorUnavailable)
Running --sliced unit succeeds, units remain--> Paused
Paused  --selected from GPU ready set-----> Running
Ready/Blocked/Paused --effective request--> Cancelled
```

`Success`, `Failure`, and `Cancelled` are terminal for the current scheduler.
`KahnScheduler::requestCancellation()` accepts a graph-owned, non-terminal task
before or concurrently with execution. Selection and request handling use one
mutex-protected claim boundary. A running CPU or ordinary GPU task completes
normally; a sliced GPU request becomes effective only after a successful unit
when more units remain. Effective cancellation stops new submissions and drains
accepted work without releasing dependants.

The result precedence is `ExecutorUnavailable`, then `PolicyError`, then
`TaskFailed`, `Cancelled`, `Success`, and `InvalidGraph`. A thrown policy or an
invalid returned index stops new submissions and drains accepted work. The first
task exception remains authoritative; otherwise `SchedulerResult::exception`
contains the policy exception. `executedTaskCount` counts logical successes,
never individual slices. Duration accumulates executor-reported payload time for
every attempted slice, including a failed slice; executor queue waiting remains
excluded. Ready-wait duration is a separate scheduler measurement and may span
multiple ready intervals for a sliced task.

Scheduler control remains single-threaded and a graph is intended for one
execution, but worker callables may overlap. Execution-information fields are
not atomic; callers must not inspect or mutate them while `execute()` is active.
Captured references and other shared resources used by concurrent callables are
the application’s synchronization responsibility. Completed tasks are not run
again.

## Presets

All checked-in configurations build below `build/<preset>`. Warnings are enabled
for Atlas targets, while ordinary CI, analysis, ASan/UBSan, and TSan remain
independent configurations.

| Preset | Generator | Configuration | Intended use |
| --- | --- | --- | --- |
| `dev` | Ninja | Debug | Cross-platform local development |
| `dev-windows` | Locally available Visual Studio | Debug | Windows/MSVC development |
| `dev-linux` | Ninja | Debug | Linux development |
| `ci-windows` | Locally available Visual Studio | Release | Windows CI with MSVC level-four warnings |
| `ci-linux` | Ninja | Release | Linux CI, warnings as errors |
| `analysis-linux` | Ninja/Clang | Debug | Required Clang-Tidy analysis |
| `asan-ubsan-linux` | Ninja/Clang | Debug | Full ASan/UBSan suite |
| `tsan-linux` | Ninja/Clang | Debug | Labelled concurrency suite under TSan |
| `vulkan-integration-linux` | Ninja/GCC | Debug | GLSL/SPIR-V build and real compute through Lavapipe |

Each configure preset has a build and test preset with the same name:

```bash
cmake --preset dev-linux
cmake --build --preset dev-linux
ctest --preset dev-linux
```

Use `cmake --list-presets=all` to inspect presets available on the current
platform.

## Sanitizers

AddressSanitizer and UndefinedBehaviorSanitizer use a dedicated Clang preset:

```bash
cmake --preset asan-ubsan-linux
cmake --build --preset asan-ubsan-linux
ctest --preset asan-ubsan-linux
```

ThreadSanitizer is separate because its runtime cannot be combined with
ASan/UBSan:

```bash
cmake --preset tsan-linux
cmake --build --preset tsan-linux
ctest --preset tsan-linux --repeat until-fail:5
```

Sanitizer flags are attached only to Atlas-owned targets. Requesting an
unsupported sanitizer configuration is an error. LeakSanitizer cannot operate
under `ptrace`; use `LSAN_OPTIONS=detect_leaks=0` only for that local environment.

## Clang-Tidy

The analysis preset requires Clang-Tidy and fails rather than silently skipping
analysis:

```bash
cmake --preset analysis-linux
cmake --build --preset analysis-linux
```

Developer presets may still request optional analysis. The checked-in
`.clang-tidy` file selects focused correctness, modernisation, performance, and
readability checks.

## Pre-commit formatting

The checked-in VS Code workspace settings use the recommended clangd extension
to run ClangFormat whenever a C or C++ file is saved. Formatting follows the
repository's `.clang-format` configuration.

The versioned `.githooks/pre-commit` hook formats staged C and C++ files with
the repository's `.clang-format` configuration. Enable it after cloning with:

```bash
git config core.hooksPath .githooks
```

The hook formats and re-stages files that have no unstaged edits. It stops when
a staged file also has unstaged changes, preventing it from staging unrelated
work.

## Adding sources and future modules

Keep build configuration target-based:

1. Add implementation and public headers only for the concrete module being
   developed.
2. Existing Atlas library and test targets discover files recursively with
   `file(GLOB_RECURSE ... CONFIGURE_DEPENDS)`, sort the resulting directory-local
   list, and attach it with `target_sources`. Place a new file under the matching
   `src`, `include/atlas`, `tests/unit`, or `tests/feature` tree; do not also list
   it manually. A genuinely new target should own its source discovery in its
   nearest `CMakeLists.txt` rather than sharing a repository-wide aggregate list.
3. Express includes, compile features, definitions, and dependencies with the
   relevant `target_*` command and the narrowest appropriate visibility.
4. Apply `atlas_set_project_warnings`, `atlas_enable_sanitizers`, and
   `atlas_enable_clang_tidy` only to new Atlas-owned targets.
5. Add focused tests to the appropriate unit or feature suite, or create a new
   test target only when its separate lifecycle justifies it.

Do not add global include directories or compiler flags. External dependencies
must remain isolated from Atlas warning and analysis settings.
