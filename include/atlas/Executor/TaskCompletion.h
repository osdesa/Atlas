#ifndef ATLAS_TASK_COMPLETION
#define ATLAS_TASK_COMPLETION

#include "atlas/Tasking/TaskHandle.h"

#include <chrono>
#include <exception>

/**
 * @file TaskCompletion.h
 * @brief Declares the result produced by a task completion.
 */

namespace Atlas
{
    /**
     * @ingroup executor
     * @brief Describes the outcome of executing a task graph.
     */
    struct TaskCompletion
    {
        /// @brief Identifies the task that completed.
        TaskHandle handle;

        /// @brief The exception captured from the callable, or nullptr on success.
        std::exception_ptr exception{ nullptr };

        /// @brief Time spent executing the callable.
        std::chrono::microseconds executionDuration{ 0 };

        /**
         * @brief Reports whether the callable completed without throwing.
         * @return True when no exception was captured.
         */
        bool succeeded() const noexcept
        {
            return exception == nullptr;
        }
    };

} // namespace Atlas

#endif // !ATLAS_TASK_COMPLETION
