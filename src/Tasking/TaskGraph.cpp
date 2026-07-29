#include "atlas/Tasking/TaskGraph.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <utility>

namespace Atlas
{
    std::optional<TaskHandle> TaskGraph::addTask(TaskFunction taskFunction, TaskOptions taskOptions)
    {
        const std::optional<TaskHandle> taskHandle{ taskIdGenerator.next() };
        if (!taskHandle.has_value())
        {
            return std::nullopt;
        }

        tasks.emplace_back(std::make_shared<Task>(taskHandle.value(), std::move(taskFunction), std::move(taskOptions)));
        return taskHandle;
    }

    std::vector<TaskHandle> TaskGraph::getTaskHandles() const
    {
        std::vector<TaskHandle> taskHandles;
        taskHandles.reserve(tasks.size());

        for (const std::shared_ptr<Task>& task : tasks)
        {
            taskHandles.emplace_back(task->getHandle());
        }

        return taskHandles;
    }

    bool TaskGraph::addDependency(TaskHandle dependent, TaskHandle dependency)
    {
        if (!validTaskLink(dependent, dependency))
        {
            return false;
        }

        const std::optional<std::shared_ptr<Task>> dependentTask{ findTask(dependent) };
        const std::optional<std::shared_ptr<Task>> dependencyTask{ findTask(dependency) };

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

    bool TaskGraph::checkForCycles(TaskHandle dependent, TaskHandle dependency) const
    {
        std::vector<TaskHandle> pending{ dependency };
        std::vector<TaskHandle> visited;

        while (!pending.empty())
        {
            const TaskHandle current{ pending.back() };
            pending.pop_back();

            if (current == dependent)
            {
                return true;
            }

            if (std::find(visited.begin(), visited.end(), current) != visited.end())
            {
                continue;
            }

            visited.emplace_back(current);

            const std::optional<std::shared_ptr<Task>> task{ findTask(current) };
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

    bool TaskGraph::validTaskLink(TaskHandle dependent, TaskHandle dependency) const noexcept
    {
        const bool validHandles{ dependent.isValid() && dependency.isValid() };
        const bool sameGraph{ dependent.getGraphID() == graphID && dependency.getGraphID() == graphID };
        const bool differentTasks{ dependent != dependency };

        return validHandles && sameGraph && differentTasks;
    }

    std::optional<std::shared_ptr<Task>> TaskGraph::findTask(TaskHandle taskHandle) const noexcept
    {
        const auto taskIt{ std::find_if(tasks.begin(), tasks.end(), [taskHandle](const std::shared_ptr<Task>& task)
                                        { return task->getHandle() == taskHandle; }) };

        if (taskIt == tasks.end())
        {
            return std::nullopt;
        }

        return *taskIt;
    }

} // namespace Atlas
