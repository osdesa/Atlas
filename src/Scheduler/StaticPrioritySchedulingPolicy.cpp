#include "atlas/Scheduler/StaticPrioritySchedulingPolicy.h"

#include <algorithm>
#include <iterator>
#include <stdexcept>

/**
 * @file StaticPrioritySchedulingPolicy.cpp
 * @brief Defines stable static-priority ready-task selection.
 */

namespace Atlas
{
    std::unique_ptr<SchedulingPolicy> StaticPrioritySchedulingPolicy::clone() const
    {
        return std::make_unique<StaticPrioritySchedulingPolicy>();
    }

    std::size_t StaticPrioritySchedulingPolicy::selectNext(const std::span<const SchedulingCandidate> candidates)
    {
        if (candidates.empty())
        {
            throw std::invalid_argument{ "Static-priority scheduling requires at least one candidate" };
        }

        const auto selected{ std::ranges::min_element(candidates, {}, &SchedulingCandidate::priority) };
        return static_cast<std::size_t>(std::distance(candidates.begin(), selected));
    }
} // namespace Atlas
