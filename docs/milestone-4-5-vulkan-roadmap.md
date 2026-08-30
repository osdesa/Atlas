# Milestones 4 and 5: Vulkan execution roadmap

## Implementation status

Milestones 4 and 5 are implemented. The sections below retain the agreed goals,
boundaries, and acceptance criteria as an architectural record. The resulting
backend includes real Lavapipe integration coverage, and the mixed scheduler
preserves the CPU-only compatibility path.

## Starting baseline

When this roadmap was written, Milestone 3 was complete and merged into `main`.
That starting baseline provided:

- graph-scoped task identities and finalised directed acyclic task graphs;
- synchronous and fixed-size worker-pool CPU executors;
- capacity-aware Kahn scheduling with attributed, out-of-order completions;
- task lifecycle, exception, duration, shutdown, and executor-failure handling;
- 98 registered unit and feature tests; and
- independent formatting, GCC, MSVC, Clang-Tidy, ASan/UBSan, TSan, and
  documentation CI jobs.

Vulkan was discovered and linked but not used. `ExecutionResource::GPU`
expressed intent only, `Task` stored only a CPU `TaskFunction`, and
`KahnScheduler` accepted only a `CpuExecutor`. The roadmap therefore separated
proving a standalone Vulkan compute backend from integrating heterogeneous graph
scheduling.

Before Milestone 4 implementation began, the README and development guide also
needed correction to describe the 17-task CLI pipeline and its execution through
both CPU executors.

## Architectural decisions

- Milestone 4 implements and proves a standalone Vulkan compute backend.
- Milestone 5 adds mixed CPU/GPU task graphs and resource-aware scheduling.
- GPU work uses a declarative dispatch rather than a raw Vulkan callback.
- Raw Vulkan C API handles remain private behind move-only RAII wrappers.
- GPU data uses persistent opaque buffer objects that dependent dispatches can
  reuse without mandatory host round-trips.
- Checked-in GLSL compute shaders are compiled to SPIR-V with `glslc` during the
  build and validated with the SPIR-V tools.
- Linux integration tests execute real Vulkan work through Mesa Lavapipe.
  Windows builds and runs device-independent tests without requiring a GPU.
- Milestone 5 uses one shared completion channel so the scheduler can process
  whichever CPU or GPU task completes first.
- Task construction becomes explicit through CPU and GPU APIs. The existing
  `addTask()` API remains as a CPU compatibility alias.

## Milestone 4: standalone Vulkan compute backend

### Goal

Create a capacity-one Vulkan compute executor that owns Vulkan execution,
accepts validated declarative dispatches, returns attributed task completions,
and proves real compute output without changing graph scheduling.

### PR 1: Vulkan contracts and baseline documentation

- Correct the stale CLI descriptions in the README, development guide, and
  repository review.
- Define `VulkanError` with the failed `VkResult` and a human-readable operation
  description.
- Define dispatch dimensions, buffer access, buffer binding, shader, pipeline,
  and dispatch contracts.
- Generalise `TaskCompletion` documentation from CPU-callable completion to
  backend execution completion while retaining task handle, exception, and
  duration fields.
- Document validation rules, ownership, borrowed lifetimes, and the distinction
  between submission failure and execution failure.

Definition of done: public contracts are documented and testable without
creating a Vulkan device, existing CPU behaviour remains unchanged, and the
documentation matches the merged Milestone 3 baseline.

### PR 2: Vulkan runtime and device selection

- Add move-only RAII wrappers for the Vulkan instance, logical device, queue,
  command pool, command buffers, semaphores, and fences used by compute work.
- Enumerate physical devices and require a compute-capable queue family.
- Select devices deterministically in discrete, integrated, virtual, then CPU
  order, with an explicit selector available for tests and applications.
- Enable validation layers only when requested and report their absence clearly.
- Translate Vulkan failures at API boundaries into `VulkanError` without leaking
  partially created handles.

Definition of done: Atlas can initialise and shut down a compute-only Vulkan
runtime repeatedly, device-selection helpers have device-independent unit tests,
and validation reports no lifetime errors.

### PR 3: persistent compute resources and declarative dispatch

- Add opaque persistent buffer and compute-pipeline objects tied to one Vulkan
  runtime.
- Allocate device-local storage buffers and perform upload/download through
  internal staging resources.
- Create shader modules and compute pipelines from validated SPIR-V.
- Represent a dispatch as a pipeline, uniquely bound buffers with declared
  access, and non-zero X/Y/Z workgroup counts.
- Reject empty or malformed SPIR-V, duplicate bindings, zero dimensions,
  cross-device resources, out-of-range dispatch dimensions, and resources that
  violate device limits before queue submission.
- Compile checked-in GLSL test shaders with `glslc` through CMake and validate the
  generated SPIR-V before tests run.

Definition of done: buffers and pipelines can be reused across dispatches,
resource ownership is deterministic, and invalid descriptions fail without
submitting Vulkan commands.

### PR 4: capacity-one Vulkan executor

- Add an asynchronous `VulkanExecutor` implementing the GPU executor contract
  with reported capacity one.
- Execute accepted dispatches on one worker, capture attributed exceptions and
  execution duration, and retain standalone completions in completion order.
- Reject work after shutdown, drain accepted dispatches, join the worker, and
  make shutdown safe to repeat.
- Add deterministic lifecycle, attribution, failure-isolation, and draining tests.

Definition of done: every accepted dispatch produces exactly one completion,
queue wait is excluded from execution duration, and shutdown loses no accepted
work or completion.

### PR 5: integration evidence, CI, documentation, and UML

- Add a standalone Vulkan example that uploads input vectors, executes a compute
  shader, downloads the result, and verifies every output element.
- Add a dedicated Linux Vulkan integration preset and CI job using Mesa
  Lavapipe, `glslc`, SPIR-V validation, and Vulkan validation layers.
- Keep Windows responsible for compilation and device-independent tests; do not
  require physical or software Vulkan execution there.
- Add executor and resource UML diagrams through the existing clang-uml and
  PlantUML documentation pipeline.
- Update the README, development guide, API index, lifecycle documentation,
  repository review, and portfolio metadata.

Definition of done: Linux CI executes verified real compute through
`VulkanExecutor`, all existing CPU quality gates still pass, and generated
documentation includes the new Vulkan types and ownership model.

### Milestone 4 exclusions

- Mixed CPU/GPU graph scheduling.
- Graphics or presentation queues.
- Multiple Vulkan queues or concurrent GPU dispatches.
- Task cancellation or runtime graph submission.
- Priority scheduling or interchangeable scheduling policies.
- Cooperative GPU slicing or claims of interrupting an active Vulkan dispatch.
- Pipeline-cache or memory-allocation optimisation beyond correctness needs.

### Milestone 4 acceptance criteria

1. Atlas creates and destroys a compute-capable Vulkan runtime safely.
2. Public dispatch descriptions contain no raw Vulkan handles.
3. Persistent buffers and pipelines can be reused across valid dispatches.
4. `VulkanExecutor` reports capacity one and returns exactly one attributed
   completion for each accepted dispatch.
5. Shutdown drains accepted GPU work and is safe to repeat.
6. A real compute shader produces verified output through Lavapipe in Linux CI.
7. CPU executor and scheduler behaviour remains unchanged.

## Milestone 5: mixed CPU/GPU graph scheduling

### Goal

Allow one finalised dependency graph to contain explicit CPU and Vulkan work,
keep both backends in flight up to their independent capacities, and process
completions in whichever order the backends finish.

### PR 1: explicit CPU and GPU task work

- Add `TaskGraph::addCpuTask()` for `TaskFunction` work and
  `TaskGraph::addGpuTask()` for declarative Vulkan dispatch work.
- Retain `addTask()` as a CPU compatibility alias and reject GPU metadata passed
  with CPU-only work.
- Store CPU callable or Vulkan dispatch work explicitly and expose typed,
  read-only accessors.
- Make the selected task-construction API authoritative for
  `ExecutionResource`, preventing metadata and payload disagreement.
- Preserve graph identity, dependency validation, finalisation, and lifecycle
  behaviour.

Definition of done: each task has exactly one executable payload whose type
matches its resource, existing CPU callers retain a migration path, and invalid
CPU/GPU combinations are rejected during graph construction.

### PR 2: shared completion channel

- Add a fixed-capacity, thread-safe completion channel created for one scheduler
  execution and sized before submission begins.
- Preallocate completion storage so publishing an accepted outcome does not
  allocate or throw on an executor thread.
- Extend CPU and Vulkan submission paths to publish into a supplied channel while
  preserving their standalone completion APIs.
- Include the task handle and execution resource in each published outcome.
- Define channel closure and producer-failure signalling so an executor contract
  failure cannot leave the scheduler blocked indefinitely.

Definition of done: synchronous CPU, worker-pool CPU, and Vulkan executors can
publish into one channel, concurrent producers are race-free, and standalone
executor behaviour remains compatible.

### PR 3: resource-aware Kahn scheduling

- Add a CPU-and-Vulkan `KahnScheduler` constructor while preserving the current
  CPU-only constructor.
- Maintain separate capacity and in-flight counts for CPU and GPU work.
- Fill each backend independently from ready tasks whose resource matches it.
- Wait on the shared completion channel and process whichever attributed outcome
  arrives first.
- Validate that the completion came from the expected resource and an in-flight
  task before changing graph state.
- Release dependants only after successful completion.

Definition of done: CPU-only behaviour remains compatible, mixed graphs use both
backends without runtime type guessing, and neither backend's capacity prevents
submission to the other.

### PR 4: mixed-graph failure and concurrency behaviour

- Cover CPU-to-GPU and GPU-to-CPU chains, CPU/GPU fan-out, cross-resource fan-in,
  simultaneous execution, and out-of-order completion.
- Stop all new submissions after the first callable, dispatch, executor, or
  completion-contract failure while draining accepted work from both backends.
- Preserve the first task exception and continue using
  `SchedulerStatus::ExecutorUnavailable` for infrastructure failures.
- Ensure missing, duplicate, unknown, mismatched-resource, and extra completions
  never release dependants.
- Add deterministic stress tests without arbitrary timing assumptions.

Definition of done: mixed graphs preserve dependency order under concurrency,
failure draining is deterministic, and TSan reports no races in shared channel
or scheduler bookkeeping.

### PR 5: mixed execution example and quality gates

- Add an end-to-end graph that prepares input on the CPU, processes persistent
  buffers on Vulkan, and validates the result on the CPU.
- Run mixed feature tests through Lavapipe while retaining the full CPU unit,
  sanitizer, static-analysis, formatting, Windows, and documentation coverage.
- Add mixed task/executor/scheduler UML diagrams using the existing generation
  pipeline.
- Update lifecycle, development, repository review, README, index, and portfolio
  documentation to distinguish current behaviour from future cooperative
  slicing.

Definition of done: one graph executes real CPU and Vulkan tasks with correct
dependencies, per-backend capacity, attributed completions, and documented
failure behaviour in repeatable CI.

### Milestone 5 exclusions

- Priority or round-robin scheduling policies.
- Runtime task submission or persistent scheduler services.
- Cancellation of queued, running, or submitted Vulkan work.
- Multiple Vulkan queues or adaptive backend selection.
- Cooperative GPU task slicing and preemptive-style policy.
- Benchmark-driven or automatic scheduling decisions.

### Milestone 5 acceptance criteria

1. CPU and GPU task work are explicit and cannot disagree with resource intent.
2. The CPU-only graph and executor APIs retain a documented compatibility path.
3. CPU and GPU capacities are tracked independently.
4. A shared completion channel returns whichever backend finishes first without
   polling.
5. Dependencies are released only by valid successful completions.
6. Failure stops new work and drains all accepted CPU and GPU submissions.
7. Linux CI executes and verifies a real mixed CPU/Vulkan dependency graph.

## Later milestones

At the Milestone 5 boundary, priority-aware policy, interchangeable schedulers,
runtime graph submission, cancellation, multiple GPU queues, benchmarking,
adaptive scheduling, and cooperative GPU slices were deferred. Milestone 6 now
implements cooperative slices and task cancellation under the narrower contract
documented in [Milestone 6: cooperative GPU slicing and task cancellation](milestone-6-cooperative-gpu-slicing.md).
The other items remain deferred.
