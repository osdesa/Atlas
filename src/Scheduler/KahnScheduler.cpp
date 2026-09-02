#include "atlas/Scheduler/KahnScheduler.h"

#include "ReadyTaskAccounting.h"
#include "SchedulerTimingAccounting.h"
#include "atlas/Scheduler/FifoSchedulingPolicy.h"
#include "atlas/Vulkan/VulkanError.h"

#include <algorithm>
#include <chrono>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <utility>

namespace Atlas
{
    namespace
    {
        template <typename Function> class ScopeExit final
        {
          public:
            explicit ScopeExit(Function function) : callback{ std::move(function) } {}
            ~ScopeExit() noexcept
            {
                callback();
            }
            ScopeExit(const ScopeExit&) = delete;
            ScopeExit& operator=(const ScopeExit&) = delete;

          private:
            Function callback;
        };

        /**
         * @brief Clones and validates one user-supplied scheduling policy.
         * @param policy Policy configuration to clone.
         * @return A fresh, non-null policy instance.
         * @throws std::invalid_argument When the policy returns a null clone.
         */
        std::unique_ptr<SchedulingPolicy> clonePolicy(const SchedulingPolicy& policy)
        {
            std::unique_ptr<SchedulingPolicy> clone{ policy.clone() };
            if (clone == nullptr)
            {
                throw std::invalid_argument{ "SchedulingPolicy::clone returned null" };
            }
            return clone;
        }

        template <typename TaskType> TraceEvent taskTraceEvent(const TraceEventKind kind, const TaskType* const task)
        {
            return TraceEvent{ .kind = kind,
                               .source = TraceEventSource::Scheduler,
                               .resource = task->options.executionResource,
                               .hasTask = true,
                               .hasResource = true,
                               .graphId = task->handle.getGraphID().getValue(),
                               .taskId = task->handle.getTaskID().getValue(),
                               .priority = task->options.priority,
                               .state = task->executionInfo.state };
        }

        bool isDeviceLoss(const std::exception_ptr& exception) noexcept
        {
            if (exception == nullptr)
            {
                return false;
            }
            try
            {
                std::rethrow_exception(exception);
            }
            catch (const VulkanError& error)
            {
                return error.result() == VK_ERROR_DEVICE_LOST;
            }
            catch (...)
            {
                return false;
            }
        }
    } // namespace

    KahnScheduler::KahnScheduler(const TaskGraph& taskGraph, CpuExecutor& cpuBackend, VulkanDispatchExecutor& gpuBackend,
                                 TraceSession* const trace)
        : KahnScheduler{ taskGraph, cpuBackend, gpuBackend, FifoSchedulingPolicy{}, trace }
    {
    }

    KahnScheduler::KahnScheduler(const TaskGraph& taskGraph, CpuExecutor& cpuBackend, VulkanDispatchExecutor& gpuBackend,
                                 const SchedulingPolicy& policy, TraceSession* const trace)
        : startingGraph{ taskGraph }, cpuExecutor{ cpuBackend }, gpuExecutor{ gpuBackend }, cpuSchedulingPolicy{ clonePolicy(policy) },
          gpuSchedulingPolicy{ clonePolicy(policy) }, readyTaskAccounting{ std::make_unique<Detail::ReadyTaskAccounting>(taskGraph) },
          schedulerTimingAccounting{ std::make_unique<Detail::SchedulerTimingAccounting>() }, traceSession{ trace }
    {
        if (!startingGraph.isFinalisedGraph())
        {
            throw std::invalid_argument{ "KahnScheduler requires a finalised task graph" };
        }
        cancellationOrder = startingGraph.getTaskHandles();
        cancellationStates.reserve(cancellationOrder.size());
        for (const TaskHandle handle : cancellationOrder)
        {
            cancellationStates.emplace(handle, CancellationState::None);
        }
    }

    KahnScheduler::~KahnScheduler() = default;

    bool KahnScheduler::ReadyQueue::empty() const noexcept
    {
        return first == storage.size();
    }

    std::size_t KahnScheduler::ReadyQueue::size() const noexcept
    {
        return storage.size() - first;
    }

    void KahnScheduler::ReadyQueue::clear() noexcept
    {
        storage.clear();
        first = 0U;
    }

    void KahnScheduler::ReadyQueue::reserve(const std::size_t capacity)
    {
        storage.reserve(capacity);
    }

    void KahnScheduler::ReadyQueue::push(SchedulingCandidate candidate)
    {
        if (first != 0U && (storage.size() == storage.capacity() || first >= storage.size() / 2U))
        {
            compact();
        }
        storage.emplace_back(std::move(candidate));
    }

    std::span<const SchedulingCandidate> KahnScheduler::ReadyQueue::candidates() const noexcept
    {
        return std::span<const SchedulingCandidate>{ storage }.subspan(first);
    }

    SchedulingCandidate KahnScheduler::ReadyQueue::remove(const std::size_t index)
    {
        const std::size_t storageIndex{ first + index };
        SchedulingCandidate candidate{ storage.at(storageIndex) };
        if (index == 0U)
        {
            ++first;
            if (first == storage.size())
            {
                clear();
            }
        }
        else
        {
            using Difference = std::vector<SchedulingCandidate>::difference_type;
            storage.erase(std::next(storage.begin(), static_cast<Difference>(storageIndex)));
        }
        return candidate;
    }

    void KahnScheduler::ReadyQueue::compact()
    {
        using Difference = std::vector<SchedulingCandidate>::difference_type;
        storage.erase(storage.begin(), std::next(storage.begin(), static_cast<Difference>(first)));
        first = 0U;
    }

    bool KahnScheduler::requestCancellation(const TaskHandle taskHandle)
    {
        std::lock_guard lock{ cancellationMutex };
        const auto entry{ cancellationStates.find(taskHandle) };
        if (executionFinished || entry == cancellationStates.end() || entry->second != CancellationState::None)
        {
            return false;
        }
        if (!executionStarted)
        {
            const TaskRecord* const task{ startingGraph.findTaskRecord(taskHandle) };
            if (task == nullptr || task->executionInfo.state == TaskState::Success ||
                task->executionInfo.state == TaskState::Failure || task->executionInfo.state == TaskState::Cancelled)
            {
                entry->second = CancellationState::Terminal;
                return false;
            }
        }
        entry->second = CancellationState::Requested;
        emitTrace(TraceEvent{ .kind = TraceEventKind::CancellationRequested,
                              .source = TraceEventSource::Scheduler,
                              .hasTask = true,
                              .graphId = taskHandle.getGraphID().getValue(),
                              .taskId = taskHandle.getTaskID().getValue() });
        return true;
    }

    SchedulerResult KahnScheduler::execute()
    {
        SchedulerResult result{ .status = SchedulerStatus::Unknown,
                                .executedTaskCount = 0U,
                                .exception = nullptr,
                                .executionTime = std::chrono::nanoseconds{ 0 } };
        const auto startTime{ std::chrono::steady_clock::now() };
        if (!startingGraph.tryBeginExecution())
        {
            std::lock_guard lock{ cancellationMutex };
            executionStarted = true;
            executionFinished = true;
            result.status = SchedulerStatus::InvalidGraph;
            result.executionTime = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - startTime);
            return result;
        }
        const ScopeExit graphExecutionGuard{ [this] { startingGraph.markExecutionFinished(); } };
        const ScopeExit schedulerExecutionGuard{ [this]
                                                 {
                                                     std::lock_guard lock{ cancellationMutex };
                                                     executionFinished = true;
                                                 } };
        schedulerTimingAccounting->reset();
        emitTrace(TraceEvent{ .kind = TraceEventKind::SchedulerStarted,
                              .source = TraceEventSource::Scheduler,
                              .graphId = startingGraph.getGraphID().getValue() });

        ExecutionState state;
        inFlightTasks.clear();
        {
            std::lock_guard lock{ cancellationMutex };
            executionStarted = true;
        }

        std::optional<SchedulerStatus> forcedStatus;

        if (!parseDependencies())
        {
            forcedStatus = SchedulerStatus::InvalidGraph;
        }
        else
        {
            state.cpuCapacity = std::min(cpuTaskCount, static_cast<std::size_t>(cpuExecutor.maxConcurrency()));
            state.gpuCapacity = std::min(gpuTaskCount, static_cast<std::size_t>(gpuExecutor.maxConcurrency()));
            inFlightTasks.reserve(state.cpuCapacity + state.gpuCapacity);

            if ((cpuTaskCount != 0U && state.cpuCapacity == 0U) || (gpuTaskCount != 0U && state.gpuCapacity == 0U))
            {
                forcedStatus = SchedulerStatus::ExecutorUnavailable;
            }
            else
            {
                CompletionChannel channel{ state.cpuCapacity + state.gpuCapacity, traceSession };
                applyPendingCancellations(state);
                runExecutorLoop(state, channel);
                channel.close();
            }
        }

        finishExecution(state);
        schedulerTimingAccounting->pause();
        result.status = forcedStatus.value_or(determineStatus(state));
        result.executedTaskCount = state.successfulTaskCount;
        result.exception =
            state.firstTaskException != nullptr
                ? state.firstTaskException
                : (state.firstInfrastructureException != nullptr ? state.firstInfrastructureException : state.firstPolicyException);
        result.executionTime = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - startTime);
        result.schedulerActiveDuration = schedulerTimingAccounting->activeDuration();
        result.immediateSliceSwitchDuration = schedulerTimingAccounting->immediateSliceSwitchDuration();
        result.immediateSliceSwitchCount = schedulerTimingAccounting->immediateSliceSwitchCount();
        if (result.status == SchedulerStatus::ExecutorUnavailable)
        {
            emitTrace(TraceEvent{ .kind = TraceEventKind::InfrastructureFailed,
                                  .source = TraceEventSource::Scheduler,
                                  .graphId = startingGraph.getGraphID().getValue() });
        }
        emitTrace(TraceEvent{ .kind = TraceEventKind::SchedulerFinished,
                              .source = TraceEventSource::Scheduler,
                              .graphId = startingGraph.getGraphID().getValue() });
        return result;
    }

    void KahnScheduler::runExecutorLoop(ExecutionState& state, CompletionChannel& channel)
    {
        while (true)
        {
            if (!processAvailableEvents(state, channel))
            {
                break;
            }
            applyPendingCancellations(state);
            submitReadyTasks(state, channel);

            if (inFlightTasks.empty())
            {
                if (state.submissionsEnabled && (!cpuReadyTasks.empty() || !gpuReadyTasks.empty()))
                {
                    continue;
                }
                break;
            }
            if (!processNextEvent(state, channel))
            {
                break;
            }
        }

        if (!inFlightTasks.empty())
        {
            state.executorFailure = true;
            state.completionStreamEndedEarly = true;
            failAllInFlight(state);
        }
    }

    void KahnScheduler::submitReadyTasks(ExecutionState& state, CompletionChannel& channel)
    {
        submitForResource(ExecutionResource::CPU, state, channel);
        if (state.submissionsEnabled)
        {
            submitForResource(ExecutionResource::GPU, state, channel);
        }
    }

    void KahnScheduler::submitForResource(const ExecutionResource resource, ExecutionState& state, CompletionChannel& channel)
    {
        std::size_t& inFlight{ resource == ExecutionResource::CPU ? state.cpuInFlight : state.gpuInFlight };
        const std::size_t capacity{ resource == ExecutionResource::CPU ? state.cpuCapacity : state.gpuCapacity };
        while (state.submissionsEnabled && inFlight < capacity)
        {
            const TaskRecord* const task{ takeNextReadyTask(resource, state) };
            if (task == nullptr)
            {
                return;
            }

            bool accepted{ false };
            std::size_t workUnitIndex{ 0U };
            TraceEvent submissionRequested{ taskTraceEvent(TraceEventKind::SubmissionRequested, task) };
            submissionRequested.workUnitIndex = task->executionInfo.completedWorkUnitCount;
            emitTrace(submissionRequested);
            schedulerTimingAccounting->pause();
            try
            {
                if (resource == ExecutionResource::CPU)
                {
                    const TaskFunction* const function{ task->cpuFunction() };
                    if (function == nullptr)
                    {
                        throw std::logic_error{ "CPU task has no CPU payload" };
                    }
                    accepted = cpuExecutor.submit(task->handle, *function, channel);
                }
                else
                {
                    if (const VulkanDispatch* const dispatch{ task->gpuDispatch() }; dispatch != nullptr)
                    {
                        workUnitIndex = dispatch->workUnitIndex();
                        accepted = gpuExecutor.submit(task->handle, *dispatch, channel);
                    }
                    else if (const SlicedVulkanDispatch* const slicedDispatch{ task->slicedGpuDispatch() }; slicedDispatch != nullptr)
                    {
                        VulkanDispatch workUnit{ slicedDispatch->slice(task->executionInfo.completedWorkUnitCount) };
                        workUnitIndex = workUnit.workUnitIndex();
                        accepted = gpuExecutor.submit(task->handle, std::move(workUnit), channel);
                    }
                    else
                    {
                        throw std::logic_error{ "GPU task has no GPU payload" };
                    }
                }
            }
            catch (...)
            {
                schedulerTimingAccounting->resume();
                restoreRejectedTask(task);
                emitTrace(taskTraceEvent(TraceEventKind::SubmissionRejected, task));
                state.executorFailure = true;
                state.submissionsEnabled = false;
                if (state.firstInfrastructureException == nullptr)
                {
                    state.firstInfrastructureException = std::current_exception();
                }
                return;
            }
            schedulerTimingAccounting->resume();

            if (!accepted)
            {
                restoreRejectedTask(task);
                emitTrace(taskTraceEvent(TraceEventKind::SubmissionRejected, task));
                state.executorFailure = true;
                state.submissionsEnabled = false;
                return;
            }

            inFlightTasks.emplace(task->handle, InFlightWork{ resource, workUnitIndex });
            TraceEvent acceptedEvent{ taskTraceEvent(TraceEventKind::SubmissionAccepted, task) };
            acceptedEvent.workUnitIndex = workUnitIndex;
            emitTrace(acceptedEvent);
            ++inFlight;
            if (resource == ExecutionResource::GPU)
            {
                schedulerTimingAccounting->recordGpuSelection(task->handle);
            }
        }
    }

    bool KahnScheduler::processNextEvent(ExecutionState& state, CompletionChannel& channel)
    {
        schedulerTimingAccounting->pause();
        const CompletionEvent event{ channel.wait() };
        schedulerTimingAccounting->resume();
        processEvent(event, state);
        return event.kind != CompletionEventKind::Closed;
    }

    bool KahnScheduler::processAvailableEvents(ExecutionState& state, CompletionChannel& channel)
    {
        while (true)
        {
            const std::optional<CompletionEvent> event{ channel.tryReceive() };
            if (!event.has_value())
            {
                return true;
            }
            processEvent(event.value(), state);
            if (event->kind == CompletionEventKind::Closed)
            {
                return false;
            }
        }
    }

    void KahnScheduler::processEvent(const CompletionEvent& event, ExecutionState& state)
    {
        switch (event.kind)
        {
        case CompletionEventKind::Completion:
            if (event.completion.has_value())
            {
                processCompletion(event.completion.value(), state);
            }
            else
            {
                state.executorFailure = true;
                state.submissionsEnabled = false;
            }
            break;
        case CompletionEventKind::ProducerFailure:
            handleProducerFailure(event.resource, state);
            break;
        case CompletionEventKind::Closed:
            if (!inFlightTasks.empty())
            {
                state.executorFailure = true;
                state.completionStreamEndedEarly = true;
                state.submissionsEnabled = false;
                failAllInFlight(state);
            }
            break;
        }
    }

    void KahnScheduler::processCompletion(const TaskCompletion& completion, ExecutionState& state)
    {
        emitTrace(TraceEvent{ .kind = TraceEventKind::CompletionObserved,
                              .source = TraceEventSource::Scheduler,
                              .resource = completion.resource,
                              .hasTask = true,
                              .hasResource = true,
                              .graphId = completion.handle.getGraphID().getValue(),
                              .taskId = completion.handle.getTaskID().getValue(),
                              .workUnitIndex = completion.workUnitIndex });
        const auto inFlightEntry{ inFlightTasks.find(completion.handle) };
        if (inFlightEntry == inFlightTasks.end())
        {
            state.executorFailure = true;
            state.submissionsEnabled = false;
            return;
        }

        const InFlightWork expectedWork{ inFlightEntry->second };
        const ExecutionResource expectedResource{ expectedWork.resource };
        decrementInFlight(expectedResource, state);
        inFlightTasks.erase(inFlightEntry);

        const TaskRecord* const task{ startingGraph.findTaskRecord(completion.handle) };
        if (task == nullptr || task->executionInfo.state != TaskState::Running || completion.resource != expectedResource ||
            completion.workUnitIndex != expectedWork.workUnitIndex)
        {
            state.executorFailure = true;
            state.submissionsEnabled = false;
            if (task != nullptr)
            {
                failInFlightTask(completion.handle);
            }
            return;
        }

        completeTask(task, completion);
        if (completion.succeeded())
        {
            if (task->executionInfo.state == TaskState::Paused)
            {
                emitTrace(taskTraceEvent(TraceEventKind::TaskPaused, task));
                if (cancellationRequested(completion.handle))
                {
                    task->executionInfo.state = TaskState::Cancelled;
                    readyTaskAccounting->recordTerminal(task);
                    state.cancellationObserved = true;
                    state.submissionsEnabled = false;
                    markCancellationTerminal(completion.handle);
                    emitTrace(taskTraceEvent(TraceEventKind::CancellationApplied, task));
                }
                else
                {
                    appendReadyTask(task);
                    schedulerTimingAccounting->recordIncompleteSlice(completion.handle);
                }
                return;
            }

            markCancellationTerminal(completion.handle);
            readyTaskAccounting->recordTerminal(task);
            ++state.successfulTaskCount;
            emitTrace(taskTraceEvent(TraceEventKind::TaskSucceeded, task));
            if (state.submissionsEnabled)
            {
                updateDependencies(task);
            }
        }
        else
        {
            markCancellationTerminal(completion.handle);
            readyTaskAccounting->recordTerminal(task);
            state.submissionsEnabled = false;
            if (isDeviceLoss(completion.exception))
            {
                state.executorFailure = true;
                if (state.firstInfrastructureException == nullptr)
                {
                    state.firstInfrastructureException = completion.exception;
                }
            }
            else
            {
                state.taskFailureObserved = true;
                if (state.firstTaskException == nullptr)
                {
                    state.firstTaskException = completion.exception;
                }
            }
            emitTrace(taskTraceEvent(TraceEventKind::TaskFailed, task));
        }
    }

    void KahnScheduler::handleProducerFailure(const ExecutionResource resource, ExecutionState& state)
    {
        state.executorFailure = true;
        state.submissionsEnabled = false;
        failInFlightForResource(resource, state);
    }

    void KahnScheduler::failInFlightTask(const TaskHandle handle) noexcept
    {
        const TaskRecord* const task{ startingGraph.findTaskRecord(handle) };
        if (task != nullptr && task->executionInfo.state == TaskState::Running)
        {
            task->executionInfo.state = TaskState::Failure;
            task->executionInfo.exception = nullptr;
            readyTaskAccounting->recordTerminal(task);
            markCancellationTerminal(handle);
        }
    }

    void KahnScheduler::failInFlightForResource(const ExecutionResource resource, ExecutionState& state) noexcept
    {
        for (auto entry{ inFlightTasks.begin() }; entry != inFlightTasks.end();)
        {
            if (entry->second.resource == resource)
            {
                failInFlightTask(entry->first);
                decrementInFlight(resource, state);
                entry = inFlightTasks.erase(entry);
            }
            else
            {
                ++entry;
            }
        }
    }

    void KahnScheduler::failAllInFlight(ExecutionState& state) noexcept
    {
        for (const auto& [handle, work] : inFlightTasks)
        {
            failInFlightTask(handle);
            decrementInFlight(work.resource, state);
        }
        inFlightTasks.clear();
    }

    void KahnScheduler::decrementInFlight(const ExecutionResource resource, ExecutionState& state) noexcept
    {
        std::size_t& count{ resource == ExecutionResource::CPU ? state.cpuInFlight : state.gpuInFlight };
        if (count != 0U)
        {
            --count;
        }
    }

    void KahnScheduler::applyPendingCancellations(ExecutionState& state)
    {
        std::lock_guard lock{ cancellationMutex };
        applyPendingCancellationsLocked(state);
    }

    void KahnScheduler::applyPendingCancellationsLocked(ExecutionState& state) noexcept
    {
        for (const TaskHandle handle : cancellationOrder)
        {
            auto cancellation{ cancellationStates.find(handle) };
            if (cancellation == cancellationStates.end() || cancellation->second != CancellationState::Requested)
            {
                continue;
            }

            const TaskRecord* const task{ startingGraph.findTaskRecord(handle) };
            if (task == nullptr)
            {
                cancellation->second = CancellationState::Terminal;
                continue;
            }
            if (task->executionInfo.state == TaskState::Running)
            {
                continue;
            }
            if (task->executionInfo.state == TaskState::Success || task->executionInfo.state == TaskState::Failure ||
                task->executionInfo.state == TaskState::Cancelled)
            {
                cancellation->second = CancellationState::Terminal;
                continue;
            }

            task->executionInfo.state = TaskState::Cancelled;
            task->executionInfo.exception = nullptr;
            readyTaskAccounting->recordTerminal(task);
            cancellation->second = CancellationState::Terminal;
            state.cancellationObserved = true;
            state.submissionsEnabled = false;
            emitTrace(taskTraceEvent(TraceEventKind::CancellationApplied, task));
        }
    }

    bool KahnScheduler::cancellationRequested(const TaskHandle handle) const
    {
        std::lock_guard lock{ cancellationMutex };
        const auto cancellation{ cancellationStates.find(handle) };
        return cancellation != cancellationStates.end() && cancellation->second == CancellationState::Requested;
    }

    void KahnScheduler::markCancellationTerminal(const TaskHandle handle) noexcept
    {
        std::lock_guard lock{ cancellationMutex };
        const auto cancellation{ cancellationStates.find(handle) };
        if (cancellation != cancellationStates.end())
        {
            cancellation->second = CancellationState::Terminal;
        }
    }

    void KahnScheduler::finishExecution(ExecutionState& state) noexcept
    {
        std::lock_guard lock{ cancellationMutex };
        applyPendingCancellationsLocked(state);
        readyTaskAccounting->finalize();
        executionFinished = true;
    }

    SchedulerStatus KahnScheduler::determineStatus(const ExecutionState& state) const noexcept
    {
        if (state.executorFailure)
        {
            return SchedulerStatus::ExecutorUnavailable;
        }
        if (state.policyFailure)
        {
            return SchedulerStatus::PolicyError;
        }
        if (state.taskFailureObserved)
        {
            return SchedulerStatus::TaskFailed;
        }
        if (state.cancellationObserved)
        {
            return SchedulerStatus::Cancelled;
        }
        if (state.successfulTaskCount == startingGraph.getTaskCount())
        {
            return SchedulerStatus::Success;
        }
        return SchedulerStatus::InvalidGraph;
    }

    bool KahnScheduler::parseDependencies()
    {
        readyTaskAccounting->reset();
        cpuReadyTasks.clear();
        gpuReadyTasks.clear();
        cpuReadyTasks.reserve(startingGraph.getTaskCount());
        gpuReadyTasks.reserve(startingGraph.getTaskCount());
        remainingDependencies.clear();
        cpuTaskCount = 0U;
        gpuTaskCount = 0U;

        for (const TaskHandle handle : startingGraph.getTaskHandles())
        {
            const TaskRecord* const task{ startingGraph.findTaskRecord(handle) };
            if (task == nullptr || !task->isValid())
            {
                return false;
            }
            if (task->options.executionResource == ExecutionResource::CPU)
            {
                ++cpuTaskCount;
            }
            else
            {
                ++gpuTaskCount;
            }
            if (task->getDependencies().empty())
            {
                appendReadyTask(task);
            }
            else
            {
                remainingDependencies.emplace(handle, task->getDependencies().size());
            }
        }
        return true;
    }

    void KahnScheduler::enqueueReadyTask(const TaskHandle handle)
    {
        const TaskRecord* const task{ startingGraph.findTaskRecord(handle) };
        if (task == nullptr)
        {
            return;
        }
        task->executionInfo.state = TaskState::Ready;
        appendReadyTask(task);
    }

    void KahnScheduler::appendReadyTask(const TaskRecord* const task)
    {
        readyTasksForResource(task->options.executionResource).push(SchedulingCandidate{ task->handle, task->options.priority });
        readyTaskAccounting->recordReady(task);
        TraceEvent event{ taskTraceEvent(TraceEventKind::TaskReady, task) };
        event.workUnitIndex = task->executionInfo.completedWorkUnitCount;
        emitTrace(event);
    }

    KahnScheduler::ReadyQueue& KahnScheduler::readyTasksForResource(const ExecutionResource resource) noexcept
    {
        return resource == ExecutionResource::CPU ? cpuReadyTasks : gpuReadyTasks;
    }

    SchedulingPolicy& KahnScheduler::policyForResource(const ExecutionResource resource) noexcept
    {
        return resource == ExecutionResource::CPU ? *cpuSchedulingPolicy : *gpuSchedulingPolicy;
    }

    void KahnScheduler::recordPolicyFailure(ExecutionState& state, std::exception_ptr exception) noexcept
    {
        state.policyFailure = true;
        state.submissionsEnabled = false;
        if (state.firstPolicyException == nullptr)
        {
            state.firstPolicyException = std::move(exception);
        }
        emitTrace(TraceEvent{ .kind = TraceEventKind::PolicyFailed,
                              .source = TraceEventSource::Scheduler,
                              .graphId = startingGraph.getGraphID().getValue() });
    }

    const KahnScheduler::TaskRecord* KahnScheduler::takeNextReadyTask(const ExecutionResource resource, ExecutionState& state)
    {
        ReadyQueue& readyQueue{ readyTasksForResource(resource) };
        while (!readyQueue.empty())
        {
            const std::span<const SchedulingCandidate> candidates{ readyQueue.candidates() };
            std::size_t selectedIndex{ 0U };
            try
            {
                selectedIndex = policyForResource(resource).selectNext(candidates);
                if (selectedIndex >= candidates.size())
                {
                    throw std::out_of_range{ "Scheduling policy selected an invalid candidate index" };
                }
            }
            catch (...)
            {
                recordPolicyFailure(state, std::current_exception());
                return nullptr;
            }

            const SchedulingCandidate selected{ candidates[selectedIndex] };
            const TaskHandle handle{ selected.handle };
            emitTrace(TraceEvent{ .kind = TraceEventKind::PolicyDecision,
                                  .source = TraceEventSource::Scheduler,
                                  .resource = resource,
                                  .hasTask = true,
                                  .hasResource = true,
                                  .graphId = handle.getGraphID().getValue(),
                                  .taskId = handle.getTaskID().getValue(),
                                  .readyCount = readyQueue.size(),
                                  .selectedIndex = selectedIndex,
                                  .priority = selected.priority });
            readyQueue.remove(selectedIndex);
            const TaskRecord* const task{ startingGraph.findTaskRecord(handle) };
            const bool schedulableState{ task != nullptr &&
                                         (task->executionInfo.state == TaskState::Ready ||
                                          (resource == ExecutionResource::GPU && task->executionInfo.state == TaskState::Paused)) };
            if (!schedulableState || task->options.executionResource != resource)
            {
                continue;
            }

            {
                std::lock_guard lock{ cancellationMutex };
                auto cancellation{ cancellationStates.find(handle) };
                if (cancellation != cancellationStates.end() && cancellation->second == CancellationState::Requested)
                {
                    cancellation->second = CancellationState::Terminal;
                    readyTaskAccounting->closeReadyInterval(task);
                    task->executionInfo.state = TaskState::Cancelled;
                    task->executionInfo.exception = nullptr;
                    readyTaskAccounting->recordTerminal(task);
                    state.cancellationObserved = true;
                    state.submissionsEnabled = false;
                    emitTrace(taskTraceEvent(TraceEventKind::CancellationApplied, task));
                    return nullptr;
                }
            }

            readyTaskAccounting->recordSelection(task, readyQueue.candidates());
            const bool resuming{ task->executionInfo.state == TaskState::Paused };
            const TaskState previousState{ task->executionInfo.state };
            task->executionInfo.state = TaskState::Running;
            task->executionInfo.exception = nullptr;
            if (!resuming)
            {
                task->executionInfo.executionDuration = std::chrono::nanoseconds{ 0 };
            }
            TraceEvent selectedEvent{ taskTraceEvent(resuming ? TraceEventKind::TaskResumed : TraceEventKind::TaskSelected, task) };
            selectedEvent.previousState = previousState;
            selectedEvent.state = TaskState::Running;
            selectedEvent.workUnitIndex = task->executionInfo.completedWorkUnitCount;
            emitTrace(selectedEvent);
            return task;
        }
        return nullptr;
    }

    void KahnScheduler::restoreRejectedTask(const TaskRecord* const task) noexcept
    {
        task->executionInfo.state = task->executionInfo.completedWorkUnitCount == 0U ? TaskState::Ready : TaskState::Paused;
        task->executionInfo.exception = nullptr;
    }

    void KahnScheduler::completeTask(const TaskRecord* const task, const TaskCompletion& completion)
    {
        task->executionInfo.exception = completion.exception;
        task->executionInfo.executionDuration += completion.executionDuration;
        if (completion.deviceExecutionDuration.has_value())
        {
            task->executionInfo.deviceExecutionDuration =
                task->executionInfo.deviceExecutionDuration.value_or(std::chrono::nanoseconds{ 0 }) +
                completion.deviceExecutionDuration.value();
        }
        if (!completion.succeeded())
        {
            task->executionInfo.state = TaskState::Failure;
            return;
        }

        ++task->executionInfo.completedWorkUnitCount;
        task->executionInfo.state = task->executionInfo.completedWorkUnitCount < task->executionInfo.totalWorkUnitCount
                                        ? TaskState::Paused
                                        : TaskState::Success;
    }

    void KahnScheduler::updateDependencies(const TaskRecord* const executedTask)
    {
        for (const TaskHandle handle : executedTask->getDependents())
        {
            auto dependencyEntry{ remainingDependencies.find(handle) };
            if (dependencyEntry == remainingDependencies.end())
            {
                continue;
            }
            --dependencyEntry->second;
            if (dependencyEntry->second == 0U)
            {
                enqueueReadyTask(handle);
                remainingDependencies.erase(dependencyEntry);
            }
        }
    }

    void KahnScheduler::emitTrace(TraceEvent event) noexcept
    {
        if constexpr (profilingEnabled)
        {
            if (traceSession != nullptr)
            {
                traceSession->emit(std::move(event));
            }
        }
        else
        {
            static_cast<void>(event);
        }
    }
} // namespace Atlas
