#ifndef ATLAS_TASK_HANDLE
#define ATLAS_TASK_HANDLE

#include <cstddef>
#include <cstdint>
#include <functional>

/**
 * @file TaskHandle.h
 * @brief Declares the value type that identifies a task within a graph.
 */

namespace Atlas
{
    /**
     * @ingroup tasking
     * @brief Reserved task ID representing an invalid task handle.
     */
    inline constexpr std::uint32_t INVALID_TASK_ID{ 0U };

    /**
     * @ingroup tasking
     * @brief Identifies a task within a specific task graph.
     *
     * @par Class diagram
     * @plantumlfile task_handle.puml

     */
    class TaskHandle
    {
      public:
        /**
         * @brief Constructs a task handle from its task and graph identifiers.
         * @param id The unique ID of the
         * task.
         * @param graphIdentifier The unique ID of the graph this task belongs to.
         */
        explicit TaskHandle(std::uint32_t id, std::uint32_t graphIdentifier) noexcept : taskID{ id }, graphID{ graphIdentifier } {}

        /**
         * @brief Validates if the current TaskHandle is valid
         * @return True if the TaskHandle is non 0, false otherwise
         */
        bool isValid() const noexcept
        {
            return taskID != INVALID_TASK_ID;
        }

        /**
         * @brief Retrieves the unique ID of the task
         * @return The unique ID of the task
         */
        std::uint32_t getTaskID() const noexcept
        {
            return taskID;
        }

        /**
         * @brief Retrieves the graph ID of the task
         * @return The graph ID of the task
         */
        std::uint32_t getGraphID() const noexcept
        {
            return graphID;
        }

        /**
         * @brief Compares two TaskHandles for equality
         * @param other The other TaskHandle to compare with
         * @return True if both TaskHandles
         * have the same task ID and graph ID, false otherwise
         */
        bool operator==(const TaskHandle& other) const noexcept = default;

        /// @brief Hashes task handles for use in unordered associative containers.
        struct Hash
        {
            /**
             * @brief Produces a hash value from both components of a task handle.
             * @param handle The
             * task handle to hash.
             * @return A hash value incorporating the graph and task identifiers.
             */
            std::size_t operator()(const TaskHandle& handle) const noexcept
            {
                const std::uint64_t key{ (static_cast<std::uint64_t>(handle.getGraphID()) << 32U) |
                                         static_cast<std::uint64_t>(handle.getTaskID()) };

                return std::hash<std::uint64_t>{}(key);
            }
        };

      private:
        /// @brief The ID of the task.
        std::uint32_t taskID{ 0U };

        /// @brief The ID of the graph this task belongs to.
        std::uint32_t graphID{ 0U };
    };
} // namespace Atlas
#endif // !ATLAS_TASK_HANDLE
