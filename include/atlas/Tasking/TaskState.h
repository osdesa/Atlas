#ifndef ATLAS_TASK_STATE
#define ATLAS_TASK_STATE

#include <cstdint>

/**
 * @file TaskState.h
 * @brief Declares the state of a task.
 */

namespace Atlas
{
    /**
     * @ingroup tasking
     * @brief valid states of a task
     */
    enum class TaskState : std::uint8_t
    {
        Unknown,  ///< The task state is unknown.
        Ready,    ///< The task is ready to execute.
        Running,  ///< The task is currently executing.
        Success,  ///< The task has completed successfully.
        Failure,  ///< The task has completed with a failure.
        Blocked,  ///< The task is blocked and cannot execute until its dependencies are satisfied.
        Paused,   ///< A sliced task is ready to resume at its next work-unit boundary.
        Cancelled ///< Cancellation became effective before the task completed.
    };

} // namespace Atlas

#endif // !ATLAS_TASK_STATE
