#include "atlas/Scheduler/KahnScheduler.h"

#include "atlas/Scheduler/FifoSchedulingPolicy.h"

#include <algorithm>
#include <chrono>
#include <iterator>
#include <memory>
#include <optional>
#include <stdexcept>

namespace Atlas
{
    namespace
    {
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
    } // namespace

    KahnScheduler::KahnScheduler(const TaskGraph& taskGraph, CpuExecutor& executor)
        : KahnScheduler{ taskGraph, executor, FifoSchedulingPolicy{} }
    {
    }

    KahnScheduler::KahnScheduler(const TaskGraph& taskGraph, CpuExecutor& executor, const SchedulingPolicy& policy)
        : BaseScheduler{ taskGraph }, cpuExecutor{ executor }, cpuSchedulingPolicy{ clonePolicy(policy) }
    {
        cancellationOrder = startingGraph.getTaskHandles();
        cancellationStates.reserve(cancellationOrder.size());
        for (const TaskHandle handle : cancellationOrder)
        {
            cancellationStates.emplace(handle, CancellationState::None);
        }
    }

    KahnScheduler::KahnScheduler(const TaskGraph& taskGraph, CpuExecutor& cpuBackend, GpuExecutor& gpuBackend)
        : KahnScheduler{ taskGraph, cpuBackend, gpuBackend, FifoSchedulingPolicy{} }
    {
    }

    KahnScheduler::KahnScheduler(const TaskGraph& taskGraph, CpuExecutor& cpuBackend, GpuExecutor& gpuBackend,
                                 const SchedulingPolicy& policy)
        : BaseScheduler{ taskGraph }, cpuExecutor{ cpuBackend }, gpuExecutor{ &gpuBackend },
          cpuSchedulingPolicy{ clonePolicy(policy) }, gpuSchedulingPolicy{ clonePolicy(policy) }
    {
        cancellationOrder = startingGraph.getTaskHandles();
        cancellationStates.reserve(cancellationOrder.size());
        for (const TaskHandle handle : cancellationOrder)
        {
            cancellationStates.emplace(handle, CancellationState::None);
        }
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
            const std::optional<std::shared_ptr<const Task>> task{ startingGraph.findTask(taskHandle) };
            if (!task.has_value() || task.value()->executionInfo.state == TaskState::Success ||
                task.value()->executionInfo.state == TaskState::Failure || task.value()->executionInfo.state == TaskState::Cancelled)
            {
                entry->second = CancellationState::Terminal;
                return false;
            }
        }
        entry->second = CancellationState::Requested;
        return true;
    }

    SchedulerResult KahnScheduler::execute()
    {
        SchedulerResult result{ .status = SchedulerStatus::Unknown,
                                .executedTaskCount = 0U,
                                .exception = nullptr,
                                .executionTime = std::chrono::microseconds{ 0 } };
        const auto startTime{ std::chrono::steady_clock::now() };

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
            state.gpuCapacity =
                gpuExecutor == nullptr ? 0U : std::min(gpuTaskCount, static_cast<std::size_t>(gpuExecutor->maxConcurrency()));
            inFlightTasks.reserve(state.cpuCapacity + state.gpuCapacity);

            if ((cpuTaskCount != 0U && state.cpuCapacity == 0U) ||
                (gpuTaskCount != 0U && (gpuExecutor == nullptr || state.gpuCapacity == 0U)))
            {
                forcedStatus = SchedulerStatus::ExecutorUnavailable;
            }
            else if (gpuTaskCount == 0U)
            {
                applyPendingCancellations(state);
                runCpuOnlyExecutorLoop(state);
            }
            else
            {
                CompletionChannel channel{ state.cpuCapacity + state.gpuCapacity };
                applyPendingCancellations(state);
                runExecutorLoop(state, channel);
                channel.close();
            }
        }

        finishExecution(state);
        result.status = forcedStatus.value_or(determineStatus(state));
        result.executedTaskCount = state.successfulTaskCount;
        result.exception = state.firstTaskException != nullptr ? state.firstTaskException : state.firstPolicyException;
        result.executionTime = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - startTime);
        return result;
    }

    void KahnScheduler::runCpuOnlyExecutorLoop(ExecutionState& state)
    {
        while (true)
        {
            applyPendingCancellations(state);
            submitCpuOnlyReadyTasks(state);
            if (inFlightTasks.empty())
            {
                break;
            }

            const std::optional<TaskCompletion> completion{ cpuExecutor.waitForCompletion() };
            if (!completion.has_value())
            {
                state.executorFailure = true;
                state.completionStreamEndedEarly = true;
                state.submissionsEnabled = false;
                failAllInFlight(state);
                break;
            }
            TaskCompletion attributedCompletion{ completion.value() };
            attributedCompletion.resource = ExecutionResource::CPU;
            processCompletion(attributedCompletion, state);
            applyPendingCancellations(state);
        }

        if (!state.completionStreamEndedEarly && cpuExecutor.waitForCompletion().has_value())
        {
            state.executorFailure = true;
            state.submissionsEnabled = false;
        }
    }

    void KahnScheduler::submitCpuOnlyReadyTasks(ExecutionState& state)
    {
        while (state.submissionsEnabled && state.cpuInFlight < state.cpuCapacity)
        {
            const std::optional<std::shared_ptr<const Task>> task{ takeNextReadyTask(ExecutionResource::CPU, state) };
            if (!task.has_value())
            {
                return;
            }

            const TaskFunction* const function{ task.value()->cpuFunction() };
            bool accepted{ false };
            try
            {
                accepted = function != nullptr && cpuExecutor.submit(task.value()->handle, *function);
            }
            catch (...)
            {
                accepted = false;
            }

            if (!accepted)
            {
                restoreRejectedTask(task.value());
                state.executorFailure = true;
                state.submissionsEnabled = false;
                return;
            }
            inFlightTasks.emplace(task.value()->handle, InFlightWork{ ExecutionResource::CPU, 0U });
            ++state.cpuInFlight;
        }
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
        const std::vector<SchedulingCandidate>& readyQueue{ readyTasksForResource(resource) };

        if (resource == ExecutionResource::GPU && !readyQueue.empty() && gpuExecutor == nullptr)
        {
            state.executorFailure = true;
            state.submissionsEnabled = false;
            return;
        }

        while (state.submissionsEnabled && inFlight < capacity)
        {
            const std::optional<std::shared_ptr<const Task>> task{ takeNextReadyTask(resource, state) };
            if (!task.has_value())
            {
                return;
            }

            bool accepted{ false };
            std::size_t workUnitIndex{ 0U };
            try
            {
                if (resource == ExecutionResource::CPU)
                {
                    const TaskFunction* const function{ task.value()->cpuFunction() };
                    if (function == nullptr)
                    {
                        throw std::logic_error{ "CPU task has no CPU payload" };
                    }
                    accepted = cpuExecutor.submit(task.value()->handle, *function, channel);
                }
                else
                {
                    if (gpuExecutor == nullptr)
                    {
                        throw std::logic_error{ "GPU task has no GPU payload or executor" };
                    }
                    if (const VulkanDispatch* const dispatch{ task.value()->gpuDispatch() }; dispatch != nullptr)
                    {
                        workUnitIndex = dispatch->workUnitIndex();
                        accepted = gpuExecutor->submit(task.value()->handle, *dispatch, channel);
                    }
                    else if (const SlicedVulkanDispatch* const slicedDispatch{ task.value()->slicedGpuDispatch() };
                             slicedDispatch != nullptr)
                    {
                        VulkanDispatch workUnit{ slicedDispatch->slice(task.value()->executionInfo.completedWorkUnitCount) };
                        workUnitIndex = workUnit.workUnitIndex();
                        accepted = gpuExecutor->submit(task.value()->handle, std::move(workUnit), channel);
                    }
                    else
                    {
                        throw std::logic_error{ "GPU task has no GPU payload" };
                    }
                }
            }
            catch (...)
            {
                restoreRejectedTask(task.value());
                state.executorFailure = true;
                state.submissionsEnabled = false;
                return;
            }

            if (!accepted)
            {
                restoreRejectedTask(task.value());
                state.executorFailure = true;
                state.submissionsEnabled = false;
                return;
            }

            inFlightTasks.emplace(task.value()->handle, InFlightWork{ resource, workUnitIndex });
            ++inFlight;
        }
    }

    bool KahnScheduler::processNextEvent(ExecutionState& state, CompletionChannel& channel)
    {
        const CompletionEvent event{ channel.wait() };
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

        const std::optional<std::shared_ptr<const Task>> task{ startingGraph.findTask(completion.handle) };
        if (!task.has_value() || task.value()->executionInfo.state != TaskState::Running || completion.resource != expectedResource ||
            completion.workUnitIndex != expectedWork.workUnitIndex)
        {
            state.executorFailure = true;
            state.submissionsEnabled = false;
            if (task.has_value())
            {
                failInFlightTask(completion.handle);
            }
            return;
        }

        completeTask(task.value(), completion);
        if (completion.succeeded())
        {
            if (task.value()->executionInfo.state == TaskState::Paused)
            {
                if (cancellationRequested(completion.handle))
                {
                    task.value()->executionInfo.state = TaskState::Cancelled;
                    state.cancellationObserved = true;
                    state.submissionsEnabled = false;
                    markCancellationTerminal(completion.handle);
                }
                else
                {
                    appendReadyTask(task.value());
                }
                return;
            }

            markCancellationTerminal(completion.handle);
            ++state.successfulTaskCount;
            if (state.submissionsEnabled)
            {
                updateDependencies(task.value());
            }
        }
        else
        {
            markCancellationTerminal(completion.handle);
            state.taskFailureObserved = true;
            state.submissionsEnabled = false;
            if (state.firstTaskException == nullptr)
            {
                state.firstTaskException = completion.exception;
            }
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
        const std::optional<std::shared_ptr<const Task>> task{ startingGraph.findTask(handle) };
        if (task.has_value() && task.value()->executionInfo.state == TaskState::Running)
        {
            task.value()->executionInfo.state = TaskState::Failure;
            task.value()->executionInfo.exception = nullptr;
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

            const std::optional<std::shared_ptr<const Task>> task{ startingGraph.findTask(handle) };
            if (!task.has_value())
            {
                cancellation->second = CancellationState::Terminal;
                continue;
            }
            if (task.value()->executionInfo.state == TaskState::Running)
            {
                continue;
            }
            if (task.value()->executionInfo.state == TaskState::Success || task.value()->executionInfo.state == TaskState::Failure ||
                task.value()->executionInfo.state == TaskState::Cancelled)
            {
                cancellation->second = CancellationState::Terminal;
                continue;
            }

            task.value()->executionInfo.state = TaskState::Cancelled;
            task.value()->executionInfo.exception = nullptr;
            cancellation->second = CancellationState::Terminal;
            state.cancellationObserved = true;
            state.submissionsEnabled = false;
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
        cpuReadyTasks.clear();
        gpuReadyTasks.clear();
        cpuReadyTasks.reserve(startingGraph.getTaskCount());
        gpuReadyTasks.reserve(startingGraph.getTaskCount());
        remainingDependencies.clear();
        cpuTaskCount = 0U;
        gpuTaskCount = 0U;

        for (const TaskHandle handle : startingGraph.getTaskHandles())
        {
            const std::optional<std::shared_ptr<const Task>> task{ startingGraph.findTask(handle) };
            if (!task.has_value() || !task.value()->isValid())
            {
                return false;
            }
            if (task.value()->options.executionResource == ExecutionResource::CPU)
            {
                ++cpuTaskCount;
            }
            else
            {
                ++gpuTaskCount;
            }
            if (task.value()->getDependencies().empty())
            {
                appendReadyTask(task.value());
            }
            else
            {
                remainingDependencies.emplace(handle, task.value()->getDependencies().size());
            }
        }
        return true;
    }

    void KahnScheduler::enqueueReadyTask(const TaskHandle handle)
    {
        const std::optional<std::shared_ptr<const Task>> task{ startingGraph.findTask(handle) };
        if (!task.has_value())
        {
            return;
        }
        task.value()->executionInfo.state = TaskState::Ready;
        appendReadyTask(task.value());
    }

    void KahnScheduler::appendReadyTask(const std::shared_ptr<const Task>& task)
    {
        readyTasksForResource(task->options.executionResource)
            .emplace_back(SchedulingCandidate{ task->handle, task->options.priority });
    }

    std::vector<SchedulingCandidate>& KahnScheduler::readyTasksForResource(const ExecutionResource resource) noexcept
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
    }

    std::optional<std::shared_ptr<const Task>> KahnScheduler::takeNextReadyTask(const ExecutionResource resource,
                                                                                ExecutionState& state)
    {
        std::vector<SchedulingCandidate>& readyQueue{ readyTasksForResource(resource) };
        while (!readyQueue.empty())
        {
            std::size_t selectedIndex{ 0U };
            try
            {
                selectedIndex = policyForResource(resource).selectNext(readyQueue);
                if (selectedIndex >= readyQueue.size())
                {
                    throw std::out_of_range{ "Scheduling policy selected an invalid candidate index" };
                }
            }
            catch (...)
            {
                recordPolicyFailure(state, std::current_exception());
                return std::nullopt;
            }

            const TaskHandle handle{ readyQueue.at(selectedIndex).handle };
            using ReadyDifference = std::vector<SchedulingCandidate>::difference_type;
            readyQueue.erase(std::next(readyQueue.begin(), static_cast<ReadyDifference>(selectedIndex)));
            const std::optional<std::shared_ptr<const Task>> task{ startingGraph.findTask(handle) };
            const bool schedulableState{ task.has_value() && (task.value()->executionInfo.state == TaskState::Ready ||
                                                              (resource == ExecutionResource::GPU &&
                                                               task.value()->executionInfo.state == TaskState::Paused)) };
            if (!schedulableState || task.value()->options.executionResource != resource)
            {
                continue;
            }

            {
                std::lock_guard lock{ cancellationMutex };
                auto cancellation{ cancellationStates.find(handle) };
                if (cancellation != cancellationStates.end() && cancellation->second == CancellationState::Requested)
                {
                    cancellation->second = CancellationState::Terminal;
                    task.value()->executionInfo.state = TaskState::Cancelled;
                    task.value()->executionInfo.exception = nullptr;
                    state.cancellationObserved = true;
                    state.submissionsEnabled = false;
                    return std::nullopt;
                }
            }

            const bool resuming{ task.value()->executionInfo.state == TaskState::Paused };
            task.value()->executionInfo.state = TaskState::Running;
            task.value()->executionInfo.exception = nullptr;
            if (!resuming)
            {
                task.value()->executionInfo.executionDuration = std::chrono::microseconds{ 0 };
            }
            return task.value();
        }
        return std::nullopt;
    }

    void KahnScheduler::restoreRejectedTask(const std::shared_ptr<const Task>& task) noexcept
    {
        task->executionInfo.state = task->executionInfo.completedWorkUnitCount == 0U ? TaskState::Ready : TaskState::Paused;
        task->executionInfo.exception = nullptr;
    }

    void KahnScheduler::completeTask(const std::shared_ptr<const Task>& task, const TaskCompletion& completion)
    {
        task->executionInfo.exception = completion.exception;
        task->executionInfo.executionDuration += completion.executionDuration;
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

    void KahnScheduler::updateDependencies(const std::shared_ptr<const Task>& executedTask)
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
} // namespace Atlas
