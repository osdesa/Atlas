#ifndef ATLAS_FIFO_SCHEDULING_POLICY
#define ATLAS_FIFO_SCHEDULING_POLICY

#include "SchedulingPolicy.h"

/**
 * @file FifoSchedulingPolicy.h
 * @brief Declares stable first-in, first-out ready-task selection.
 */

namespace Atlas
{
    /**
     * @ingroup scheduling
     * @brief Selects the oldest candidate in a resource-specific ready set.
     */
    class FifoSchedulingPolicy final : public SchedulingPolicy
    {
      public:
        /// @copydoc SchedulingPolicy::clone
        [[nodiscard]] std::unique_ptr<SchedulingPolicy> clone() const override;
        /// @copydoc SchedulingPolicy::selectNext
        [[nodiscard]] std::size_t selectNext(std::span<const SchedulingCandidate> candidates) override;
    };
} // namespace Atlas

#endif // !ATLAS_FIFO_SCHEDULING_POLICY
