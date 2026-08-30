#include "atlas/Scheduler/RoundRobinSchedulingPolicy.h"

#include <algorithm>
#include <iterator>
#include <stdexcept>

/**
 * @file RoundRobinSchedulingPolicy.cpp
 * @brief Defines configurable work-unit round-robin selection.
 */

namespace Atlas
{
    RoundRobinSchedulingPolicy::RoundRobinSchedulingPolicy(const std::size_t quantumValue) : quantum{ quantumValue }
    {
        if (quantum == 0U)
        {
            throw std::invalid_argument{ "Round-robin quantum must be greater than zero" };
        }
    }

    std::unique_ptr<SchedulingPolicy> RoundRobinSchedulingPolicy::clone() const
    {
        return std::make_unique<RoundRobinSchedulingPolicy>(quantum);
    }

    std::size_t RoundRobinSchedulingPolicy::selectNext(const std::span<const SchedulingCandidate> candidates)
    {
        if (candidates.empty())
        {
            throw std::invalid_argument{ "Round-robin scheduling requires at least one candidate" };
        }

        if (activeTask.has_value() && remainingSelections != 0U)
        {
            const auto retained{ std::ranges::find(candidates, activeTask.value(), &SchedulingCandidate::handle) };
            if (retained != candidates.end())
            {
                --remainingSelections;
                if (remainingSelections == 0U)
                {
                    activeTask.reset();
                }
                return static_cast<std::size_t>(std::distance(candidates.begin(), retained));
            }
        }

        activeTask = candidates.front().handle;
        remainingSelections = quantum - 1U;
        if (remainingSelections == 0U)
        {
            activeTask.reset();
        }
        return 0U;
    }

    std::size_t RoundRobinSchedulingPolicy::getQuantum() const noexcept
    {
        return quantum;
    }
} // namespace Atlas
