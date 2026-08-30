# Atlas repository review and remaining roadmap

## 1. Executive summary

Atlas is a healthy, well-tested heterogeneous scheduling prototype. The latest
fully completed milestone, using the numbering in the review brief, is
**Milestone 9: Preemptive-style priority scheduling**.

Milestones 1–9 are implemented, connected to executable paths, and meaningfully
tested. Atlas currently provides:

- graph-scoped CPU/Vulkan tasks;
- synchronous and worker-pool CPU execution;
- real Vulkan compute through Lavapipe;
- mixed CPU/GPU dependency scheduling;
- cooperative GPU slicing and boundary cancellation;
- FIFO, round-robin, and stable static-priority policies; and
- per-task ready-wait duration and same-resource selection-bypass accounting.

Milestone 9 defines arrival as an already-declared task becoming ready after
dependency completion. Deterministic and real-Vulkan tests prove
`low slice 0 -> newly ready high-priority task -> low slice 1`, preserved work
unit progress, and exact finite-backlog bypass counts. Runtime graph admission
and starvation mitigation remain intentionally deferred.

The foundation is sound. No critical correctness defect was found. The
next objective should be **Milestone 10: a reproducible benchmarking framework**
built on the ready-set measurements completed here. General runtime graph
mutation should remain deferred.

No source files were modified during the initial review. This document was
subsequently updated alongside the Milestone 9 implementation.

## 2. Build and validation results

### Review coverage

All source-controlled documentation, repository-local ignored documentation,
the untracked handoff, public headers, implementations, applications, tests,
shaders, CMake modules, presets, CI, dependency declarations, Doxygen, and UML
configuration were inspected.

The only pre-existing worktree change was:

```text
?? MILESTONE_6_HANDOFF.md
```

It was not modified.

### Commands and outcomes

| Validation | Result |
| --- | --- |
| `cmake --list-presets=all` | Passed; Linux, Windows, analysis, sanitizer, docs, and Vulkan presets are defined. |
| `cmake --preset dev-linux` | Passed. |
| `cmake --build --preset dev-linux --parallel` | Passed with Clang-Tidy and ASan/UBSan enabled by the preset. |
| `ctest --preset dev-linux --output-on-failure` | Initially affected by LeakSanitizer’s documented `ptrace` incompatibility. |
| `LSAN_OPTIONS=detect_leaks=0 ctest --preset dev-linux --output-on-failure` | **155/155 passed**. |
| `cmake --preset ci-linux` and build | Passed with GCC warnings as errors. |
| `ctest --preset ci-linux --output-on-failure` | **155/155 passed**. |
| `cmake --preset analysis-linux` and build | Passed; no Clang-Tidy diagnostics. |
| `cmake --preset asan-ubsan-linux` | Configured successfully. Initial test discovery encountered the same LeakSanitizer environment issue. |
| ASan/UBSan build and tests with `LSAN_OPTIONS=detect_leaks=0` | **155/155 passed**; local leak detection was therefore not validated. |
| `cmake --preset tsan-linux` and build | Passed. |
| `ctest --preset tsan-linux --repeat until-fail:5` | **26 concurrency tests**, each repeated five times, passed. |
| `cmake --preset vulkan-integration-linux` and build | Passed. GLSL compilation and SPIR-V validation succeeded. |
| Lavapipe Vulkan integration suite | **6/6 passed** with validation enabled, including recorded priority intervention order. |
| `atlas_all_example` | Passed CPU pipeline, standalone Vulkan vector addition, and mixed CPU/sliced-GPU/CPU execution. |
| `atlas_cli` under development and CI builds | Passed; both CPU executors produced identical results. Its elapsed times are illustrative, not a benchmark. |
| `cmake --preset docs-linux` and documentation build | Passed; Doxygen and UML generation completed without emitted warnings. |
| ClangFormat dry run | Passed. |
| `git diff --check` | Passed. |

### Environmental limitations

- Windows/MSVC could not be run from the Linux environment; its presets and CI
  job were reviewed statically.
- LeakSanitizer cannot operate under the current `ptrace` environment. ASan and
  UBSan passed with leak detection disabled; CI retains leak detection.
- `vulkaninfo` was unavailable, but this did not prevent successful real Vulkan
  execution through the installed Lavapipe ICD.
- Default multi-driver discovery emitted validation messages locally; pinning
  the installed Lavapipe manifest through `VK_DRIVER_FILES`, as CI does, passed
  all validation checks without hard-coding an ICD path in Atlas.
- Remote GitHub Actions results were not queried; the workflow itself was
  inspected.
- No benchmark target exists, so no benchmark suite could be run.

## 3. Milestone status

| Milestone | Status | Evidence | Missing work |
| --- | --- | --- | --- |
| 1. Project foundation | **Complete and well tested** | C++20, target-based CMake, presets, GCC/MSVC CI, warnings, sanitizers, Clang-Tidy, Catch2/CTest, Vulkan discovery, apps, tests, and documentation builds. | Local Windows execution was unavailable; packaging/install targets are not part of the stated milestone. |
| 2. Core task model | **Complete and well tested** | Strong graph/task identities, states, immutable priority/resource metadata, captured exceptions, documented lifecycle, dependency validation, and broad unit coverage. | Tasking is concretely coupled to Vulkan payload types, which limits future backend modularity. |
| 3. CPU execution backend | **Complete and well tested** | Synchronous and fixed-size worker-pool executors, attributed completions, failure isolation, capacity reporting, draining shutdown, deterministic latch-based concurrency tests. | No material milestone gap found. |
| 4. Baseline scheduler | **Complete and well tested** | Resource-ready sets, Kahn dependency scheduling, FIFO policy, executor separation, failure draining, cancellation, shutdown and contract-violation tests. | No material milestone gap found. |
| 5. Vulkan compute backend | **Complete and well tested** | RAII/PIMPL runtime, deterministic device selection, queue family/device/command resources, buffers, staging, pipelines, synchronization, duration reporting, and real vector addition. | GPU timings are host-observed execution intervals, not Vulkan timestamp-query measurements. |
| 6. Cooperative GPU slicing | **Complete and well tested** | Deterministic 3D slice tiling, dispatch-base execution, parent progress, pause/resume, cancellation boundaries, uneven slices, fake and real Vulkan tests. | No active-dispatch interruption is attempted or claimed. |
| 7. Unified CPU/GPU scheduling | **Complete and well tested** | Independent CPU/GPU capacities, shared attributed completion channel, backend-neutral policies, mixed dependency graph and real CPU-to-Vulkan-to-CPU test. | Multiple GPU queues are intentionally absent. |
| 8. Multiple scheduling policies | **Complete and well tested** | FIFO, configurable work-unit round-robin, static priority, independent per-backend clones, stable ties, error handling, and real sliced execution with every policy. | Weighted fair, SJF, and EDF remain future policies. |
| 9. Preemptive-style priority scheduling | **Complete and well tested** | Ready-wait and same-resource bypass measurements, controlled dependency-driven intervention, exact finite-backlog accounting, and real-Vulkan low/high/low order. | Priority aging, runtime graph admission, and active-dispatch interruption remain explicitly deferred. |
| 10. Benchmarking framework | **Not started** | No benchmark target, experiment schema, metric collector, or reproducible workload runner. | All listed metrics and configurable workload categories. |
| 11. Baseline comparisons | **Scaffolding only** | Direct Vulkan and scheduled examples plus all three policies provide paths that could become baselines. | Common workload generation, repeated trials, normalization, result export, and comparison analysis. |
| 12. Profiling and visualisation | **Scaffolding only** | Per-task execution duration, ready wait, selection bypasses, and work-unit progress exist. | Machine-readable events, executor queue timestamps, worker activity, GPU timestamps, traces, and timeline tooling. |
| 13. Adaptive scheduling | **Not started** | No adaptive policy or feedback path. | Queue/latency/utilization feedback, bounded adaptation, evaluation, and stability controls. |
| 14. Robustness and validation | **Partially implemented** | Stress graph, cancellation, shutdown, malformed completions, cross-runtime rejection, sanitizers, repeated TSan, static analysis, validation layers. | Device-loss coverage, randomized DAG testing, long-duration runs, larger scale, fault injection, and performance-regression gates. |
| 15. Final evaluation | **Not started** | No reproducible experiment package or stored results. | Manifests, raw data, scripts, plots, statistical analysis, limitations, conclusions, and future-work report. |

No milestone is currently blocked by the repository. Later milestones depend on
adding benchmark-grade tracing and a reproducible benchmarking framework.

## 4. Current architecture

### Targets and dependencies

The root build creates one static `atlas` library and `Atlas::Atlas` alias, with
Vulkan public and Threads private dependencies.

Executable and test targets are:

- `atlas_cli`;
- integration-only `atlas_vulkan_example`, `atlas_mixed_example`, and
  `atlas_all_example`;
- `atlas_unit_tests`;
- `atlas_feature_tests`; and
- documentation, shader compilation, and shader-validation helper targets.

Catch2 3.8.1 is pinned by URL and SHA-256 through `FetchContent`.

### Modules and dependency direction

- **Tasking:** graph identity, task handles, metadata, lifecycle information,
  work variants, and dependency graph.
- **Executor:** CPU/GPU contracts, synchronous and worker-pool CPU
  implementations, Vulkan executor, completions, and completion channel.
- **Scheduler:** base scheduler, Kahn scheduler, policy interface, and three
  built-in policies.
- **Vulkan:** opaque resource wrappers, runtime, compute descriptions, slicing,
  and Vulkan errors.

Scheduling policies receive only handles and immutable priorities; Vulkan does
not leak into the policy interface. However, generic tasking includes concrete
Vulkan payloads through `TaskWork`, and `Vulkan::Vulkan` is a public library
dependency. This keeps the current prototype simple but prevents a CPU-only core
build.

### Runtime flow

1. A `TaskGraph` owns CPU callables or ordinary/sliced Vulkan dispatch
   descriptions.
2. Successful finalisation freezes graph structure and marks roots `Ready`.
3. `KahnScheduler` builds separate stable CPU and GPU ready sets.
4. A cloned backend-neutral policy selects candidates independently for each
   resource.
5. CPU and GPU executors publish attributed completions through one preallocated
   channel.
6. The single scheduler control thread validates handle, resource, running
   state, and work-unit index.
7. A successful incomplete slice becomes `Paused` and re-enters the GPU ready
   set; only final logical success releases dependants.
8. Failure or effective cancellation stops submissions and drains accepted
   work.

The graph and executors are borrowed for one scheduler execution. Vulkan buffers
and pipelines retain a shared private runtime context.

### Error handling and shutdown

The implementation captures task and Vulkan failures into completions,
validates completion attribution, preserves the first task exception, maps
infrastructure failures to `ExecutorUnavailable`, and maps policy failures to
`PolicyError`. Executor shutdown rejects new work, drains accepted work, retains
completions, joins workers, and is idempotent.

### Testing and CI

Tests are outcome-oriented and deterministic. No arbitrary sleeps occur in
tests; latches, channels, futures, and scripted executors control concurrency.
The only sleep is inside the example CPU workload.

Real Vulkan tests are opt-in locally and mandatory in their CI job; they do not
silently skip when the configured Vulkan environment is broken. CI separately
covers formatting, GCC, MSVC, Clang-Tidy, ASan/UBSan, TSan, documentation, and
Lavapipe.

No convincing dead production code, placeholder backend, or duplicated
scheduler implementation was found. `KahnScheduler.cpp` is now 767 lines and
carries readiness, policy, completion, cancellation, and lifecycle
responsibilities; it is still coherent, but further observability and adaptive
behavior should not simply be added as more inline state.

## 5. Problems and risks

### Critical

None found.

### High

| Finding | Why it matters and evidence | Recommended resolution |
| --- | --- | --- |
| Benchmark-quality observability remains incomplete. | `TaskExecutionInfo` now records payload duration, ready wait, bypasses, and progress. Vulkan duration still wraps host command preparation, submission, and fence waiting; it is not GPU timestamp data. Scheduler overhead, utilization, fairness, and slice-switch overhead cannot yet be derived reliably. | Add structured events and Vulkan timestamp queries in Milestones 10–12 before making performance claims. |
| Strict priority still permits starvation. | Ready-wait and bypass measurements expose starvation pressure, but strict priority intentionally has no aging and can still starve lower-priority tasks. | Use the new measurements in baselines. Do not claim starvation mitigation unless a later milestone implements and evaluates aging. |
| External runtime admission is unsupported. | Graphs remain finalised and single-use with no runtime structural submission. Milestone 9 precisely defines arrival as dependency-driven readiness. | Defer persistent runtime graph mutation until research results demonstrate it is needed. |

### Medium

| Finding | Why it matters and evidence | Recommended resolution |
| --- | --- | --- |
| Core tasking is coupled to Vulkan. | `TaskWork` contains concrete `VulkanDispatch` types and Atlas publicly links Vulkan. CPU-only users must still have Vulkan development dependencies. | Keep this for the current two-backend prototype. Before adding another accelerator backend, split backend-neutral graph metadata from backend payload storage or create separate `atlas_core`/`atlas_vulkan` targets. |
| Scheduler responsibilities are becoming concentrated. | Kahn scheduling owns dependency accounting, two ready sets, policy cloning, completion validation, cancellation, failure precedence, and lifecycle mutation. Milestone 9 moved ready-residency state into a private helper, but later tracing and adaptive control could still make the scheduler oversized. | Keep later trace sinks separate and event-driven rather than giving them task or executor ownership. |
| Local documentation contains contradictory stale files. | Ignored `docs/repository-review.md` says the repository is after Milestone 6 and FIFO-only. The untracked handoff says Milestone 6 is incomplete, while history and implementation show Milestone 7 complete. | Archive or remove the stale local artifacts after confirming they are no longer needed. Keep one tracked current-status document. |

### Low

| Finding | Why it matters and evidence | Recommended resolution |
| --- | --- | --- |
| Documentation warnings are not fatal and private extraction is disabled. | `Doxyfile.in` sets `EXTRACT_PRIVATE=NO` and `WARN_AS_ERROR=NO`, weaker than the repository’s documentation convention. | Enable private extraction and fail the docs job on warnings once current output is confirmed clean. |
| Direct pushes to `develop` are not CI-triggered. | The workflow’s push trigger includes only `main`. Pull requests remain covered. | Add `develop` to the push trigger if direct pushes to that branch are part of the workflow. |
| Portfolio metadata trails the implementation. | It omits cooperative slicing, cancellation, and scheduling policies. | Refresh it with the next documentation milestone; do not claim benchmarking or adaptive scheduling yet. |

## 6. Completed Milestone 9 contract

### Delivered objective

The scheduler performs cooperative priority intervention and now exposes
starvation-related measurements without introducing a persistent scheduler or
mutable runtime graph.

### Public interface

These fields are appended to `TaskExecutionInfo`:

- `std::chrono::microseconds readyWaitDuration{0}`: accumulated time spent in
  the CPU/GPU ready set across initial readiness and sliced-task resumptions. It
  excludes blocked time, executor queueing, and payload execution.
- `std::size_t selectionBypassCount{0}`: number of times another candidate from
  the same resource ready set is selected while this task remains ready or
  paused.

Existing `executionDuration`, completion interfaces, policy interfaces, task
priority, and cancellation semantics remain unchanged.

### Accounting semantics

- Start the first ready interval when scheduler parsing enqueues the task, not
  when the graph was finalised.
- Start a new interval whenever an incomplete slice is re-enqueued.
- Close and accumulate an interval when the task is selected.
- Increment bypass counts for every non-selected candidate remaining in that
  same resource ready set.
- At scheduler termination, close intervals for still-ready tasks so fail-stop
  runs retain useful waiting measurements.
- Keep priorities immutable and strict; this milestone measures starvation
  exposure but does not add aging.

### Explicit deferrals

- Runtime graph mutation and repeated graph execution.
- Dynamic priority changes or priority aging.
- Public pause/resume.
- Multiple Vulkan queues.
- Active CPU or Vulkan dispatch interruption.
- Full event tracing and benchmark harnesses.

### Acceptance evidence

- A controlled completion-channel test produces exactly
  `lower[0], higher[0], lower[1]` while the lower task is `Running` when the
  higher task becomes eligible.
- The lower task resumes from work unit 1, completes without replaying unit 0,
  and records one bypass plus non-negative accumulated ready wait.
- A finite high-priority backlog produces exact bypass counts `0, 1, 2, 3`.
- Fail-stop paths close ready intervals left open at termination.
- FIFO, round-robin, cancellation, failures, and mixed scheduling retain their
  existing contracts.
- Real sliced Vulkan execution passes for every built-in policy and a dedicated
  real-Vulkan test records the low/high/low intervention order.
- Documentation continues to state that an executing dispatch is never
  interrupted.

## 7. Ordered next steps and complete remaining roadmap

### Milestone 9 implementation completed

1. Added and documented `readyWaitDuration` and `selectionBypassCount` at the
   end of `TaskExecutionInfo`.
2. Added a private scheduler accounting helper for ready-entry time, selection,
   bypass increments, resumption, and finalization.
3. Extended task-model tests for default and accumulated measurement values.
4. Strengthened the scripted GPU intervention test with synchronization that
   makes the high-priority task ready at a controlled slice boundary.
5. Added a finite strict-priority starvation-measurement test with an exact
   bypass count.
6. Validated with the complete normal, warnings-as-errors, ASan/UBSan, repeated
   TSan, real Vulkan, documentation, and formatting gates.
7. Updated README, lifecycle, scheduling-policy, development, index, UML/Doxygen
   inputs, and current-status metadata.

### Roadmap after Milestone 9

1. **Milestone 10 — Benchmarking framework**
   - Add a separate `atlas_bench` target.
   - Use versioned JSON experiment manifests with a pinned parser dependency.
   - Configure seeds, warmups, repetitions, CPU/GPU task counts, dependency
     shape, work size, priorities, burst activation, policy, worker count, and
     slice geometry.
   - Export per-run JSON Lines plus normalized CSV.
   - Measure latency, ready wait, throughput, completion time, scheduler
     overhead, slice-switch overhead, CPU busy fraction, GPU timestamp busy
     fraction where supported, and Jain fairness.
   - Rebuild a fresh graph per repetition; do not weaken the current
     one-execution graph contract.
2. **Milestone 11 — Baseline comparisons**
   - Run identical generated workloads through direct CPU/Vulkan submission,
     FIFO, unsliced priority, round-robin slice-size/quantum matrices, and static
     priority.
   - Cover CPU-bound, GPU-bound, mixed, short/long, mixed-priority,
     bursty-readiness, and contention-heavy workloads.
   - Store environment metadata and confidence intervals; report trade-offs
     rather than selecting a universal winner.
3. **Milestone 12 — Profiling and visualisation**
   - Define a versioned event schema for submission/readiness, selection,
     backend start/end, pause, resume, cancellation, completion, and policy
     decision.
   - Use monotonic host timestamps and Vulkan timestamp queries with
     capability/valid-bit checks.
   - Add a non-blocking trace sink and Python timeline/summary scripts.
   - Preserve measurement-disabled builds with negligible overhead.
4. **Milestone 13 — Adaptive scheduling**
   - Begin only after baselines identify a concrete optimization target.
   - First adapt policy quantum/weights from queue depth and measured latency;
     retain fixed atomic slice boundaries.
   - Add bounded update intervals, minimum/maximum values, hysteresis, and a
     deterministic fixed-policy fallback.
   - Consider dynamic slice geometry only after quantum adaptation is
     evaluated.
5. **Milestone 14 — Robustness and validation**
   - Add randomized DAG/property tests, very large graphs,
     cancellation/shutdown fault matrices, malformed executor streams, Vulkan
     failure injection, device-loss handling, long-duration soak tests, and
     repeated stress under sanitizers.
   - Add performance-regression thresholds only after benchmark variance is
     characterized.
   - Continue real Lavapipe execution and add optional physical-GPU runs without
     making them required for ordinary development.
6. **Milestone 15 — Final evaluation**
   - Check in experiment manifests, environment capture, raw result layout,
     analysis scripts, tables, and plots.
   - Analyze latency, throughput, fairness, scheduler overhead, slice cost, and
     utilization with uncertainty.
   - Document limitations: software Vulkan versus hardware, cooperative
     boundaries, static graph model, single queue, and lack of active-dispatch
     interruption.
   - Tie conclusions directly to stated research questions and preserve a
     one-command reproduction path.

### Explicit post-evaluation boundaries

These are not required for Milestones 9–15 unless experiments justify them:

- runtime structural task submission;
- repeated execution of the same graph object;
- public pause/resume or general cancellation tokens;
- multiple Vulkan queues or concurrent GPU dispatches;
- graphics/presentation; and
- allocator and pipeline-cache optimization.

True interruption of an executing Vulkan dispatch remains out of scope
permanently; Atlas should continue using “preemptive-style scheduling through
cooperative execution slices.”

## 8. Milestone 9 implementation slices

| Pull request | Scope | Main modules | Tests | Definition of done |
| --- | --- | --- | --- | --- |
| **Slice 1: Ready-residency and bypass accounting** | Added the two public statistics and private scheduler accounting helper while preserving payload-duration semantics. | Tasking runtime information; Kahn scheduler; task/scheduler unit tests. | Defaults, multiple ready intervals, paused requeue, fail-stop finalization, exact bypass counts. | Implemented. |
| **Slice 2: Cooperative-boundary priority evidence** | Strengthened controlled low/high/low ordering and starvation-exposure scenarios. | Mixed scheduler test doubles and real Vulkan feature tests. | In-flight readiness transition, exact work-unit order, progress preservation, finite high-priority backlog. | Implemented without implying active-dispatch preemption. |
| **Slice 3: Documentation and validation** | Updated current status, lifecycle, policy semantics, Doxygen inputs, and limitations. Stale ignored review artifacts remain untouched. | README, docs, CMake documentation configuration. | Docs build, Doxygen warnings, ClangFormat, `git diff --check`, full scheduler/Vulkan validation matrix. | Implemented; validation evidence is recorded above. |

## 9. CV claim readiness

| Claim | Readiness | Evidence required or qualification |
| --- | --- | --- |
| Developed a user-space heterogeneous CPU/GPU task scheduler. | **Supported** | Mixed CPU/Vulkan graphs, independent capacities, shared attributed completion path, dependency release, and real Vulkan execution support this. “Prototype” is an appropriate qualifier. |
| Implemented multiple scheduling policies. | **Supported** | FIFO, round-robin, static priority, custom policy interface, stable behavior, failure handling, and tests with every built-in policy. |
| Implemented preemptive-style GPU scheduling through cooperative slices. | **Supported** | Sliced dispatches pause and resume between independent Vulkan submissions, including priority intervention. The wording must retain “preemptive-style” and “cooperative”; claiming hardware or active-dispatch preemption would be misleading. |
| Built a Vulkan compute backend. | **Supported** | RAII runtime/resources, pipelines, buffers, transfers, synchronization, dispatch-base slicing, validation layers, and real Lavapipe output. |
| Built an automated benchmarking framework. | **Not yet supported** | Requires the Milestone 10 configurable runner, metric schema, repetitions, and machine-readable results. The current CLI timer is not sufficient. |
| Evaluated latency, throughput, fairness, and scheduling overhead. | **Not yet supported** | Requires Milestones 10–11, reproducible workloads, instrumentation, baseline comparisons, stored data, and analysis. |

## 10. Final recommendation

- **Single next technical objective:** build Milestone 10's reproducible
  benchmarking framework on the completed ready-set measurements.
- **First implementation task:** define the versioned experiment manifest and
  per-run machine-readable result schema before selecting a parser dependency.
- **Main design mistake to avoid:** making finalised graphs dynamically mutable
  merely to simulate arrival. For this milestone, use dependency-driven
  `Blocked -> Ready` transitions and preserve the one-execution graph contract.
- **Milestone 9 completion result:** a deterministic
  `low[0] -> high[0] -> low[1]` execution with exact progress and bypass
  accounting, plus the same recorded submission order under real Vulkan, while
  the normal, repeated TSan, real Vulkan, documentation, analysis, sanitizer,
  and formatting gates remain green.
