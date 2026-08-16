#ifndef ATLAS_KAHN_SCHEDULER
#define ATLAS_KAHN_SCHEDULER

#include "BaseScheduler.h"
#include "atlas/Tasking/TaskHandle.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <queue>
#include <unordered_map>

/**
 * @file KahnScheduler.h
 * @brief Declares the sequential scheduler based on Kahn's topological-sort algorithm.
 */

namespace Atlas
{
    /**
     * @ingroup scheduling
     * @brief Executes task graphs in dependency order using Kahn's algorithm.
     * @hideinheritancegraph
     * @plantumlfile kahn_scheduler.puml
     */
    class KahnScheduler : public BaseScheduler
    {
      public:
        /**
         * @brief Constructs a sequential Kahn scheduler for a finalised task graph.
         * @param taskGraph The finalised task graph to execute.
         * @throws std::invalid_argument When the task graph is not finalised.
         */
        explicit KahnScheduler(const TaskGraph& taskGraph);

        /**
         * @brief Executes every task in dependency order.
         * @return The execution status, completed-task count, captured exception, and elapsed time.
         */
        SchedulerResult execute() override;

        /// @brief Destroys the Kahn scheduler.
        ~KahnScheduler() override = default;

        /// @brief Prevents copying a scheduler with a borrowed graph reference.
        KahnScheduler(const KahnScheduler&) = delete;

        /// @brief Prevents copy-assigning a scheduler with a borrowed graph reference.
        KahnScheduler& operator=(const KahnScheduler&) = delete;

        /// @brief Prevents moving a scheduler with a borrowed graph reference.
        KahnScheduler(KahnScheduler&&) = delete;

        /// @brief Prevents move-assigning a scheduler with a borrowed graph reference.
        KahnScheduler& operator=(KahnScheduler&&) = delete;

      private:
        /**
         * @brief Builds the initial dependency counts and ready-task queue.
         * @return True when every graph task was found; false otherwise.
         */
        bool parseDependencies();

        /**
         * @brief Marks a task ready and adds it to the FIFO ready queue.
         * @param taskHandle The task that has become ready.
         */
        void enqueueReadyTask(TaskHandle taskHandle);

        /**
         * @brief Removes queued entries until a currently ready task is found.
         * @return The next ready task, or an empty optional when none remain.
         */
        std::optional<std::shared_ptr<const Task>> takeNextReadyTask();

        /**
         * @brief Records the terminal state and per-task result after execution.
         * @param task The task that finished executing.
         * @param executionResult The result produced by executing its function.
         */
        void completeTask(const std::shared_ptr<const Task>& task, const SchedulerResult& executionResult);

        /**
         * @brief Releases tasks whose final outstanding dependency has completed.
         * @param executedTask The task that completed successfully.
         */
        void updateDependencies(const std::shared_ptr<const Task>& executedTask);

        /// @brief Tasks whose dependencies have all completed.
        std::queue<TaskHandle> readyTasks;

        /// @brief Remaining dependency counts for tasks that are not yet ready.
        std::unordered_map<TaskHandle, std::size_t, TaskHandle::Hash> remainingDependencies;
    };
} // namespace Atlas

#endif // !ATLAS_KAHN_SCHEDULER
