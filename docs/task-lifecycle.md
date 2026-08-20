# Task lifecycle

Atlas currently stores lifecycle state directly on each graph-owned `Task`.
The scheduler control thread owns state transitions even when a worker pool runs
multiple task callables concurrently.

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
and callable duration. Ready tasks are submitted until the executor's
`maxConcurrency()` capacity is full, and completions may arrive in a different
order from submission. Dependencies are released only by successful
completions.

The scheduler borrows an initially drained executor and requires exclusive
access to its public interface during `execute()`. It does not shut the executor
down. `SynchronousCpuExecutor` reports capacity one;
`WorkerpoolExecutor` reports its validated, non-zero worker count.

## Execution information and failure

`TaskExecutionInfo` is the single source of runtime state for a task. It also
stores the exception thrown by the callable, when applicable, and the duration
spent executing the callable. Static submission metadata remains separate in
`TaskOptions`. The graph-level `SchedulerResult` reports total successful
completions, graph elapsed time, and the first captured task exception.

The first callable failure disables new submissions, but work already accepted
is drained and every real completion is applied. The result preserves the first
observed task exception and counts successful completions rather than
submissions. Dependants that were not released before failure remain `Blocked`.

Callable failure is reported as `TaskFailed`. Submission rejection or a missing,
unknown, duplicate, extra, or mismatched completion is an executor
infrastructure failure and is reported as `ExecutorUnavailable`. Infrastructure
status takes precedence if both kinds of failure are observed.

## Executor lifecycle

`WorkerpoolExecutor` owns its work queue, completion queue, synchronization, and
threads. Shutdown stops acceptance, drains queued and running work, joins all
workers, and retains produced completions for retrieval. Repeated shutdown calls
are safe. `SynchronousCpuExecutor` has no worker lifetime to join but follows the
same rejection and completion-retention contract.

Executor public calls are serialized by their caller. Worker callables may run
at the same time, so captured references and other shared application resources
require application-owned synchronization. Worker threads never access
`TaskExecutionInfo`.

## Current limitations

- Scheduler control is single-threaded, but worker callables may execute concurrently.
- Execution information is not atomic and must not be accessed concurrently with execution.
- A graph is intended for one execution and completed tasks are not run again.
- One executor must not contain unrelated accepted work or queued completions
  when lent to `KahnScheduler`.
- Priority and execution-resource intent do not affect FIFO selection or dispatch.
- Both CPU- and GPU-designated tasks currently run host callables.
- Atlas does not currently define task cancellation; it will be introduced only
  alongside a concrete cancellation API and execution policy.
- Mutating execution information through a retained task view is possible;
  callers and scheduler implementations are responsible for maintaining
  coherent values.
