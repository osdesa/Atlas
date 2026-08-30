# Milestone 6: cooperative GPU slicing and task cancellation

## Implementation status

Milestone 6 is implemented. Atlas can represent one logical Vulkan compute task
as ordered dispatch-base work units, interleave incomplete logical tasks at safe
boundaries, and apply fail-stop cancellation without weakening completion
attribution or draining guarantees.

## Logical dispatches and work units

A `VulkanDispatch` describes one complete logical compute domain: an opaque
pipeline, exact storage-buffer bindings, and non-zero X/Y/Z workgroup counts.
`SlicedVulkanDispatch` retains that validated description and adds maximum
per-work-unit extents.

The tiler covers the logical domain exactly once. X advances fastest, followed
by Y and Z. A trailing region on any axis may be smaller than the configured
maximum. For a logical `(5, 3, 2)` dispatch sliced by `(2, 2, 1)`, enumeration
begins:

```text
index 0: base (0, 0, 0), extent (2, 2, 1)
index 1: base (2, 0, 0), extent (2, 2, 1)
index 2: base (4, 0, 0), extent (1, 2, 1)
index 3: base (0, 2, 0), extent (2, 1, 1)
...
```

Every region shares immutable pipeline and buffer resources. The public API
continues to expose opaque resource values rather than raw Vulkan handles.

## Vulkan 1.1 boundary

Atlas now requires a Vulkan 1.1 loader and a compute-capable Vulkan 1.1 physical
device. Runtime creation requests Vulkan 1.1, compute pipelines use
`VK_PIPELINE_CREATE_DISPATCH_BASE_BIT`, integration shaders target Vulkan 1.1,
and the executor records every dispatch with `vkCmdDispatchBase`.

Ordinary dispatches use base `(0, 0, 0)`. Sliced units use their workgroup base,
so shaders can observe the logical position through built-ins such as
`gl_WorkGroupID` and `gl_GlobalInvocationID`. Atlas intentionally does not add a
Vulkan 1.0 push-constant offset ABI.

## Scheduling and progress

The scheduler submits at most one work unit for a sliced task at a time. A
successful incomplete unit follows this lifecycle:

```text
Ready -> Running -> Paused -> Running -> ... -> Success
```

`Paused` is internal to the scheduler; there is no public pause/resume API. An
incomplete task is appended to the GPU FIFO tail. With a capacity-one Vulkan
executor, two ready logical tasks therefore alternate at unit boundaries while
preserving each task's internal index order.

`TaskExecutionInfo::totalWorkUnitCount` is one for ordinary work and the computed
slice count for sliced work. `completedWorkUnitCount` advances only for
successful units. `SchedulerResult::executedTaskCount` counts logical tasks, not
dispatch submissions.

Execution duration excludes executor queue waiting. For sliced work it
accumulates executor-reported payload durations across attempted units. A unit
that returns an exception contributes its payload duration but does not advance
completed progress. Prior duration and progress survive task or infrastructure
failure.

## Exact completion attribution

`TaskCompletion` carries a zero-based `workUnitIndex`; ordinary payloads use
zero. The scheduler stores the expected resource and index for each accepted
submission. A completion must match:

- an exact in-flight task handle;
- the task's declared CPU/GPU resource;
- the task's `Running` state; and
- the exact expected work-unit index.

Missing, duplicate, unknown, extra, stale, skipped, resource-mismatched, or
index-mismatched completions are executor infrastructure failures. They cannot
advance progress or release dependants.

## Cancellation contract

`KahnScheduler::requestCancellation()` may run before or concurrently with
`execute()`. Requests are preallocated per graph task and synchronized with the
scheduler's task-claim boundary. Invalid, cross-graph, unknown, duplicate,
terminal, and post-execution requests are rejected.

Cancellation is cooperative:

- `Ready`, `Blocked`, and `Paused` work can be cancelled before a new
  submission is claimed;
- a running CPU task or ordinary Vulkan dispatch is non-preemptible, so its
  completion wins;
- a running sliced task can become cancelled after its current successful unit
  when more work remains; and
- a successful final unit wins over a concurrent request.

Once any request becomes effective, the graph stops making new submissions and
drains all accepted CPU/GPU work. Cancelled tasks do not release dependants.
Already accepted work can still reveal higher-precedence failures. Final status
ordering is:

```text
ExecutorUnavailable > TaskFailed > Cancelled > Success > InvalidGraph
```

## Validation evidence

Device-independent tests cover tiling, edge regions, shared resources, progress,
interleaving, cancellation races, failure draining, exact attribution, and
status precedence. Linux Vulkan integration executes an uneven five-workgroup
logical vector workload as `(2, 2, 1)` workgroup slices through Lavapipe. The
shader indexes with `gl_GlobalInvocationID`, output starts at a sentinel, every
element is verified, and the validation callback remains empty. Ordinary
zero-base dispatch remains covered by the standalone executor tests.

## Deferred scope

Milestone 6 does not add public pause/resume, interruption of an active CPU or
Vulkan payload, priority or interchangeable scheduling policies, multiple
Vulkan queues, repeated graph execution, runtime graph submission, general
cancellation tokens, adaptive backend selection, or allocator/pipeline-cache
optimization.
