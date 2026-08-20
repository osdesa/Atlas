#include "atlas/Executor/WorkerpoolExecutor.h"

#include <stdexcept>

namespace Atlas
{
    WorkerpoolExecutor::WorkerpoolExecutor(std::uint32_t maxThreads)
        : numThreads{ maxThreads }
    {
        if(numThreads == 0)
        {
            throw std::invalid_argument("maxThreads must be greater than 0");
        }
    }

    bool WorkerpoolExecutor::submit(TaskHandle taskHandle, TaskFunction taskFunction)
    {
        if(!taskHandle.isValid())
        {
            throw std::invalid_argument{ "Invalid task handle provided to WorkerpoolExecutor::submit" };
        }

        if(!acceptingSubmissions)
        {
            return false;
        }

        // Implementation for submitting a task to the worker pool
        return true; // Placeholder return value
    }

    std::optional<TaskCompletion> WorkerpoolExecutor::waitForCompletion()
    {
        // Implementation for waiting for a task completion
        return std::nullopt; // Placeholder return value
    }

    void WorkerpoolExecutor::shutdown() noexcept
    {
        // Join all active threads and prevent further submissions
        acceptingSubmissions = false;
    }
} // namespace Atlas