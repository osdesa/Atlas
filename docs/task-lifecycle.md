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
| `Success` | The callable returned normally with no captured exception. |
| `Failure` | Execution failed, or the executor accepted the task but did not return its matching completion. A callable exception is retained when one exists. |

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
This allows stale or already completed entries to be skipped without changing
the queue container.

`KahnScheduler` marks a selected task `Running` before submitting it to its
borrowed CPU executor. A rejected submission restores the task to `Ready`. A
valid completion changes it to `Success` or `Failure` and supplies its exception
and callable duration. If accepted work produces no completion for the selected
handle, the scheduler cannot safely resubmit it; the task becomes `Failure`, the
graph result is `ExecutorUnavailable`, and its dependants remain `Blocked`.

## Execution information and failure

`TaskExecutionInfo` is the single source of runtime state for a task. It also
stores the exception thrown by the callable, when applicable, and the duration
spent executing the callable. Static submission metadata remains separate in
`TaskOptions`. The graph-level `SchedulerResult` reports total successful
completions, graph elapsed time, and the first captured task exception.

Execution stops at the first task or executor failure. Tasks that still depend
on the affected task remain `Blocked`, retain an empty exception pointer, and
are not executed.

Callable failure is reported as `TaskFailed`. Submission rejection or a missing
or mismatched completion is an executor infrastructure failure and is reported
as `ExecutorUnavailable` instead.

## Current limitations

- Scheduling is synchronous and single-threaded; execution information is not atomic.
- A graph is intended for one execution and completed tasks are not run again.
- Priority and execution-resource intent do not affect FIFO selection or dispatch.
- Both CPU- and GPU-designated tasks currently run host callables.
- Atlas does not currently define task cancellation; it will be introduced only
  alongside a concrete cancellation API and execution policy.
- Mutating execution information through a retained task view is possible;
  callers and scheduler implementations are responsible for maintaining
  coherent values.
