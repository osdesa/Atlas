#ifndef ATLAS_TASK
#define ATLAS_TASK

#include "TaskHandle.h"
#include "TaskOptions.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <span>
#include <utility>
#include <vector>

/**
 * @file Task.h
 * @brief Declares the graph-owned representation of a task.
 */

namespace Atlas
{
    /**
     * @ingroup tasking
     * @brief Callable work executed when its task becomes ready.
     */
    using TaskFunction = std::function<void()>;

    /**
     * @ingroup tasking
     * @brief Stores a task's identity, work, metadata, and graph edges.
     *
     * @par Class diagram
     * @plantumlfile task.puml
     */
    class Task
    {
      public:
        /**
         * @brief Constructs a task from its graph-owned identity and work.
         * @param taskHandle The immutable identity assigned by the task graph.
         * @param taskFunction The function to execute for this task.
         * @param taskOptions The metadata associated with this task.
         */
        explicit Task(TaskHandle taskHandle, TaskFunction taskFunction, TaskOptions taskOptions) noexcept
            : handle{ taskHandle }, function{ std::move(taskFunction) }, options{ std::move(taskOptions) }
        {
        }

        /**
         * @brief Reports whether this task has a valid handle.
         * @return True when the task handle is valid;
         * false otherwise.
         */
        bool isValid() const noexcept
        {
            return handle.isValid();
        }

        /**
         * @brief Retrieves the unique handle of this task.
         * @return The task handle.
         */
        TaskHandle getHandle() const noexcept
        {
            return handle;
        }

        /**
         * @brief Retrieves the callable work associated with this task.
         * @return The task's callable work.
 */
        const TaskFunction& getFunction() const noexcept
        {
            return function;
        }

        /**
         * @brief Retrieves the dependencies of the task.
         * @return A read-only view of the task's dependencies.

         */
        std::span<const TaskHandle> getDependencies() const noexcept
        {
            return dependencies;
        }

        /**
         * @brief Retrieves the tasks that depend on this task.
         * @return A read-only view of dependent task
         * handles.
         */
        std::span<const TaskHandle> getDependents() const noexcept
        {
            return dependents;
        }

        /**
         * @brief Adds a new dependency to this task, indicating that this task cannot execute until
         * the specified dependency has completed.
         * @param dependency The task that must be completed before this task can execute.
         * @return True if the dependency was added successfully, false if it was already present.
         */
        bool addDependency(TaskHandle dependency);

        /**
         * @brief Adds a new dependent to this task, indicating that the specified dependent cannot
         * execute until this task has completed.
         * @param dependent The task that depends on this task to be executed.
         * @return True if the dependent was added successfully, false if it was already present.
         */
        bool addDependent(TaskHandle dependent);

        /**
         * @brief Removes an existing dependency from this task.
         * @param dependency The dependency to remove.
 */
        void removeDependency(TaskHandle dependency) noexcept;

        /**
         * @brief Compares two tasks for equality based on their handles.
         * @param other The other task to compare
         * with.
         * @return True when both tasks have the same handle; false otherwise.
         */
        bool operator==(const Task& other) const noexcept
        {
            return handle == other.handle;
        }

      private:
        /// @brief The identity information of the task.
        TaskHandle handle;

        /// @brief The function to be executed by the task.
        TaskFunction function;

        /// @brief The options for the task.
        TaskOptions options;

        /// @brief The tasks which need to be done before this task can be executed.
        std::vector<TaskHandle> dependencies;

        /// @brief The tasks which depend on this task to be executed.
        std::vector<TaskHandle> dependents;
    };
} // namespace Atlas
#endif // !ATLAS_TASK
