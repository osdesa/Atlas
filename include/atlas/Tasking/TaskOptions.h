#ifndef ATLAS_TASK_OPTIONS
#define ATLAS_TASK_OPTIONS

#include "ExecutionResource.h"

#include <cstdint>
#include <string>
#include <utility>

/**
 * @file TaskOptions.h
 * @brief Declares configuration metadata for a task.
 */

namespace Atlas
{
    /**
     * @ingroup tasking
     * @brief Contains backend-neutral, immutable-on-submission metadata for a task.
     *
     * @par Class diagram
     * @plantumlfile task_options.puml
     */
    struct TaskOptions
    {
        /**
         * @brief Constructs task metadata.
         * @param taskName The optional human-readable task name.
         * @param resource The resource on which the task is intended to execute.
         * @param taskPriority The task's static priority.
         */
        explicit TaskOptions(std::string taskName = {}, ExecutionResource resource = ExecutionResource::CPU,
                             std::uint32_t taskPriority = 0U) noexcept
            : name{ std::move(taskName) }, executionResource{ resource }, priority{ taskPriority }
        {
        }

        /**
         * @brief Reports whether the task metadata uses a supported execution resource.
         * @return True for named and anonymous tasks with CPU or GPU resource intent.
         */
        bool isValid() const noexcept
        {
            return isValidExecutionResource(executionResource);
        }

        /// @brief Optional human-readable name of the task; empty names identify anonymous tasks.
        std::string name;

        /// @brief The resource on which the task is intended to execute.
        ExecutionResource executionResource;

        /// @brief The task's static priority, where lower values are higher priority.
        std::uint32_t priority;
    };
} // namespace Atlas

#endif // !ATLAS_TASK_OPTIONS
