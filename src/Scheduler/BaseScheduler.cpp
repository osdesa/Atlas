#include "atlas/Scheduler/BaseScheduler.h"

#include <chrono>
#include <stdexcept>

/**
 * @file BaseScheduler.cpp
 * @brief Defines common scheduler task-graph validation.
 */

namespace Atlas
{
    BaseScheduler::BaseScheduler(const TaskGraph& taskGraph) : startingGraph{ taskGraph }
    {
        if (!startingGraph.isFinalisedGraph())
        {
            throw std::invalid_argument{ "BaseScheduler requires a finalised task graph" };
        }
    }

    SchedulerResult BaseScheduler::executeFunction(const Atlas::TaskFunction& taskFunction)
    {
        const auto startTime{ std::chrono::steady_clock::now() };
        SchedulerResult result{
            .status = SchedulerStatus::Success, .executedTaskCount = 0, .exception = nullptr, .executionTime = {}
        };
        try
        {
            if (taskFunction)
            {
                taskFunction();
            }
        }
        catch (...)
        {
            result.status = SchedulerStatus::TaskFailed;
            result.exception = std::current_exception();
        }

        result.executionTime = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - startTime);

        return result;
    }
} // namespace Atlas
