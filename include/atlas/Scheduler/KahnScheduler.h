#ifndef ATLAS_KAHN_SCHEDULER
#define ATLAS_KAHN_SCHEDULER

#include "BaseScheduler.h"
#include "atlas/Executor/CompletionChannel.h"
#include "atlas/Executor/CpuExecutor.h"
#include "atlas/Executor/GpuExecutor.h"
#include "atlas/Tasking/TaskGraph.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <queue>
#include <unordered_map>

/** @file KahnScheduler.h @brief Declares resource-aware dependency scheduling. */

namespace Atlas
{
    /**
     * @ingroup scheduling
     * @brief Keeps CPU and Vulkan work in flight independently in dependency order.
     * @plantumlfile kahn_scheduler.puml
     */
    class KahnScheduler : public BaseScheduler
    {
      public:
        /**
         * @brief Borrows one CPU executor for CPU-only graph execution.
         * @param taskGraph Finalised graph to execute.
         * @param executor CPU backend used for callable tasks.
         */
        explicit KahnScheduler(const TaskGraph& taskGraph, CpuExecutor& executor);
        /**
         * @brief Borrows independent CPU and GPU executors for mixed graph execution.
         * @param taskGraph Finalised graph to execute.
         * @param cpuExecutor CPU backend used for callable tasks.
         * @param gpuExecutor GPU backend used for declarative dispatches.
         */
        KahnScheduler(const TaskGraph& taskGraph, CpuExecutor& cpuExecutor, GpuExecutor& gpuExecutor);

        /// @brief Executes the finalised graph until all accepted work drains.
        SchedulerResult execute() override;
        /// @brief Destroys the scheduler without taking ownership of executors.
        ~KahnScheduler() override = default;

        /// @brief Prevents copying scheduler state.
        KahnScheduler(const KahnScheduler&) = delete;
        /// @brief Prevents copy assignment of scheduler state.
        KahnScheduler& operator=(const KahnScheduler&) = delete;
        /// @brief Prevents moving scheduler state.
        KahnScheduler(KahnScheduler&&) = delete;
        /// @brief Prevents move assignment of scheduler state.
        KahnScheduler& operator=(KahnScheduler&&) = delete;

      private:
        struct ExecutionState
        {
            /// @brief Maximum CPU submissions allowed in flight.
            std::size_t cpuCapacity{ 0U };
            /// @brief Maximum GPU submissions allowed in flight.
            std::size_t gpuCapacity{ 0U };
            /// @brief Number of accepted CPU tasks awaiting completion.
            std::size_t cpuInFlight{ 0U };
            /// @brief Number of accepted GPU tasks awaiting completion.
            std::size_t gpuInFlight{ 0U };
            /// @brief Number of tasks completed successfully by the graph.
            std::size_t successfulTaskCount{ 0U };
            /// @brief First callable or dispatch exception observed.
            std::exception_ptr firstTaskException{ nullptr };
            /// @brief Whether new task submissions are still permitted.
            bool submissionsEnabled{ true };
            /// @brief Whether an executor or completion contract failed.
            bool executorFailure{ false };
            /// @brief Whether a task-attributed execution failure was observed.
            bool taskFailureObserved{ false };
            /// @brief Whether the completion producer ended before in-flight work drained.
            bool completionStreamEndedEarly{ false };
        };

        /// @brief Parses graph dependencies and seeds resource-specific ready queues.
        bool parseDependencies();
        /// @brief Runs the compatibility CPU-only completion loop.
        void runCpuOnlyExecutorLoop(ExecutionState& state);
        /// @brief Fills CPU capacity through the standalone executor API.
        void submitCpuOnlyReadyTasks(ExecutionState& state);
        /// @brief Runs the heterogeneous completion-channel loop.
        void runExecutorLoop(ExecutionState& state, CompletionChannel& channel);
        /// @brief Fills each backend's available capacity from its ready queue.
        void submitReadyTasks(ExecutionState& state, CompletionChannel& channel);
        /// @brief Submits ready work matching one execution resource.
        void submitForResource(ExecutionResource resource, ExecutionState& state, CompletionChannel& channel);
        /// @brief Waits for and processes one channel event.
        bool processNextEvent(ExecutionState& state, CompletionChannel& channel);
        /// @brief Processes available channel events and reports whether the channel remains open.
        bool processAvailableEvents(ExecutionState& state, CompletionChannel& channel);
        /// @brief Routes a completion, failure, or closure event.
        void processEvent(const CompletionEvent& event, ExecutionState& state);
        /// @brief Validates and applies one attributed task completion.
        void processCompletion(const TaskCompletion& completion, ExecutionState& state);
        /// @brief Records a backend producer failure and fails its in-flight tasks.
        void handleProducerFailure(ExecutionResource resource, ExecutionState& state);
        /// @brief Marks one in-flight task as failed due to infrastructure failure.
        void failInFlightTask(TaskHandle handle) noexcept;
        /// @brief Fails and removes all in-flight tasks for one resource.
        void failInFlightForResource(ExecutionResource resource, ExecutionState& state) noexcept;
        /// @brief Fails and removes every currently in-flight task.
        void failAllInFlight(ExecutionState& state) noexcept;
        /// @brief Decrements the resource-specific in-flight counter safely.
        void decrementInFlight(ExecutionResource resource, ExecutionState& state) noexcept;

        /// @brief Converts accumulated execution state into the public scheduler status.
        SchedulerStatus determineStatus(const ExecutionState& state) const noexcept;
        /// @brief Marks a task ready and places it in the matching FIFO queue.
        void enqueueReadyTask(TaskHandle taskHandle);
        /// @brief Removes the next currently-ready task for one resource.
        std::optional<std::shared_ptr<const Task>> takeNextReadyTask(ExecutionResource resource);
        /// @brief Restores a task after its executor rejected submission.
        void restoreRejectedTask(const std::shared_ptr<const Task>& task) noexcept;
        /// @brief Applies a completion to graph-owned task execution state.
        void completeTask(const std::shared_ptr<const Task>& task, const TaskCompletion& completion);
        /// @brief Releases dependants after a successful prerequisite.
        void updateDependencies(const std::shared_ptr<const Task>& executedTask);

        /// @brief FIFO queue of ready CPU task handles.
        std::queue<TaskHandle> cpuReadyTasks;
        /// @brief FIFO queue of ready GPU task handles.
        std::queue<TaskHandle> gpuReadyTasks;
        /// @brief Remaining prerequisite counts keyed by dependent handle.
        std::unordered_map<TaskHandle, std::size_t, TaskHandle::Hash> remainingDependencies;
        /// @brief Accepted tasks and their expected completion resource.
        std::unordered_map<TaskHandle, ExecutionResource, TaskHandle::Hash> inFlightTasks;
        /// @brief Number of CPU tasks parsed from the graph for capacity validation.
        std::size_t cpuTaskCount{ 0U };
        /// @brief Number of GPU tasks parsed from the graph for capacity validation.
        std::size_t gpuTaskCount{ 0U };
        /// @brief Borrowed CPU executor used for this execution.
        CpuExecutor& cpuExecutor;
        /// @brief Optional borrowed GPU executor for mixed execution.
        GpuExecutor* gpuExecutor{ nullptr };
    };
} // namespace Atlas

#endif // !ATLAS_KAHN_SCHEDULER
