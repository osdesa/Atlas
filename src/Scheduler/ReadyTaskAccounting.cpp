#include "ReadyTaskAccounting.h"

#include <optional>

/**
 * @file ReadyTaskAccounting.cpp
 * @brief Implements scheduler-internal ready-residency and bypass accounting.
 */

namespace Atlas::Detail
{
    ReadyTaskAccounting::ReadyTaskAccounting(const TaskGraph& taskGraph) noexcept : graph{ taskGraph } {}

    void ReadyTaskAccounting::reset()
    {
        readySince.clear();
        readySince.reserve(graph.getTaskCount());
        firstReadySince.clear();
        firstReadySince.reserve(graph.getTaskCount());
    }

    void ReadyTaskAccounting::recordReady(const TaskRecord* const task)
    {
        const Clock::time_point readyTime{ Clock::now() };
        readySince.try_emplace(task->handle, readyTime);
        firstReadySince.try_emplace(task->handle, readyTime);
    }

    void ReadyTaskAccounting::recordSelection(const TaskRecord* const selectedTask,
                                              const std::span<const SchedulingCandidate> remainingCandidates)
    {
        const Clock::time_point selectionTime{ Clock::now() };
        closeReadyIntervalAt(selectedTask, selectionTime);

        for (const SchedulingCandidate& candidate : remainingCandidates)
        {
            const TaskRecord* const task{ graph.findTaskRecord(candidate.handle) };
            if (task == nullptr || readySince.find(candidate.handle) == readySince.end())
            {
                continue;
            }

            const TaskState state{ task->executionInfo.state };
            if (state == TaskState::Ready || state == TaskState::Paused)
            {
                ++task->executionInfo.selectionBypassCount;
            }
        }
    }

    void ReadyTaskAccounting::closeReadyInterval(const TaskRecord* const task) noexcept
    {
        closeReadyIntervalAt(task, Clock::now());
    }

    void ReadyTaskAccounting::recordTerminal(const TaskRecord* const task) noexcept
    {
        const Clock::time_point terminalTime{ Clock::now() };
        closeReadyIntervalAt(task, terminalTime);
        const auto firstReady{ firstReadySince.find(task->handle) };
        if (firstReady == firstReadySince.end())
        {
            return;
        }

        task->executionInfo.responseDuration = std::chrono::duration_cast<std::chrono::nanoseconds>(terminalTime - firstReady->second);
        firstReadySince.erase(firstReady);
    }

    void ReadyTaskAccounting::finalize() noexcept
    {
        const Clock::time_point finishTime{ Clock::now() };
        for (auto entry{ readySince.begin() }; entry != readySince.end();)
        {
            const TaskRecord* const task{ graph.findTaskRecord(entry->first) };
            if (task != nullptr)
            {
                task->executionInfo.readyWaitDuration +=
                    std::chrono::duration_cast<std::chrono::nanoseconds>(finishTime - entry->second);
            }
            entry = readySince.erase(entry);
        }
        firstReadySince.clear();
    }

    void ReadyTaskAccounting::closeReadyIntervalAt(const TaskRecord* const task, const Clock::time_point endTime) noexcept
    {
        const auto entry{ readySince.find(task->handle) };
        if (entry == readySince.end())
        {
            return;
        }

        task->executionInfo.readyWaitDuration += std::chrono::duration_cast<std::chrono::nanoseconds>(endTime - entry->second);
        readySince.erase(entry);
    }
} // namespace Atlas::Detail
