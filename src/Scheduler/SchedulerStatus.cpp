#include "atlas/Scheduler/SchedulerStatus.h"

/**
 * @file SchedulerStatus.cpp
 * @brief Defines scheduler-status display names.
 */

namespace Atlas
{
    std::string_view toString(SchedulerStatus status) noexcept
    {
        switch (status)
        {
        case SchedulerStatus::Success:
            return "Success";
        case SchedulerStatus::InvalidGraph:
            return "InvalidGraph";
        case SchedulerStatus::GraphNotFinalised:
            return "GraphNotFinalised";
        case SchedulerStatus::TaskFailed:
            return "TaskFailed";
        case SchedulerStatus::ExecutorUnavailable:
            return "ExecutorUnavailable";
        case SchedulerStatus::Unknown:
            return "Unknown";
        }

        return "Unknown";
    }
} // namespace Atlas
