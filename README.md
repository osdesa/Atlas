# Atlas

Atlas is an early C++20 task-graph prototype and the foundation for a future
user-space heterogeneous CPU/Vulkan GPU scheduler.

The current implementation can:

- create tasks identified by graph-scoped handles;
- attach an optional name, static priority, and CPU/GPU execution-resource intent
  to each task;
- build and finalise directed acyclic task graphs;
- reject missing, duplicate, self, cross-graph, and cyclic dependencies;
- execute individual CPU callables through a standalone synchronous executor
  that reports task-attributed completion, exceptions, and duration;
- execute independent CPU callables concurrently through a fixed-size worker
  pool with failure isolation and draining shutdown;
- execute a finalised graph through either CPU executor with capacity-bounded
  Kahn dependency scheduling; and
- record per-task lifecycle state, exceptions, and execution duration alongside
  graph-level completion count, elapsed time, and exceptions.

The example CLI exercises that path with a six-task graph. It constructs a
`SynchronousCpuExecutor` and lends it to `KahnScheduler`, which submits selected
tasks and applies attributed completions to their execution information. Unit
and feature tests cover the current graph, scheduler, synchronous executor, and
worker-pool executor behaviour.

Atlas does **not** yet provide runtime task submission, interchangeable
scheduling policies, Vulkan initialisation or compute execution, GPU task
slicing, mixed CPU/GPU scheduling, or a benchmarking framework. Vulkan is
currently limited to SDK discovery and link validation.

## Task model and lifecycle

`TaskOptions` describes immutable-on-submission intent. An empty name is valid,
priority is an unsigned 32-bit value where a lower value means higher priority,
and `ExecutionResource` classifies work as CPU or GPU. The current Kahn scheduler
is FIFO and dispatches through a borrowed `CpuExecutor`; it does not yet use
priority for ordering or resource intent for backend dispatch. The CLI supplies
a `SynchronousCpuExecutor`, so its graph execution remains on the calling thread.
Supplying a `WorkerpoolExecutor` allows independent ready tasks to overlap up to
the configured worker count without changing scheduler code.

Tasks begin `Unknown` while their graph is being constructed. Successful graph
finalisation makes tasks without dependencies `Ready` and tasks waiting on
dependencies `Blocked`. `KahnScheduler` changes a selected task to `Running`,
then records `Success` or `Failure`, a captured exception when applicable, and
the callable's execution duration. Successful completion makes newly unblocked
dependants `Ready`.

Schedulers own state changes; `Task` does not validate a universal transition
matrix. `KahnScheduler::execute()` is a single control-thread operation even
when worker threads run callables concurrently, and a graph is intended for one
execution. Callers must not read or mutate execution information concurrently
with execution. Atlas does not currently expose cancellation. See
[Task lifecycle](docs/task-lifecycle.md) for the exact contract and limitations.

The intended GPU design is cooperative: a logical GPU task will be divided into
independently submitted execution slices, and scheduling control will return to
Atlas between slices. Atlas will not claim to interrupt a Vulkan dispatch that
is already executing. The accurate future description is **preemptive-style GPU
scheduling through cooperative execution slices**.

## Prerequisites

- CMake 3.24 or newer
- A C++20 compiler:
  - MSVC on Windows
  - GCC on Linux
  - Clang where supported by the platform toolchain
- Git, used by CMake while obtaining Catch2
- A Vulkan development environment:
  - Windows: install the [LunarG Vulkan SDK](https://vulkan.lunarg.com/sdk/home)
    and open a new terminal so `VULKAN_SDK` is available.
  - Linux: install the Vulkan loader development package, for example
    `libvulkan-dev` on Ubuntu.
- Ninja for the generic `dev` and Linux presets

The first test-enabled configure downloads Catch2 v3.8.1 through CMake
`FetchContent`.

## Build and test

### Windows

The Windows preset uses the newest Visual Studio generator known to the locally
installed CMake:

```powershell
cmake --preset dev-windows
cmake --build --preset dev-windows
ctest --preset dev-windows
```

The cross-platform `dev` preset is also available when Ninja and a configured
compiler environment are on `PATH`, such as from a Visual Studio Developer
PowerShell:

```powershell
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

### Linux

On Ubuntu, the prerequisites can be installed with:

```bash
sudo apt-get update
sudo apt-get install build-essential cmake ninja-build libvulkan-dev
```

Then configure, build, and test:

```bash
cmake --preset dev-linux
cmake --build --preset dev-linux
ctest --preset dev-linux
```

The generic `dev` preset uses the same Ninja-based development settings and is
also suitable on Linux.

## CMake targets

- `atlas` / `Atlas::Atlas`: compiled Atlas library and namespaced alias
- `atlas_cli`: example sequential task-graph executable linked to `Atlas::Atlas`
- `atlas_unit_tests`: Catch2 unit-test executable discovered by CTest
- `atlas_feature_tests`: Catch2 feature-test executable discovered by CTest

## CMake options

| Option | Raw CMake default | Purpose |
| --- | --- | --- |
| `ATLAS_BUILD_TESTS` | `ON` | Build and register the unit and feature tests |
| `ATLAS_WARNINGS_AS_ERRORS` | `OFF` | Promote warnings on Atlas-owned targets to errors |
| `ATLAS_ENABLE_SANITIZERS` | `OFF` | Enable AddressSanitizer and UndefinedBehaviorSanitizer on compatible non-Windows GCC/Clang builds |
| `ATLAS_ENABLE_THREAD_SANITIZER` | `OFF` | Enable ThreadSanitizer on compatible non-Windows GCC/Clang builds; mutually exclusive with ASan/UBSan |
| `ATLAS_ENABLE_CLANG_TIDY` | `OFF` | Run Clang-Tidy when it is available |
| `ATLAS_REQUIRE_CLANG_TIDY` | `OFF` | Fail configuration if requested Clang-Tidy analysis is unavailable |

Options can be overridden on a preset invocation. For example, disable
sanitizers when the local execution environment cannot run them:

```bash
cmake --preset dev-linux -DATLAS_ENABLE_SANITIZERS=OFF
```

The checked-in presets keep ordinary builds, static analysis, ASan/UBSan, and
TSan separate. Sanitizer options fail configuration on unsupported toolchains,
and ASan/UBSan cannot be combined with TSan.

Warnings, sanitizers, and static analysis are applied only to Atlas targets;
third-party dependencies do not inherit those settings.

## Continuous integration

GitHub Actions exposes independent format, GCC, MSVC, Clang-Tidy, ASan/UBSan,
TSan, and documentation jobs. Ordinary Linux and Windows jobs run the full suite
and CLI smoke test. TSan repeats the labelled concurrency subset five times.
The Vulkan SDK is required for discovery and linking, but no physical GPU or
Vulkan execution is required.

The dedicated Linux quality presets are reproducible locally:

```bash
cmake --preset analysis-linux
cmake --build --preset analysis-linux

cmake --preset asan-ubsan-linux
cmake --build --preset asan-ubsan-linux
ctest --preset asan-ubsan-linux

cmake --preset tsan-linux
cmake --build --preset tsan-linux
ctest --preset tsan-linux --repeat until-fail:5
```

LeakSanitizer cannot run when the test process is controlled through `ptrace`.
For that local environment only, use `LSAN_OPTIONS=detect_leaks=0`; CI retains
leak detection.
