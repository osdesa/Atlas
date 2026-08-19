#ifndef ATLAS_CPU_EXECUTOR
#define ATLAS_CPU_EXECUTOR

#include "atlas/Executor/TaskCompletion.h"
#include "atlas/Tasking/TaskFunction.h"
#include "atlas/Tasking/TaskHandle.h"

#include <optional>

/**
 * @file CpuExecutor.h
 * @brief Declares the interface for submitting host-callable task work.
 */

/**
 * @defgroup executor Executor
 * @brief Types used to execute task work on backend resources.
 *
 * Executor provides submission and completion interfaces for current and future
 * execution backends. Dependency tracking and scheduling policy remain outside
 * this module.
 */

namespace Atlas
{
    /**
     * @ingroup executor
     * @brief Defines the common interface implemented by Atlas CPU executors.
     *
     * Implementations own submitted callables by value. A successful submission
     * produces exactly one task-attributed completion. Callable exceptions are
     * captured in that completion rather than escaping the executor.
     *
     * Calling shutdown prevents later submissions. Implementations retain
     * completions produced for work accepted before shutdown, and repeated calls
     * to shutdown are valid.
     * @hideinheritancegraph
     */
    class CpuExecutor
    {
      public:
        /**
         * @brief Submits one task callable for CPU execution.
         * @param taskHandle The identity to attach to the resulting completion.
         * @param taskFunction Callable work transferred into the executor.
         * @return True when the work was accepted; false after shutdown begins.
         * @throws std::invalid_argument If @p taskHandle is invalid.
         *
         * A rejected submission does not produce a completion. Exceptions raised
         * while copying, moving, or storing the callable are not task failures and
         * may propagate to the caller.
         */
        virtual bool submit(TaskHandle taskHandle, TaskFunction taskFunction) = 0;

        /**
         * @brief Retrieves the next completion for previously accepted work.
         * @return The next available completion, or an empty optional when no
         * accepted or completed work remains.
         *
         * An implementation may block while accepted work is still executing.
         * Completions already produced remain retrievable after shutdown.
         */
        virtual std::optional<TaskCompletion> waitForCompletion() = 0;

        /**
         * @brief Stops accepting submissions while preserving accepted work and completions.
         *
         * Implementations must make repeated calls safe. Threaded implementations
         * will drain accepted work and join their workers before returning.
         */
        virtual void shutdown() noexcept = 0;

        /// @brief Destroys an executor through its common interface.
        virtual ~CpuExecutor() = default;

        /// @brief Prevents copying executor state.
        CpuExecutor(const CpuExecutor&) = delete;

        /// @brief Prevents copy-assigning executor state.
        CpuExecutor& operator=(const CpuExecutor&) = delete;

        /// @brief Prevents moving executor state.
        CpuExecutor(CpuExecutor&&) = delete;

        /// @brief Prevents move-assigning executor state.
        CpuExecutor& operator=(CpuExecutor&&) = delete;

      protected:
        /// @brief Constructs a CPU executor through its common interface.
        CpuExecutor() = default;
    };
} // namespace Atlas

#endif // !ATLAS_CPU_EXECUTOR
