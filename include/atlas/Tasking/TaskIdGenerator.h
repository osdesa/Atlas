#ifndef ATLAS_TASK_ID_GENERATOR
#define ATLAS_TASK_ID_GENERATOR

#include "TaskHandle.h"

#include <cstdint>
#include <optional>

/**
 * @file TaskIdGenerator.h
 * @brief Declares the task-handle generator used by a task graph.
 */

namespace Atlas
{
    /**
     * @ingroup tasking
     * @brief Allocates unique task handles for a single task graph.
     *
     * @par Class diagram
     * @plantumlfile task_id_generator.puml
     */
    class TaskIdGenerator
    {
      public:
        /**
         * @brief Constructs a task-handle generator for a graph.
         * @param graphIdentifier The graph ID assigned to every generated handle.
         * @param initialTaskID The first task ID to allocate.
         */
        explicit TaskIdGenerator(GraphId graphIdentifier, TaskId initialTaskID = TaskId{ 1U }) noexcept
            : graphID{ graphIdentifier }, nextTaskID{ initialTaskID.getValue() }
        {
        }

        /**
         * @brief Allocates the next task handle.
         * @return A valid task handle, or an empty optional when no task IDs remain.
         */
        std::optional<TaskHandle> next() noexcept
        {
            if (!graphID.isValid() || nextTaskID == INVALID_TASK_ID_VALUE)
            {
                return std::nullopt;
            }

            const TaskHandle taskHandle{ TaskId{ nextTaskID }, graphID };
            ++nextTaskID;
            return taskHandle;
        }

      private:
        /// @brief The graph ID assigned to generated task handles.
        GraphId graphID;

        /// @brief The next task ID to allocate.
        std::uint32_t nextTaskID;
    };
} // namespace Atlas

#endif // !ATLAS_TASK_ID_GENERATOR
