#ifndef ATLAS_TASK_EXECUTION_INFO
#define ATLAS_TASK_EXECUTION_INFO

#include "TaskState.h"

#include <chrono>
#include <cstddef>
#include <exception>

/**
 * @file TaskExecutionInfo.h
 * @brief Declares the mutable runtime information associated with a task.
 */

namespace Atlas
{
    /**
     * @ingroup tasking
     * @brief Stores lifecycle, failure, timing, fairness, and logical work-unit progress.
     */
    struct TaskExecutionInfo
    {
        /// @brief The task's current lifecycle state.
        TaskState state{ TaskState::Unknown };

        /// @brief The exception captured from the task callable, or an empty pointer when none was captured.
        std::exception_ptr exception{ nullptr };

        /// @brief Accumulated payload execution time, excluding executor queue waiting.
        std::chrono::microseconds executionDuration{ 0 };

        /// @brief Number of successfully completed payload work units.
        std::size_t completedWorkUnitCount{ 0U };

        /// @brief Total payload work units required for logical task success.
        std::size_t totalWorkUnitCount{ 1U };

        /**
         * @brief Accumulated time eligible for selection in a resource ready set.
         *
         * This excludes dependency-blocked time, executor queueing, and payload
         * execution. Sliced tasks accumulate a new interval after every incomplete
         * work unit is returned to the GPU ready set.
         */
        std::chrono::microseconds readyWaitDuration{ 0 };

        /**
         * @brief Number of same-resource selections that bypassed this ready task.
         *
         * The scheduler increments this count whenever it selects another valid
         * candidate while this task remains Ready or Paused in the same ready set.
         */
        std::size_t selectionBypassCount{ 0U };
    };
} // namespace Atlas

#endif // !ATLAS_TASK_EXECUTION_INFO
