#ifndef ATLAS_SCHEDULER_TIMING_ACCOUNTING
#define ATLAS_SCHEDULER_TIMING_ACCOUNTING

#include "atlas/Tasking/TaskHandle.h"

#include <chrono>
#include <cstddef>
#include <optional>

/**
 * @file SchedulerTimingAccounting.h
 * @brief Declares scheduler-internal scalar control-time measurements.
 */

namespace Atlas::Detail
{
    /**
     * @brief Accumulates scheduler-active and uncontested slice-switch durations.
     *
     * The scheduler control thread is the sole caller. Timing pauses around
     * executor submissions and blocking completion waits.
     */
    class SchedulerTimingAccounting final
    {
      public:
        /// @brief Resets all measurements and starts an active interval.
        void reset() noexcept;
        /// @brief Closes the current active interval when one is open.
        void pause() noexcept;
        /// @brief Starts an active interval when timing is paused.
        void resume() noexcept;
        /// @brief Marks an incomplete slice as awaiting its next GPU selection.
        void recordIncompleteSlice(TaskHandle handle) noexcept;
        /// @brief Records or rejects the pending immediate switch for an accepted GPU selection.
        void recordGpuSelection(TaskHandle handle) noexcept;

        /// @brief Returns accumulated active control time, including an open interval.
        std::chrono::microseconds activeDuration() const noexcept;
        /// @brief Returns accumulated immediate slice-switch control time.
        std::chrono::microseconds immediateSliceSwitchDuration() const noexcept;
        /// @brief Returns the number of sampled immediate slice switches.
        std::size_t immediateSliceSwitchCount() const noexcept;

      private:
        /// @brief Monotonic host clock used for scalar scheduler measurements.
        using Clock = std::chrono::steady_clock;

        /// @brief Active control interval start, or empty while paused.
        std::optional<Clock::time_point> activeSince;
        /// @brief Closed scheduler-active intervals.
        Clock::duration accumulatedActiveDuration{ Clock::duration::zero() };
        /// @brief Task whose incomplete slice may be resumed immediately.
        std::optional<TaskHandle> pendingSliceTask;
        /// @brief Active-duration snapshot when the incomplete task was requeued.
        Clock::duration pendingSliceActiveStart{ Clock::duration::zero() };
        /// @brief Accumulated active time for accepted immediate resumptions.
        Clock::duration accumulatedSliceSwitchDuration{ Clock::duration::zero() };
        /// @brief Number of accepted immediate resumption samples.
        std::size_t sliceSwitchCount{ 0U };
    };
} // namespace Atlas::Detail

#endif // !ATLAS_SCHEDULER_TIMING_ACCOUNTING
