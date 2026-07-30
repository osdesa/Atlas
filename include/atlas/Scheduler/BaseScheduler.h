#ifndef ATLAS_BASE_SCHEDULER
#define ATLAS_BASE_SCHEDULER

#include "SchedulerResult.h"
#include "atlas/Tasking/TaskGraph.h"

/**
 * @file BaseScheduler.h
 * @brief Declares the common interface implemented by Atlas schedulers.
 */

/**
 * @defgroup scheduling Scheduling
 * @brief Types used to execute finalised task graphs.
 *
 * Scheduling provides a common interface and result model for current and future
 * task-graph execution strategies.
 */

namespace Atlas
{
    /**
     * @ingroup scheduling
     * @brief Defines the common interface implemented by Atlas schedulers.
     * @hideinheritancegraph
     * @plantumlfile base_scheduler.puml
     */
    class BaseScheduler
    {
      public:
        /**
         * @brief Constructs a scheduler for a finalised task graph.
         *
         * The scheduler does not own the graph. The graph must outlive the scheduler.
         *
         * @param taskGraph The finalised task graph to execute.
         * @throws std::invalid_argument When the task graph is not finalised.
         */
        explicit BaseScheduler(const TaskGraph& taskGraph);

        /**
         * @brief Executes the assigned task graph using the scheduler implementation.
         * @return The result of executing the graph.
         */
        virtual SchedulerResult execute() = 0;

        /**
         * @brief Executes one task function and captures any exception it throws.
         * @param taskFunction The task function to execute. An empty function is treated as
         * successful work.
         * @return A successful result when the function completes, or a task-failed result
         * containing the captured exception.
         */
        SchedulerResult executeFunction(const Atlas::TaskFunction& taskFunction);

        /// @brief Destroys the scheduler through its common interface.
        virtual ~BaseScheduler() = default;

        /// @brief Prevents copying a scheduler with a borrowed graph reference.
        BaseScheduler(const BaseScheduler&) = delete;

        /// @brief Prevents copy-assigning a scheduler with a borrowed graph reference.
        BaseScheduler& operator=(const BaseScheduler&) = delete;

        /// @brief Prevents moving a scheduler with a borrowed graph reference.
        BaseScheduler(BaseScheduler&&) = delete;

        /// @brief Prevents move-assigning a scheduler with a borrowed graph reference.
        BaseScheduler& operator=(BaseScheduler&&) = delete;

      protected:
        /// @brief The finalised task graph executed by this scheduler.
        const TaskGraph& startingGraph;
    };
} // namespace Atlas

#endif // !ATLAS_BASE_SCHEDULER
