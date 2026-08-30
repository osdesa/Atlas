#ifndef ATLAS_TASK
#define ATLAS_TASK

#include "TaskExecutionInfo.h"
#include "TaskFunction.h"
#include "TaskHandle.h"
#include "TaskOptions.h"
#include "atlas/Vulkan/VulkanCompute.h"

#include <algorithm>
#include <cstdint>
#include <span>
#include <utility>
#include <variant>
#include <vector>

/**
 * @file Task.h
 * @brief Declares the graph-owned representation of a task.
 */

namespace Atlas
{
    /// @brief Exactly one typed CPU callable or declarative Vulkan dispatch.
    using TaskWork = std::variant<TaskFunction, VulkanDispatch, SlicedVulkanDispatch>;

    /**
     * @ingroup tasking
     * @brief Stores a task's identity, work, metadata, and graph edges.
     *
     * @par Class diagram
     * @plantumlfile task.puml
     */
    class Task
    {
      public:
        /**
         * @brief Constructs a task from its graph-owned identity and work.
         * @param taskHandle The immutable identity assigned by the task graph.
         * @param taskFunction The function to execute for this task.
         * @param taskOptions The metadata associated with this task.
         */
        explicit Task(TaskHandle taskHandle, TaskFunction taskFunction, TaskOptions taskOptions) noexcept
            : handle{ taskHandle }, options{ std::move(taskOptions) }, work{ std::move(taskFunction) }
        {
        }

        /**
         * @brief Constructs a task containing one declarative Vulkan dispatch.
         * @param taskHandle The immutable identity assigned by the task graph.
         * @param dispatch The validated GPU work to execute.
         * @param taskOptions The metadata associated with this task.
         */
        explicit Task(TaskHandle taskHandle, VulkanDispatch dispatch, TaskOptions taskOptions) noexcept
            : handle{ taskHandle }, options{ std::move(taskOptions) }, work{ std::move(dispatch) }
        {
        }

        /**
         * @brief Constructs a task containing one cooperatively sliced Vulkan dispatch.
         * @param taskHandle The immutable identity assigned by the task graph.
         * @param dispatch The validated logical GPU work and slicing geometry.
         * @param taskOptions The metadata associated with this task.
         */
        explicit Task(TaskHandle taskHandle, SlicedVulkanDispatch dispatch, TaskOptions taskOptions) noexcept
            : handle{ taskHandle }, options{ std::move(taskOptions) },
              executionInfo{ TaskState::Unknown, nullptr, std::chrono::microseconds{ 0 }, 0U, dispatch.sliceCount() },
              work{ std::move(dispatch) }
        {
        }

        /// @brief The immutable identity information of the task.
        const TaskHandle handle;

        /// @brief The immutable options for the task.
        const TaskOptions options;

        /// @brief Runtime state, failure, and duration information updated by the scheduler.
        mutable TaskExecutionInfo executionInfo;

        /**
         * @brief Reports whether this task has a valid handle and execution resource.
         * @return True when the handle and task options are valid; false otherwise.
         */
        bool isValid() const noexcept
        {
            const bool payloadMatchesResource{
                (std::holds_alternative<TaskFunction>(work) && options.executionResource == ExecutionResource::CPU) ||
                ((std::holds_alternative<VulkanDispatch>(work) || std::holds_alternative<SlicedVulkanDispatch>(work)) &&
                 options.executionResource == ExecutionResource::GPU)
            };
            return handle.isValid() && options.isValid() && payloadMatchesResource;
        }

        /// @brief Returns the CPU callable, or null when this is a GPU task.
        const TaskFunction* cpuFunction() const noexcept
        {
            return std::get_if<TaskFunction>(&work);
        }

        /// @brief Returns the Vulkan dispatch, or null when this is a CPU task.
        const VulkanDispatch* gpuDispatch() const noexcept
        {
            return std::get_if<VulkanDispatch>(&work);
        }

        /// @brief Returns the sliced Vulkan dispatch, or null for ordinary CPU or GPU work.
        const SlicedVulkanDispatch* slicedGpuDispatch() const noexcept
        {
            return std::get_if<SlicedVulkanDispatch>(&work);
        }

        /**
         * @brief Retrieves the dependencies of the task.
         * @return A read-only view of the task's dependencies.
         */
        std::span<const TaskHandle> getDependencies() const noexcept
        {
            return dependencies;
        }

        /**
         * @brief Retrieves the tasks that depend on this task.
         * @return A read-only view of dependent task
         * handles.
         */
        std::span<const TaskHandle> getDependents() const noexcept
        {
            return dependents;
        }

        /**
         * @brief Adds a new dependency to this task, indicating that this task cannot execute until
         * the specified dependency has completed.
         * @param dependency The task that must be completed before this task can execute.
         * @return True if the dependency was added successfully, false if it was already present.
         */
        bool addDependency(TaskHandle dependency);

        /**
         * @brief Adds a new dependent to this task, indicating that the specified dependent cannot
         * execute until this task has completed.
         * @param dependent The task that depends on this task to be executed.
         * @return True if the dependent was added successfully, false if it was already present.
         */
        bool addDependent(TaskHandle dependent);

        /**
         * @brief Removes an existing dependency from this task.
         * @param dependency The dependency to remove.
         */
        void removeDependency(TaskHandle dependency) noexcept;

        /**
         * @brief Compares two tasks for equality based on their handles.
         * @param other The other task to compare
         * with.
         * @return True when both tasks have the same handle; false otherwise.
         */
        bool operator==(const Task& other) const noexcept
        {
            return handle == other.handle;
        }

        /// @brief Prevents copying a graph-owned task.
        Task(const Task&) = delete;

        /// @brief Prevents copy-assigning a graph-owned task.
        Task& operator=(const Task&) = delete;

        /// @brief Prevents moving a graph-owned task.
        Task(Task&&) = delete;

        /// @brief Prevents move-assigning a graph-owned task.
        Task& operator=(Task&&) = delete;

      private:
        /// @brief Exactly one immutable CPU, ordinary Vulkan, or sliced Vulkan payload.
        const TaskWork work;

        /// @brief The tasks which need to be done before this task can be executed.
        std::vector<TaskHandle> dependencies;

        /// @brief The tasks which depend on this task to be executed.
        std::vector<TaskHandle> dependents;
    };
} // namespace Atlas
#endif // !ATLAS_TASK
