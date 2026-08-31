#include "BaselineAnalysis.h"
#include "BaselineConfig.h"
#include "BaselineRunner.h"
#include "BaselineWriter.h"
#include "BenchmarkConfig.h"
#include "BenchmarkMetrics.h"
#include "BenchmarkRunner.h"
#include "ResultWriter.h"
#include "WorkloadGenerator.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <optional>
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

TEST_CASE("Version-one benchmark writer emits schema-compatible scalars and task arrays", "[BENCHMARK]")
{
    const Atlas::Benchmark::ExperimentManifest manifest{ Atlas::Benchmark::loadManifest(
        std::filesystem::path{ ATLAS_CPU_BENCHMARK_MANIFEST_PATH }) };
    Atlas::Benchmark::BenchmarkRunner runner{ manifest };
    const Atlas::Benchmark::RunRecord record{ runner.runSingle(manifest.seeds.front(), 0U) };
    const std::filesystem::path outputDirectory{ std::filesystem::temp_directory_path() / "atlas-version-one-writer-contract" };
    std::filesystem::remove_all(outputDirectory);
    {
        Atlas::Benchmark::ResultWriter writer{ outputDirectory, manifest, false };
        writer.append(record);
    }

    std::ifstream runs{ outputDirectory / "runs.jsonl" };
    nlohmann::json output;
    runs >> output;
    REQUIRE(output.at("environment").is_object());
    REQUIRE(output.at("metrics").at("cpu_busy_fraction").is_number());
    REQUIRE(output.at("metrics").at("gpu_host_busy_fraction").is_null());
    REQUIRE(output.at("tasks").is_array());
    REQUIRE(output.at("tasks").front().is_object());
    std::filesystem::remove_all(outputDirectory);
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

TEST_CASE("Baseline suite parser accepts checked smoke and canonical matrices", "[BENCHMARK]")
{
    const Atlas::Benchmark::BaselineSuite smoke{ Atlas::Benchmark::loadBaselineSuite(
        std::filesystem::path{ ATLAS_BASELINE_CPU_SUITE_PATH }) };
    const Atlas::Benchmark::BaselineSuite canonical{ Atlas::Benchmark::loadBaselineSuite(
        std::filesystem::path{ ATLAS_BASELINE_CANONICAL_SUITE_PATH }) };

    REQUIRE(smoke.suiteId == "baseline-cpu-smoke");
    REQUIRE(smoke.cases.size() == 1U);
    REQUIRE(smoke.cases.front().variants.size() == 3U);
    REQUIRE(smoke.cases.front().variants.front().executionMode == Atlas::Benchmark::ExecutionMode::Direct);
    REQUIRE(canonical.cases.size() == 6U);
    REQUIRE(canonical.seeds.size() == 10U);
}

TEST_CASE("Checked benchmark schemas are syntactically valid JSON", "[BENCHMARK]")
{
    const std::filesystem::path directory{ ATLAS_BENCHMARK_SCHEMA_DIRECTORY };
    std::size_t count{ 0U };
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator{ directory })
    {
        if (entry.path().extension() != ".json")
        {
            continue;
        }
        std::ifstream input{ entry.path() };
        nlohmann::json schema;
        REQUIRE_NOTHROW(input >> schema);
        REQUIRE(schema.is_object());
        ++count;
    }
    REQUIRE(count == 6U);
}

TEST_CASE("Baseline environment metadata is strict and optional fields remain optional", "[BENCHMARK]")
{
    const std::filesystem::path validPath{ std::filesystem::temp_directory_path() / "atlas-valid-environment.json" };
    const std::filesystem::path invalidPath{ std::filesystem::temp_directory_path() / "atlas-invalid-environment.json" };
    {
        std::ofstream output{ validPath };
        output << R"({"schema_version":1,"environment_id":"test","cpu_model":"example","physical_memory_bytes":1024})";
    }
    {
        std::ofstream output{ invalidPath };
        output << R"({"schema_version":1,"environment_id":"test","unexpected":true})";
    }

    const Atlas::Benchmark::EnvironmentMetadata metadata{ Atlas::Benchmark::loadEnvironmentMetadata(validPath) };
    REQUIRE(metadata.environmentId == "test");
    REQUIRE(metadata.cpuModel == "example");
    REQUIRE(metadata.physicalMemoryBytes == 1024U);
    REQUIRE_THROWS_AS(Atlas::Benchmark::loadEnvironmentMetadata(invalidPath), std::runtime_error);
    std::filesystem::remove(validPath);
    std::filesystem::remove(invalidPath);
}

TEST_CASE("Every baseline variant materializes identical generated logical work", "[BENCHMARK]")
{
    const Atlas::Benchmark::BaselineSuite suite{ Atlas::Benchmark::loadBaselineSuite(
        std::filesystem::path{ ATLAS_BASELINE_CPU_SUITE_PATH }) };
    const Atlas::Benchmark::BaselineCase& comparisonCase{ suite.cases.front() };
    const Atlas::Benchmark::ExperimentManifest direct{ Atlas::Benchmark::makeExperimentManifest(suite, comparisonCase,
                                                                                                comparisonCase.variants.at(0U)) };
    const Atlas::Benchmark::ExperimentManifest priority{ Atlas::Benchmark::makeExperimentManifest(suite, comparisonCase,
                                                                                                  comparisonCase.variants.at(2U)) };
    const auto directTasks{ Atlas::Benchmark::generateWorkload(direct, 42U) };
    const auto priorityTasks{ Atlas::Benchmark::generateWorkload(priority, 42U) };

    REQUIRE(directTasks.size() == priorityTasks.size());
    for (std::size_t index{ 0U }; index < directTasks.size(); ++index)
    {
        REQUIRE(directTasks.at(index).resource == priorityTasks.at(index).resource);
        REQUIRE(directTasks.at(index).priority == priorityTasks.at(index).priority);
        REQUIRE(directTasks.at(index).dependencies == priorityTasks.at(index).dependencies);
    }
}

TEST_CASE("Direct and scheduled CPU suite variants complete paired trials", "[BENCHMARK]")
{
    const Atlas::Benchmark::BaselineSuite suite{ Atlas::Benchmark::loadBaselineSuite(
        std::filesystem::path{ ATLAS_BASELINE_CPU_SUITE_PATH }) };
    Atlas::Benchmark::BaselineSuiteRunner runner{ suite };
    const Atlas::Benchmark::BaselineBatch batch{ runner.run() };

    REQUIRE(batch.succeeded);
    REQUIRE(batch.records.size() == 6U);
    REQUIRE(batch.records.at(0U).variantId == "direct");
    REQUIRE(batch.records.at(3U).variantId == "fifo-unsliced");
    for (const Atlas::Benchmark::BaselineRunRecord& record : batch.records)
    {
        REQUIRE(record.run.schedulerResult.status == Atlas::SchedulerStatus::Success);
        REQUIRE(record.run.schedulerResult.executedTaskCount == 8U);
        REQUIRE(record.run.tasks.size() == 8U);
    }

    const Atlas::Benchmark::BaselineSummary summary{ Atlas::Benchmark::calculateBaselineSummary(suite, batch.records) };
    REQUIRE(summary.cases.size() == 1U);
    REQUIRE(summary.cases.front().variants.size() == 3U);
    REQUIRE(summary.cases.front().comparisons.size() == 2U);
    REQUIRE_FALSE(summary.cases.front().variants.front().metrics.front().confidenceInterval.lower.has_value());
}

TEST_CASE("Baseline bootstrap analysis is deterministic and paired by seed and repetition", "[BENCHMARK]")
{
    Atlas::Benchmark::BaselineSuite suite;
    suite.suiteId = "analysis";
    suite.seeds = { 1U, 2U };
    suite.repetitions = 2U;
    Atlas::Benchmark::BaselineCase comparisonCase;
    comparisonCase.caseId = "case";
    comparisonCase.referenceVariant = "direct";
    comparisonCase.variants = { { "direct", Atlas::Benchmark::ExecutionMode::Direct, {}, false, {} },
                                { "fifo", Atlas::Benchmark::ExecutionMode::Scheduled, {}, false, {} } };
    suite.cases.push_back(comparisonCase);

    std::vector<Atlas::Benchmark::BaselineRunRecord> records;
    for (const std::uint64_t seed : suite.seeds)
    {
        for (std::size_t repetition{ 0U }; repetition < suite.repetitions; ++repetition)
        {
            for (const Atlas::Benchmark::BaselineVariant& variant : comparisonCase.variants)
            {
                const bool direct{ variant.executionMode == Atlas::Benchmark::ExecutionMode::Direct };
                const auto duration{ std::chrono::microseconds{ static_cast<std::int64_t>(direct ? 100U : 120U) } };
                Atlas::SchedulerResult result{ .status = Atlas::SchedulerStatus::Success,
                                               .executedTaskCount = 1U,
                                               .exception = nullptr,
                                               .executionTime = duration,
                                               .schedulerActiveDuration = std::chrono::microseconds{ 10 },
                                               .immediateSliceSwitchDuration = {},
                                               .immediateSliceSwitchCount = 0U };
                Atlas::Benchmark::RunMetrics metrics;
                metrics.throughputTasksPerSecond = direct ? 10'000.0 : 8'333.333333333334;
                metrics.schedulerActiveFraction = direct ? 0.1 : (1.0 / 12.0);
                Atlas::Benchmark::RunRecord run{ "analysis", seed, repetition, result, {}, metrics, {}, {}, {} };
                records.push_back(Atlas::Benchmark::BaselineRunRecord{ suite.suiteId, comparisonCase.caseId, variant.variantId,
                                                                       variant.executionMode, 0U, std::move(run) });
            }
        }
    }

    const Atlas::Benchmark::BaselineSummary first{ Atlas::Benchmark::calculateBaselineSummary(suite, records) };
    const Atlas::Benchmark::BaselineSummary second{ Atlas::Benchmark::calculateBaselineSummary(suite, records) };
    const auto& completion{ first.cases.front().comparisons.front().metrics.front() };
    REQUIRE(completion.meanDifference == Catch::Approx(20.0));
    REQUIRE(completion.meanPercentChange == Catch::Approx(20.0));
    REQUIRE(completion.differenceConfidenceInterval.lower.has_value());
    REQUIRE(completion.differenceConfidenceInterval.lower ==
            second.cases.front().comparisons.front().metrics.front().differenceConfidenceInterval.lower);
}

TEST_CASE("Baseline writer emits scalar optional values and all successful suite files", "[BENCHMARK]")
{
    const Atlas::Benchmark::BaselineSuite suite{ Atlas::Benchmark::loadBaselineSuite(
        std::filesystem::path{ ATLAS_BASELINE_CPU_SUITE_PATH }) };
    Atlas::Benchmark::BaselineSuiteRunner runner{ suite };
    const Atlas::Benchmark::BaselineBatch batch{ runner.run() };
    const std::filesystem::path outputDirectory{ std::filesystem::temp_directory_path() / "atlas-baseline-writer-contract" };
    std::filesystem::remove_all(outputDirectory);
    {
        Atlas::Benchmark::BaselineWriter writer{ outputDirectory, suite, std::nullopt, batch.records, false };
        for (const Atlas::Benchmark::BaselineRunRecord& record : batch.records)
        {
            writer.append(record);
        }
        writer.writeSummary(Atlas::Benchmark::calculateBaselineSummary(suite, batch.records));
    }

    std::ifstream runs{ outputDirectory / "runs.jsonl" };
    nlohmann::json record;
    runs >> record;
    REQUIRE(record.at("environment").is_object());
    REQUIRE(record.at("metrics").at("cpu_busy_fraction").is_number());
    REQUIRE(record.at("metrics").at("gpu_host_busy_fraction").is_null());
    REQUIRE(record.at("tasks").front().at("selection_bypasses").is_null());
    REQUIRE(std::filesystem::exists(outputDirectory / "comparisons.json"));
    REQUIRE(std::filesystem::exists(outputDirectory / "comparisons.csv"));
    std::filesystem::remove_all(outputDirectory);
}
