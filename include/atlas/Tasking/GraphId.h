#ifndef ATLAS_GRAPH_ID
#define ATLAS_GRAPH_ID

#include <cstdint>

/**
 * @file GraphId.h
 * @brief Declares the opaque process-unique identity of a task graph.
 */

namespace Atlas
{
    /// @brief Reserved graph ID value representing an invalid graph identifier.
    inline constexpr std::uint64_t INVALID_GRAPH_ID_VALUE{ 0U };

    /**
     * @ingroup tasking
     * @brief Identifies one task graph within the current process.
     *
     * Valid graph identifiers are created by @ref create. Their underlying values
     * cannot be selected by callers, preventing independently constructed graphs
     * from accidentally sharing an identity.
     */
    class GraphId
    {
      public:
        /// @brief Constructs an invalid graph identifier.
        constexpr GraphId() noexcept = default;

        /**
         * @brief Allocates a process-unique graph identifier.
         * @return A valid graph identifier, or an invalid identifier if the ID space is exhausted.
         */
        static GraphId create() noexcept;

        /**
         * @brief Reports whether this identifier can identify a graph.
         * @return True when the identifier is not the reserved invalid value.
         */
        constexpr bool isValid() const noexcept
        {
            return value != INVALID_GRAPH_ID_VALUE;
        }

        /**
         * @brief Retrieves the underlying process-local identifier value.
         * @return The graph identifier value.
         */
        constexpr std::uint64_t getValue() const noexcept
        {
            return value;
        }

        /// @brief Compares graph identifiers by value.
        constexpr bool operator==(const GraphId& other) const noexcept = default;

      private:
        /**
         * @brief Constructs a valid graph identifier from an allocated value.
         * @param identifier The process-unique graph identifier value.
         */
        explicit constexpr GraphId(std::uint64_t identifier) noexcept : value{ identifier } {}

        /// @brief The process-local graph identifier value.
        std::uint64_t value{ INVALID_GRAPH_ID_VALUE };
    };
} // namespace Atlas

#endif // !ATLAS_GRAPH_ID
