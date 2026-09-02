#ifndef ATLAS_TASK_GRAPH
#define ATLAS_TASK_GRAPH

#include "TaskFunction.h"
#include "TaskSnapshot.h"
#include "atlas/Vulkan/VulkanCompute.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <variant>
#include <vector>

/**
 * @file TaskGraph.h
 * @brief Declares the task graph API.
 */
/**
 * @defgroup tasking Tasking
 * @brief Types used to describe tasks and their dependency graphs.
 *
 * Tasking models units of work, their metadata, and the relationships that
 * determine when work may execute.
 */

namespace Atlas
{
    class KahnScheduler;
    namespace Detail
    {
        class ReadyTaskAccounting;
    }

    /**
     * @ingroup tasking
     * @brief Represents a graph of tasks and their dependencies.
     *
     * A task graph owns a collection of tasks, defines their dependencies, and
     * later provides the scheduler with an execution-ready work description.
     *
     * @par Class diagram
     * @plantumlfile task_graph.puml
     */
    class TaskGraph
    {
      public:
        /**
         * @brief Constructs a new TaskGraph with a process-unique ID.
         */
        TaskGraph() noexcept : graphID{ GraphId::create() } {}

        /**
         * @brief Adds explicit CPU callable work to the graph.
         * @param taskFunction The callable to execute on the CPU backend.
         * @param taskOptions CPU task metadata; its resource must remain CPU.
         * @return The assigned handle, or empty when the graph rejects the task.
         */
        std::optional<TaskHandle> addCpuTask(TaskFunction taskFunction, TaskOptions taskOptions = TaskOptions{});

        /**
         * @brief Adds explicit Vulkan dispatch work to the graph.
         * @param dispatch The validated declarative dispatch to execute.
         * @param taskOptions GPU task metadata; its resource must remain GPU.
         * @return The assigned handle, or empty when the graph rejects the task.
         */
        std::optional<TaskHandle> addGpuTask(VulkanDispatch dispatch,
                                             TaskOptions taskOptions = TaskOptions{ {}, ExecutionResource::GPU });

        /**
         * @brief Adds cooperatively sliced Vulkan work to the graph.
         * @param dispatch The validated logical dispatch and slice geometry.
         * @param taskOptions GPU task metadata; its resource must remain GPU.
         * @return The assigned handle, or empty when the graph rejects the task.
         */
        std::optional<TaskHandle> addGpuTask(SlicedVulkanDispatch dispatch,
                                             TaskOptions taskOptions = TaskOptions{ {}, ExecutionResource::GPU });

        /**
         * @brief Retrieves the handles of every task owned by this graph.
         * @return The task handles in insertion order.
         */
        std::vector<TaskHandle> getTaskHandles() const;

        /**
         * @brief Copies a task's observable metadata and execution state.
         * @param taskHandle Identity to inspect.
         * @return A detached snapshot, or empty when the task is not graph-owned.
         */
        std::optional<TaskSnapshot> snapshotTask(TaskHandle taskHandle) const;

        /**
         * @brief Adds a dependency between two tasks in the graph.
         *
         * @param dependent The task that depends on the other task.
         * @param dependency The task that must be completed before the dependent task can execute.
         * @return True when a new edge was added; false for invalid, cross-graph,
         * self, missing, or duplicate dependencies. Cycle validation is deferred
         * to @ref finishTaskGraph so bulk graph construction remains linear.
         */
        bool addDependency(TaskHandle dependent, TaskHandle dependency);

        /**
         * @brief Removes one dependency edge before graph finalisation.
         * @param dependent The task that no longer depends on @p dependency.
         * @param dependency The prerequisite to remove.
         * @return True when the existing edge was removed; false otherwise.
         */
        bool removeDependency(TaskHandle dependent, TaskHandle dependency);

        /**
         * @brief Finalises a graph that contains no cycles and has at least one root task.
         * @return True when
         * the graph is finalised, including when it was already finalised;
         * false when its current structure cannot be
         * finalised.
         */
        bool finishTaskGraph();

        /**
         * @brief Returns the isFinalised state of the graph.
         * @return True when the graph is finalised; false otherwise.
         */
        bool isFinalisedGraph() const noexcept
        {
            return lifecycle.load(std::memory_order_acquire) != Lifecycle::Building;
        }

        /**
         * @brief Retrieves the number of tasks in the graph.
         * @return The number of tasks in the graph.
         */
        std::size_t getTaskCount() const noexcept
        {
            return tasks.size();
        }

        /**
         * @brief Retrieves the process-unique ID of this graph.
         * @return The process-unique ID of this graph.
         */
        GraphId getGraphID() const noexcept
        {
            return graphID;
        }

        /// @brief Destroys this task graph.
        ~TaskGraph() = default;

        /// @brief Prevents copying a task graph.
        TaskGraph(const TaskGraph&) = delete;

        /// @brief Prevents copy-assigning a task graph.
        TaskGraph& operator=(const TaskGraph&) = delete;

        /// @brief Prevents moving a task graph.
        TaskGraph(TaskGraph&&) = delete;

        /// @brief Prevents move-assigning a task graph.
        TaskGraph& operator=(TaskGraph&&) = delete;

      private:
        /** @brief Single-submission lifecycle for graph structure and execution. */
        enum class Lifecycle : std::uint8_t
        {
            Building,
            Finalised,
            Executing,
            Executed
        };

        /// @brief Exactly one graph-owned CPU or Vulkan payload.
        using TaskWork = std::variant<TaskFunction, VulkanDispatch, SlicedVulkanDispatch>;

        /** @brief Contiguous graph-private task storage and scheduler state. */
        struct TaskRecord final
        {
            TaskRecord(TaskHandle taskHandle, TaskWork taskWork, TaskOptions taskOptions) noexcept
                : handle{ taskHandle }, options{ std::move(taskOptions) }, work{ std::move(taskWork) }
            {
                if (const auto* sliced{ std::get_if<SlicedVulkanDispatch>(&work) }; sliced != nullptr)
                {
                    executionInfo.totalWorkUnitCount = sliced->sliceCount();
                }
            }

            bool isValid() const noexcept;
            const TaskFunction* cpuFunction() const noexcept;
            const VulkanDispatch* gpuDispatch() const noexcept;
            const SlicedVulkanDispatch* slicedGpuDispatch() const noexcept;
            std::span<const TaskHandle> getDependencies() const noexcept;
            std::span<const TaskHandle> getDependents() const noexcept;
            bool addDependency(TaskHandle dependency);
            bool addDependent(TaskHandle dependent);
            void removeDependency(TaskHandle dependency) noexcept;
            void removeDependent(TaskHandle dependent) noexcept;

            TaskHandle handle;
            TaskOptions options;
            mutable TaskExecutionInfo executionInfo;
            TaskWork work;
            std::vector<TaskHandle> dependencies;
            std::vector<TaskHandle> dependents;
        };

        std::optional<TaskHandle> addTaskWork(TaskWork work, TaskOptions taskOptions);

        /**
         * @brief Finds a mutable task owned by this graph for internal graph construction.
         *
         * @param taskHandle The identity of the task to find.
         * @return The matching mutable task, or an empty
         * optional when it is not in this graph.
         */
        TaskRecord* findTaskRecord(TaskHandle taskHandle) noexcept;
        const TaskRecord* findTaskRecord(TaskHandle taskHandle) const noexcept;

        /**
         * @brief Validates if the dependent and dependency tasks are valid and belong to this graph.
         * @param dependent The task that depends on the other task.
         * @param dependency The task that must be completed before the dependent task can execute.
         * @return True when both handles are valid, belong to this graph, and identify different
         * tasks.
         */
        bool validTaskLink(TaskHandle dependent, TaskHandle dependency) const noexcept;

        /**
         * @brief Adds a dependency edge to both tasks.
         *
         * If recording the dependent on the prerequisite fails, the dependency added to the
         * dependent task is removed before this function returns or rethrows.
         *
         * @param dependentTask The task that depends on the prerequisite.
         * @param dependent The handle of the dependent task.
         * @param dependencyTask The prerequisite task.
         * @param dependency The handle of the prerequisite task.
         * @return True when both tasks record the new edge; false when either record already
         * exists.
         */
        bool addTaskLink(TaskRecord& dependentTask, TaskHandle dependent, TaskRecord& dependencyTask, TaskHandle dependency);

        /**
         * @brief Sets the initial state of all tasks in the graph to Ready or Blocked based on their dependencies.
         */
        void setInitialStateForTasks() noexcept;

        /// @brief Atomically claims this finalised graph for its only execution.
        bool tryBeginExecution() const noexcept;
        /// @brief Records that the graph's single execution attempt has ended.
        void markExecutionFinished() const noexcept;

        /// @brief The collection of tasks owned by this graph.
        std::vector<TaskRecord> tasks;

        /// @brief The process-unique ID of this graph.
        GraphId graphID;

        /// @brief Next graph-local task identifier; zero is reserved as invalid.
        std::uint32_t nextTaskId{ 1U };

        /// @brief Enforces immutable structure and exactly one scheduler execution.
        mutable std::atomic<Lifecycle> lifecycle{ Lifecycle::Building };

        friend class KahnScheduler;
        friend class Detail::ReadyTaskAccounting;
    };
} // namespace Atlas

#endif // !ATLAS_TASK_GRAPH
