#include "atlas/Tasking/TaskGraph.h"

#include <algorithm>
#include <optional>
#include <utility>

/**
 * @file TaskGraph.cpp
 * @brief Defines task graph construction, validation, and lookup.
 */

namespace Atlas
{
    bool TaskGraph::TaskRecord::isValid() const noexcept
    {
        const bool payloadMatchesResource{
            (std::holds_alternative<TaskFunction>(work) && options.executionResource == ExecutionResource::CPU) ||
            ((std::holds_alternative<VulkanDispatch>(work) || std::holds_alternative<SlicedVulkanDispatch>(work)) &&
             options.executionResource == ExecutionResource::GPU)
        };
        return handle.isValid() && options.isValid() && payloadMatchesResource;
    }

    const TaskFunction* TaskGraph::TaskRecord::cpuFunction() const noexcept
    {
        return std::get_if<TaskFunction>(&work);
    }

    const VulkanDispatch* TaskGraph::TaskRecord::gpuDispatch() const noexcept
    {
        return std::get_if<VulkanDispatch>(&work);
    }

    const SlicedVulkanDispatch* TaskGraph::TaskRecord::slicedGpuDispatch() const noexcept
    {
        return std::get_if<SlicedVulkanDispatch>(&work);
    }

    std::span<const TaskHandle> TaskGraph::TaskRecord::getDependencies() const noexcept
    {
        return dependencies;
    }

    std::span<const TaskHandle> TaskGraph::TaskRecord::getDependents() const noexcept
    {
        return dependents;
    }

    bool TaskGraph::TaskRecord::addDependency(const TaskHandle dependency)
    {
        if (std::find(dependencies.begin(), dependencies.end(), dependency) != dependencies.end())
        {
            return false;
        }
        dependencies.emplace_back(dependency);
        return true;
    }

    bool TaskGraph::TaskRecord::addDependent(const TaskHandle dependent)
    {
        if (std::find(dependents.begin(), dependents.end(), dependent) != dependents.end())
        {
            return false;
        }
        dependents.emplace_back(dependent);
        return true;
    }

    void TaskGraph::TaskRecord::removeDependency(const TaskHandle dependency) noexcept
    {
        const auto entry{ std::find(dependencies.begin(), dependencies.end(), dependency) };
        if (entry != dependencies.end())
        {
            dependencies.erase(entry);
        }
    }

    void TaskGraph::TaskRecord::removeDependent(const TaskHandle dependent) noexcept
    {
        const auto entry{ std::find(dependents.begin(), dependents.end(), dependent) };
        if (entry != dependents.end())
        {
            dependents.erase(entry);
        }
    }

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
        if (lifecycle.load(std::memory_order_acquire) != Lifecycle::Building || !taskOptions.isValid())
        {
            return std::nullopt;
        }

        if (!graphID.isValid() || nextTaskId == INVALID_TASK_ID_VALUE)
        {
            return std::nullopt;
        }
        const TaskHandle taskHandle{ TaskId{ nextTaskId++ }, graphID };

        tasks.emplace_back(taskHandle, std::move(work), std::move(taskOptions));
        return taskHandle;
    }

    std::vector<TaskHandle> TaskGraph::getTaskHandles() const
    {
        std::vector<TaskHandle> taskHandles;
        taskHandles.reserve(tasks.size());

        for (const TaskRecord& task : tasks)
        {
            taskHandles.emplace_back(task.handle);
        }

        return taskHandles;
    }

    bool TaskGraph::addDependency(TaskHandle dependent, TaskHandle dependency)
    {
        if (lifecycle.load(std::memory_order_acquire) != Lifecycle::Building)
        {
            return false;
        }

        if (!validTaskLink(dependent, dependency))
        {
            return false;
        }

        TaskRecord* const dependentTask{ findTaskRecord(dependent) };
        TaskRecord* const dependencyTask{ findTaskRecord(dependency) };
        if (dependentTask == nullptr || dependencyTask == nullptr)
        {
            return false;
        }

        return addTaskLink(*dependentTask, dependent, *dependencyTask, dependency);
    }

    bool TaskGraph::removeDependency(const TaskHandle dependent, const TaskHandle dependency)
    {
        if (lifecycle.load(std::memory_order_acquire) != Lifecycle::Building || !validTaskLink(dependent, dependency))
        {
            return false;
        }
        TaskRecord* const dependentTask{ findTaskRecord(dependent) };
        TaskRecord* const dependencyTask{ findTaskRecord(dependency) };
        if (dependentTask == nullptr || dependencyTask == nullptr)
        {
            return false;
        }
        if (std::find(dependentTask->getDependencies().begin(), dependentTask->getDependencies().end(), dependency) ==
            dependentTask->getDependencies().end())
        {
            return false;
        }
        dependentTask->removeDependency(dependency);
        dependencyTask->removeDependent(dependent);
        return true;
    }

    bool TaskGraph::addTaskLink(TaskRecord& dependentTask, const TaskHandle dependent, TaskRecord& dependencyTask,
                                const TaskHandle dependency)
    {
        if (!dependentTask.addDependency(dependency))
        {
            return false;
        }

        try
        {
            if (!dependencyTask.addDependent(dependent))
            {
                dependentTask.removeDependency(dependency);
                return false;
            }
        }
        catch (...)
        {
            dependentTask.removeDependency(dependency);
            throw;
        }

        return true;
    }

    bool TaskGraph::finishTaskGraph()
    {
        if (lifecycle.load(std::memory_order_acquire) != Lifecycle::Building)
        {
            return true;
        }

        std::vector<std::size_t> remainingDependencies(tasks.size());
        std::vector<std::size_t> ready;
        ready.reserve(tasks.size());
        for (std::size_t index{ 0U }; index < tasks.size(); ++index)
        {
            const std::size_t dependencyCount{ tasks.at(index).getDependencies().size() };
            remainingDependencies.at(index) = dependencyCount;
            if (dependencyCount == 0U)
            {
                ready.emplace_back(index);
            }
        }

        std::size_t processedCount{ 0U };
        for (std::size_t readyIndex{ 0U }; readyIndex < ready.size(); ++readyIndex)
        {
            const TaskRecord& task{ tasks.at(ready.at(readyIndex)) };
            ++processedCount;
            for (const TaskHandle dependent : task.getDependents())
            {
                const std::size_t dependentIndex{ static_cast<std::size_t>(dependent.getTaskID().getValue() - 1U) };
                if (dependentIndex >= remainingDependencies.size() || remainingDependencies.at(dependentIndex) == 0U)
                {
                    return false;
                }
                --remainingDependencies.at(dependentIndex);
                if (remainingDependencies.at(dependentIndex) == 0U)
                {
                    ready.emplace_back(dependentIndex);
                }
            }
        }

        const bool finalised{ !ready.empty() && processedCount == tasks.size() };

        if (finalised)
        {
            setInitialStateForTasks();
            lifecycle.store(Lifecycle::Finalised, std::memory_order_release);
        }

        return finalised;
    }

    void TaskGraph::setInitialStateForTasks() noexcept
    {
        for (TaskRecord& task : tasks)
        {
            if (task.getDependencies().empty())
            {
                task.executionInfo.state = TaskState::Ready;
            }
            else
            {
                task.executionInfo.state = TaskState::Blocked;
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

    TaskGraph::TaskRecord* TaskGraph::findTaskRecord(const TaskHandle taskHandle) noexcept
    {
        if (!taskHandle.isValid() || taskHandle.getGraphID() != graphID)
        {
            return nullptr;
        }
        const std::size_t index{ static_cast<std::size_t>(taskHandle.getTaskID().getValue() - 1U) };
        return index < tasks.size() ? &tasks.at(index) : nullptr;
    }

    const TaskGraph::TaskRecord* TaskGraph::findTaskRecord(const TaskHandle taskHandle) const noexcept
    {
        if (!taskHandle.isValid() || taskHandle.getGraphID() != graphID)
        {
            return nullptr;
        }
        const std::size_t index{ static_cast<std::size_t>(taskHandle.getTaskID().getValue() - 1U) };
        return index < tasks.size() ? &tasks.at(index) : nullptr;
    }

    std::optional<TaskSnapshot> TaskGraph::snapshotTask(const TaskHandle taskHandle) const
    {
        const TaskRecord* const task{ findTaskRecord(taskHandle) };
        if (task == nullptr)
        {
            return std::nullopt;
        }
        return TaskSnapshot{ task->handle,
                             task->options,
                             task->executionInfo,
                             { task->getDependencies().begin(), task->getDependencies().end() },
                             { task->getDependents().begin(), task->getDependents().end() } };
    }

    bool TaskGraph::tryBeginExecution() const noexcept
    {
        Lifecycle expected{ Lifecycle::Finalised };
        return lifecycle.compare_exchange_strong(expected, Lifecycle::Executing, std::memory_order_acq_rel, std::memory_order_acquire);
    }

    void TaskGraph::markExecutionFinished() const noexcept
    {
        lifecycle.store(Lifecycle::Executed, std::memory_order_release);
    }

} // namespace Atlas
