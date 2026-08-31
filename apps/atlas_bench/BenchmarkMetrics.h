#ifndef ATLAS_BENCHMARK_METRICS
#define ATLAS_BENCHMARK_METRICS

#include "BenchmarkConfig.h"
#include "WorkloadGenerator.h"
#include "atlas/Scheduler/SchedulerResult.h"
#include "atlas/Tasking/TaskState.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

/**
 * @file BenchmarkMetrics.h
 * @brief Declares normalized per-task and per-run benchmark measurements.
 */

namespace Atlas::Benchmark
{
    /// @brief Aggregate distribution values in microseconds.
    struct DurationDistribution
    {
        std::optional<double> mean;
        std::optional<double> p50;
        std::optional<double> p95;
        std::optional<double> maximum;
    };

    /// @brief One graph task's immutable identity and terminal measurements.
    struct TaskMeasurement
    {
        std::size_t index{ 0U };
        std::string name;
        ExecutionResource resource{ ExecutionResource::CPU };
        std::uint32_t priority{ 0U };
        std::size_t burstIndex{ 0U };
        TaskState state{ TaskState::Unknown };
        std::chrono::microseconds executionDuration{ 0 };
        std::chrono::microseconds readyWaitDuration{ 0 };
        std::optional<std::chrono::microseconds> responseDuration;
        std::size_t selectionBypassCount{ 0U };
        std::size_t completedWorkUnitCount{ 0U };
        std::size_t totalWorkUnitCount{ 0U };
    };

    /// @brief Derived graph-level benchmark metrics.
    struct RunMetrics
    {
        double throughputTasksPerSecond{ 0.0 };
        DurationDistribution responseLatency;
        DurationDistribution readyWait;
        std::optional<double> schedulerActiveFraction;
        std::optional<double> immediateSliceSwitchMeanMicroseconds;
        std::optional<double> cpuBusyFraction;
        std::optional<double> gpuHostBusyFraction;
        std::optional<double> cpuJainFairness;
        std::optional<double> gpuJainFairness;
    };

    /// @brief Complete serializable outcome for one measured repetition.
    struct RunRecord
    {
        std::string experimentId;
        std::uint64_t seed{ 0U };
        std::size_t repetition{ 0U };
        SchedulerResult schedulerResult;
        std::vector<TaskMeasurement> tasks;
        RunMetrics metrics;
        std::optional<std::string> gpuDeviceName;
        std::optional<std::uint32_t> gpuApiVersion;
    };

    /**
     * @brief Derives aggregate metrics from one scheduler result and its tasks.
     * @param result Completed scheduler result.
     * @param tasks Per-task measurements from the same graph.
     * @param workerCount Configured fixed CPU-worker capacity.
     * @return Normalized aggregate measurements with unavailable values empty.
     */
    RunMetrics calculateMetrics(const SchedulerResult& result, const std::vector<TaskMeasurement>& tasks, std::uint32_t workerCount);
} // namespace Atlas::Benchmark

#endif // !ATLAS_BENCHMARK_METRICS
