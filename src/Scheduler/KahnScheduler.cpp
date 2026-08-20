#include "atlas/Scheduler/KahnScheduler.h"

#include <chrono>
#include <memory>
#include <optional>

/**
 * @file KahnScheduler.cpp
 * @brief Defines capacity-aware task-graph execution using Kahn's algorithm.
 */

namespace Atlas
{
    KahnScheduler::KahnScheduler(const TaskGraph& taskGraph, CpuExecutor& executor)
        : BaseScheduler{ taskGraph }, cpuExecutor{ executor }
    {
    }

    SchedulerResult KahnScheduler::execute()
    {
        SchedulerResult result{ .status = SchedulerStatus::Unknown,
                                .executedTaskCount = 0U,
                                .exception = nullptr,
                                .executionTime = std::chrono::microseconds{ 0 } };

        const auto startTime{ std::chrono::steady_clock::now() };
        ExecutionState state{ .executorCapacity = static_cast<std::size_t>(cpuExecutor.maxConcurrency()) };

        inFlightTasks.clear();
        inFlightTasks.reserve(state.executorCapacity);

        if (!parseDependencies())
        {
            result.status = SchedulerStatus::InvalidGraph;
        }
        else
        {
            if (state.executorCapacity == 0U)
            {
                state.executorFailure = true;
            }
            else
            {
                runExecutorLoop(state);
            }

            result.status = determineStatus(state);
            result.executedTaskCount = state.successfulTaskCount;
            result.exception = state.firstTaskException;
        }

        result.executionTime = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - startTime);
        return result;
    }

    void KahnScheduler::runExecutorLoop(ExecutionState& state)
    {
        while (true)
        {
            submitReadyTasks(state);

            if (inFlightTasks.empty() || !processNextCompletion(state))
            {
                break;
            }
        }

        if (!state.completionStreamEndedEarly && cpuExecutor.waitForCompletion().has_value())
        {
            state.executorFailure = true;
        }
    }

    void KahnScheduler::submitReadyTasks(ExecutionState& state)
    {
        while (state.submissionsEnabled && inFlightTasks.size() < state.executorCapacity)
        {
            const std::optional<std::shared_ptr<const Task>> task{ takeNextReadyTask() };

            if (!task.has_value())
            {
                return;
            }

            if (!cpuExecutor.submit(task.value()->handle, task.value()->function))
            {
                task.value()->executionInfo.state = TaskState::Ready;
                task.value()->executionInfo.exception = nullptr;
                task.value()->executionInfo.executionDuration = std::chrono::microseconds{ 0 };
                state.executorFailure = true;
                state.submissionsEnabled = false;
                return;
            }

            inFlightTasks.emplace(task.value()->handle);
        }
    }

    bool KahnScheduler::processNextCompletion(ExecutionState& state)
    {
        const std::optional<TaskCompletion> completion{ cpuExecutor.waitForCompletion() };

        if (!completion.has_value())
        {
            handleMissingCompletion(state);
            return false;
        }

        processReceivedCompletion(completion.value(), state);
        return true;
    }

    void KahnScheduler::handleMissingCompletion(ExecutionState& state)
    {
        state.executorFailure = true;
        state.completionStreamEndedEarly = true;
        failUnresolvedInFlightTasks();
        inFlightTasks.clear();
    }

    void KahnScheduler::processReceivedCompletion(const TaskCompletion& completion, ExecutionState& state)
    {
        const std::optional<std::shared_ptr<const Task>> task{ resolveCompletedTask(completion, state) };

        if (task.has_value())
        {
            recordCompletionOutcome(task.value(), completion, state);
        }
    }

    std::optional<std::shared_ptr<const Task>> KahnScheduler::resolveCompletedTask(const TaskCompletion& completion,
                                                                                   ExecutionState& state)
    {
        const auto inFlightEntry{ inFlightTasks.find(completion.handle) };
        if (inFlightEntry == inFlightTasks.end())
        {
            state.executorFailure = true;
            state.submissionsEnabled = false;
            return std::nullopt;
        }

        inFlightTasks.erase(inFlightEntry);
        std::optional<std::shared_ptr<const Task>> task{ startingGraph.findTask(completion.handle) };
        if (!task.has_value() || task.value()->executionInfo.state != TaskState::Running)
        {
            state.executorFailure = true;
            state.submissionsEnabled = false;
            return std::nullopt;
        }

        return task;
    }

    void KahnScheduler::recordCompletionOutcome(const std::shared_ptr<const Task>& task, const TaskCompletion& completion,
                                                ExecutionState& state)
    {
        completeTask(task, completion);

        if (completion.succeeded())
        {
            state.successfulTaskCount++;
            if (state.submissionsEnabled)
            {
                updateDependencies(task);
            }
        }
        else
        {
            state.taskFailureObserved = true;
            state.submissionsEnabled = false;
            if (state.firstTaskException == nullptr)
            {
                state.firstTaskException = completion.exception;
            }
        }
    }

    SchedulerStatus KahnScheduler::determineStatus(const ExecutionState& state) const noexcept
    {
        if (state.executorFailure)
        {
            return SchedulerStatus::ExecutorUnavailable;
        }

        if (state.taskFailureObserved)
        {
            return SchedulerStatus::TaskFailed;
        }

        if (state.successfulTaskCount == startingGraph.getTaskCount())
        {
            return SchedulerStatus::Success;
        }

        return SchedulerStatus::InvalidGraph;
    }

    bool KahnScheduler::parseDependencies()
    {
        readyTasks = {};
        remainingDependencies.clear();

        for (const TaskHandle taskHandle : startingGraph.getTaskHandles())
        {
            const std::optional<std::shared_ptr<const Task>> task{ startingGraph.findTask(taskHandle) };

            if (!task.has_value())
            {
                return false;
            }

            const std::size_t dependenciesCount{ task.value()->getDependencies().size() };

            if (dependenciesCount == 0)
            {
                readyTasks.emplace(taskHandle);
            }
            else
            {
                remainingDependencies.emplace(taskHandle, dependenciesCount);
            }
        }

        return true;
    }

    void KahnScheduler::enqueueReadyTask(TaskHandle taskHandle)
    {
        const std::optional<std::shared_ptr<const Task>> task{ startingGraph.findTask(taskHandle) };
        task.value()->executionInfo.state = TaskState::Ready;
        readyTasks.emplace(taskHandle);
    }

    std::optional<std::shared_ptr<const Task>> KahnScheduler::takeNextReadyTask()
    {
        while (!readyTasks.empty())
        {
            const TaskHandle taskHandle{ readyTasks.front() };
            readyTasks.pop();

            const std::optional<std::shared_ptr<const Task>> task{ startingGraph.findTask(taskHandle) };
            if (!task.has_value() || task.value()->executionInfo.state != TaskState::Ready)
            {
                continue;
            }

            task.value()->executionInfo.state = TaskState::Running;
            task.value()->executionInfo.exception = nullptr;
            task.value()->executionInfo.executionDuration = std::chrono::microseconds{ 0 };
            return task;
        }

        return std::nullopt;
    }

    void KahnScheduler::completeTask(const std::shared_ptr<const Task>& task, const TaskCompletion& completion)
    {
        if (completion.succeeded())
        {
            task->executionInfo.state = TaskState::Success;
            task->executionInfo.exception = nullptr;
        }
        else
        {
            task->executionInfo.state = TaskState::Failure;
            task->executionInfo.exception = completion.exception;
        }

        task->executionInfo.executionDuration = completion.executionDuration;
    }

    void KahnScheduler::failUnresolvedInFlightTasks()
    {
        for (const TaskHandle taskHandle : inFlightTasks)
        {
            const std::optional<std::shared_ptr<const Task>> task{ startingGraph.findTask(taskHandle) };
            if (task.has_value() && task.value()->executionInfo.state == TaskState::Running)
            {
                task.value()->executionInfo.state = TaskState::Failure;
                task.value()->executionInfo.exception = nullptr;
                task.value()->executionInfo.executionDuration = std::chrono::microseconds{ 0 };
            }
        }
    }

    void KahnScheduler::updateDependencies(const std::shared_ptr<const Task>& executedTask)
    {
        for (TaskHandle taskHandle : executedTask->getDependents())
        {
            std::size_t dependenciesCount{ remainingDependencies.at(taskHandle) };
            dependenciesCount--;

            if (dependenciesCount == 0)
            {
                enqueueReadyTask(taskHandle);
                remainingDependencies.erase(taskHandle);
            }
            else
            {
                remainingDependencies.at(taskHandle) = dependenciesCount;
            }
        }
    }

} // namespace Atlas
