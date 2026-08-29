#ifndef ATLAS_WORKERPOOL_CPU_EXECUTOR
#define ATLAS_WORKERPOOL_CPU_EXECUTOR

#include "atlas/Executor/CpuExecutor.h"
#include "atlas/Executor/TaskCompletion.h"
#include "atlas/Tasking/TaskFunction.h"

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <list>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

/**
 * @file WorkerpoolExecutor.h
 * @brief Declares fixed-size concurrent CPU execution.
 */

namespace Atlas
{
    /**
     * @ingroup executor
     * @brief Executes accepted task callables on a fixed-size worker pool.
     *
     * Submission transfers callable ownership into a FIFO work queue. Workers
     * execute callables outside the state mutex and publish task-attributed
     * completions in completion order. Callable exceptions are captured and do
     * not escape worker threads.
     *
     * Public calls must be serialized by the caller. A submitted callable must
     * not call lifecycle operations on this executor.
     *
     * @par Class diagram
     * @plantumlfile workerpool_executor.puml
     */
    class WorkerpoolExecutor final : public CpuExecutor
    {
      public:
        /**
         * @brief Constructs a worker-pool executor with a fixed number of threads.
         * @param maxThreads The maximum number of threads to use in the workpool.
         * @throws std::invalid_argument When @p maxThreads is zero.
         */
        explicit WorkerpoolExecutor(std::uint32_t maxThreads);

        /// @brief Drains accepted work and joins every worker.
        ~WorkerpoolExecutor() override;

        /**
         * @brief Accepts one callable for asynchronous worker-pool execution.
         * @param taskHandle The identity attached to the completion.
         * @param taskFunction Callable work transferred into the executor.
         * @return True when execution was accepted; false after shutdown.
         * @throws std::invalid_argument If @p taskHandle is invalid.
         */
        bool submit(TaskHandle taskHandle, TaskFunction taskFunction) override;

        /**
         * @brief Accepts one callable and publishes its outcome to a shared channel.
         * @param taskHandle The identity attached to the completion.
         * @param taskFunction Callable work transferred into the executor.
         * @param completionChannel Channel that outlives all accepted work.
         * @return True when execution was accepted; false after shutdown.
         * @throws std::invalid_argument If @p taskHandle is invalid.
         */
        bool submit(TaskHandle taskHandle, TaskFunction taskFunction, CompletionChannel& completionChannel) override;

        /**
         * @brief Waits for and removes the next produced completion.
         * @return The next completion, or an empty optional when no accepted or
         * completed work remains.
         */
        std::optional<TaskCompletion> waitForCompletion() override;

        /**
         * @brief Prevents later submissions, drains accepted work, and joins every worker.
         *
         * Repeated calls have the same effect as the first call.
         */
        void shutdown() noexcept override;

      private:
        /// @brief Lifecycle states controlling submission and worker shutdown.
        enum class Lifecycle : std::uint8_t
        {
            Running,      ///< New submissions are accepted.
            ShuttingDown, ///< Accepted work is draining; new submissions are rejected.
            Stopped       ///< All workers have exited and no work is accepted.
        };

        struct WorkItem
        {
            /// @brief Callable transferred to a worker.
            TaskFunction function;
            /// @brief Mutable outcome populated by the worker.
            TaskCompletion completion;
            /// @brief Optional scheduler-owned publication destination.
            CompletionChannel* completionChannel{ nullptr };
        };

        /// @brief Queues work for standalone or channel-targeted execution.
        bool submitWork(TaskHandle taskHandle, TaskFunction taskFunction, CompletionChannel* completionChannel);

        /// @brief Waits for and executes queued work until shutdown has drained it.
        void workerLoop();

        /// @brief Protects queues, unfinished-work accounting, and lifecycle state.
        std::mutex stateMutex;

        /// @brief Indicates a task is ready to be executed by a worker thread.
        std::condition_variable workAvailable;

        /// @brief Indicates a task has completed and is ready to be retrieved.
        std::condition_variable workComplete;

        /// @brief Queue of tasks waiting to be executed by worker threads.
        std::list<WorkItem> taskQueue;

        /**
         * @brief Completed work waiting to be retrieved in completion order.
         *
         * Nodes are allocated in taskQueue during submission and spliced here
         * after execution, so publishing a completion does not allocate.
         */
        std::list<WorkItem> completions;

        /// @brief Number of tasks that have been submitted but not yet completed.
        std::size_t unfinishedTasks{ 0 };

        /// @brief Lifecycle state of the executor.
        Lifecycle lifecycle{ Lifecycle::Running };

        /// @brief Worker threads for executing tasks in the pool.
        std::vector<std::jthread> workerThreads;
    };
} // namespace Atlas

#endif // !ATLAS_WORKERPOOL_CPU_EXECUTOR
