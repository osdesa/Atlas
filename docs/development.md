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

`VK_ERROR_DEVICE_LOST` is an infrastructure failure even when it arrives in a
task-attributed completion. The shared Vulkan context latches the loss, later
device operations fail immediately, queued accepted dispatches receive one
failure completion each, and the scheduler reports `ExecutorUnavailable` after
draining accepted work. Other dispatch exceptions remain task failures.

See [Task lifecycle](task-lifecycle.md) for the state machine and
[User Guide](user-guide.md) for current external behavior.

## Research evidence

The [Milestone 13 evaluation](milestone-13-evaluation.md) preregistered a 5%
practical-effect threshold and conservative 95% confidence rules, then ran the
4,300-trial canonical suite on physical Intel Iris Xe and Mesa Lavapipe. Every
stable material same-slice quantum separation favored quantum 1, and static
priority produced no material stable fairness gain to offset its response
cost. Adaptive scheduling is consequently skipped. The
[Milestone 16 final evaluation](milestone-16-evaluation.md) repeats the full
suite on both implementations and completes that evaluation.

## Final evaluation pipeline

`atlas_bench` remains responsible for executing paired trials and writing suite
v1 and result v2 records. The standard-library Python tool
`tools/atlas_evaluation.py` owns the separate study-v1 contract: it validates
complete raw directories, computes arbitrary predeclared paired contrasts, and
generates the final machine-readable result, tables, and SVG plots. This keeps
research interpretation outside the Atlas library and benchmark executor.

The published collection used clean source revision `49da375e5670`, a new
output directory per environment, an explicit `VK_DRIVER_FILES` selection
supplied through `--icd`, and checked
environment metadata. Both declared environments must contain the exact
planned successful run keys and the same embedded Git revision. GPU-bearing
runs must contain capability-checked timestamp utilization. Repeating the study
after a code, suite, or analysis change requires new bundles and a new artifact
index rather than mutating the published evidence.

Raw outputs are attached to GitHub Release `milestone-16-evaluation-v1`; their
digests and source provenance are checked in
`benchmarks/evaluation/final-v1/artifacts.json`. Derived JSON, CSV, Markdown,
and SVG evidence is checked in beside the study contract. Use the tool's
`verify` command to download both assets, validate outer and internal hashes,
check provenance, and regenerate the analysis.

Ordinary CI runs the Python contract tests and analyzes the small Lavapipe
smoke suite. The 4,300-run canonical suite remains a controlled manual/release
operation and does not create a shared-runner timing gate.

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

Generated DAG and large-graph tests are tagged `STRESS`. Their bounded defaults
use seed `684453` and 128 generated rounds. Replay or extend them with strict
unsigned decimal environment values:

```bash
ATLAS_STRESS_SEED=684453 ATLAS_STRESS_ROUNDS=10000 \
  ctest --preset ci-linux -L STRESS --output-on-failure
```

The manual `Manual robustness` workflow runs this 10,000-round soak on
Lavapipe, the full ASan/UBSan suite, and the TSan concurrency suite twenty times.
It has no schedule and does not gate ordinary pull requests.

For an optional local physical-GPU run, select an installed ICD externally and
use the same build rather than checking a machine path into the repository:

```bash
export VK_DRIVER_FILES=/absolute/path/to/physical_icd.json
cmake --preset ci-linux
cmake --build --preset ci-linux --parallel
ctest --preset ci-linux
ATLAS_STRESS_SEED=684453 ATLAS_STRESS_ROUNDS=10000 \
  ctest --preset ci-linux -L STRESS --output-on-failure
./build/ci-linux/apps/atlas/atlas
./build/ci-linux/apps/atlas_bench/atlas_bench \
  --suite benchmarks/manifests/smoke-v1.json \
  --environment-file <environment.json> \
  --output-dir build/ci-linux/physical-robustness-smoke
```

These are correctness and deadlock checks. Atlas records benchmark timing but
does not apply absolute performance thresholds to shared CI or unrelated
physical hosts.

## Code and documentation

Use target-scoped CMake, C++20, explicit ownership, repository formatting, and
warnings-as-errors compatibility. Public headers live under
`include/atlas/<Module>/`; implementations live under matching `src/` paths.
Shader targets use `atlas_compile_shader`, which compiles and validates SPIR-V.

Update tests and all user-facing documentation with behavior changes. Review
`docs/user-guide.md` after every milestone and keep it limited to the current
implementation. Do not add compatibility aliases, obsolete format readers,
backend fallbacks, or speculative interfaces.
