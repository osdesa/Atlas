#ifndef ATLAS_CPU_EXECUTOR
#define ATLAS_CPU_EXECUTOR

#include "atlas/Executor/CompletionChannel.h"
#include "atlas/Tasking/TaskFunction.h"
#include "atlas/Tasking/TaskHandle.h"

#include <cstdint>

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
     *
     * Callers must serialize calls to the public interface unless a concrete
     * implementation explicitly documents stronger guarantees. Executors run
     * callables but never mutate graph-owned task state; the scheduler applies
     * returned completions on its control thread.
     * @hideinheritancegraph
     * @plantumlfile cpu_executor.puml
     */
    class CpuExecutor
    {
      public:
        /**
         * @brief Submits CPU work whose outcome is published to a shared channel.
         * @param taskHandle The identity attached to the completion.
         * @param taskFunction Callable work transferred into the executor.
         * @param completionChannel Channel that outlives the accepted work.
         * @return True when the work was accepted; false after shutdown begins.
         */
        virtual bool submit(TaskHandle taskHandle, TaskFunction taskFunction, CompletionChannel& completionChannel) = 0;

        /**
         * @brief Reports the backend's maximum useful callable concurrency.
         * @return The number of tasks this executor can run simultaneously.
         */
        std::uint32_t maxConcurrency() const noexcept
        {
            return maximumConcurrency;
        }

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
        explicit CpuExecutor(std::uint32_t maxJobs) : maximumConcurrency{ maxJobs } {}

      private:
        /// @brief Maximum number of callables that can execute concurrently.
        const std::uint32_t maximumConcurrency;
    };
} // namespace Atlas

#endif // !ATLAS_CPU_EXECUTOR
