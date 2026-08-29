#include "atlas/Scheduler/KahnScheduler.h"

#include <algorithm>
#include <chrono>
#include <memory>
#include <optional>
#include <stdexcept>

namespace Atlas
{
    KahnScheduler::KahnScheduler(const TaskGraph& taskGraph, CpuExecutor& executor)
        : BaseScheduler{ taskGraph }, cpuExecutor{ executor }
    {
    }

    KahnScheduler::KahnScheduler(const TaskGraph& taskGraph, CpuExecutor& cpuBackend, GpuExecutor& gpuBackend)
        : BaseScheduler{ taskGraph }, cpuExecutor{ cpuBackend }, gpuExecutor{ &gpuBackend }
    {
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

        if (!parseDependencies())
        {
            result.status = SchedulerStatus::InvalidGraph;
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
                result.status = SchedulerStatus::ExecutorUnavailable;
            }
            else if (gpuTaskCount == 0U)
            {
                runCpuOnlyExecutorLoop(state);
                result.status = determineStatus(state);
                result.executedTaskCount = state.successfulTaskCount;
                result.exception = state.firstTaskException;
            }
            else
            {
                CompletionChannel channel{ state.cpuCapacity + state.gpuCapacity };
                runExecutorLoop(state, channel);
                channel.close();
                result.status = determineStatus(state);
                result.executedTaskCount = state.successfulTaskCount;
                result.exception = state.firstTaskException;
            }
        }

        result.executionTime = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - startTime);
        return result;
    }

    void KahnScheduler::runCpuOnlyExecutorLoop(ExecutionState& state)
    {
        while (true)
        {
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
            const std::optional<std::shared_ptr<const Task>> task{ takeNextReadyTask(ExecutionResource::CPU) };
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
            inFlightTasks.emplace(task.value()->handle, ExecutionResource::CPU);
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
        std::queue<TaskHandle>& readyQueue{ resource == ExecutionResource::CPU ? cpuReadyTasks : gpuReadyTasks };

        if (resource == ExecutionResource::GPU && !readyQueue.empty() && gpuExecutor == nullptr)
        {
            state.executorFailure = true;
            state.submissionsEnabled = false;
            return;
        }

        while (state.submissionsEnabled && inFlight < capacity)
        {
            const std::optional<std::shared_ptr<const Task>> task{ takeNextReadyTask(resource) };
            if (!task.has_value())
            {
                return;
            }

            bool accepted{ false };
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
                    const VulkanDispatch* const dispatch{ task.value()->gpuDispatch() };
                    if (dispatch == nullptr || gpuExecutor == nullptr)
                    {
                        throw std::logic_error{ "GPU task has no GPU payload or executor" };
                    }
                    accepted = gpuExecutor->submit(task.value()->handle, *dispatch, channel);
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

            inFlightTasks.emplace(task.value()->handle, resource);
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

        const ExecutionResource expectedResource{ inFlightEntry->second };
        decrementInFlight(expectedResource, state);
        inFlightTasks.erase(inFlightEntry);

        const std::optional<std::shared_ptr<const Task>> task{ startingGraph.findTask(completion.handle) };
        if (!task.has_value() || task.value()->executionInfo.state != TaskState::Running || completion.resource != expectedResource)
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
            ++state.successfulTaskCount;
            if (state.submissionsEnabled)
            {
                updateDependencies(task.value());
            }
        }
        else
        {
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
            task.value()->executionInfo.executionDuration = std::chrono::microseconds{ 0 };
        }
    }

    void KahnScheduler::failInFlightForResource(const ExecutionResource resource, ExecutionState& state) noexcept
    {
        for (auto entry{ inFlightTasks.begin() }; entry != inFlightTasks.end();)
        {
            if (entry->second == resource)
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
        for (const auto& [handle, resource] : inFlightTasks)
        {
            failInFlightTask(handle);
            decrementInFlight(resource, state);
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

    SchedulerStatus KahnScheduler::determineStatus(const ExecutionState& state) const noexcept
    {
        if (state.executorFailure)
        {
            return SchedulerStatus::ExecutorUnavailable;
        }
        if (state.taskFailureObserved)
        {
            return SchedulerStatus::TaskFailed;
        }
        if (state.successfulTaskCount == startingGraph.getTaskCount())
        {
            return SchedulerStatus::Success;
        }
        return SchedulerStatus::InvalidGraph;
    }

    bool KahnScheduler::parseDependencies()
    {
        cpuReadyTasks = {};
        gpuReadyTasks = {};
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
                if (task.value()->options.executionResource == ExecutionResource::CPU)
                {
                    cpuReadyTasks.emplace(handle);
                }
                else
                {
                    gpuReadyTasks.emplace(handle);
                }
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
        if (task.value()->options.executionResource == ExecutionResource::CPU)
        {
            cpuReadyTasks.emplace(handle);
        }
        else
        {
            gpuReadyTasks.emplace(handle);
        }
    }

    std::optional<std::shared_ptr<const Task>> KahnScheduler::takeNextReadyTask(const ExecutionResource resource)
    {
        std::queue<TaskHandle>& readyQueue{ resource == ExecutionResource::CPU ? cpuReadyTasks : gpuReadyTasks };
        while (!readyQueue.empty())
        {
            const TaskHandle handle{ readyQueue.front() };
            readyQueue.pop();
            const std::optional<std::shared_ptr<const Task>> task{ startingGraph.findTask(handle) };
            if (!task.has_value() || task.value()->executionInfo.state != TaskState::Ready ||
                task.value()->options.executionResource != resource)
            {
                continue;
            }

            task.value()->executionInfo.state = TaskState::Running;
            task.value()->executionInfo.exception = nullptr;
            task.value()->executionInfo.executionDuration = std::chrono::microseconds{ 0 };
            return task;
        }
        return std::nullopt;
    }

    void KahnScheduler::restoreRejectedTask(const std::shared_ptr<const Task>& task) noexcept
    {
        task->executionInfo.state = TaskState::Ready;
        task->executionInfo.exception = nullptr;
        task->executionInfo.executionDuration = std::chrono::microseconds{ 0 };
    }

    void KahnScheduler::completeTask(const std::shared_ptr<const Task>& task, const TaskCompletion& completion)
    {
        task->executionInfo.state = completion.succeeded() ? TaskState::Success : TaskState::Failure;
        task->executionInfo.exception = completion.exception;
        task->executionInfo.executionDuration = completion.executionDuration;
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
