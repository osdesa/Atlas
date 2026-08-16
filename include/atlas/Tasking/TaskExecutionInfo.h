#ifndef ATLAS_TASK_EXECUTION_INFO
#define ATLAS_TASK_EXECUTION_INFO

#include "TaskState.h"

#include <chrono>
#include <exception>

/**
 * @file TaskExecutionInfo.h
 * @brief Declares the mutable runtime information associated with a task.
 */

namespace Atlas
{
    /**
     * @ingroup tasking
     * @brief Stores the current lifecycle state and latest execution details of a task.
     */
    struct TaskExecutionInfo
    {
        /// @brief The task's current lifecycle state.
        TaskState state{ TaskState::Unknown };

        /// @brief The exception captured from the task callable, or an empty pointer when none was captured.
        std::exception_ptr exception{ nullptr };

        /// @brief Time spent executing the task callable during its latest execution attempt.
        std::chrono::microseconds executionDuration{ 0 };
    };
} // namespace Atlas

#endif // !ATLAS_TASK_EXECUTION_INFO
