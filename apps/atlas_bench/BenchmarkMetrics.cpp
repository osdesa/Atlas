#include "BenchmarkMetrics.h"

#include <algorithm>
#include <cmath>
#include <numeric>

/**
 * @file BenchmarkMetrics.cpp
 * @brief Implements normalized benchmark metric calculations.
 */

namespace Atlas::Benchmark
{
    namespace
    {
        DurationDistribution distribution(std::vector<double> values)
        {
            if (values.empty())
            {
                return {};
            }
            std::sort(values.begin(), values.end());
            const auto nearestRank = [&values](const double percentile)
            {
                const double rank{ std::ceil(percentile * static_cast<double>(values.size())) };
                const std::size_t index{ static_cast<std::size_t>(std::max(1.0, rank)) - 1U };
                return values.at(index);
            };
            const double sum{ std::accumulate(values.begin(), values.end(), 0.0) };
            return DurationDistribution{ sum / static_cast<double>(values.size()), nearestRank(0.50), nearestRank(0.95),
                                         values.back() };
        }

        std::optional<double> jainFairness(const std::vector<TaskMeasurement>& tasks, const ExecutionResource resource)
        {
            std::vector<double> shares;
            for (const TaskMeasurement& task : tasks)
            {
                if (task.resource != resource || !task.responseDuration.has_value() || task.responseDuration->count() <= 0 ||
                    task.executionDuration.count() <= 0)
                {
                    continue;
                }
                shares.push_back(static_cast<double>(task.executionDuration.count()) /
                                 static_cast<double>(task.responseDuration->count()));
            }
            if (shares.empty())
            {
                return std::nullopt;
            }
            const double sum{ std::accumulate(shares.begin(), shares.end(), 0.0) };
            const double squares{ std::inner_product(shares.begin(), shares.end(), shares.begin(), 0.0) };
            if (squares == 0.0)
            {
                return std::nullopt;
            }
            return (sum * sum) / (static_cast<double>(shares.size()) * squares);
        }
    } // namespace

    RunMetrics calculateMetrics(const SchedulerResult& result, const std::vector<TaskMeasurement>& tasks,
                                const std::uint32_t workerCount)
    {
        RunMetrics metrics;
        const double completionMicroseconds{ static_cast<double>(result.executionTime.count()) };
        if (completionMicroseconds > 0.0)
        {
            metrics.throughputTasksPerSecond = static_cast<double>(result.executedTaskCount) * 1'000'000.0 / completionMicroseconds;
            metrics.schedulerActiveFraction = static_cast<double>(result.schedulerActiveDuration.count()) / completionMicroseconds;
        }

        std::vector<double> responses;
        std::vector<double> readyWaits;
        std::int64_t cpuExecution{ 0 };
        std::int64_t gpuExecution{ 0 };
        for (const TaskMeasurement& task : tasks)
        {
            readyWaits.push_back(static_cast<double>(task.readyWaitDuration.count()));
            if (task.responseDuration.has_value())
            {
                responses.push_back(static_cast<double>(task.responseDuration->count()));
            }
            if (task.resource == ExecutionResource::CPU)
            {
                cpuExecution += task.executionDuration.count();
            }
            else
            {
                gpuExecution += task.executionDuration.count();
            }
        }
        metrics.responseLatency = distribution(std::move(responses));
        metrics.readyWait = distribution(std::move(readyWaits));

        if (completionMicroseconds > 0.0 && workerCount != 0U &&
            std::any_of(tasks.begin(), tasks.end(),
                        [](const TaskMeasurement& task) { return task.resource == ExecutionResource::CPU; }))
        {
            metrics.cpuBusyFraction = static_cast<double>(cpuExecution) / (completionMicroseconds * static_cast<double>(workerCount));
        }
        if (completionMicroseconds > 0.0 && std::any_of(tasks.begin(), tasks.end(), [](const TaskMeasurement& task)
                                                        { return task.resource == ExecutionResource::GPU; }))
        {
            metrics.gpuHostBusyFraction = static_cast<double>(gpuExecution) / completionMicroseconds;
        }
        if (result.immediateSliceSwitchCount != 0U)
        {
            metrics.immediateSliceSwitchMeanMicroseconds = static_cast<double>(result.immediateSliceSwitchDuration.count()) /
                                                           static_cast<double>(result.immediateSliceSwitchCount);
        }
        metrics.cpuJainFairness = jainFairness(tasks, ExecutionResource::CPU);
        metrics.gpuJainFairness = jainFairness(tasks, ExecutionResource::GPU);
        return metrics;
    }
} // namespace Atlas::Benchmark
