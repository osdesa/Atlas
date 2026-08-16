#ifndef ATLAS_TASK_HANDLE
#define ATLAS_TASK_HANDLE

#include "GraphId.h"
#include "TaskId.h"

#include <cstddef>
#include <functional>

/**
 * @file TaskHandle.h
 * @brief Declares the value type that identifies a task within a graph.
 */

namespace Atlas
{
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
         * @param id The graph-local identity of the task.
         * @param graphIdentifier The process-unique identity of the graph this task belongs to.
         */
        explicit TaskHandle(TaskId id, GraphId graphIdentifier) noexcept : taskID{ id }, graphID{ graphIdentifier } {}

        /**
         * @brief Validates if the current TaskHandle is valid
         * @return True if the TaskHandle is non 0, false otherwise
         */
        bool isValid() const noexcept
        {
            return taskID.isValid() && graphID.isValid();
        }

        /**
         * @brief Retrieves the graph-local ID of the task.
         * @return The graph-local ID of the task.
         */
        TaskId getTaskID() const noexcept
        {
            return taskID;
        }

        /**
         * @brief Retrieves the process-unique graph ID of the task.
         * @return The process-unique graph ID of the task.
         */
        GraphId getGraphID() const noexcept
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
                const std::size_t graphHash{ std::hash<std::uint64_t>{}(handle.getGraphID().getValue()) };
                const std::size_t taskHash{ std::hash<std::uint32_t>{}(handle.getTaskID().getValue()) };
                constexpr std::size_t hashConstant{ 0x9e3779b9U };

                return graphHash ^ (taskHash + hashConstant + (graphHash << 6U) + (graphHash >> 2U));
            }
        };

      private:
        /// @brief The ID of the task.
        TaskId taskID;

        /// @brief The ID of the graph this task belongs to.
        GraphId graphID;
    };
} // namespace Atlas
#endif // !ATLAS_TASK_HANDLE
