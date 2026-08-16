# Task lifecycle

Atlas currently stores lifecycle state directly on each graph-owned `Task`.
This is an MVP model for the synchronous Kahn executor, not yet a concurrent
runtime state machine.

## States

| State | Current meaning |
| --- | --- |
| `Unknown` | The graph is still being constructed and no execution state has been assigned. |
| `Blocked` | At least one dependency has not completed successfully. |
| `Ready` | The task is eligible for selection by the current scheduler. |
| `Running` | The scheduler has selected the task and its callable is executing. |
| `Success` | The callable returned normally and a successful `TaskResult` exists. |
| `Failure` | The callable threw and a failed `TaskResult` contains its exception. |
| `Cancelled` | The task must not be selected for execution. Full cancellation behavior is not implemented. |

## Current scheduler behavior

Successful graph finalisation directly initializes roots as `Ready` and tasks
with dependencies as `Blocked`. The FIFO Kahn scheduler then applies these state
changes:

```text
Blocked -> Ready
Ready -> Running
Running -> Success
Running -> Failure
```

The scheduler is responsible for choosing and applying its state changes. `Task`
does not enforce a universal transition matrix, allowing future schedulers and
cooperative GPU execution to define behavior appropriate to their execution
model. Scheduler tests are therefore responsible for verifying their observable
lifecycle behavior.

The ready queue stores task handles in FIFO order. Queue membership alone is not
enough to execute a task: selection rechecks that its current state is `Ready`.
This allows stale, cancelled, or already completed entries to be skipped without
changing the queue container.

## Results and failure

`Task::result` is empty until the scheduler records a terminal outcome. Success
and failure results contain the affected `TaskHandle`; failure also retains the
exception thrown by the task callable. The graph-level `SchedulerResult` still
reports total successful completions, elapsed time, and the first captured task
exception.

Execution stops at the first failure. Tasks that still depend on the failed task
remain `Blocked` and have no task result.

## Current limitations

- Scheduling is synchronous and single-threaded; state and result are not atomic.
- A graph is intended for one execution and completed tasks are not run again.
- Priority and execution-resource intent do not affect FIFO selection or dispatch.
- Both CPU- and GPU-designated tasks currently run host callables.
- Cancellation has no submission API, terminal result creation, graph-level
  status, or dependent-task propagation policy.
- Mutating lifecycle state through a retained task view is possible; callers and
  scheduler implementations are responsible for maintaining coherent state and
  result values.
