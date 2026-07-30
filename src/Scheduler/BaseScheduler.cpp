#include "atlas/Scheduler/BaseScheduler.h"

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
        SchedulerResult result{ .status = SchedulerStatus::Success };
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

        return result;
    }
} // namespace Atlas
