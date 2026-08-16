#ifndef ATLAS_TASK_ID
#define ATLAS_TASK_ID

#include <cstdint>

/**
 * @file TaskId.h
 * @brief Declares the strongly typed identifier of a task within a graph.
 */

namespace Atlas
{
    /// @brief Reserved task ID value representing an invalid task identifier.
    inline constexpr std::uint32_t INVALID_TASK_ID_VALUE{ 0U };

    /**
     * @ingroup tasking
     * @brief Identifies one task within its owning graph.
     */
    class TaskId
    {
      public:
        /// @brief Constructs an invalid task identifier.
        constexpr TaskId() noexcept = default;

        /**
         * @brief Constructs a task identifier from its underlying value.
         * @param identifier The graph-local task identifier value.
         */
        explicit constexpr TaskId(std::uint32_t identifier) noexcept : value{ identifier } {}

        /**
         * @brief Reports whether this identifier can identify a task.
         * @return True when the identifier is not the reserved invalid value.
         */
        constexpr bool isValid() const noexcept
        {
            return value != INVALID_TASK_ID_VALUE;
        }

        /**
         * @brief Retrieves the underlying graph-local identifier value.
         * @return The task identifier value.
         */
        constexpr std::uint32_t getValue() const noexcept
        {
            return value;
        }

        /// @brief Compares task identifiers by value.
        constexpr bool operator==(const TaskId& other) const noexcept = default;

      private:
        /// @brief The graph-local task identifier value.
        std::uint32_t value{ INVALID_TASK_ID_VALUE };
    };

    /// @brief Invalid strongly typed task identifier.
    inline constexpr TaskId INVALID_TASK_ID{};
} // namespace Atlas

#endif // !ATLAS_TASK_ID
