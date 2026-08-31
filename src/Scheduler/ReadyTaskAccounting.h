#ifndef ATLAS_READY_TASK_ACCOUNTING
#define ATLAS_READY_TASK_ACCOUNTING

#include "atlas/Scheduler/SchedulingPolicy.h"
#include "atlas/Tasking/TaskGraph.h"

#include <chrono>
#include <memory>
#include <span>
#include <unordered_map>

/**
 * @file ReadyTaskAccounting.h
 * @brief Declares scheduler-internal ready-residency and bypass accounting.
 */

namespace Atlas::Detail
{
    /**
     * @brief Tracks active ready intervals and applies selection statistics.
     *
     * The scheduler control thread is the sole caller. The helper borrows the
     * graph and updates only graph-owned TaskExecutionInfo values.
     */
    class ReadyTaskAccounting final
    {
      public:
        /**
         * @brief Borrows the graph whose ready tasks will be measured.
         * @param taskGraph Graph executed by the owning scheduler.
         */
        explicit ReadyTaskAccounting(const TaskGraph& taskGraph) noexcept;

        /// @brief Clears transient intervals before scheduler parsing begins.
        void reset();

        /**
         * @brief Starts a ready interval for a newly enqueued or resumed task.
         * @param task Task entering its resource-specific ready set.
         */
        void recordReady(const std::shared_ptr<const Task>& task);

        /**
         * @brief Closes the selected interval and increments remaining candidates.
         * @param selectedTask Valid task claimed by the scheduler.
         * @param remainingCandidates Same-resource ready entries after removal of
         * the selected candidate.
         */
        void recordSelection(const std::shared_ptr<const Task>& selectedTask,
                             std::span<const SchedulingCandidate> remainingCandidates);

        /**
         * @brief Closes one task's active interval without recording a selection.
         * @param task Task leaving readiness because cancellation became effective.
         */
        void closeReadyInterval(const std::shared_ptr<const Task>& task) noexcept;

        /**
         * @brief Records a terminal response interval for a previously ready task.
         * @param task Task that reached Success, Failure, or Cancelled.
         */
        void recordTerminal(const std::shared_ptr<const Task>& task) noexcept;

        /// @brief Closes all intervals still active at scheduler termination.
        void finalize() noexcept;

      private:
        /// @brief Monotonic clock used for ready-residency measurements.
        using Clock = std::chrono::steady_clock;

        /**
         * @brief Adds one interval ending at a shared selection/finalization time.
         * @param task Task whose active interval should be closed.
         * @param endTime Monotonic end of the interval.
         */
        void closeReadyIntervalAt(const std::shared_ptr<const Task>& task, Clock::time_point endTime) noexcept;

        /// @brief Borrowed graph used to resolve ready candidates by handle.
        const TaskGraph& graph;
        /// @brief Start time for each task currently resident in a ready set.
        std::unordered_map<TaskHandle, Clock::time_point, TaskHandle::Hash> readySince;
        /// @brief First scheduler-observed readiness for response-time accounting.
        std::unordered_map<TaskHandle, Clock::time_point, TaskHandle::Hash> firstReadySince;
    };
} // namespace Atlas::Detail

#endif // !ATLAS_READY_TASK_ACCOUNTING
