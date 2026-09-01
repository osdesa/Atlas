#include "atlas/Tasking/TaskGraph.h"

#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>

/**
 * @file TaskGraph.cpp
 * @brief Defines task graph construction, validation, and lookup.
 */

namespace Atlas
{
    std::optional<TaskHandle> TaskGraph::addCpuTask(TaskFunction taskFunction, TaskOptions taskOptions)
    {
        if (taskOptions.executionResource != ExecutionResource::CPU)
        {
            return std::nullopt;
        }
        return addTaskWork(TaskWork{ std::move(taskFunction) }, std::move(taskOptions));
    }

    std::optional<TaskHandle> TaskGraph::addGpuTask(VulkanDispatch dispatch, TaskOptions taskOptions)
    {
        if (taskOptions.executionResource != ExecutionResource::GPU)
        {
            return std::nullopt;
        }
        return addTaskWork(TaskWork{ std::move(dispatch) }, std::move(taskOptions));
    }

    std::optional<TaskHandle> TaskGraph::addGpuTask(SlicedVulkanDispatch dispatch, TaskOptions taskOptions)
    {
        if (taskOptions.executionResource != ExecutionResource::GPU)
        {
            return std::nullopt;
        }
        return addTaskWork(TaskWork{ std::move(dispatch) }, std::move(taskOptions));
    }

    std::optional<TaskHandle> TaskGraph::addTaskWork(TaskWork work, TaskOptions taskOptions)
    {
        if (isFinalised || !taskOptions.isValid())
        {
            return std::nullopt;
        }

        const std::optional<TaskHandle> taskHandle{ taskIdGenerator.next() };
        if (!taskHandle.has_value())
        {
            return std::nullopt;
        }

        std::shared_ptr<Task> task;
        if (std::holds_alternative<TaskFunction>(work))
        {
            task = std::make_shared<Task>(taskHandle.value(), std::move(std::get<TaskFunction>(work)), std::move(taskOptions));
        }
        else if (std::holds_alternative<VulkanDispatch>(work))
        {
            task = std::make_shared<Task>(taskHandle.value(), std::move(std::get<VulkanDispatch>(work)), std::move(taskOptions));
        }
        else
        {
            task = std::make_shared<Task>(taskHandle.value(), std::move(std::get<SlicedVulkanDispatch>(work)), std::move(taskOptions));
        }

        tasks.emplace_back(task);
        try
        {
            const bool inserted{ taskIndex.emplace(taskHandle.value(), std::move(task)).second };
            if (!inserted)
            {
                tasks.pop_back();
                return std::nullopt;
            }
        }
        catch (...)
        {
            tasks.pop_back();
            throw;
        }
        return taskHandle;
    }

    std::vector<TaskHandle> TaskGraph::getTaskHandles() const
    {
        std::vector<TaskHandle> taskHandles;
        taskHandles.reserve(tasks.size());

        for (const std::shared_ptr<Task>& task : tasks)
        {
            taskHandles.emplace_back(task->handle);
        }

        return taskHandles;
    }

    bool TaskGraph::addDependency(TaskHandle dependent, TaskHandle dependency)
    {
        if (isFinalised)
        {
            return false;
        }

        if (!validTaskLink(dependent, dependency))
        {
            return false;
        }

        const std::optional<std::shared_ptr<Task>> dependentTask{ findMutableTask(dependent) };
        const std::optional<std::shared_ptr<Task>> dependencyTask{ findMutableTask(dependency) };

        if (!dependentTask.has_value() || !dependencyTask.has_value())
        {
            return false;
        }

        if (checkForCycles(dependent, dependency))
        {
            return false;
        }

        return addTaskLink(dependentTask.value(), dependent, dependencyTask.value(), dependency);
    }

    bool TaskGraph::addTaskLink(const std::shared_ptr<Task>& dependentTask, TaskHandle dependent,
                                const std::shared_ptr<Task>& dependencyTask, TaskHandle dependency)
    {
        if (!dependentTask->addDependency(dependency))
        {
            return false;
        }

        try
        {
            if (!dependencyTask->addDependent(dependent))
            {
                dependentTask->removeDependency(dependency);
                return false;
            }
        }
        catch (...)
        {
            dependentTask->removeDependency(dependency);
            throw;
        }

        return true;
    }

    bool TaskGraph::finishTaskGraph()
    {
        if (isFinalised)
        {
            return true;
        }

        std::unordered_map<TaskHandle, std::size_t, TaskHandle::Hash> remainingDependencies;
        remainingDependencies.reserve(tasks.size());
        std::vector<TaskHandle> ready;
        ready.reserve(tasks.size());
        for (const std::shared_ptr<Task>& task : tasks)
        {
            const std::size_t dependencyCount{ task->getDependencies().size() };
            remainingDependencies.emplace(task->handle, dependencyCount);
            if (dependencyCount == 0U)
            {
                ready.emplace_back(task->handle);
            }
        }

        std::size_t processedCount{ 0U };
        for (std::size_t readyIndex{ 0U }; readyIndex < ready.size(); ++readyIndex)
        {
            const std::optional<std::shared_ptr<const Task>> task{ findTask(ready.at(readyIndex)) };
            if (!task.has_value())
            {
                return false;
            }
            ++processedCount;
            for (const TaskHandle dependent : task.value()->getDependents())
            {
                auto remaining{ remainingDependencies.find(dependent) };
                if (remaining == remainingDependencies.end() || remaining->second == 0U)
                {
                    return false;
                }
                --remaining->second;
                if (remaining->second == 0U)
                {
                    ready.emplace_back(dependent);
                }
            }
        }

        isFinalised = !ready.empty() && processedCount == tasks.size();

        if (isFinalised)
        {
            setInitialStateForTasks();
        }

        return isFinalised;
    }

    bool TaskGraph::checkForCycles(TaskHandle dependent, TaskHandle dependency) const
    {
        std::vector<TaskHandle> pending{ dependency };
        std::unordered_set<TaskHandle, TaskHandle::Hash> visited;
        visited.reserve(tasks.size());

        while (!pending.empty())
        {
            const TaskHandle current{ pending.back() };
            pending.pop_back();

            if (current == dependent)
            {
                return true;
            }

            if (!visited.emplace(current).second)
            {
                continue;
            }

            const std::optional<std::shared_ptr<const Task>> task{ findTask(current) };
            if (!task.has_value())
            {
                continue;
            }

            for (const TaskHandle next : task.value()->getDependencies())
            {
                pending.emplace_back(next);
            }
        }

        return false;
    }

    void TaskGraph::setInitialStateForTasks() noexcept
    {
        for (const std::shared_ptr<Task>& task : tasks)
        {
            if (task->getDependencies().empty())
            {
                task->executionInfo.state = TaskState::Ready;
            }
            else
            {
                task->executionInfo.state = TaskState::Blocked;
            }
        }
    }

    bool TaskGraph::validTaskLink(TaskHandle dependent, TaskHandle dependency) const noexcept
    {
        const bool validHandles{ dependent.isValid() && dependency.isValid() };
        const bool sameGraph{ dependent.getGraphID() == graphID && dependency.getGraphID() == graphID };
        const bool differentTasks{ dependent != dependency };

        return validHandles && sameGraph && differentTasks;
    }

    std::optional<std::shared_ptr<Task>> TaskGraph::findMutableTask(TaskHandle taskHandle) noexcept
    {
        const auto taskIt{ taskIndex.find(taskHandle) };
        if (taskIt == taskIndex.end())
        {
            return std::nullopt;
        }
        return taskIt->second;
    }

    std::optional<std::shared_ptr<const Task>> TaskGraph::findTask(TaskHandle taskHandle) const noexcept
    {
        const auto taskIt{ taskIndex.find(taskHandle) };
        if (taskIt == taskIndex.end())
        {
            return std::nullopt;
        }
        return std::shared_ptr<const Task>{ taskIt->second };
    }

} // namespace Atlas
