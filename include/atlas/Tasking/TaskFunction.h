#ifndef ATLAS_TASK_FUNCTION
#define ATLAS_TASK_FUNCTION

#include <functional>

/**
 * @file TaskFunction.h
 * @brief Declares host-callable task work accepted by CPU executors.
 */

namespace Atlas
{
    /**
     * @ingroup tasking
     * @brief Type-erased host-callable work associated with a task.
     *
     * Inputs and outputs are supplied through captures or other user-managed
     * storage. An executor owns the function object after successful submission,
     * but callers remain responsible for the lifetime of referenced captures.
     * Callable exceptions are captured by the executor.
     */
    using TaskFunction = std::function<void()>;
} // namespace Atlas

#endif // !ATLAS_TASK_FUNCTION
