#include "atlas/Tasking/Task.h"

namespace Atlas
{
    bool Task::addDependent(TaskHandle dependent)
    {
        // Ensure the dependent is not already in the list of dependents.
        if (std::find(dependents.begin(), dependents.end(), dependent) != dependents.end())
        {
            return false;
        }

        dependents.emplace_back(dependent);
        return true;
    }

    bool Task::addDependency(TaskHandle dependency)
    {
        // Ensure the dependency is not already in the list of dependencies.
        if (std::find(dependencies.begin(), dependencies.end(), dependency) != dependencies.end())
        {
            return false;
        }

        dependencies.emplace_back(dependency);
        return true;
    }

    void Task::removeDependency(TaskHandle dependency) noexcept
    {
        auto it = std::find(dependencies.begin(), dependencies.end(), dependency);
        if (it != dependencies.end())
        {
            dependencies.erase(it);
        }
    }
} // namespace Atlas
