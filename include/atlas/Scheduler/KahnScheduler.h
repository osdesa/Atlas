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
#include <unordered_set>

/**
 * @file KahnScheduler.h
 * @brief Declares the capacity-aware scheduler based on Kahn's topological-sort algorithm.
 */

namespace Atlas
{
    /**
     * @ingroup scheduling
     * @brief Dispatches task graphs through a borrowed CPU executor in dependency order.
     *
     * The scheduler owns dependency and task-state changes. The executor owns
     * callable execution and returns task-attributed completion records. Ready
     * work is kept in flight up to the executor's reported concurrency capacity.
     * The borrowed executor must be initially drained and exclusively available
     * to this scheduler for the duration of execute().
     * @hideinheritancegraph
     * @plantumlfile kahn_scheduler.puml
     */
    class KahnScheduler : public BaseScheduler
    {
      public:
        /**
         * @brief Constructs a Kahn scheduler for a finalised task graph.
         * @param taskGraph The finalised task graph to execute.
         * @param executor The CPU executor used to run selected task callables.
         * @throws std::invalid_argument When the task graph is not finalised.
         *
         * The scheduler borrows both arguments. They must outlive the scheduler.
         * It never shuts down the executor.
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
        /// @brief Mutable bookkeeping shared by the executor-loop helpers.
        struct ExecutionState
        {
            /// @brief Maximum number of accepted tasks permitted at once.
            std::size_t executorCapacity{ 0U };

            /// @brief Number of valid successful completions observed.
            std::size_t successfulTaskCount{ 0U };

            /// @brief Exception from the first observed callable failure.
            std::exception_ptr firstTaskException{ nullptr };

            /// @brief Whether the scheduler may accept another ready task.
            bool submissionsEnabled{ true };

            /// @brief Whether the executor violated its lifecycle or completion contract.
            bool executorFailure{ false };

            /// @brief Whether at least one callable completion reported failure.
            bool taskFailureObserved{ false };

            /// @brief Whether accepted tasks were left without completions.
            bool completionStreamEndedEarly{ false };
        };

        /**
         * @brief Builds the initial dependency counts and ready-task queue.
         * @return True when every graph task was found; false otherwise.
         */
        bool parseDependencies();

        /**
         * @brief Runs capacity filling and completion processing until no work remains.
         * @param state Mutable state for this execution attempt.
         */
        void runExecutorLoop(ExecutionState& state);

        /**
         * @brief Submits ready tasks until capacity is full or submission stops.
         * @param state Mutable state for this execution attempt.
         */
        void submitReadyTasks(ExecutionState& state);

        /**
         * @brief Waits for and applies one completion from the executor.
         * @param state Mutable state for this execution attempt.
         * @return True when the executor stream can continue being drained.
         */
        bool processNextCompletion(ExecutionState& state);

        /**
         * @brief Records an executor stream that ended before accepted work completed.
         * @param state Mutable state for this execution attempt.
         */
        void handleMissingCompletion(ExecutionState& state);

        /**
         * @brief Validates and records one completion returned by the executor.
         * @param completion The completion to process.
         * @param state Mutable state for this execution attempt.
         */
        void processReceivedCompletion(const TaskCompletion& completion, ExecutionState& state);

        /**
         * @brief Validates completion attribution and removes its in-flight handle.
         * @param completion The completion to correlate with accepted work.
         * @param state Mutable state for this execution attempt.
         * @return The matching running task, or an empty optional for an invalid completion.
         */
        std::optional<std::shared_ptr<const Task>> resolveCompletedTask(const TaskCompletion& completion, ExecutionState& state);

        /**
         * @brief Applies one valid completion and updates graph-level bookkeeping.
         * @param task The running task attributed by @p completion.
         * @param completion The valid completion returned by the executor.
         * @param state Mutable state for this execution attempt.
         */
        void recordCompletionOutcome(const std::shared_ptr<const Task>& task, const TaskCompletion& completion, ExecutionState& state);

        /**
         * @brief Selects the graph-level outcome after executor processing.
         * @param state Final state for this execution attempt.
         * @return The scheduler status represented by @p state.
         */
        SchedulerStatus determineStatus(const ExecutionState& state) const noexcept;

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
         * @param task The in-flight task identified by the completion.
         * @param completion The task-attributed result produced by the CPU executor.
         */
        void completeTask(const std::shared_ptr<const Task>& task, const TaskCompletion& completion);

        /**
         * @brief Marks tasks whose completions are missing as infrastructure failures.
         *
         * The tasks retain empty exceptions and zero durations because no callable
         * outcome was supplied by the executor.
         */
        void failUnresolvedInFlightTasks();

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

        /// @brief Tasks that have been submitted to the executor but not yet completed.
        std::unordered_set<TaskHandle, TaskHandle::Hash> inFlightTasks;
    };
} // namespace Atlas

#endif // !ATLAS_KAHN_SCHEDULER
