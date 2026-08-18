#ifndef ATLAS_TASK_COMPLETION
#define ATLAS_TASK_COMPLETION

#include "atlas/Tasking/TaskHandle.h"

#include <chrono>
#include <exception>

/**
 * @file TaskCompletion.h
 * @brief Declares the outcome produced by executing one task callable.
 */

namespace Atlas
{
    /**
     * @ingroup executor
     * @brief Transfers one task execution outcome from an executor to a scheduler.
     *
     * This transient value does not contain task lifecycle state. The scheduler
     * remains responsible for applying the completion to TaskExecutionInfo.
     */
    struct TaskCompletion
    {
        /// @brief Identifies the task that completed.
        TaskHandle handle;

        /// @brief The exception captured from the callable, or nullptr on success.
        std::exception_ptr exception{ nullptr };

        /// @brief Time spent executing the task callable, excluding queue wait time.
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
