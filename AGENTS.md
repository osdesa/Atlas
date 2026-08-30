# Atlas agent guide

This file applies to the entire repository. It is the starting point for an
agent continuing Atlas development.

## Project overview

Atlas is a C++20 heterogeneous CPU/Vulkan task-graph prototype. It builds and
finalises directed acyclic task graphs, executes CPU work through synchronous or
worker-pool executors, executes declarative Vulkan compute dispatches, and can
schedule mixed CPU/GPU graphs with independent backend capacities.

Milestones 4 through 6 introduced the Vulkan backend, mixed scheduling,
cooperative dispatch slicing, and task cancellation. The current implementation
includes:

- graph-scoped task identities and immutable, variant-backed CPU/Vulkan work;
- `TaskGraph::addCpuTask()` and `addGpuTask()`, with `addTask()` retained as the
  CPU compatibility API;
- synchronous and fixed-size worker-pool CPU executors;
- a compute-only Vulkan runtime, persistent buffers and pipelines, staging
  transfers, Vulkan 1.1 dispatch-base execution, and a capacity-one Vulkan
  executor;
- a preallocated shared completion channel for CPU and GPU producers;
- a resource-aware Kahn scheduler that processes whichever backend completes
  first, interleaves sliced GPU work at FIFO boundaries, and applies fail-stop
  cancellation; and
- standalone CPU, Vulkan, mixed, and combined examples.

Read these documents before making architectural changes:

- `README.md` for supported behavior, prerequisites, and primary commands;
- `docs/development.md` for ownership, lifecycle, build, and coding details;
- `docs/task-lifecycle.md` for scheduler state transitions and failure rules;
- `docs/milestone-4-5-vulkan-roadmap.md` for the implemented design boundaries;
- `docs/milestone-6-cooperative-gpu-slicing.md` for slicing, progress, and
  cancellation contracts;
- `docs/index.md` for the public documentation overview.

## First actions in a new session

1. Run `git status --short` and inspect relevant diffs before editing. The
   worktree may contain uncommitted milestone changes belonging to the
   user. Never reset, overwrite, or reformat unrelated changes.
2. Use `rg` and `rg --files` for repository searches.
3. Read the nearest implementation, public header, and tests together before
   changing a contract.
4. Build and test with the narrowest preset that covers the change, then widen
   validation in proportion to risk.

Do not edit or commit generated content below `build/`.

## Repository layout

- `include/atlas/Tasking/`: task identities, metadata, work, and graph APIs.
- `include/atlas/Executor/`: CPU/GPU executor contracts and completion types.
- `include/atlas/Scheduler/`: graph scheduling APIs.
- `include/atlas/Vulkan/`: public opaque Vulkan resources and runtime APIs.
- `src/`: implementations matching the public module structure.
- `apps/atlas_cli/`: 17-task CPU pipeline run through both CPU executors.
- `apps/atlas_vulkan_example/`: standalone Vulkan vector addition.
- `apps/atlas_mixed_example/`: CPU to Vulkan to CPU dependency graph.
- `apps/atlas_all_example/`: runs all three executables in sequence.
- `tests/unit/`: device-independent Catch2 tests.
- `tests/feature/`: end-to-end scheduler and real Vulkan tests.
- `tests/shaders/`: checked-in GLSL compiled to SPIR-V during integration builds.
- `tests/support/`: shared test doubles and Vulkan test helpers.
- `cmake/`: warnings, sanitizers, documentation, analysis, and shader helpers.
- `docs/`: Doxygen input, development guides, lifecycle documentation, and UML
  configuration.
- `.github/workflows/ci.yml`: the authoritative CI quality matrix.

## Build prerequisites

Atlas requires CMake 3.24+, a C++20 compiler, Ninja for Linux presets, Threads,
Vulkan development headers, and a Vulkan 1.1 loader/device. Real Vulkan
integration also requires `glslc`, `spirv-val`, Mesa Lavapipe, and Vulkan
validation layers.

On Ubuntu the expected packages are:

```bash
sudo apt-get install build-essential cmake ninja-build libvulkan-dev \
  mesa-vulkan-drivers vulkan-validationlayers glslc spirv-tools
```

The first test-enabled configure may download Catch2 through CMake
`FetchContent`.

## Primary build and test commands

Normal Linux development:

```bash
cmake --preset dev-linux
cmake --build --preset dev-linux --parallel
ctest --preset dev-linux
```

Warnings-as-errors GCC build matching ordinary Linux CI:

```bash
cmake --preset ci-linux
cmake --build --preset ci-linux --parallel
ctest --preset ci-linux
```

Real Vulkan compute through the integration configuration:

```bash
cmake --preset vulkan-integration-linux
cmake --build --preset vulkan-integration-linux --parallel
ctest --preset vulkan-integration-linux
./build/vulkan-integration-linux/apps/atlas_all_example/atlas_all_example
```

The combined runner executes the CPU pipeline, standalone Vulkan compute, and
mixed graph in that order, stopping at the first failure. It is created only
when `ATLAS_BUILD_VULKAN_INTEGRATION_TESTS=ON`.

The Vulkan loader normally discovers the installed driver. CI pins Lavapipe
with `VK_DRIVER_FILES`. Do not hard-code a local ICD path into source or CMake;
installed filenames differ across distributions and architectures.

Other quality presets:

```bash
cmake --preset analysis-linux
cmake --build --preset analysis-linux --parallel

cmake --preset asan-ubsan-linux
cmake --build --preset asan-ubsan-linux --parallel
ctest --preset asan-ubsan-linux

cmake --preset tsan-linux
cmake --build --preset tsan-linux --parallel
ctest --preset tsan-linux --repeat until-fail:5

cmake --preset docs-linux
cmake --build --preset docs-linux --parallel
```

LeakSanitizer may be unable to run under `ptrace`. Use
`LSAN_OPTIONS=detect_leaks=0` only for that local limitation; do not weaken CI.

## Validation expectations

- Documentation-only changes: build `docs-linux` and run `git diff --check`.
- Localized C++ changes: build and run the relevant unit/feature tests.
- Task, executor, scheduler, or concurrency changes: run the complete normal
  suite plus ASan/UBSan and the repeated TSan concurrency subset.
- Vulkan runtime, resources, executor, shader, or mixed graph changes: run the
  normal suite and `vulkan-integration-linux` using a real Vulkan implementation
  such as Lavapipe.
- CMake or preset changes: configure every affected preset and build at least
  one representative target.
- Public API changes: update tests, README/development documentation, Doxygen,
  and UML configuration where applicable.

Before handing off, run:

```bash
rg --files -g '*.cpp' -g '*.h' -g '*.hpp' -0 | \
  xargs -0 clang-format --dry-run --Werror
git diff --check
```

## Architectural invariants

- A task handle is valid only within its graph identity. Cross-graph handles
  must never be accepted as graph members or dependency endpoints.
- Each `Task` contains exactly one `TaskFunction`, `VulkanDispatch`, or
  `SlicedVulkanDispatch`. The variant alternative must agree with
  `TaskOptions::executionResource`.
- `addTask()` remains a CPU compatibility alias. Do not silently reinterpret a
  CPU callable as GPU work or infer a backend from runtime types.
- A graph is structurally mutable only before successful finalisation and is
  intended for one scheduler execution.
- Scheduler control is single-threaded. Executors may run payloads concurrently,
  but only the scheduler applies graph task state and releases dependencies.
- CPU and GPU capacities and in-flight counts are independent. A busy backend
  must not prevent ready work from being submitted to the other backend.
- Every accepted submission must produce exactly one task-attributed completion.
  Invalid, missing, duplicate, unknown, extra, resource-mismatched, or
  work-unit-mismatched completions must never release dependants.
- Incomplete sliced GPU tasks retain accumulated progress and duration, enter
  `Paused`, and return to the GPU FIFO tail. Only logical success increments
  `executedTaskCount` or releases dependants.
- Cancellation requests synchronize with scheduler task claiming. Effective
  cancellation stops submissions and drains accepted work; running CPU and
  ordinary GPU payloads complete normally, while sliced work can stop only at a
  completed unit boundary.
- After the first task or infrastructure failure, stop new submissions and drain
  all accepted work. Preserve the first task exception. Infrastructure failures
  map to `SchedulerStatus::ExecutorUnavailable`.
- `CompletionChannel` storage is allocated before producers publish. Producer
  publication must remain thread-safe, allocation-free, and non-throwing.
- Executor shutdown rejects new work, drains accepted work, retains required
  completions, joins workers, and is idempotent.
- `VulkanExecutor` reports capacity one. Atlas does not claim to preempt an
  active Vulkan dispatch.
- Public Vulkan contracts expose no raw ownership handles. Raw handles and
  destruction order remain private behind RAII/PIMPL-style implementations.
- Buffers and pipelines belong to one runtime context. Reject cross-runtime
  resources and invalid dispatch limits before queue submission.
- Persistent resources keep the shared Vulkan context alive. `VulkanExecutor`
  requires a valid `VulkanRuntime` at construction and then retains that shared
  context while it exists.
- Queue waiting is not part of `TaskCompletion::executionDuration`; duration
  measures payload execution after the worker selects the item.

## C++ and CMake conventions

- Use C++20 and the repository `.clang-format` and `.clang-tidy` configurations.
- Treat warnings as errors on the Linux CI path. Avoid suppressions unless the
  reason is narrow and documented.
- Keep public declarations under `include/atlas/<Module>/` and implementations
  under the matching `src/<Module>/` directory.
- Prefer standard-library ownership and synchronization types. Make ownership
  explicit and preserve existing non-copyable/move-only contracts.
- Use `Atlas` as the public namespace. Keep implementation helpers private or
  under the established `Detail` namespace.
- Source and test files are discovered recursively with
  `file(GLOB_RECURSE ... CONFIGURE_DEPENDS)`. Put files in the correct tree; do
  not list them manually as well.
- New executables own their sources in their nearest `CMakeLists.txt` and must
  apply `atlas_set_project_warnings`, `atlas_enable_sanitizers`, and
  `atlas_enable_clang_tidy`.
- Use target-scoped CMake commands and the narrowest correct visibility. Do not
  add global include paths, compiler flags, definitions, or link settings.
- Integration-only shaders must be compiled with `atlas_compile_shader`, which
  invokes both `glslc` and `spirv-val`.

## VS Code workflow

Install the Microsoft CMake Tools extension in addition to the recommended
clangd extension. Select `vulkan-integration-linux` with **CMake: Select
Configure Preset**, configure, and build. Select `atlas_all_example` as the
launch target or run it from the integrated terminal using the path shown in
the primary commands above.

## Documentation conventions

Documentation coverage is part of the implementation, including private code.
Follow the style in `include/atlas/Tasking/` and the existing implementation
files:

- Give every file an `@file` and `@brief` comment.
- Document public and private classes, structs, enums, aliases, functions,
  constructors, fields, constants, and important local helper types/variables.
- Use `@param`, `@return`, `@throws`, ownership/lifetime notes, and concurrency
  constraints where applicable.
- Prefer concise comments that explain contract, ownership, validation, or
  synchronization intent rather than restating syntax.
- Keep Doxygen topic groups parallel to the public modules: `Tasking`,
  `Executor`, `Scheduling`, and `Vulkan`. A group needs one `@defgroup` and its
  public members need the corresponding `@ingroup` tag.
- Keep `@plantumlfile` references and `docs/clang-uml.yml.in` aligned with
  public architecture changes.
- Build the documentation and inspect generated warnings, not just whether the
  command exits successfully.

The generated Topics page includes a dedicated `@defgroup vulkan Vulkan` with
matching `@ingroup vulkan` annotations beside Executor, Scheduling, and Tasking.
Preserve that organization when touching Vulkan docs.

## Testing conventions

- Tests use Catch2 and are registered with CTest through
  `catch_discover_tests`.
- Put deterministic, device-independent behavior in `tests/unit/`.
- Put real Vulkan execution in `tests/feature/vulkan/` and label it consistently
  with the existing integration tests.
- Prefer synchronization primitives, latches, channels, or controlled test
  doubles over arbitrary sleeps in concurrency tests.
- Cover success, rejection, exception attribution, completion ordering,
  draining shutdown, and contract violations when extending executors or the
  scheduler.
- Windows builds device-independent Vulkan contracts; Linux Lavapipe CI proves
  real compute output.

## Scope boundaries

The following are intentionally deferred unless the user explicitly changes
the project scope:

- runtime graph submission and repeated graph execution;
- public pause/resume and general cancellation tokens;
- priority-aware or interchangeable scheduling policies;
- multiple Vulkan queues or concurrent Vulkan dispatch execution;
- graphics/presentation support;
- true CPU or Vulkan dispatch preemption;
- adaptive backend selection, benchmarking, and allocator/pipeline-cache
  optimization.

When a requested change approaches one of these boundaries, explain the scope
impact before expanding the architecture.
