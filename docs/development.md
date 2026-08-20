# Development guide

## Repository layout

The repository is deliberately small while the core task model and concurrent
CPU task-graph executor are being developed:

- `include/atlas/`: public Atlas headers
- `src/`: compiled library implementation
- `apps/atlas_cli/`: example task-graph executable using the synchronous backend
- `tests/`: Catch2/CTest unit and feature tests
- `cmake/`: target-scoped warnings, sanitizers, and static-analysis helpers
- `.github/workflows/`: Windows and Linux continuous integration

Directories for future runtime components should be added only when their
implementations begin.

## Targets

- `atlas` is a compiled static library. `Atlas::Atlas` is its namespaced alias.
- `atlas_cli` links to `Atlas::Atlas` and executes an example frame task graph.
- `atlas_unit_tests` and `atlas_feature_tests` link to `Atlas::Atlas` and Catch2.
  `catch_discover_tests` registers their test cases with CTest.

The Vulkan SDK is discovered at configure time with `find_package(Vulkan
REQUIRED)`. `atlas` links to `Vulkan::Vulkan`, but no Vulkan API is called at
this stage.

The executor module defines a backend-neutral CPU submission/completion contract,
a `SynchronousCpuExecutor`, and a fixed-size `WorkerpoolExecutor`. The synchronous
implementation executes on the submitting thread. The worker-pool implementation
owns a FIFO work queue, runs independent callables concurrently, captures every
callable exception, and drains accepted work before joining during shutdown.
Both implementations return task-attributed completions containing callable
duration and any captured exception. `KahnScheduler` remains backend-neutral:
capacity one preserves synchronous FIFO behavior, while a worker pool keeps
independent ready tasks in flight up to its worker count.

`KahnScheduler` borrows a `CpuExecutor`; the caller owns that executor, keeps it
alive, and remains responsible for shutdown. The executor must be initially
drained and exclusively available to the scheduler during `execute()`. The
scheduler marks selected work `Running`, tracks accepted handles, and applies
completions to `TaskExecutionInfo` on its control thread. A rejected submission
restores the task to `Ready`. Missing, unknown, duplicate, or mismatched
completions report `ExecutorUnavailable` and never release dependants.

`WorkerpoolExecutor` requires a non-zero fixed worker count. Public executor
calls must be serialized, although accepted callables run concurrently. Its FIFO
work queue is protected by a mutex and condition variable; a separate protected
completion queue returns results in completion order. Shutdown rejects new work,
drains queued and running work, retains produced completions, joins every worker,
and is idempotent. A callable must not invoke lifecycle operations on its own
executor.

## Current task and execution model

`TaskGraph` owns task definitions and exposes `shared_ptr<const Task>` views.
Identity, callable work, and options are immutable. `TaskExecutionInfo` groups
the logically mutable lifecycle state, captured exception, and callable duration
so the scheduler control thread can update them through that otherwise read-only
view. Retaining a returned `shared_ptr` extends the task object's lifetime beyond
the graph, so callers must not infer a strict non-owning lifetime from the
graph-owned description.

Task options currently contain an optional name, a static `uint32_t` priority,
and CPU/GPU execution-resource intent. Lower numeric priorities represent higher
priority, but the FIFO Kahn scheduler does not yet use priority or resource intent
when selecting or dispatching work. Both CPU- and GPU-designated tasks are sent
to the supplied CPU executor; the CLI's synchronous executor runs their host
callables on the calling thread.

Tasks are `Unknown` during graph construction. Successful finalisation assigns
`Ready` to roots and `Blocked` to tasks with dependencies. `KahnScheduler` owns
subsequent state changes and does not call a shared transition validator:

```text
Unknown --successful graph finalisation--> Ready or Blocked
Blocked --final dependency succeeds------> Ready
Ready   --selected by scheduler-----------> Running
Running --callable returns----------------> Success
Running --callable throws-----------------> Failure
Running --missing/mismatched completion---> Failure (ExecutorUnavailable)
```

`Success` and `Failure` are terminal for the current scheduler. The task's
execution information retains the captured exception when applicable and the
time spent executing its callable. A queued task not marked `Ready` is skipped;
if this prevents every graph task from completing, execution is reported as
`InvalidGraph`. Atlas does not currently define a cancellation state or
cancellation API.

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
