#include "atlas/Executor/WorkerpoolExecutor.h"

#include "atlas/Executor/CompletionChannel.h"

#include <chrono>
#include <exception>
#include <stdexcept>
#include <utility>

namespace Atlas
{
    WorkerpoolExecutor::WorkerpoolExecutor(std::uint32_t maxThreads) : CpuExecutor{ maxThreads }
    {
        if (maxThreads == 0)
        {
            throw std::invalid_argument{ "maxThreads must be greater than 0" };
        }

        try
        {
            workerThreads.reserve(maxThreads);
            for (std::uint32_t workerIndex{ 0 }; workerIndex < maxThreads; ++workerIndex)
            {
                workerThreads.emplace_back([this, workerIndex] { workerLoop(workerIndex); });
            }
        }
        catch (...)
        {
            shutdown();
            throw;
        }
    }

    WorkerpoolExecutor::~WorkerpoolExecutor()
    {
        shutdown();
    }

    bool WorkerpoolExecutor::submit(TaskHandle taskHandle, TaskFunction taskFunction, CompletionChannel& completionChannel)
    {
        if (!taskHandle.isValid())
        {
            throw std::invalid_argument{ "Invalid task handle provided to WorkerpoolExecutor::submit" };
        }

        {
            std::lock_guard lock{ stateMutex };
            if (lifecycle != Lifecycle::Running)
            {
                return false;
            }

            taskQueue.emplace_back(WorkItem{
                std::move(taskFunction), TaskCompletion{ taskHandle, nullptr, std::chrono::microseconds{ 0 }, ExecutionResource::CPU },
                &completionChannel, completionChannel.traceSession() });
            ++unfinishedTasks;
        }

        workAvailable.notify_one();
        return true;
    }

    void WorkerpoolExecutor::shutdown() noexcept
    {
        {
            std::lock_guard lock{ stateMutex };
            if (lifecycle == Lifecycle::Stopped)
            {
                return;
            }

            lifecycle = Lifecycle::ShuttingDown;
        }

        workAvailable.notify_all();
        workerThreads.clear();

        {
            std::lock_guard lock{ stateMutex };
            lifecycle = Lifecycle::Stopped;
        }
    }

    void WorkerpoolExecutor::workerLoop(const std::size_t workerIndex)
    {
        while (true)
        {
            std::list<WorkItem> executingWork;
            {
                std::unique_lock lock{ stateMutex };
                workAvailable.wait(lock, [this] { return !taskQueue.empty() || lifecycle != Lifecycle::Running; });

                // If the executor is shutting down and there is no work left, exit the loop.
                if (taskQueue.empty())
                {
                    return;
                }

                executingWork.splice(executingWork.end(), taskQueue, taskQueue.begin());
            }

            WorkItem& workItem{ executingWork.front() };
            if constexpr (profilingEnabled)
            {
                if (workItem.traceSession != nullptr)
                {
                    workItem.traceSession->emit(TraceEvent{ .kind = TraceEventKind::BackendStarted,
                                                            .source = TraceEventSource::CpuExecutor,
                                                            .resource = ExecutionResource::CPU,
                                                            .hasTask = true,
                                                            .hasResource = true,
                                                            .graphId = workItem.completion.handle.getGraphID().getValue(),
                                                            .taskId = workItem.completion.handle.getTaskID().getValue(),
                                                            .workUnitIndex = 0U,
                                                            .workerIndex = workerIndex });
                }
            }
            const auto startTime{ std::chrono::steady_clock::now() };

            try
            {
                if (workItem.function)
                {
                    workItem.function();
                }
            }
            catch (...)
            {
                workItem.completion.exception = std::current_exception();
            }

            workItem.completion.executionDuration =
                std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - startTime);

            if constexpr (profilingEnabled)
            {
                if (workItem.traceSession != nullptr)
                {
                    workItem.traceSession->emit(TraceEvent{
                        .kind = TraceEventKind::BackendFinished,
                        .source = TraceEventSource::CpuExecutor,
                        .resource = ExecutionResource::CPU,
                        .hasTask = true,
                        .hasResource = true,
                        .graphId = workItem.completion.handle.getGraphID().getValue(),
                        .taskId = workItem.completion.handle.getTaskID().getValue(),
                        .workUnitIndex = 0U,
                        .workerIndex = workerIndex,
                        .hostDurationNanoseconds = static_cast<std::uint64_t>(
                            std::chrono::duration_cast<std::chrono::nanoseconds>(workItem.completion.executionDuration).count()) });
                }
            }

            workItem.completionChannel->publish(std::move(workItem.completion));
            std::lock_guard lock{ stateMutex };
            --unfinishedTasks;
        }
    }
} // namespace Atlas
