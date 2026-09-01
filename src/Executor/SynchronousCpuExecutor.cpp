#include "atlas/Executor/SynchronousCpuExecutor.h"

#include "atlas/Executor/CompletionChannel.h"

#include <chrono>
#include <exception>
#include <stdexcept>
#include <utility>

namespace Atlas
{
    bool SynchronousCpuExecutor::submit(TaskHandle taskHandle, TaskFunction taskFunction, CompletionChannel& completionChannel)
    {
        if (!taskHandle.isValid())
        {
            throw std::invalid_argument{ "Invalid task handle provided to SynchronousCpuExecutor::submit" };
        }

        if (!acceptingSubmissions)
        {
            return false;
        }

        TraceSession* const traceSession{ completionChannel.traceSession() };
        if constexpr (profilingEnabled)
        {
            if (traceSession != nullptr)
            {
                traceSession->emit(TraceEvent{ .kind = TraceEventKind::BackendStarted,
                                               .source = TraceEventSource::CpuExecutor,
                                               .resource = ExecutionResource::CPU,
                                               .hasTask = true,
                                               .hasResource = true,
                                               .graphId = taskHandle.getGraphID().getValue(),
                                               .taskId = taskHandle.getTaskID().getValue(),
                                               .workUnitIndex = 0U,
                                               .workerIndex = 0U });
            }
        }
        TaskCompletion completion{ execute(taskHandle, taskFunction) };
        if constexpr (profilingEnabled)
        {
            if (traceSession != nullptr)
            {
                traceSession->emit(
                    TraceEvent{ .kind = TraceEventKind::BackendFinished,
                                .source = TraceEventSource::CpuExecutor,
                                .resource = ExecutionResource::CPU,
                                .hasTask = true,
                                .hasResource = true,
                                .graphId = taskHandle.getGraphID().getValue(),
                                .taskId = taskHandle.getTaskID().getValue(),
                                .workUnitIndex = 0U,
                                .workerIndex = 0U,
                                .hostDurationNanoseconds = static_cast<std::uint64_t>(
                                    std::chrono::duration_cast<std::chrono::nanoseconds>(completion.executionDuration).count()) });
            }
        }
        completionChannel.publish(std::move(completion));
        return true;
    }

    TaskCompletion SynchronousCpuExecutor::execute(TaskHandle taskHandle, const TaskFunction& taskFunction)
    {
        TaskCompletion completion{ taskHandle, nullptr, std::chrono::microseconds{ 0 }, ExecutionResource::CPU };

        const auto startTime{ std::chrono::steady_clock::now() };
        try
        {
            if (taskFunction)
            {
                taskFunction();
            }
        }
        catch (...)
        {
            completion.exception = std::current_exception();
        }

        const auto endTime{ std::chrono::steady_clock::now() };
        completion.executionDuration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
        return completion;
    }

    void SynchronousCpuExecutor::shutdown() noexcept
    {
        acceptingSubmissions = false;
    }
} // namespace Atlas
