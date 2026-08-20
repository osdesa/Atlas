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
} // namespace Atlas
