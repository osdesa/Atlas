#include "SchedulerTimingAccounting.h"

/**
 * @file SchedulerTimingAccounting.cpp
 * @brief Implements scheduler-internal scalar control-time measurements.
 */

namespace Atlas::Detail
{
    void SchedulerTimingAccounting::reset() noexcept
    {
        accumulatedActiveDuration = Clock::duration::zero();
        pendingSliceTask.reset();
        pendingSliceActiveStart = Clock::duration::zero();
        accumulatedSliceSwitchDuration = Clock::duration::zero();
        sliceSwitchCount = 0U;
        activeSince = Clock::now();
    }

    void SchedulerTimingAccounting::pause() noexcept
    {
        if (!activeSince.has_value())
        {
            return;
        }
        accumulatedActiveDuration += Clock::now() - activeSince.value();
        activeSince.reset();
    }

    void SchedulerTimingAccounting::resume() noexcept
    {
        if (!activeSince.has_value())
        {
            activeSince = Clock::now();
        }
    }

    void SchedulerTimingAccounting::recordIncompleteSlice(const TaskHandle handle) noexcept
    {
        pendingSliceTask = handle;
        pendingSliceActiveStart = accumulatedActiveDuration;
        if (activeSince.has_value())
        {
            pendingSliceActiveStart += Clock::now() - activeSince.value();
        }
    }

    void SchedulerTimingAccounting::recordGpuSelection(const TaskHandle handle) noexcept
    {
        if (!pendingSliceTask.has_value())
        {
            return;
        }

        if (pendingSliceTask.value() == handle)
        {
            const Clock::duration currentActive{ activeSince.has_value()
                                                     ? accumulatedActiveDuration + (Clock::now() - activeSince.value())
                                                     : accumulatedActiveDuration };
            accumulatedSliceSwitchDuration += currentActive - pendingSliceActiveStart;
            ++sliceSwitchCount;
        }
        pendingSliceTask.reset();
    }

    std::chrono::nanoseconds SchedulerTimingAccounting::activeDuration() const noexcept
    {
        const Clock::duration duration{ activeSince.has_value() ? accumulatedActiveDuration + (Clock::now() - activeSince.value())
                                                                : accumulatedActiveDuration };
        return std::chrono::duration_cast<std::chrono::nanoseconds>(duration);
    }

    std::chrono::nanoseconds SchedulerTimingAccounting::immediateSliceSwitchDuration() const noexcept
    {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(accumulatedSliceSwitchDuration);
    }

    std::size_t SchedulerTimingAccounting::immediateSliceSwitchCount() const noexcept
    {
        return sliceSwitchCount;
    }
} // namespace Atlas::Detail
