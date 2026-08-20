#ifndef ATLAS_SYNCHRONOUS_CPU_EXECUTOR
#define ATLAS_SYNCHRONOUS_CPU_EXECUTOR

#include "atlas/Executor/CpuExecutor.h"
#include "atlas/Executor/TaskCompletion.h"
#include "atlas/Tasking/TaskFunction.h"

#include <optional>
#include <queue>

/**
 * @file SynchronousCpuExecutor.h
 * @brief Declares immediate CPU execution on the submitting thread.
 */

namespace Atlas
{
    /**
     * @ingroup executor
     * @brief Executes accepted task callables immediately on the submitting thread.
     *
     * Submission does not return until the callable finishes and its completion
     * has been queued. Completions are retained in submission order until they
     * are retrieved. This implementation owns no worker threads and is intended
     * to be called from one thread at a time. It is neither thread-safe nor
     * reentrant; a task callable must not call back into the same executor.
     *
     * @par Class diagram
     * @plantumlfile synchronous_cpu_executor.puml
     */
    class SynchronousCpuExecutor final : public CpuExecutor
    {

      public:
        SynchronousCpuExecutor() : CpuExecutor{ 1U } {}

        /**
         * @brief Executes one accepted callable and queues its completion.
         * @param taskHandle The identity attached to the completion.
         * @param taskFunction Callable work transferred into the executor.
         * @return True when execution was accepted; false after shutdown.
         * @throws std::invalid_argument If @p taskHandle is invalid.
         */
        bool submit(TaskHandle taskHandle, TaskFunction taskFunction) override;

        /**
         * @brief Removes and returns the oldest queued completion without blocking.
         * @return The oldest completion, or an empty optional when the queue is empty.
         */
        std::optional<TaskCompletion> waitForCompletion() override;

        /**
         * @brief Prevents later submissions without discarding queued completions.
         *
         * Repeated calls have the same effect as the first call.
         */
        void shutdown() noexcept override;

      private:
        /// @brief Completed task outcomes waiting to be retrieved in submission order.
        std::queue<TaskCompletion> completions;

        /// @brief Whether the executor will accept another submission.
        bool acceptingSubmissions{ true };
    };
} // namespace Atlas

#endif // !ATLAS_SYNCHRONOUS_CPU_EXECUTOR
