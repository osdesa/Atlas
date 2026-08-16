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
     *
     * @par Class diagram
     * @plantumlfile task_state.puml
     */
    enum class TaskState : std::uint8_t
    {
        Unknown,   ///< The task state is unknown.
        Ready,     ///< The task is ready to execute.
        Running,   ///< The task is currently executing.
        Success,   ///< The task has completed successfully.
        Failure,   ///< The task has completed with a failure.
        Blocked,   ///< The task is blocked and cannot execute until its dependencies are satisfied.
        Cancelled, ///< The task has been cancelled and will not execute.
    };

} // namespace Atlas

#endif // !ATLAS_TASK_STATE
