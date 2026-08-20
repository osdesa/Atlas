#ifndef ATLAS_KAHN_SCHEDULER
#define ATLAS_KAHN_SCHEDULER

#include "BaseScheduler.h"
#include "atlas/Executor/CpuExecutor.h"
#include "atlas/Executor/TaskCompletion.h"
#include "atlas/Tasking/TaskGraph.h"
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
     * @brief Dispatches task graphs through a borrowed CPU executor in dependency order.
     *
     * The scheduler owns dependency and task-state changes. The executor owns
     * callable execution and returns task-attributed completion records.
     * @hideinheritancegraph
     * @plantumlfile kahn_scheduler.puml
     */
    class KahnScheduler : public BaseScheduler
    {
      public:
        /**
         * @brief Constructs a sequential Kahn scheduler for a finalised task graph.
         * @param taskGraph The finalised task graph to execute.
         * @param executor The CPU executor used to run selected task callables.
         * @throws std::invalid_argument When the task graph is not finalised.
         *
         * The scheduler borrows both arguments. They must outlive the scheduler.
         */
        explicit KahnScheduler(const TaskGraph& taskGraph, CpuExecutor& executor);

        /**
         * @brief Executes every task in dependency order.
         * @return The execution status, successful-completion count, captured task
         * exception, and elapsed time. Executor rejection or an invalid completion
         * is reported as SchedulerStatus::ExecutorUnavailable.
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
         * @brief Records the terminal state, captured exception, and duration after execution.
         * @param completion The task-attributed result produced by the CPU executor.
         */
        void completeTask(const TaskCompletion& completion);

        /**
         * @brief Releases tasks whose final outstanding dependency has completed.
         * @param executedTask The task that completed successfully.
         */
        void updateDependencies(const std::shared_ptr<const Task>& executedTask);

        /// @brief Tasks whose dependencies have all completed.
        std::queue<TaskHandle> readyTasks;

        /// @brief Remaining dependency counts for tasks that are not yet ready.
        std::unordered_map<TaskHandle, std::size_t, TaskHandle::Hash> remainingDependencies;

        /// @brief Borrowed CPU executor that must outlive this scheduler.
        CpuExecutor& cpuExecutor;
    };
} // namespace Atlas

#endif // !ATLAS_KAHN_SCHEDULER
