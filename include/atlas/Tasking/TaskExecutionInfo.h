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
     * @brief Stores lifecycle, failure, duration, and logical work-unit progress.
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
    };
} // namespace Atlas

#endif // !ATLAS_TASK_EXECUTION_INFO
