#ifndef ATLAS_KAHN_SCHEDULER
#define ATLAS_KAHN_SCHEDULER

#include "BaseScheduler.h"
#include "atlas/Executor/CompletionChannel.h"
#include "atlas/Executor/CpuExecutor.h"
#include "atlas/Executor/VulkanDispatchExecutor.h"
#include "atlas/Profiling/Trace.h"
#include "atlas/Scheduler/SchedulingPolicy.h"
#include "atlas/Tasking/TaskGraph.h"

#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

/** @file KahnScheduler.h @brief Declares resource-aware dependency scheduling. */

namespace Atlas
{
    namespace Detail
    {
        class ReadyTaskAccounting;
        class SchedulerTimingAccounting;
    } // namespace Detail

    /**
     * @ingroup scheduling
     * @brief Keeps CPU and Vulkan work in flight independently in dependency order.
     * @plantumlfile kahn_scheduler.puml
     */
    class KahnScheduler : public BaseScheduler
    {
      public:
        /**
         * @brief Borrows independent CPU and Vulkan executors for graph execution.
         * @param taskGraph Finalised graph to execute.
         * @param cpuExecutor CPU backend used for callable tasks.
         * @param gpuExecutor GPU backend used for declarative dispatches.
         * @param traceSession Optional borrowed trace session that must outlive execution.
         */
        KahnScheduler(const TaskGraph& taskGraph, CpuExecutor& cpuExecutor, VulkanDispatchExecutor& gpuExecutor,
                      TraceSession* traceSession = nullptr);
        /**
         * @brief Borrows independent executors and clones one policy per backend.
         * @param taskGraph Finalised graph to execute.
         * @param cpuExecutor CPU backend used for callable tasks.
         * @param gpuExecutor GPU backend used for declarative dispatches.
         * @param policy Backend-neutral selection policy cloned independently for CPU and GPU work.
         * @param traceSession Optional borrowed trace session that must outlive execution.
         * @throws std::invalid_argument When either policy clone is null.
         */
        KahnScheduler(const TaskGraph& taskGraph, CpuExecutor& cpuExecutor, VulkanDispatchExecutor& gpuExecutor,
                      const SchedulingPolicy& policy, TraceSession* traceSession = nullptr);

        /// @brief Executes the finalised graph until all accepted work drains.
        SchedulerResult execute() override;

        /**
         * @brief Requests fail-stop cancellation of one graph-owned task.
         *
         * The request may be made before or concurrently with execute(). It is
         * effective for work not yet claimed by the scheduler and at sliced GPU
         * work-unit boundaries. Running CPU and ordinary GPU work complete normally.
         * @param taskHandle Task in this scheduler's graph to cancel.
         * @return True when a new request was latched; false for invalid, duplicate,
         * terminal, or post-execution requests.
         */
        bool requestCancellation(TaskHandle taskHandle);
        /// @brief Destroys the scheduler without taking ownership of executors.
        ~KahnScheduler() override;

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
            /// @brief First policy exception or synthesized contract error observed.
            std::exception_ptr firstPolicyException{ nullptr };
            /// @brief First thrown submission error or permanent backend exception.
            std::exception_ptr firstInfrastructureException{ nullptr };
            /// @brief Whether new task submissions are still permitted.
            bool submissionsEnabled{ true };
            /// @brief Whether an executor or completion contract failed.
            bool executorFailure{ false };
            /// @brief Whether a task-attributed execution failure was observed.
            bool taskFailureObserved{ false };
            /// @brief Whether selection failed or violated the policy contract.
            bool policyFailure{ false };
            /// @brief Whether the completion producer ended before in-flight work drained.
            bool completionStreamEndedEarly{ false };
            /// @brief Whether at least one cancellation request became effective.
            bool cancellationObserved{ false };
        };

        /// @brief Completion identity expected for one accepted backend work unit.
        struct InFlightWork
        {
            /// @brief Backend expected to produce the completion.
            ExecutionResource resource{ ExecutionResource::CPU };
            /// @brief Exact work-unit index expected from that backend.
            std::size_t workUnitIndex{ 0U };
        };

        /// @brief Scheduler-side lifecycle of an externally requested cancellation.
        enum class CancellationState : std::uint8_t
        {
            None,      ///< No request has been made.
            Requested, ///< A request is awaiting a scheduler boundary.
            Terminal   ///< The target has completed or cancellation became effective.
        };

        /// @brief Parses graph dependencies and seeds resource-specific ready queues.
        bool parseDependencies();
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
        /// @brief Applies pending cancellation requests that do not target in-flight work.
        void applyPendingCancellations(ExecutionState& state);
        /// @brief Applies pending cancellation requests while @ref cancellationMutex is held.
        void applyPendingCancellationsLocked(ExecutionState& state) noexcept;
        /// @brief Reports whether cancellation is pending for one task.
        bool cancellationRequested(TaskHandle handle) const;
        /// @brief Marks one task's cancellation record terminal.
        void markCancellationTerminal(TaskHandle handle) noexcept;
        /// @brief Applies the final request boundary and rejects all later requests.
        void finishExecution(ExecutionState& state) noexcept;

        /// @brief Converts accumulated execution state into the public scheduler status.
        SchedulerStatus determineStatus(const ExecutionState& state) const noexcept;
        /// @brief Marks a task ready and appends it to the matching ready set.
        void enqueueReadyTask(TaskHandle taskHandle);
        /// @brief Appends one task to its resource-specific ready set without changing its state.
        void appendReadyTask(const std::shared_ptr<const Task>& task);
        /// @brief Returns the ready set matching one execution resource.
        std::vector<SchedulingCandidate>& readyTasksForResource(ExecutionResource resource) noexcept;
        /// @brief Returns the independent policy matching one execution resource.
        SchedulingPolicy& policyForResource(ExecutionResource resource) noexcept;
        /// @brief Records a policy exception and stops new submissions.
        void recordPolicyFailure(ExecutionState& state, std::exception_ptr exception) noexcept;
        /// @brief Uses the resource policy to remove the next currently-ready task.
        std::optional<std::shared_ptr<const Task>> takeNextReadyTask(ExecutionResource resource, ExecutionState& state);
        /// @brief Restores a task after its executor rejected submission.
        void restoreRejectedTask(const std::shared_ptr<const Task>& task) noexcept;
        /// @brief Applies a completion to graph-owned task execution state.
        void completeTask(const std::shared_ptr<const Task>& task, const TaskCompletion& completion);
        /// @brief Releases dependants after a successful prerequisite.
        void updateDependencies(const std::shared_ptr<const Task>& executedTask);
        /// @brief Publishes one best-effort event when profiling is active.
        void emitTrace(TraceEvent event) noexcept;

        /// @brief Stable enqueue-ordered ready CPU candidates.
        std::vector<SchedulingCandidate> cpuReadyTasks;
        /// @brief Stable enqueue-ordered ready GPU candidates.
        std::vector<SchedulingCandidate> gpuReadyTasks;
        /// @brief Remaining prerequisite counts keyed by dependent handle.
        std::unordered_map<TaskHandle, std::size_t, TaskHandle::Hash> remainingDependencies;
        /// @brief Accepted tasks and their expected completion resource.
        std::unordered_map<TaskHandle, InFlightWork, TaskHandle::Hash> inFlightTasks;
        /// @brief Synchronizes cancellation requests with scheduler boundary checks.
        mutable std::mutex cancellationMutex;
        /// @brief Preallocated request state for every task in the starting graph.
        std::unordered_map<TaskHandle, CancellationState, TaskHandle::Hash> cancellationStates;
        /// @brief Stable graph order used to apply concurrent cancellation requests.
        std::vector<TaskHandle> cancellationOrder;
        /// @brief Distinguishes pre-execution terminal-state validation from concurrent requests.
        bool executionStarted{ false };
        /// @brief Rejects cancellation requests after graph execution returns.
        bool executionFinished{ false };
        /// @brief Number of CPU tasks parsed from the graph for capacity validation.
        std::size_t cpuTaskCount{ 0U };
        /// @brief Number of GPU tasks parsed from the graph for capacity validation.
        std::size_t gpuTaskCount{ 0U };
        /// @brief Borrowed CPU executor used for this execution.
        CpuExecutor& cpuExecutor;
        /// @brief Borrowed Vulkan dispatch executor.
        VulkanDispatchExecutor& gpuExecutor;
        /// @brief Independently stateful CPU ready-task policy.
        std::unique_ptr<SchedulingPolicy> cpuSchedulingPolicy;
        /// @brief Independently stateful GPU ready-task policy.
        std::unique_ptr<SchedulingPolicy> gpuSchedulingPolicy;
        /// @brief Scheduler-internal ready-residency and selection-bypass accounting.
        std::unique_ptr<Detail::ReadyTaskAccounting> readyTaskAccounting;
        /// @brief Scheduler-internal scalar control and slice-switch timing.
        std::unique_ptr<Detail::SchedulerTimingAccounting> schedulerTimingAccounting;
        /// @brief Optional borrowed trace session that outlives accepted work.
        TraceSession* traceSession{ nullptr };
    };
} // namespace Atlas

#endif // !ATLAS_KAHN_SCHEDULER
