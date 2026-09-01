# Atlas Development Guide

## Architecture

Atlas separates immutable graph work, backend execution, and scheduler control:

- `TaskGraph` owns graph-scoped handles, task payload variants, dependencies,
  and finalisation.
- `CpuExecutor` implementations run CPU callables. `VulkanExecutor` owns a
  worker and submits declarative dispatches through a retained Vulkan context.
- `CompletionChannel` is the single preallocated completion path used by CPU
  and Vulkan producers.
- `KahnScheduler` owns ready queues, independent capacities/in-flight counts,
  policy instances, state transitions, dependency release, cancellation, and
  metrics. It always receives both CPU and Vulkan executors.
- `VulkanRuntime` owns the instance/device/queue context. Public buffers and
  pipelines retain that context without exposing raw owning handles.
- `TraceSession` timestamps fixed-size events from scheduler and executor
  producers. `BoundedTraceBuffer` provides best-effort non-blocking publication,
  while `TraceJsonlWriter` drains records on its own thread.
- `atlas_bench` is a C++ harness because it exercises executor and scheduler
  APIs directly for paired direct-versus-scheduled comparisons. JSON parsing,
  result writing, and statistics remain outside the Atlas library boundary.

Vulkan is not a pluggable optional backend. `VulkanDispatchExecutor` exists as
the narrow scheduler test seam for Vulkan dispatch submission, not as a generic
multi-backend API.

## Repository layout

- `include/atlas/Tasking/`: task identity, metadata, payload, and graph APIs.
- `include/atlas/Executor/`: CPU/Vulkan executor and completion contracts.
- `include/atlas/Scheduler/`: scheduler and policy APIs.
- `include/atlas/Vulkan/`: opaque resource and runtime APIs.
- `include/atlas/Profiling/`: trace event, buffer, session, and JSONL writer APIs.
- `src/`: implementations matching those public modules.
- `apps/atlas/`: current mixed-graph executable and shader.
- `apps/atlas_bench/`: suite parser, runners, analysis, and result writers.
- `benchmarks/manifests/`: canonical and smoke suite definitions.
- `benchmarks/schema/`: current suite and output schemas.
- `tests/unit/`, `tests/feature/`, `tests/benchmark/`: behavioral tests.

## Ownership and lifecycle

A task handle is valid only in its originating graph. A graph is structurally
mutable until finalisation and is intended for one scheduler execution. Tasks
contain exactly one CPU callable, ordinary Vulkan dispatch, or sliced Vulkan
dispatch matching their declared execution resource.

The scheduler is the only writer of task lifecycle state. Executors own work
values while executing but publish outcomes instead of mutating graph objects.
Every accepted submission must generate exactly one completion attributed to
the accepted handle/resource/work unit. Unknown, duplicate, missing, extra, or
mismatched outcomes are infrastructure failures and never release dependants.

The completion channel capacity is the sum of active backend capacities and is
allocated before submission. Producer publication is non-throwing and does not
allocate. Executor shutdown is idempotent, rejects new work, drains accepted
work, and joins owned threads.

Vulkan buffers and pipelines belong to one runtime context. Cross-context
dispatch resources are rejected before queue submission. Destruction order is
private RAII state; a resource or `VulkanExecutor` retains the context it needs.

Tracing is an optional borrowed association: the writer/session must outlive
the scheduler and all accepted executor work. The completion channel carries
that association without widening executor submission interfaces. Event
publication is bounded, allocation-free after setup, non-throwing, and may
drop under capacity or lock contention. Consumers must use footer counters when
deciding whether a trace is complete enough for analysis.

When compiled with profiling, `VulkanExecutor` reuses a two-query timestamp
pool. Support requires a nonzero `timestampValidBits` value on the selected
compute queue and a positive device timestamp period. Results are availability
checked, masked to the reported valid-bit width, and converted to nanoseconds;
host duration and device duration remain separate measurements.

## Scheduler behavior

CPU and Vulkan ready sets and capacities are independent. FIFO,
`RoundRobinSchedulingPolicy`, and `StaticPrioritySchedulingPolicy` are cloned
once per backend and receive only stable candidates. They do not own graph or
executor state.

Sliced dispatches preserve completed work-unit count and accumulated duration.
An incomplete task enters `Paused` and returns to the Vulkan ready tail. Only a
logical task success increments the executed-task count or releases dependants.

Cancellation synchronizes with task claiming. Once effective, submissions stop
and accepted work drains. Ordinary running work completes; sliced Vulkan work
can stop only after a completed unit. Task, policy, and infrastructure failures
are fail-stop and preserve the first applicable exception.

See [Task lifecycle](task-lifecycle.md) for the state machine and
[User Guide](user-guide.md) for current external behavior.

## Build and test

The normal build always discovers Vulkan, compiles both shaders, builds
`atlas` and `atlas_bench`, and includes real Vulkan feature tests when tests are
enabled:

```bash
cmake -S . -B build
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Linux CI uses Mesa Lavapipe selected through `VK_DRIVER_FILES`. Driver manifest
names vary, so no source, CMake, or checked configuration may hard-code one.
CI also builds and tests `profiling-disabled-linux`, exercises trace generation,
and validates the resulting stream with `tools/atlas_trace.py`.

Use focused tests while iterating, then validate in proportion to risk. Run
real Vulkan tests for any runtime, resource, dispatch, executor, scheduler, app,
or benchmark change. Use ASan/UBSan and repeated TSan tests for ownership and
concurrency changes. A local LeakSanitizer invocation under `ptrace` may require
`LSAN_OPTIONS=detect_leaks=0`; never weaken CI for that limitation.

## Code and documentation

Use target-scoped CMake, C++20, explicit ownership, repository formatting, and
warnings-as-errors compatibility. Public headers live under
`include/atlas/<Module>/`; implementations live under matching `src/` paths.
Shader targets use `atlas_compile_shader`, which compiles and validates SPIR-V.

Update tests and all user-facing documentation with behavior changes. Review
`docs/user-guide.md` after every milestone and keep it limited to the current
implementation. Do not add compatibility aliases, obsolete format readers,
backend fallbacks, or speculative interfaces.
