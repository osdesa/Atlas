# Atlas

Atlas is a C++20 heterogeneous CPU/Vulkan task-graph prototype.

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
- create a compute-only Vulkan runtime with deterministic device selection,
  persistent storage buffers and pipelines, staging transfers, and validated
  declarative dispatches;
- divide a logical Vulkan dispatch into deterministic X-major work units using
  Vulkan 1.1 dispatch-base execution, including uneven edge slices;
- execute Vulkan work asynchronously through a capacity-one draining executor;
- build explicit CPU and GPU tasks and execute mixed graphs while tracking each
  backend's capacity independently through one shared completion channel;
- request fail-stop task cancellation before submission or at sliced GPU
  work-unit boundaries while draining work already accepted;
- select ready work through interchangeable FIFO, configurable work-unit
  round-robin, or stable static-priority policies;
- measure per-task ready-set wait, response duration, and same-resource
  selection bypasses;
- record scheduler-active and uncontested slice-switch durations alongside
  graph-level logical completion count and elapsed time; and
- run versioned deterministic CPU/Vulkan benchmark manifests with warmups,
  repetitions, generated DAGs, and JSON Lines/CSV export.

The CPU CLI runs the same 17-task sensor pipeline through the synchronous and
four-thread worker-pool executors and compares their output. Opt-in Vulkan
examples verify standalone vector addition and a CPU-to-sliced-Vulkan-to-CPU
graph that reports logical work-unit progress.

Atlas does **not** yet provide runtime task submission, repeated graph
execution, dynamic priority or starvation mitigation, multiple Vulkan queues,
true active-dispatch preemption, direct baseline comparisons, full event
tracing, or Vulkan timestamp-query utilization. Static-priority starvation
exposure is measurable, but Atlas does not claim to prevent it.

## Task model and lifecycle

`TaskGraph::addCpuTask()` and `addGpuTask()` make payload type authoritative;
`addTask()` remains a CPU compatibility alias. The Kahn scheduler borrows a CPU
executor and, for mixed graphs, a GPU executor. It fills both capacities
independently, consumes whichever attributed completion arrives first, and uses
independently cloned policy state for each backend. Existing constructors use
FIFO; overloads accept FIFO, round-robin, static-priority, or user-defined
backend-neutral policies. Lower numeric priorities run first under static
priority, with FIFO ordering for ties.

Tasks begin `Unknown` while their graph is being constructed. Successful graph
finalisation makes tasks without dependencies `Ready` and tasks waiting on
dependencies `Blocked`. `KahnScheduler` changes a selected task to `Running`,
then records `Success` or `Failure`, a captured exception when applicable, and
the payload's execution duration. A sliced GPU task alternates between
`Running` and scheduler-internal `Paused` states until its final work unit
succeeds. Only logical task success makes newly unblocked dependants `Ready`.
For Milestone 9, this dependency-driven `Blocked -> Ready` transition is the
supported arrival model; finalised graphs are not mutated during execution.

`TaskExecutionInfo::readyWaitDuration` accumulates time resident in a
resource-specific ready set, starting when scheduler parsing enqueues a root and
again after every incomplete sliced work unit. It excludes dependency-blocked
time, executor queueing, and payload execution. `selectionBypassCount` records
each selection of another valid candidate while the task remains ready or
paused in that same resource set.

`responseDuration` measures from first scheduler-observed readiness through a
terminal outcome. `SchedulerResult` also records control-thread active time and
uncontested immediate sliced-GPU turnaround samples. See the
[Milestone 10 design](docs/milestone-10-benchmarking-framework.md) for the
benchmark schemas, metric formulas, and availability rules.

Schedulers own state changes; `Task` does not validate a universal transition
matrix. `KahnScheduler::execute()` is a single control-thread operation even
when worker threads run callables concurrently, and a graph is intended for one
execution. Callers must not read or mutate execution information concurrently
with execution. `KahnScheduler::requestCancellation()` may be called before or
concurrently with `execute()`. Running CPU and ordinary GPU payloads are not
interruptible; running sliced GPU work can stop only after its current work unit
completes. See [Task lifecycle](docs/task-lifecycle.md) and the
[Milestone 6 design](docs/milestone-6-cooperative-gpu-slicing.md) for the exact
contracts. See [Milestone 7 scheduling policies](docs/milestone-7-scheduling-policies.md)
for selection, quantum, priority, and policy-failure semantics. See the
[Milestone 9 design](docs/milestone-9-preemptive-style-priority-scheduling.md)
for cooperative intervention and starvation-exposure measurements.

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
  - A Vulkan 1.1 loader and compute-capable Vulkan 1.1 device or software
    implementation. Atlas uses `vkCmdDispatchBase` for ordinary and sliced
    dispatches.
- Ninja for the generic `dev` and Linux presets
- `glslc` and SPIR-V Tools for Vulkan integration builds
- Mesa Lavapipe and Vulkan validation layers for Linux integration execution

The first test-enabled configure downloads Catch2 v3.8.1 through CMake
`FetchContent`. Benchmark-enabled configurations similarly download the pinned
nlohmann/json parser release.

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
sudo apt-get install build-essential cmake ninja-build libvulkan-dev \
  mesa-vulkan-drivers vulkan-validationlayers glslc spirv-tools
```

Then configure, build, and test:

```bash
cmake --preset dev-linux
cmake --build --preset dev-linux
ctest --preset dev-linux

cmake --preset vulkan-integration-linux
cmake --build --preset vulkan-integration-linux
ctest --preset vulkan-integration-linux

cmake --preset benchmark-linux
cmake --build --preset benchmark-linux
ctest --preset benchmark-linux
./build/benchmark-linux/apps/atlas_bench/atlas_bench \
  --manifest benchmarks/manifests/mixed-sliced-smoke-v1.json \
  --output-dir build/benchmark-linux/results
```

The generic `dev` preset uses the same Ninja-based development settings and is
also suitable on Linux.

## CMake targets

- `atlas` / `Atlas::Atlas`: compiled Atlas library and namespaced alias
- `atlas_cli`: example sequential task-graph executable linked to `Atlas::Atlas`
- `atlas_vulkan_example`: opt-in standalone Vulkan vector addition
- `atlas_mixed_example`: opt-in CPU/Vulkan dependency graph
- `atlas_all_example`: opt-in runner that executes all three examples in sequence
- `atlas_bench`: opt-in versioned manifest benchmark runner
- `atlas_unit_tests`: Catch2 unit-test executable discovered by CTest
- `atlas_feature_tests`: Catch2 feature-test executable discovered by CTest
- `atlas_benchmark_tests`: opt-in benchmark schema, generation, and metric tests

## CMake options

| Option | Raw CMake default | Purpose |
| --- | --- | --- |
| `ATLAS_BUILD_TESTS` | `ON` | Build and register the unit and feature tests |
| `ATLAS_BUILD_VULKAN_INTEGRATION_TESTS` | `OFF` | Build shaders, examples, and real Vulkan integration tests |
| `ATLAS_BUILD_BENCHMARKS` | `OFF` | Build `atlas_bench` and its benchmark contract tests |
| `ATLAS_BENCHMARK_ENABLE_VULKAN` | `OFF` | Compile the benchmark shader and enable real GPU workloads; requires benchmark builds |
| `ATLAS_WARNINGS_AS_ERRORS` | `OFF` | Promote warnings on Atlas-owned targets to errors with non-MSVC toolchains |
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
TSan, documentation, and Lavapipe Vulkan jobs. Ordinary Windows builds remain
device-independent; Linux integration forces the Mesa software ICD and executes
both Vulkan examples and the labelled real-compute tests with validation enabled.

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
