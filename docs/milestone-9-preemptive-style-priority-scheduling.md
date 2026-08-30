# Milestone 9: preemptive-style priority scheduling

## Implementation status

Milestone 9 is implemented. Static priority can intervene between cooperative
Vulkan work units, and Atlas records per-task ready-set residence and selection
bypasses to expose starvation pressure. It does not interrupt an executing CPU
payload or Vulkan dispatch and does not claim hardware preemption.

## Arrival and intervention boundary

Atlas retains finalised, one-execution graphs. For this milestone, task arrival
means an already-declared task changes from `Blocked` to `Ready` after its final
dependency succeeds. No task or edge is added during scheduler execution.

With a capacity-one GPU executor, an incomplete sliced task returns to the GPU
ready-set tail after its current dispatch finishes. If dependency completion
has made a higher-priority GPU task ready, static priority may select it before
the lower-priority task's next unit:

```text
lower[0] -> higher[0] -> lower[1]
```

The lower task resumes at work-unit index one; successful work unit zero is not
re-executed. Intervention happens only at the boundary between independent
Vulkan submissions.

## Ready-wait duration

`TaskExecutionInfo::readyWaitDuration` is an accumulated monotonic host-clock
measurement. A root's first interval starts when scheduler parsing places it in
the matching resource ready set, not when graph finalisation changes its public
state. A dependency-driven readiness transition starts the first interval for a
blocked task, and every incomplete sliced-unit requeue starts another interval.

Selection and effective cancellation close the affected interval. Scheduler
termination closes intervals left open by fail-stop cancellation or task,
policy, executor, and completion-contract failures. The value excludes time
blocked on dependencies, queued inside an executor, or executing a payload.

## Selection-bypass count

`TaskExecutionInfo::selectionBypassCount` counts selections of another valid
candidate while the task remains `Ready` or `Paused` in the same resource ready
set. The scheduler increments all such non-selected candidates after validating
the selected task. Stale entries and candidates for the other backend do not
count.

Bypass accounting applies to every policy. Under strict static priority, a
finite higher-priority backlog therefore produces an exact starvation-exposure
count for the lower-priority task. The metric does not change priorities,
selection order, or policy state.

## Concurrency and ownership

A private ready-accounting helper borrows the graph and runs only on the
scheduler control thread. Executors never mutate `TaskExecutionInfo`. As with
the other execution fields, callers must not inspect ready-wait or bypass values
concurrently with `KahnScheduler::execute()`.

## Validation evidence

Device-independent tests control completion publication so a dependent
higher-priority task becomes ready while the lower slice is still `Running` in
scheduler state. They verify exact `lower[0]`, `higher[0]`, `lower[1]` order,
preserved progress, accumulated ready intervals, and deterministic finite
backlog bypass counts. A real Lavapipe test records the same submission order
while executing and validating actual sliced Vulkan compute. Existing policy,
cancellation, failure, mixed-scheduling, sanitizer, and concurrency suites
remain part of the quality matrix.

## Deferred behavior

Milestone 9 does not add runtime graph mutation, repeated graph execution,
dynamic priority, priority aging, starvation mitigation, public pause/resume,
multiple Vulkan queues, active CPU or Vulkan dispatch interruption, full event
tracing, or benchmarking.
