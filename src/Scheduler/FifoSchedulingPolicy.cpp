#include "atlas/Scheduler/FifoSchedulingPolicy.h"

#include <stdexcept>

/**
 * @file FifoSchedulingPolicy.cpp
 * @brief Defines stable first-in, first-out ready-task selection.
 */

namespace Atlas
{
    std::unique_ptr<SchedulingPolicy> FifoSchedulingPolicy::clone() const
    {
        return std::make_unique<FifoSchedulingPolicy>();
    }

    std::size_t FifoSchedulingPolicy::selectNext(const std::span<const SchedulingCandidate> candidates)
    {
        if (candidates.empty())
        {
            throw std::invalid_argument{ "FIFO scheduling requires at least one candidate" };
        }
        return 0U;
    }
} // namespace Atlas
