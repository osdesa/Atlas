#include "atlas/Executor/WorkerpoolExecutor.h"

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
                workerThreads.emplace_back([this] { workerLoop(); });
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

    bool WorkerpoolExecutor::submit(TaskHandle taskHandle, TaskFunction taskFunction)
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

            taskQueue.emplace_back(
                WorkItem{ std::move(taskFunction), TaskCompletion{ taskHandle, nullptr, std::chrono::microseconds{ 0 } } });
            ++unfinishedTasks;
        }

        workAvailable.notify_one();
        return true;
    }

    std::optional<TaskCompletion> WorkerpoolExecutor::waitForCompletion()
    {
        std::unique_lock lock{ stateMutex };
        workComplete.wait(lock, [this] { return !completions.empty() || unfinishedTasks == 0; });

        if (completions.empty())
        {
            return std::nullopt;
        }

        TaskCompletion completion{ std::move(completions.front().completion) };
        completions.pop_front();
        return completion;
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

        workComplete.notify_all();
    }

    void WorkerpoolExecutor::workerLoop()
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

            {
                std::lock_guard lock{ stateMutex };
                completions.splice(completions.end(), executingWork, executingWork.begin());
                --unfinishedTasks;
            }

            workComplete.notify_one();
        }
    }
} // namespace Atlas
