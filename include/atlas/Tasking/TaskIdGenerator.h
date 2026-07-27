#ifndef ATLAS_TASK_ID_GENERATOR
#define ATLAS_TASK_ID_GENERATOR

#include "TaskHandle.h"

#include <cstdint>
#include <optional>

namespace Atlas
{
    /**
     * @ingroup tasking
     * @brief Allocates unique task handles for a single task graph.
     */
    class TaskIdGenerator
    {
      public:
        /**
         * @brief Constructs a task-handle generator for a graph.
         * @param graphID The graph ID assigned to every generated handle.
         * @param nextTaskID The first task ID to allocate.
         */
        explicit TaskIdGenerator(std::uint32_t graphID, std::uint32_t nextTaskID = 1U) noexcept
            : graphID{ graphID }, nextTaskID{ nextTaskID }
        {
        }

        /**
         * @brief Allocates the next task handle.
         * @return A valid task handle, or an empty optional when no task IDs remain.
         */
        std::optional<TaskHandle> next() noexcept
        {
            if (nextTaskID == INVALID_TASK_ID)
            {
                return std::nullopt;
            }

            const TaskHandle taskHandle{ nextTaskID, graphID };
            ++nextTaskID;
            return taskHandle;
        }

      private:
        /// @brief The graph ID assigned to generated task handles.
        std::uint32_t graphID;

        /// @brief The next task ID to allocate.
        std::uint32_t nextTaskID;
    };
} // namespace Atlas

#endif // !ATLAS_TASK_ID_GENERATOR
