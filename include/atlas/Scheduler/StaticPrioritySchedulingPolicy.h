#ifndef ATLAS_STATIC_PRIORITY_SCHEDULING_POLICY
#define ATLAS_STATIC_PRIORITY_SCHEDULING_POLICY

#include "SchedulingPolicy.h"

/**
 * @file StaticPrioritySchedulingPolicy.h
 * @brief Declares stable static-priority ready-task selection.
 */

namespace Atlas
{
    /**
     * @ingroup scheduling
     * @brief Selects the highest static priority and preserves FIFO ties.
     */
    class StaticPrioritySchedulingPolicy final : public SchedulingPolicy
    {
      public:
        /// @copydoc SchedulingPolicy::clone
        [[nodiscard]] std::unique_ptr<SchedulingPolicy> clone() const override;
        /// @copydoc SchedulingPolicy::selectNext
        [[nodiscard]] std::size_t selectNext(std::span<const SchedulingCandidate> candidates) override;
    };
} // namespace Atlas

#endif // !ATLAS_STATIC_PRIORITY_SCHEDULING_POLICY
