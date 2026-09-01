#ifndef ATLAS_TASK_COMPLETION
#define ATLAS_TASK_COMPLETION

#include "atlas/Tasking/ExecutionResource.h"
#include "atlas/Tasking/TaskHandle.h"

#include <chrono>
#include <cstddef>
#include <exception>
#include <optional>
#include <utility>

/**
 * @file TaskCompletion.h
 * @brief Declares the outcome produced by executing one task payload.
 */

namespace Atlas
{
    /**
     * @ingroup executor
     * @brief Transfers one task execution outcome from an executor to a scheduler.
     *
     * This transient value does not contain task lifecycle state. The scheduler
     * remains responsible for applying the completion to TaskExecutionInfo.
     *
     * @par Class diagram
     * @plantumlfile task_completion.puml
     */
    struct TaskCompletion
    {
        /**
         * @brief Builds one attributed executor outcome.
         * @param taskHandle Graph-scoped task identity.
         * @param taskException Captured payload exception, if any.
         * @param duration Host-observed payload execution duration.
         * @param executionResource Backend that executed the payload.
         * @param unitIndex Zero-based sliced work-unit index.
         * @param deviceDuration Optional Vulkan device-clock duration.
         */
        TaskCompletion(TaskHandle taskHandle, std::exception_ptr taskException = nullptr,
                       std::chrono::microseconds duration = std::chrono::microseconds{ 0 },
                       ExecutionResource executionResource = ExecutionResource::CPU, std::size_t unitIndex = 0U,
                       std::optional<std::chrono::nanoseconds> deviceDuration = std::nullopt) noexcept
            : handle{ taskHandle }, exception{ std::move(taskException) }, executionDuration{ duration },
              resource{ executionResource }, workUnitIndex{ unitIndex }, deviceExecutionDuration{ deviceDuration }
        {
        }

        /// @brief Identifies the task that completed.
        TaskHandle handle;

        /// @brief The exception captured from backend execution, or nullptr on success.
        std::exception_ptr exception{ nullptr };

        /// @brief Time spent executing the task payload, excluding executor queue wait time.
        std::chrono::microseconds executionDuration{ 0 };

        /// @brief Backend resource that produced this completion.
        ExecutionResource resource{ ExecutionResource::CPU };

        /// @brief Zero-based work-unit index, or zero for an ordinary task payload.
        std::size_t workUnitIndex{ 0U };

        /// @brief Optional Vulkan device-clock duration for this work unit.
        std::optional<std::chrono::nanoseconds> deviceExecutionDuration;

        /**
         * @brief Reports whether backend execution completed without an exception.
         * @return True when no exception was captured.
         */
        bool succeeded() const noexcept
        {
            return exception == nullptr;
        }
    };
} // namespace Atlas

#endif // !ATLAS_TASK_COMPLETION
