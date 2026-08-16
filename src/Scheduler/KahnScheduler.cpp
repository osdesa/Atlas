#include "atlas/Scheduler/KahnScheduler.h"

#include <chrono>
#include <memory>
#include <optional>

/**
 * @file KahnScheduler.cpp
 * @brief Defines sequential task-graph execution using Kahn's algorithm.
 */

namespace Atlas
{
    KahnScheduler::KahnScheduler(const TaskGraph& taskGraph) : BaseScheduler{ taskGraph } {}

    SchedulerResult KahnScheduler::execute()
    {
        SchedulerResult result{ .status = SchedulerStatus::Unknown,
                                .executedTaskCount = 0U,
                                .exception = nullptr,
                                .executionTime = std::chrono::microseconds{ 0 } };

        const auto startTime{ std::chrono::steady_clock::now() };

        if (!parseDependencies())
        {
            result.status = SchedulerStatus::InvalidGraph;
        }
        else
        {
            while (const std::optional<std::shared_ptr<const Task>> task{ takeNextReadyTask() })
            {
                const SchedulerResult executeStatus{ executeFunction(task.value()->function) };
                completeTask(task.value(), executeStatus);

                if (executeStatus.status != SchedulerStatus::Success)
                {
                    result.status = executeStatus.status;
                    result.exception = executeStatus.exception;
                    result.executionTime =
                        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - startTime);
                    return result;
                }

                result.executedTaskCount++;
                updateDependencies(task.value());
            }
        }

        if (result.executedTaskCount == startingGraph.getTaskCount())
        {
            result.status = SchedulerStatus::Success;
        }
        else
        {
            result.status = SchedulerStatus::InvalidGraph;
        }

        result.executionTime = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - startTime);
        return result;
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

    void KahnScheduler::completeTask(const std::shared_ptr<const Task>& task, const SchedulerResult& executionResult)
    {
        if (executionResult.status == SchedulerStatus::Success)
        {
            task->executionInfo.state = TaskState::Success;
            task->executionInfo.exception = nullptr;
        }
        else
        {
            task->executionInfo.state = TaskState::Failure;
            task->executionInfo.exception = executionResult.exception;
        }

        task->executionInfo.executionDuration = executionResult.executionTime;
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
