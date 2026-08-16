#ifndef ATLAS_TASK_GRAPH
#define ATLAS_TASK_GRAPH

#include "Task.h"
#include "TaskIdGenerator.h"

#include <cstddef>
#include <memory>
#include <optional>
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
        TaskGraph() noexcept : graphID{ GraphId::create() }, taskIdGenerator{ graphID }, isFinalised{ false } {}

        /**
         * @brief Adds a new task to the graph with the given work and metadata.
         * @param taskFunction The function to execute for this task. Empty functions are valid, but will not perform
         * any work.
         * @param taskOptions The metadata associated with this task.
         * @return The handle assigned to the new task.
         */
        std::optional<TaskHandle> addTask(TaskFunction taskFunction, TaskOptions taskOptions);

        /**
         * @brief Retrieves the handles of every task owned by this graph.
         * @return The task handles in insertion order.
         */
        std::vector<TaskHandle> getTaskHandles() const;

        /**
         * @brief Finds a task owned by this graph.
         * @param taskHandle The identity of the task to find.
         * @return The matching read-only task, or an empty optional when it is not in this graph.
         */
        std::optional<std::shared_ptr<const Task>> findTask(TaskHandle taskHandle) const noexcept;

        /**
         * @brief Adds a dependency between two tasks in the graph.
         *
         * @param dependent The task that depends on the other task.
         * @param dependency The task that must be completed before the dependent task can execute.
         * @return True when a new edge was added; false for invalid, cross-graph,
         * self, missing, duplicate, or cyclic dependencies.
         */
        bool addDependency(TaskHandle dependent, TaskHandle dependency);

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
            return isFinalised;
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
        /**
         * @brief Finds a mutable task owned by this graph for internal graph construction.
         *
         * @param taskHandle The identity of the task to find.
         * @return The matching mutable task, or an empty
         * optional when it is not in this graph.
         */
        std::optional<std::shared_ptr<Task>> findMutableTask(TaskHandle taskHandle) noexcept;

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
        bool addTaskLink(const std::shared_ptr<Task>& dependentTask, TaskHandle dependent, const std::shared_ptr<Task>& dependencyTask,
                         TaskHandle dependency);

        /**
         * @brief Checks if adding a dependency edge would create a cycle in the graph.
         * @param dependent The task that depends on the other task.
         * @param dependency The task that must be completed before the dependent task can execute.
         * @return True when a cycle would be created; false otherwise.
         */
        bool checkForCycles(TaskHandle dependent, TaskHandle dependency) const;

        /// @brief The collection of tasks owned by this graph.
        std::vector<std::shared_ptr<Task>> tasks;

        /// @brief The process-unique ID of this graph.
        GraphId graphID;

        /// @brief Allocates task handles for this graph.
        TaskIdGenerator taskIdGenerator;

        /// @brief Indicates the graph has been finalised and no more tasks can be added.
        bool isFinalised;
    };
} // namespace Atlas

#endif // !ATLAS_TASK_GRAPH
