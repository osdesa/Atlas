#ifndef ATLAS_TASK_FUNCTION
#define ATLAS_TASK_FUNCTION

#include <functional>

/**
 * @file TaskFunction.h
 * @brief Declares callable CPU task work.
 */

namespace Atlas
{
    /**
     * @ingroup tasking
     * @brief Callable work executed by a CPU executor.
     *
     * Inputs and outputs are supplied through captures or other user-managed
     * storage. Exceptions are captured by the executor.
     */
    using TaskFunction = std::function<void()>;
} // namespace Atlas

#endif // !ATLAS_TASK_FUNCTION