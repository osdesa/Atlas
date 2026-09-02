# Task lifecycle

Atlas stores lifecycle and progress directly on each graph-private contiguous
task record and exposes detached `TaskSnapshot` values for inspection. The
scheduler control thread owns every transition even when CPU and Vulkan workers
run concurrently.

## States

| State | Meaning |
| --- | --- |
| `Unknown` | The graph is under construction and has no execution state. |
| `Blocked` | At least one dependency has not completed successfully. |
| `Ready` | The task is eligible for scheduler selection. |
| `Running` | The scheduler claimed one CPU payload, ordinary GPU dispatch, or sliced GPU work unit. |
| `Paused` | A sliced GPU unit succeeded and another unit is ready to be selected. This state is scheduler-internal. |
| `Success` | The logical task completed every required work unit successfully. |
| `Failure` | Payload execution failed or accepted work violated the completion contract. |
| `Cancelled` | A request became effective before logical task completion. |

Successful graph finalisation initializes roots as `Ready` and tasks with
dependencies as `Blocked`. The Kahn scheduler applies these transitions:

```text
Unknown --successful graph finalisation-----> Ready or Blocked
Blocked --final dependency succeeds---------> Ready
Ready   --scheduler claims payload----------> Running
Running --ordinary/final unit succeeds------> Success
Running --sliced unit succeeds, units remain> Paused
Paused  --scheduler claims next unit--------> Running
Running --payload or infrastructure fails---> Failure
Ready/Blocked/Paused --request is applied---> Cancelled
Running sliced --unit succeeds, units remain> Cancelled (when requested)
```

`Success`, `Failure`, and `Cancelled` are terminal for one scheduler execution.
The stable ready sets contain backend-neutral handle/priority candidates.
Selection removes the chosen entry and rechecks task state and execution
resource. FIFO head removal is constant-time, and storage is compacted only
when consumed prefixes would otherwise grow the queue. An incomplete sliced
GPU task is placed at the GPU tail. FIFO and
round-robin quantum one therefore interleave logical tasks at work-unit
boundaries, while larger round-robin quanta may retain one task for consecutive
units.

## Scheduling policies

Existing `KahnScheduler` constructors use FIFO. Policy-aware constructors clone
one supplied policy independently for each backend, so CPU selection state never
changes GPU selection state. Policies order only tasks competing for the same
resource; CPU and GPU capacities remain independent.

Static priority selects lower numeric values first and preserves FIFO order for
ties. Priorities are immutable for one graph execution. A newly ready
higher-priority GPU task can run after the current slice completes, but Atlas
does not interrupt an active Vulkan dispatch. Strict priority provides no aging
and may starve ready lower-priority tasks.

For Milestone 9, a task "arrives" when dependency completion changes an
already-declared task from `Blocked` to `Ready`. Runtime graph mutation remains
unsupported. This permits a higher-priority task to intervene at a completed
slice boundary without implying admission of new graph structure or
interruption of active work.

## Ready-residency and bypass measurements

`TaskExecutionInfo::readyWaitDuration` accumulates each interval in which the
task is present and eligible in its CPU or GPU ready set. The first root
interval begins when scheduler parsing enqueues it, not when graph finalisation
sets its public state. Dependency release starts the first interval for a
previously blocked task, and every incomplete slice requeue starts another
interval. Selection, effective cancellation, or scheduler termination closes
an active interval. Blocked time, executor queueing, and payload execution are
excluded.

`TaskExecutionInfo::selectionBypassCount` increments whenever another valid
candidate is selected from the same resource ready set while this task remains
`Ready` or `Paused`. CPU and GPU selections never increment each other's
counts. Stale ready-set entries are ignored. Fail-stop termination closes
intervals for work left ready, preserving useful wait measurements after task,
executor, policy, or cancellation failure.

These statistics expose finite and unbounded starvation pressure; they do not
alter selection. Priorities remain immutable and strict, with no aging or
starvation mitigation.

## Response and scheduler timing

`TaskExecutionInfo::responseDuration` starts with the first scheduler-observed
ready interval and closes when that task reaches `Success`, `Failure`, or
`Cancelled`. It includes all later ready intervals, executor submission and
queueing, payload execution, and sliced-task resumptions. It excludes time spent
`Blocked` before first readiness. A task that never becomes ready or never
reaches a terminal outcome retains an empty response duration.

`SchedulerResult::schedulerActiveDuration` accumulates graph parsing, policy
selection, completion validation, dependency release, cancellation, and
lifecycle bookkeeping. Timing pauses around executor submission calls and
blocking completion waits, so the value is a scheduler-control measurement
rather than graph elapsed time and may overlap backend execution.

`immediateSliceSwitchDuration` and `immediateSliceSwitchCount` cover only an
incomplete GPU task that is the next GPU task accepted for execution. Selecting
another GPU task invalidates that transition's sample. This keeps isolated
slice-turnaround measurements separate from intentional policy intervention.

## Work-unit progress and attribution

Ordinary CPU and GPU tasks have one work unit. A `SlicedVulkanDispatch` reports
the total number of regions needed to cover its logical dimensions. The
scheduler increments `completedWorkUnitCount` only after an attributed successful
completion. `executedTaskCount` increments only after logical `Success`, and
dependants are released only then.

Every completion must match an in-flight task handle, declared resource,
`Running` state, and exact work-unit index. Missing, duplicate, unknown, extra,
stale, skipped, resource-mismatched, or index-mismatched completions are
infrastructure failures and never release dependants.

Host-side execution, ready-wait, response, scheduler-active, and graph elapsed
durations are retained internally at nanosecond resolution. `executionDuration`
excludes executor queue waiting. Ordinary tasks retain the
reported payload duration. Sliced tasks accumulate reported durations across
successful units and include the reported duration of a unit that finishes with
an exception. Progress counts only successful units.

When profiling is compiled in and the selected compute queue supports timestamp
queries, `deviceExecutionDuration` separately accumulates masked, availability-
checked Vulkan device-clock duration. It is not queue wait time and does not
replace the host-observed executor duration.

An attached trace session observes readiness, policy choice, selection,
submission, backend start/end, completion, pause/resume, cancellation, and
terminal transitions. These events are diagnostic observations: best-effort
publication never changes lifecycle state or scheduling decisions. Concurrent
producers can serialize out of sequence-number order, and a trace footer's drop
count must be checked before treating the event stream as exhaustive.

## Cancellation

`KahnScheduler::requestCancellation()` accepts a graph-owned non-terminal task
before or concurrently with `execute()`. Cross-graph, unknown, duplicate,
terminal, and post-execution requests return false. Requests and scheduler
selection share one mutex-protected claim boundary:

- a task requested before it is claimed becomes `Cancelled`;
- a running CPU or ordinary GPU payload cannot be interrupted, so its real
  completion wins and makes the request ineffective;
- a running sliced GPU task completes its current unit, then becomes
  `Cancelled` if work remains; and
- a request during the final sliced unit is ineffective when that unit succeeds,
  because logical completion wins.

Effective cancellation is fail-stop for the graph. The scheduler stops all new
submissions, drains accepted CPU and GPU work, does not release dependants from
the cancelled task, and reports `Cancelled` unless a higher-precedence failure
occurs. Multiple pending requests are applied in graph insertion order.

## Failure and result precedence

The first task failure stops new submissions, preserves the first task exception,
and drains accepted work. Submission rejection, channel or producer failure, and
completion-contract violations are executor infrastructure failures. A thrown
scheduling policy or an invalid selected index is a policy error. Both stop new
submissions and drain accepted work. Useful duration and progress recorded
before a later failure are retained.

`VK_ERROR_DEVICE_LOST` is always an executor infrastructure failure, including
when a CPU callable or Vulkan worker reports it through an attributed task
completion. That task enters `Failure`, its dependants remain blocked, the
shared runtime context rejects later device work, and accepted work on either
resource drains before `execute()` returns. The result retains the first task
exception when one preceded the loss; otherwise it exposes the device-loss
exception.

Final status precedence is:

```text
ExecutorUnavailable > PolicyError > TaskFailed > Cancelled > Success > InvalidGraph
```

This permits already accepted work to reveal a task or infrastructure failure
while the graph is draining after cancellation.

## Executor and access rules

The scheduler borrows initially drained executors and requires exclusive access
to their public interfaces during `execute()`. It never shuts them down.
`SynchronousCpuExecutor` and `VulkanExecutor` report capacity one;
`WorkerpoolExecutor` reports its validated worker count. Executor shutdown
rejects new work, drains accepted work, joins workers, and retains required
standalone completions.

Task execution information is not atomic and must not be inspected or mutated
concurrently with execution. The cancellation API is the exception: its private
request state is synchronized and the scheduler control thread remains the only
writer of `TaskExecutionInfo`.

## Current limitations

- A graph permits exactly one atomically claimed execution; runtime submission and repeated
  execution are unavailable.
- Priorities are static; dynamic changes and starvation mitigation are unavailable.
- Vulkan uses one compute queue and one active dispatch.
- Pause/resume is not public, and an active CPU or Vulkan payload is not
  preempted.
- Graphics, multiple queues, adaptive backend selection, and general
  cancellation tokens remain unavailable.
- Trace output is best-effort rather than a lossless general event log, and
  Vulkan timestamp availability depends on the selected compute queue.
