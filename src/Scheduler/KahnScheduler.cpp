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
            // Go through all tasks that can be executed
            while (!readyTasks.empty())
            {
                TaskHandle taskHandle{ readyTasks.front() };
                readyTasks.pop();

                // Must exist
                const std::optional<std::shared_ptr<const Task>> task{ startingGraph.findTask(taskHandle) };

                // Execute the task
                const SchedulerResult executeStatus{ executeFunction(task.value()->function) };

                if (executeStatus.status != SchedulerStatus::Success)
                {
                    result.status = executeStatus.status;
                    result.exception = executeStatus.exception;
                    result.executionTime =
                        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - startTime);
                    return result;
                }

                result.executedTaskCount++;

                // Update tasks which had a dependency on executed task
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

    void KahnScheduler::updateDependencies(const std::shared_ptr<const Task>& executedTask)
    {
        for (TaskHandle taskHandle : executedTask->getDependents())
        {
            std::size_t dependenciesCount{ remainingDependencies.at(taskHandle) };
            dependenciesCount--;

            if (dependenciesCount == 0)
            {
                readyTasks.emplace(taskHandle);
                remainingDependencies.erase(taskHandle);
            }
            else
            {
                remainingDependencies.at(taskHandle) = dependenciesCount;
            }
        }
    }

} // namespace Atlas
