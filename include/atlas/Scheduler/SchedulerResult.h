#ifndef ATLAS_SCHEDULER_RESULT
#define ATLAS_SCHEDULER_RESULT

#include "SchedulerStatus.h"

#include <chrono>
#include <cstddef>
#include <exception>
#include <iosfwd>

/**
 * @file SchedulerResult.h
 * @brief Declares the result produced by a scheduler execution.
 */

namespace Atlas
{
    /**
     * @ingroup scheduling
     * @brief Describes the outcome of executing a task graph.
     */
    struct SchedulerResult
    {
        /// @brief Overall scheduler execution status.
        SchedulerStatus status{ SchedulerStatus::Unknown };

        /// @brief Number of tasks successfully executed.
        std::size_t executedTaskCount{ 0U };

        /**
         * @brief First task exception, otherwise the policy exception for a policy error.
         *
         * Empty when no task or scheduling-policy exception was captured.
         */
        std::exception_ptr exception;

        /// @brief Total elapsed time spent executing the graph, measured in microseconds.
        std::chrono::microseconds executionTime{ 0 };
    };

    /**
     * @ingroup scheduling
     * @brief Writes a human-readable scheduler result to an output stream.
     * @param stream The output stream to write to.
     * @param result The scheduler result to print.
     * @return The supplied output stream.
     */
    std::ostream& operator<<(std::ostream& stream, const SchedulerResult& result);
} // namespace Atlas

#endif // !ATLAS_SCHEDULER_RESULT
