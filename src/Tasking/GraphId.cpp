#include "atlas/Tasking/GraphId.h"

#include <atomic>
#include <cstdint>

/**
 * @file GraphId.cpp
 * @brief Defines process-unique task-graph identity allocation.
 */

namespace
{
    std::atomic<std::uint64_t> nextGraphId{ 1U };
}

namespace Atlas
{
    GraphId GraphId::create() noexcept
    {
        std::uint64_t candidate{ nextGraphId.load(std::memory_order_relaxed) };

        while (candidate != INVALID_GRAPH_ID_VALUE)
        {
            const std::uint64_t following{ candidate + 1U };
            if (nextGraphId.compare_exchange_weak(candidate, following, std::memory_order_relaxed, std::memory_order_relaxed))
            {
                return GraphId{ candidate };
            }
        }

        return GraphId{};
    }
} // namespace Atlas
