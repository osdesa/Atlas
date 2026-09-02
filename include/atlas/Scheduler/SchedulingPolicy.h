#ifndef ATLAS_SCHEDULING_POLICY
#define ATLAS_SCHEDULING_POLICY

#include "atlas/Tasking/TaskHandle.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

/**
 * @file SchedulingPolicy.h
 * @brief Declares backend-neutral ready-task selection contracts.
 */
/**
 * @defgroup scheduling Scheduling
 * @brief Dependency-aware resource scheduling and ready-task policies.
 */

namespace Atlas
{
    /**
     * @ingroup scheduling
     * @brief Describes one ready task without exposing its payload or backend.
     */
    struct SchedulingCandidate
    {
        /// @brief Graph-scoped identity of the ready task.
        TaskHandle handle;
        /// @brief Static task priority, where lower values have higher priority.
        std::uint32_t priority{ 0U };
    };

    /**
     * @ingroup scheduling
     * @brief Selects one task from a stable, enqueue-ordered ready set.
     *
     * Policies may retain private selection state. The scheduler owns ready
     * tasks and remains solely responsible for validation, lifecycle changes,
     * dependency release, and executor submission.
     * @plantumlfile scheduling_policy.puml
     */
    class SchedulingPolicy
    {
      public:
        /// @brief Destroys the policy through its public interface.
        virtual ~SchedulingPolicy() = default;

        /**
         * @brief Creates a fresh policy with the same configuration.
         * @return A non-null policy whose runtime selection state is reset.
         * @throws Any exception raised while allocating or copying configuration.
         */
        [[nodiscard]] virtual std::unique_ptr<SchedulingPolicy> clone() const = 0;

        /**
         * @brief Selects one candidate from a non-empty ready set.
         * @param candidates Ready candidates in stable enqueue order.
         * @return The zero-based index of the selected candidate.
         * @throws Any exception used to report a policy-specific failure.
         */
        [[nodiscard]] virtual std::size_t selectNext(std::span<const SchedulingCandidate> candidates) = 0;
    };
} // namespace Atlas

#endif // !ATLAS_SCHEDULING_POLICY
