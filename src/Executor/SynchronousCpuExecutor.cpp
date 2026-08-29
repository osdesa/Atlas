#include "atlas/Executor/SynchronousCpuExecutor.h"

#include "atlas/Executor/CompletionChannel.h"

#include <chrono>
#include <exception>
#include <stdexcept>
#include <utility>

namespace Atlas
{
    bool SynchronousCpuExecutor::submit(TaskHandle taskHandle, TaskFunction taskFunction)
    {
        if (!taskHandle.isValid())
        {
            throw std::invalid_argument{ "Invalid task handle provided to SynchronousCpuExecutor::submit" };
        }

        if (!acceptingSubmissions)
        {
            return false;
        }

        completions.emplace(execute(taskHandle, taskFunction));
        return true;
    }

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

        completionChannel.publish(execute(taskHandle, taskFunction));
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

    std::optional<TaskCompletion> SynchronousCpuExecutor::waitForCompletion()
    {
        if (completions.empty())
        {
            return std::nullopt;
        }

        TaskCompletion completion{ std::move(completions.front()) };
        completions.pop();
        return completion;
    }

    void SynchronousCpuExecutor::shutdown() noexcept
    {
        acceptingSubmissions = false;
    }
} // namespace Atlas
