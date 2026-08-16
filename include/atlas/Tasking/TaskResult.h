#ifndef ATLAS_TASK_RESULT
#define ATLAS_TASK_RESULT

#include "TaskHandle.h"
#include "TaskState.h"

#include <exception>

/**
 * @file TaskResult.h
 * @brief Declares the result of a task execution.
 */

namespace Atlas
{
    /**
     * @ingroup tasking
     * @brief Represents the result of a task execution.
     *
     * @par Class diagram
     * @plantumlfile task_result.puml
     */
    struct TaskResult
    {
        /// @brief The identity of the task that produced this result.
        TaskHandle handle;

        /// @brief The state of the task after execution.
        TaskState state{ TaskState::Unknown };

        /// @brief The exception thrown by the task, if any.
        std::exception_ptr exception{ nullptr };
    };
} // namespace Atlas

#endif // !ATLAS_TASK_RESULT
