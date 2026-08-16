# Development guide

## Repository layout

The repository is deliberately small while the core task model and sequential
task-graph executor are being developed:

- `include/atlas/`: public Atlas headers
- `src/`: compiled library implementation
- `apps/atlas_cli/`: example sequential task-graph executable
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

## Current task and execution model

`TaskGraph` owns task definitions and exposes `shared_ptr<const Task>` views.
Identity, callable work, and options are immutable. `TaskExecutionInfo` groups
the logically mutable lifecycle state, captured exception, and callable duration
so the sequential scheduler can update them through that otherwise read-only
view. Retaining a returned `shared_ptr` extends the task object's lifetime beyond
the graph, so callers must not infer a strict non-owning lifetime from the
graph-owned description.

Task options currently contain an optional name, a static `uint32_t` priority,
and CPU/GPU execution-resource intent. Lower numeric priorities represent higher
priority, but the FIFO Kahn scheduler does not yet use priority or resource intent
when selecting or dispatching work. Both CPU- and GPU-designated tasks currently
execute their host callable synchronously on the calling thread.

Tasks are `Unknown` during graph construction. Successful finalisation assigns
`Ready` to roots and `Blocked` to tasks with dependencies. `KahnScheduler` owns
subsequent state changes and does not call a shared transition validator:

```text
Unknown --successful graph finalisation--> Ready or Blocked
Blocked --final dependency succeeds------> Ready
Ready   --selected by scheduler-----------> Running
Running --callable returns----------------> Success
Running --callable throws-----------------> Failure
```

`Success` and `Failure` are terminal for the current scheduler. The task's
execution information retains the captured exception when applicable and the
time spent executing its callable. A queued task not marked `Ready` is skipped;
if this prevents every graph task from completing, execution is reported as
`InvalidGraph`. Atlas does not currently define a cancellation state or
cancellation API.

The current model is intentionally single-threaded and intended for one execution
of a graph. Execution-information fields are not atomic, concurrent mutation is
not supported, and a completed task is not executed again. These restrictions
must be revisited when a worker executor and scheduler service are introduced.

## Presets

All checked-in configurations build below `build/<preset>`. They inherit a
shared quality configuration that enables warnings as errors, sanitizers, and
Clang-Tidy; Windows presets disable the unsupported sanitizer configuration.

| Preset | Generator | Configuration | Intended use |
| --- | --- | --- | --- |
| `dev` | Ninja | Debug | Cross-platform local development |
| `dev-windows` | Locally available Visual Studio | Debug | Windows/MSVC development |
| `dev-linux` | Ninja | Debug | Linux development |
| `ci-windows` | Locally available Visual Studio | Release | Windows CI, warnings as errors |
| `ci-linux` | Ninja | Release | Linux CI, warnings as errors |

Each configure preset has a build and test preset with the same name:

```bash
cmake --preset dev-linux
cmake --build --preset dev-linux
ctest --preset dev-linux
```

Use `cmake --list-presets=all` to inspect presets available on the current
platform.

## Sanitizers

AddressSanitizer and UndefinedBehaviorSanitizer are enabled by the checked-in
Linux presets and are available with compatible non-Windows GCC and Clang
toolchains:

```bash
cmake --preset dev-linux
cmake --build --preset dev-linux
ctest --preset dev-linux
```

Sanitizer flags are attached only to Atlas-owned targets. Unsupported toolchains
produce a configure-time warning and otherwise remain buildable.

## Clang-Tidy

Clang-Tidy is requested by the checked-in presets and can also be enabled for a
custom configuration:

```bash
cmake --preset dev
cmake --build --preset dev
```

If `clang-tidy` is unavailable, configuration reports a warning and continues
without analysis. The checked-in `.clang-tidy` file selects focused correctness,
modernisation, performance, and readability checks.

## Pre-commit formatting

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
