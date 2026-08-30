#ifndef ATLAS_ROUND_ROBIN_SCHEDULING_POLICY
#define ATLAS_ROUND_ROBIN_SCHEDULING_POLICY

#include "SchedulingPolicy.h"

#include <optional>

/**
 * @file RoundRobinSchedulingPolicy.h
 * @brief Declares configurable work-unit round-robin selection.
 */

namespace Atlas
{
    /**
     * @ingroup scheduling
     * @brief Selects one logical task for at most a configured work-unit quantum.
     *
     * A selected task may move within the ready set when the scheduler appends
     * an incomplete work unit. If it remains ready, it retains the resource for
     * the rest of its quantum. A quantum of one matches FIFO work-unit rotation.
     */
    class RoundRobinSchedulingPolicy final : public SchedulingPolicy
    {
      public:
        /**
         * @brief Constructs a round-robin policy.
         * @param quantumValue Maximum consecutive selections for one logical task.
         * @throws std::invalid_argument When @p quantumValue is zero.
         */
        explicit RoundRobinSchedulingPolicy(std::size_t quantumValue = 1U);

        /// @copydoc SchedulingPolicy::clone
        [[nodiscard]] std::unique_ptr<SchedulingPolicy> clone() const override;
        /// @copydoc SchedulingPolicy::selectNext
        [[nodiscard]] std::size_t selectNext(std::span<const SchedulingCandidate> candidates) override;

        /// @brief Returns the configured work-unit quantum.
        [[nodiscard]] std::size_t getQuantum() const noexcept;

      private:
        /// @brief Maximum consecutive selections granted to one logical task.
        std::size_t quantum;
        /// @brief Task retaining the current resource-specific quantum.
        std::optional<TaskHandle> activeTask;
        /// @brief Selections still available to @ref activeTask.
        std::size_t remainingSelections{ 0U };
    };
} // namespace Atlas

#endif // !ATLAS_ROUND_ROBIN_SCHEDULING_POLICY
