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

    void ReadyTaskAccounting::recordReady(const std::shared_ptr<const Task>& task)
    {
        const Clock::time_point readyTime{ Clock::now() };
        readySince.try_emplace(task->handle, readyTime);
        firstReadySince.try_emplace(task->handle, readyTime);
    }

    void ReadyTaskAccounting::recordSelection(const std::shared_ptr<const Task>& selectedTask,
                                              const std::span<const SchedulingCandidate> remainingCandidates)
    {
        const Clock::time_point selectionTime{ Clock::now() };
        closeReadyIntervalAt(selectedTask, selectionTime);

        for (const SchedulingCandidate& candidate : remainingCandidates)
        {
            const std::optional<std::shared_ptr<const Task>> task{ graph.findTask(candidate.handle) };
            if (!task.has_value() || readySince.find(candidate.handle) == readySince.end())
            {
                continue;
            }

            const TaskState state{ task.value()->executionInfo.state };
            if (state == TaskState::Ready || state == TaskState::Paused)
            {
                ++task.value()->executionInfo.selectionBypassCount;
            }
        }
    }

    void ReadyTaskAccounting::closeReadyInterval(const std::shared_ptr<const Task>& task) noexcept
    {
        closeReadyIntervalAt(task, Clock::now());
    }

    void ReadyTaskAccounting::recordTerminal(const std::shared_ptr<const Task>& task) noexcept
    {
        const Clock::time_point terminalTime{ Clock::now() };
        closeReadyIntervalAt(task, terminalTime);
        const auto firstReady{ firstReadySince.find(task->handle) };
        if (firstReady == firstReadySince.end())
        {
            return;
        }

        task->executionInfo.responseDuration =
            std::chrono::duration_cast<std::chrono::microseconds>(terminalTime - firstReady->second);
        firstReadySince.erase(firstReady);
    }

    void ReadyTaskAccounting::finalize() noexcept
    {
        const Clock::time_point finishTime{ Clock::now() };
        for (auto entry{ readySince.begin() }; entry != readySince.end();)
        {
            const std::optional<std::shared_ptr<const Task>> task{ graph.findTask(entry->first) };
            if (task.has_value())
            {
                task.value()->executionInfo.readyWaitDuration +=
                    std::chrono::duration_cast<std::chrono::microseconds>(finishTime - entry->second);
            }
            entry = readySince.erase(entry);
        }
        firstReadySince.clear();
    }

    void ReadyTaskAccounting::closeReadyIntervalAt(const std::shared_ptr<const Task>& task, const Clock::time_point endTime) noexcept
    {
        const auto entry{ readySince.find(task->handle) };
        if (entry == readySince.end())
        {
            return;
        }

        task->executionInfo.readyWaitDuration += std::chrono::duration_cast<std::chrono::microseconds>(endTime - entry->second);
        readySince.erase(entry);
    }
} // namespace Atlas::Detail
