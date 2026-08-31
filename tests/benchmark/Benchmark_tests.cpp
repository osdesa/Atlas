#include "BenchmarkConfig.h"
#include "BenchmarkMetrics.h"
#include "WorkloadGenerator.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

/** @file Benchmark_tests.cpp @brief Tests benchmark schemas, generation, and metric formulas. */

TEST_CASE("Benchmark manifest parser accepts the checked CPU smoke experiment", "[BENCHMARK]")
{
    const Atlas::Benchmark::ExperimentManifest manifest{ Atlas::Benchmark::loadManifest(
        std::filesystem::path{ ATLAS_CPU_BENCHMARK_MANIFEST_PATH }) };

    REQUIRE(manifest.schemaVersion == 1U);
    REQUIRE(manifest.experimentId == "cpu-smoke");
    REQUIRE(manifest.cpu.taskCount == 8U);
    REQUIRE(manifest.gpu.taskCount == 0U);
    REQUIRE(manifest.bursts.count == 2U);
    REQUIRE(manifest.policy.kind == Atlas::Benchmark::PolicyKind::StaticPriority);
}

TEST_CASE("Benchmark manifest parser rejects unknown fields", "[BENCHMARK]")
{
    const std::filesystem::path path{ std::filesystem::temp_directory_path() / "atlas-invalid-benchmark-manifest.json" };
    {
        std::ofstream output{ path };
        output << R"({"schema_version":1,"unexpected":true})";
    }
    REQUIRE_THROWS_AS(Atlas::Benchmark::loadManifest(path), std::runtime_error);
    std::filesystem::remove(path);
}

TEST_CASE("Generated workloads are deterministic, acyclic, and dependency-bursted", "[BENCHMARK]")
{
    Atlas::Benchmark::ExperimentManifest manifest;
    manifest.experimentId = "generated";
    manifest.seeds = { 7U };
    manifest.cpu.taskCount = 6U;
    manifest.gpu.taskCount = 2U;
    manifest.dependencies = { Atlas::Benchmark::DependencyShape::Random, 1U, 0.5 };
    manifest.priorities = { Atlas::Benchmark::PriorityAssignment::Random, { 0U, 4U, 9U } };
    manifest.bursts.count = 2U;

    const std::vector<Atlas::Benchmark::TaskDescriptor> first{ Atlas::Benchmark::generateWorkload(manifest, 99U) };
    const std::vector<Atlas::Benchmark::TaskDescriptor> second{ Atlas::Benchmark::generateWorkload(manifest, 99U) };

    REQUIRE(first.size() == 8U);
    REQUIRE(second.size() == first.size());
    for (std::size_t index{ 0U }; index < first.size(); ++index)
    {
        REQUIRE(first.at(index).resource == second.at(index).resource);
        REQUIRE(first.at(index).priority == second.at(index).priority);
        REQUIRE(first.at(index).dependencies == second.at(index).dependencies);
        for (const std::size_t dependency : first.at(index).dependencies)
        {
            REQUIRE(dependency < index);
        }
    }
    REQUIRE_FALSE(first.at(4U).dependencies.empty());
    REQUIRE(first.at(4U).dependencies.front() == 0U);
}

TEST_CASE("Benchmark metrics calculate utilization and Jain fairness", "[BENCHMARK]")
{
    Atlas::SchedulerResult result{ .status = Atlas::SchedulerStatus::Success,
                                   .executedTaskCount = 2U,
                                   .exception = nullptr,
                                   .executionTime = std::chrono::microseconds{ 100 },
                                   .schedulerActiveDuration = std::chrono::microseconds{ 10 },
                                   .immediateSliceSwitchDuration = std::chrono::microseconds{ 8 },
                                   .immediateSliceSwitchCount = 2U };
    const std::vector<Atlas::Benchmark::TaskMeasurement> tasks{
        { 0U, "first", Atlas::ExecutionResource::CPU, 0U, 0U, Atlas::TaskState::Success, std::chrono::microseconds{ 40 },
          std::chrono::microseconds{ 10 }, std::chrono::microseconds{ 50 }, 0U, 1U, 1U },
        { 1U, "second", Atlas::ExecutionResource::CPU, 0U, 0U, Atlas::TaskState::Success, std::chrono::microseconds{ 40 },
          std::chrono::microseconds{ 10 }, std::chrono::microseconds{ 50 }, 0U, 1U, 1U }
    };

    const Atlas::Benchmark::RunMetrics metrics{ Atlas::Benchmark::calculateMetrics(result, tasks, 2U) };
    REQUIRE(metrics.throughputTasksPerSecond == Catch::Approx(20'000.0));
    REQUIRE(metrics.responseLatency.mean == Catch::Approx(50.0));
    REQUIRE(metrics.schedulerActiveFraction == Catch::Approx(0.1));
    REQUIRE(metrics.immediateSliceSwitchMeanMicroseconds == Catch::Approx(4.0));
    REQUIRE(metrics.cpuBusyFraction == Catch::Approx(0.4));
    REQUIRE(metrics.cpuJainFairness == Catch::Approx(1.0));
    REQUIRE_FALSE(metrics.gpuHostBusyFraction.has_value());
}
