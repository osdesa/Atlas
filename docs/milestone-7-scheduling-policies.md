# Milestone 7: Scheduling policies

Milestone 7 separates ready-task selection from graph execution and provides
FIFO, configurable work-unit round-robin, and stable static-priority policies.
The resource-aware Kahn scheduler still owns dependency accounting, task state,
executor submission, completion validation, cancellation, and failure draining.

## Policy boundary

`SchedulingPolicy` receives a non-empty, stable enqueue-ordered span of
`SchedulingCandidate` values. A candidate contains only a graph-scoped task
handle and immutable static priority. Policies cannot inspect or mutate task
payloads, execution state, dependencies, executors, or Vulkan objects.

The scheduler clones one supplied policy independently for CPU and GPU ready
work. This retains one configuration while preventing stateful round-robin
selection on one resource from affecting the other. Existing constructors use
FIFO and remain source compatible.

Policies return the index of one candidate. The scheduler validates the index,
removes that candidate, rechecks its current state and declared resource, then
claims and submits it. A policy is never called with an empty ready set.

## Built-in policies

### FIFO

`FifoSchedulingPolicy` selects index zero. Newly ready tasks and incomplete GPU
tasks are appended to the matching resource ready set. Existing FIFO execution
order is therefore preserved.

### Round-robin

`RoundRobinSchedulingPolicy` accepts a positive work-unit quantum. It retains a
logical task for up to that many consecutive selections while the task remains
ready. A task that completes or stops being ready relinquishes the remainder of
its quantum. Quantum one matches FIFO work-unit rotation.

CPU and ordinary GPU tasks contain one work unit, so larger quanta materially
affect only cooperatively sliced GPU tasks in the current architecture. Quantum
zero is rejected during policy construction.

### Static priority

`StaticPrioritySchedulingPolicy` selects the lowest numeric priority. Equal
priorities retain stable FIFO ordering. An incomplete slice returns to the ready
tail, allowing equal-priority sliced tasks to rotate.

A newly ready higher-priority GPU task may run before a lower-priority task
receives its next slice. Intervention occurs only after the active dispatch
finishes; Atlas never claims to interrupt an executing Vulkan dispatch. Static
priority is strict, has no aging, and can starve lower-priority work.

## Failure contract

A thrown selection or an index outside the supplied candidate span is a
`SchedulerStatus::PolicyError`. The scheduler stops new submissions, drains all
accepted work, and does not release dependants because of the invalid decision.
Executor failure has higher result precedence than policy failure; policy
failure has higher precedence than task failure and cancellation.

`SchedulerResult::exception` preserves the first task exception discovered
while draining. If no task failed, it contains the policy exception. An invalid
index produces an `std::out_of_range` exception. A policy clone must be non-null;
a null clone is rejected by the scheduler constructor.

## Deferred behavior

This milestone does not add runtime task submission, repeated graph execution,
dynamic priorities, priority aging, weighted fair scheduling, shortest-job or
deadline policies, multiple Vulkan queues, benchmarking, or tracing. CPU work
and active Vulkan dispatches remain non-preemptible.

## Validation

Device-independent tests cover policy semantics, stable ties, quantum behavior,
independent clones, policy contract violations, failure precedence, draining,
dependency blocking, and exact sliced submission order. Linux integration tests
run real sliced Vulkan compute under every built-in policy using Lavapipe.
