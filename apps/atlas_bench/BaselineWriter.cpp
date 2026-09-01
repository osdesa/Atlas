#include "BaselineWriter.h"

#include "atlas/Scheduler/SchedulerStatus.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <locale>
#include <nlohmann/json.hpp>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

/**
 * @file BaselineWriter.cpp
 * @brief Implements additive Milestone 11 suite result serialization.
 */

#ifndef ATLAS_BENCHMARK_ATLAS_VERSION
#define ATLAS_BENCHMARK_ATLAS_VERSION "unknown"
#endif
#ifndef ATLAS_BENCHMARK_GIT_REVISION
#define ATLAS_BENCHMARK_GIT_REVISION "unknown"
#endif
#ifndef ATLAS_BENCHMARK_COMPILER
#define ATLAS_BENCHMARK_COMPILER "unknown"
#endif
#ifndef ATLAS_BENCHMARK_BUILD_TYPE
#define ATLAS_BENCHMARK_BUILD_TYPE "unknown"
#endif
#ifndef ATLAS_BENCHMARK_SYSTEM
#define ATLAS_BENCHMARK_SYSTEM "unknown"
#endif
#ifndef ATLAS_BENCHMARK_SYSTEM_PROCESSOR
#define ATLAS_BENCHMARK_SYSTEM_PROCESSOR "unknown"
#endif
#ifndef ATLAS_BENCHMARK_CMAKE_VERSION
#define ATLAS_BENCHMARK_CMAKE_VERSION "unknown"
#endif

namespace Atlas::Benchmark
{
    namespace
    {
        using Json = nlohmann::json;

        Json optionalNumber(const std::optional<double> value)
        {
            return value.has_value() ? Json(value.value()) : Json(nullptr);
        }

        Json optionalString(const std::optional<std::string>& value)
        {
            return value.has_value() ? Json(value.value()) : Json(nullptr);
        }

        Json dimensionsJson(const DispatchDimensions dimensions)
        {
            return Json{ { "x", dimensions.x }, { "y", dimensions.y }, { "z", dimensions.z } };
        }

        Json policyJson(const PolicyConfig& policy)
        {
            Json result{ { "type", toString(policy.kind) } };
            if (policy.kind == PolicyKind::RoundRobin)
            {
                result["quantum"] = policy.quantum;
            }
            return result;
        }

        Json workloadJson(const BaselineWorkloadConfig& workload)
        {
            Json dependencies{ { "shape", toString(workload.dependencies.shape) } };
            if (workload.dependencies.shape == DependencyShape::Layered)
            {
                dependencies["layers"] = workload.dependencies.layers;
            }
            if (workload.dependencies.shape == DependencyShape::Random)
            {
                dependencies["edge_probability"] = workload.dependencies.edgeProbability;
            }
            return Json{ { "cpu", { { "task_count", workload.cpu.taskCount }, { "iterations", workload.cpu.iterations } } },
                         { "gpu",
                           { { "task_count", workload.gpu.taskCount }, { "workgroups", dimensionsJson(workload.gpu.workgroups) } } },
                         { "dependencies", std::move(dependencies) },
                         { "priorities",
                           { { "assignment", toString(workload.priorities.assignment) }, { "values", workload.priorities.values } } },
                         { "bursts", { { "count", workload.bursts.count } } } };
        }

        Json suiteJson(const BaselineSuite& suite)
        {
            Json cases = Json::array();
            for (const BaselineCase& comparisonCase : suite.cases)
            {
                Json variants = Json::array();
                for (const BaselineVariant& variant : comparisonCase.variants)
                {
                    Json item{ { "variant_id", variant.variantId }, { "execution", toString(variant.executionMode) } };
                    if (variant.executionMode == ExecutionMode::Scheduled)
                    {
                        item["policy"] = policyJson(variant.policy);
                        item["slice_workgroups"] = variant.sliced ? dimensionsJson(variant.sliceWorkgroups) : Json(nullptr);
                    }
                    variants.push_back(std::move(item));
                }
                cases.push_back(Json{ { "case_id", comparisonCase.caseId },
                                      { "workload", workloadJson(comparisonCase.workload) },
                                      { "reference_variant", comparisonCase.referenceVariant },
                                      { "variants", std::move(variants) } });
            }
            return Json{ { "schema_version", suite.schemaVersion },
                         { "suite_id", suite.suiteId },
                         { "seeds", suite.seeds },
                         { "warmup_runs", suite.warmupRuns },
                         { "repetitions", suite.repetitions },
                         { "worker_count", suite.workerCount },
                         { "cases", std::move(cases) } };
        }

        Json userEnvironmentJson(const std::optional<EnvironmentMetadata>& environment)
        {
            if (!environment.has_value())
            {
                return nullptr;
            }
            return Json{ { "schema_version", environment->schemaVersion },
                         { "environment_id", environment->environmentId },
                         { "cpu_model", optionalString(environment->cpuModel) },
                         { "physical_memory_bytes", environment->physicalMemoryBytes.has_value()
                                                        ? Json(environment->physicalMemoryBytes.value())
                                                        : Json(nullptr) },
                         { "os_version", optionalString(environment->osVersion) },
                         { "gpu_driver", optionalString(environment->gpuDriver) },
                         { "power_profile", optionalString(environment->powerProfile) },
                         { "notes", optionalString(environment->notes) } };
        }

        Json environmentJson(const BaselineSuite& suite, const std::optional<EnvironmentMetadata>& userEnvironment,
                             const std::vector<BaselineRunRecord>& records)
        {
            std::optional<std::string> gpuName;
            std::optional<std::uint32_t> gpuApiVersion;
            std::optional<std::uint32_t> gpuDeviceType;
            for (const BaselineRunRecord& record : records)
            {
                if (record.run.gpuDeviceName.has_value())
                {
                    gpuName = record.run.gpuDeviceName;
                    gpuApiVersion = record.run.gpuApiVersion;
                    gpuDeviceType = record.run.gpuDeviceType;
                    break;
                }
            }
            return Json{ { "atlas_version", ATLAS_BENCHMARK_ATLAS_VERSION },
                         { "git_revision", ATLAS_BENCHMARK_GIT_REVISION },
                         { "compiler", ATLAS_BENCHMARK_COMPILER },
                         { "build_type", ATLAS_BENCHMARK_BUILD_TYPE },
                         { "operating_system", ATLAS_BENCHMARK_SYSTEM },
                         { "system_processor", ATLAS_BENCHMARK_SYSTEM_PROCESSOR },
                         { "cmake_version", ATLAS_BENCHMARK_CMAKE_VERSION },
                         { "logical_processor_count", std::thread::hardware_concurrency() },
                         { "worker_count", suite.workerCount },
                         { "gpu_device", optionalString(gpuName) },
                         { "gpu_api_version", gpuApiVersion.has_value() ? Json(gpuApiVersion.value()) : Json(nullptr) },
                         { "gpu_device_type", gpuDeviceType.has_value() ? Json(gpuDeviceType.value()) : Json(nullptr) },
                         { "user_metadata", userEnvironmentJson(userEnvironment) } };
        }

        std::string taskStateName(const TaskState state)
        {
            switch (state)
            {
            case TaskState::Unknown:
                return "Unknown";
            case TaskState::Ready:
                return "Ready";
            case TaskState::Running:
                return "Running";
            case TaskState::Success:
                return "Success";
            case TaskState::Failure:
                return "Failure";
            case TaskState::Blocked:
                return "Blocked";
            case TaskState::Paused:
                return "Paused";
            case TaskState::Cancelled:
                return "Cancelled";
            }
            return "Unknown";
        }

        std::string resourceName(const ExecutionResource resource)
        {
            return resource == ExecutionResource::CPU ? "cpu" : "gpu";
        }

        Json distributionJson(const DurationDistribution& distribution)
        {
            return Json{ { "mean_us", optionalNumber(distribution.mean) },
                         { "p50_us", optionalNumber(distribution.p50) },
                         { "p95_us", optionalNumber(distribution.p95) },
                         { "max_us", optionalNumber(distribution.maximum) } };
        }

        Json taskJson(const TaskMeasurement& task, const ExecutionMode mode)
        {
            return Json{ { "index", task.index },
                         { "name", task.name },
                         { "resource", resourceName(task.resource) },
                         { "priority", task.priority },
                         { "burst", task.burstIndex },
                         { "state", taskStateName(task.state) },
                         { "execution_us", task.executionDuration.count() },
                         { "device_execution_ns",
                           task.deviceExecutionDuration.has_value() ? Json(task.deviceExecutionDuration->count()) : Json(nullptr) },
                         { "ready_wait_us", task.readyWaitDuration.count() },
                         { "response_us", task.responseDuration.has_value() ? Json(task.responseDuration->count()) : Json(nullptr) },
                         { "selection_bypasses", mode == ExecutionMode::Scheduled ? Json(task.selectionBypassCount) : Json(nullptr) },
                         { "completed_work_units", task.completedWorkUnitCount },
                         { "total_work_units", task.totalWorkUnitCount } };
        }

        std::optional<double> bypassMean(const BaselineRunRecord& record)
        {
            if (record.executionMode == ExecutionMode::Direct || record.run.tasks.empty())
            {
                return std::nullopt;
            }
            std::size_t total{ 0U };
            for (const TaskMeasurement& task : record.run.tasks)
            {
                total += task.selectionBypassCount;
            }
            return static_cast<double>(total) / static_cast<double>(record.run.tasks.size());
        }

        std::optional<double> bypassMaximum(const BaselineRunRecord& record)
        {
            if (record.executionMode == ExecutionMode::Direct || record.run.tasks.empty())
            {
                return std::nullopt;
            }
            const auto item = std::max_element(record.run.tasks.begin(), record.run.tasks.end(),
                                               [](const TaskMeasurement& left, const TaskMeasurement& right)
                                               { return left.selectionBypassCount < right.selectionBypassCount; });
            return static_cast<double>(item->selectionBypassCount);
        }

        Json runJson(const BaselineRunRecord& record, const Json& environment)
        {
            Json tasks = Json::array();
            for (const TaskMeasurement& task : record.run.tasks)
            {
                tasks.push_back(taskJson(task, record.executionMode));
            }
            return Json{
                { "run_schema_version", 2U },
                { "suite_id", record.suiteId },
                { "case_id", record.caseId },
                { "variant_id", record.variantId },
                { "execution", toString(record.executionMode) },
                { "seed", record.run.seed },
                { "repetition", record.run.repetition },
                { "execution_order", record.executionOrder },
                { "environment", environment },
                { "result",
                  { { "status", toString(record.run.schedulerResult.status) },
                    { "executed_task_count", record.run.schedulerResult.executedTaskCount },
                    { "completion_time_us", record.run.schedulerResult.executionTime.count() },
                    { "control_active_us", record.run.schedulerResult.schedulerActiveDuration.count() },
                    { "control_active_fraction", optionalNumber(record.run.metrics.schedulerActiveFraction) } } },
                { "metrics",
                  { { "throughput_tasks_per_second", record.run.metrics.throughputTasksPerSecond },
                    { "response_latency", distributionJson(record.run.metrics.responseLatency) },
                    { "ready_wait", distributionJson(record.run.metrics.readyWait) },
                    { "selection_bypass_mean", optionalNumber(bypassMean(record)) },
                    { "selection_bypass_max", optionalNumber(bypassMaximum(record)) },
                    { "immediate_slice_switch_mean_us", optionalNumber(record.run.metrics.immediateSliceSwitchMeanMicroseconds) },
                    { "cpu_busy_fraction", optionalNumber(record.run.metrics.cpuBusyFraction) },
                    { "gpu_host_busy_fraction", optionalNumber(record.run.metrics.gpuHostBusyFraction) },
                    { "gpu_timestamp_supported", record.run.gpuTimestampSupported },
                    { "gpu_timestamp_busy_fraction", optionalNumber(record.run.metrics.gpuTimestampBusyFraction) },
                    { "cpu_jain_fairness", optionalNumber(record.run.metrics.cpuJainFairness) },
                    { "gpu_jain_fairness", optionalNumber(record.run.metrics.gpuJainFairness) } } },
                { "tasks", std::move(tasks) }
            };
        }

        Json intervalJson(const ConfidenceInterval& interval)
        {
            return Json{ { "lower", optionalNumber(interval.lower) }, { "upper", optionalNumber(interval.upper) } };
        }

        Json summaryJson(const BaselineSummary& summary, const Json& environment)
        {
            Json cases = Json::array();
            for (const CaseSummary& comparisonCase : summary.cases)
            {
                Json variants = Json::array();
                for (const VariantSummary& variant : comparisonCase.variants)
                {
                    Json metrics = Json::array();
                    for (const VariantMetricSummary& metric : variant.metrics)
                    {
                        metrics.push_back(Json{ { "metric", metric.metric },
                                                { "direction", toString(metric.direction) },
                                                { "sample_count", metric.sampleCount },
                                                { "mean", optionalNumber(metric.mean) },
                                                { "confidence_interval", intervalJson(metric.confidenceInterval) } });
                    }
                    variants.push_back(Json{ { "variant_id", variant.variantId },
                                             { "execution", toString(variant.executionMode) },
                                             { "metrics", std::move(metrics) } });
                }

                Json comparisons = Json::array();
                for (const VariantComparison& comparison : comparisonCase.comparisons)
                {
                    Json metrics = Json::array();
                    for (const ComparisonMetricSummary& metric : comparison.metrics)
                    {
                        metrics.push_back(
                            Json{ { "metric", metric.metric },
                                  { "direction", toString(metric.direction) },
                                  { "paired_sample_count", metric.pairedSampleCount },
                                  { "mean_difference", optionalNumber(metric.meanDifference) },
                                  { "difference_confidence_interval", intervalJson(metric.differenceConfidenceInterval) },
                                  { "percent_sample_count", metric.percentSampleCount },
                                  { "mean_percent_change", optionalNumber(metric.meanPercentChange) },
                                  { "percent_confidence_interval", intervalJson(metric.percentConfidenceInterval) } });
                    }
                    comparisons.push_back(Json{ { "variant_id", comparison.variantId },
                                                { "reference_variant_id", comparison.referenceVariantId },
                                                { "metrics", std::move(metrics) } });
                }
                cases.push_back(Json{ { "case_id", comparisonCase.caseId },
                                      { "reference_variant_id", comparisonCase.referenceVariantId },
                                      { "variants", std::move(variants) },
                                      { "comparisons", std::move(comparisons) } });
            }
            return Json{ { "summary_schema_version", 2U },
                         { "suite_id", summary.suiteId },
                         { "environment", environment },
                         { "analysis",
                           { { "confidence_level", 0.95 },
                             { "bootstrap_resamples", 10'000U },
                             { "method", "deterministic_hierarchical_percentile_bootstrap" } } },
                         { "cases", std::move(cases) } };
        }

        std::string csvEscape(const std::string_view value)
        {
            if (value.find_first_of(",\"\r\n") == std::string_view::npos)
            {
                return std::string{ value };
            }
            std::string escaped{ "\"" };
            for (const char character : value)
            {
                escaped += character == '"' ? "\"\"" : std::string(1U, character);
            }
            escaped += '"';
            return escaped;
        }

        std::string csvOptional(const std::optional<double> value)
        {
            if (!value.has_value())
            {
                return {};
            }
            std::ostringstream output;
            output.imbue(std::locale::classic());
            output << std::setprecision(17) << value.value();
            return output.str();
        }

        void requireOpen(const std::ofstream& stream, const std::filesystem::path& path)
        {
            if (!stream)
            {
                throw std::runtime_error{ "Unable to open baseline output: " + path.string() };
            }
        }
    } // namespace

    BaselineWriter::BaselineWriter(const std::filesystem::path& outputDirectory, BaselineSuite suite,
                                   std::optional<EnvironmentMetadata> environment, const std::vector<BaselineRunRecord>& records,
                                   const bool overwrite)
        : resolvedSuite{ std::move(suite) }, userEnvironment{ std::move(environment) },
          summaryPath{ outputDirectory / "comparisons.json" }, comparisonCsvPath{ outputDirectory / "comparisons.csv" }
    {
        std::filesystem::create_directories(outputDirectory);
        const std::array paths{ outputDirectory / "suite.resolved.json", outputDirectory / "environment.resolved.json",
                                outputDirectory / "runs.jsonl",          outputDirectory / "runs.csv",
                                outputDirectory / "tasks.csv",           summaryPath,
                                outputDirectory / "comparisons.csv" };
        if (!overwrite)
        {
            for (const std::filesystem::path& path : paths)
            {
                if (std::filesystem::exists(path))
                {
                    throw std::runtime_error{ "Baseline output already exists; pass --overwrite to replace it: " + path.string() };
                }
            }
        }

        const Json environmentValue = environmentJson(resolvedSuite, userEnvironment, records);
        resolvedEnvironmentJson = environmentValue.dump();
        std::ofstream resolved{ paths.at(0) };
        std::ofstream environmentOutput{ paths.at(1) };
        requireOpen(resolved, paths.at(0));
        requireOpen(environmentOutput, paths.at(1));
        resolved << suiteJson(resolvedSuite).dump(2) << '\n';
        environmentOutput << environmentValue.dump(2) << '\n';

        jsonLines.open(paths.at(2));
        runCsv.open(paths.at(3));
        taskCsv.open(paths.at(4));
        requireOpen(jsonLines, paths.at(2));
        requireOpen(runCsv, paths.at(3));
        requireOpen(taskCsv, paths.at(4));
        jsonLines.imbue(std::locale::classic());
        runCsv.imbue(std::locale::classic());
        taskCsv.imbue(std::locale::classic());
        runCsv << "run_schema_version,suite_id,case_id,variant_id,execution,seed,repetition,execution_order,status,"
                  "executed_task_count,completion_time_us,throughput_tasks_per_second,response_mean_us,response_p95_us,"
                  "ready_wait_mean_us,ready_wait_p95_us,selection_bypass_mean,selection_bypass_max,control_active_us,"
                  "control_active_fraction,immediate_slice_switch_mean_us,cpu_busy_fraction,gpu_host_busy_fraction,"
                  "gpu_timestamp_supported,gpu_timestamp_busy_fraction,cpu_jain_fairness,gpu_jain_fairness\n";
        taskCsv << "run_schema_version,suite_id,case_id,variant_id,execution,seed,repetition,task_index,name,resource,priority,"
                   "burst,state,execution_us,device_execution_ns,ready_wait_us,response_us,selection_bypasses,completed_work_units,"
                   "total_work_units\n";
    }

    void BaselineWriter::append(const BaselineRunRecord& record)
    {
        const Json environmentValue = Json::parse(resolvedEnvironmentJson);
        jsonLines << runJson(record, environmentValue).dump() << '\n';
        runCsv << "2," << csvEscape(record.suiteId) << ',' << csvEscape(record.caseId) << ',' << csvEscape(record.variantId) << ','
               << toString(record.executionMode) << ',' << record.run.seed << ',' << record.run.repetition << ','
               << record.executionOrder << ',' << toString(record.run.schedulerResult.status) << ','
               << record.run.schedulerResult.executedTaskCount << ',' << record.run.schedulerResult.executionTime.count() << ','
               << std::setprecision(17) << record.run.metrics.throughputTasksPerSecond << ','
               << csvOptional(record.run.metrics.responseLatency.mean) << ',' << csvOptional(record.run.metrics.responseLatency.p95)
               << ',' << csvOptional(record.run.metrics.readyWait.mean) << ',' << csvOptional(record.run.metrics.readyWait.p95) << ','
               << csvOptional(bypassMean(record)) << ',' << csvOptional(bypassMaximum(record)) << ','
               << record.run.schedulerResult.schedulerActiveDuration.count() << ','
               << csvOptional(record.run.metrics.schedulerActiveFraction) << ','
               << csvOptional(record.run.metrics.immediateSliceSwitchMeanMicroseconds) << ','
               << csvOptional(record.run.metrics.cpuBusyFraction) << ',' << csvOptional(record.run.metrics.gpuHostBusyFraction) << ','
               << (record.run.gpuTimestampSupported ? "true" : "false") << ','
               << csvOptional(record.run.metrics.gpuTimestampBusyFraction) << ',' << csvOptional(record.run.metrics.cpuJainFairness)
               << ',' << csvOptional(record.run.metrics.gpuJainFairness) << '\n';

        for (const TaskMeasurement& task : record.run.tasks)
        {
            taskCsv << "2," << csvEscape(record.suiteId) << ',' << csvEscape(record.caseId) << ',' << csvEscape(record.variantId)
                    << ',' << toString(record.executionMode) << ',' << record.run.seed << ',' << record.run.repetition << ','
                    << task.index << ',' << csvEscape(task.name) << ',' << resourceName(task.resource) << ',' << task.priority << ','
                    << task.burstIndex << ',' << taskStateName(task.state) << ',' << task.executionDuration.count() << ','
                    << (task.deviceExecutionDuration.has_value() ? std::to_string(task.deviceExecutionDuration->count())
                                                                 : std::string{})
                    << ',' << task.readyWaitDuration.count() << ','
                    << (task.responseDuration.has_value() ? std::to_string(task.responseDuration->count()) : std::string{}) << ',';
            if (record.executionMode == ExecutionMode::Scheduled)
            {
                taskCsv << task.selectionBypassCount;
            }
            taskCsv << ',' << task.completedWorkUnitCount << ',' << task.totalWorkUnitCount << '\n';
        }
        if (!jsonLines || !runCsv || !taskCsv)
        {
            throw std::runtime_error{ "Unable to write baseline run results" };
        }
    }

    void BaselineWriter::writeSummary(const BaselineSummary& summary)
    {
        const Json environmentValue = Json::parse(resolvedEnvironmentJson);
        std::ofstream output{ summaryPath };
        requireOpen(output, summaryPath);
        output.imbue(std::locale::classic());
        output << summaryJson(summary, environmentValue).dump(2) << '\n';

        comparisonCsv.open(comparisonCsvPath);
        requireOpen(comparisonCsv, comparisonCsvPath);
        comparisonCsv.imbue(std::locale::classic());
        comparisonCsv << "summary_schema_version,row_type,suite_id,case_id,variant_id,reference_variant_id,metric,direction,"
                         "sample_count,mean,ci_lower,ci_upper,paired_sample_count,mean_difference,difference_ci_lower,"
                         "difference_ci_upper,percent_sample_count,mean_percent_change,percent_ci_lower,percent_ci_upper\n";

        for (const CaseSummary& comparisonCase : summary.cases)
        {
            for (const VariantSummary& variant : comparisonCase.variants)
            {
                for (const VariantMetricSummary& metric : variant.metrics)
                {
                    comparisonCsv << "2,absolute," << csvEscape(summary.suiteId) << ',' << csvEscape(comparisonCase.caseId) << ','
                                  << csvEscape(variant.variantId) << ",," << metric.metric << ',' << toString(metric.direction) << ','
                                  << metric.sampleCount << ',' << csvOptional(metric.mean) << ','
                                  << csvOptional(metric.confidenceInterval.lower) << ','
                                  << csvOptional(metric.confidenceInterval.upper) << ",,,,,,,,\n";
                }
            }
            for (const VariantComparison& comparison : comparisonCase.comparisons)
            {
                for (const ComparisonMetricSummary& metric : comparison.metrics)
                {
                    comparisonCsv << "2,paired," << csvEscape(summary.suiteId) << ',' << csvEscape(comparisonCase.caseId) << ','
                                  << csvEscape(comparison.variantId) << ',' << csvEscape(comparison.referenceVariantId) << ','
                                  << metric.metric << ',' << toString(metric.direction) << ",,,,," << metric.pairedSampleCount << ','
                                  << csvOptional(metric.meanDifference) << ','
                                  << csvOptional(metric.differenceConfidenceInterval.lower) << ','
                                  << csvOptional(metric.differenceConfidenceInterval.upper) << ',' << metric.percentSampleCount << ','
                                  << csvOptional(metric.meanPercentChange) << ','
                                  << csvOptional(metric.percentConfidenceInterval.lower) << ','
                                  << csvOptional(metric.percentConfidenceInterval.upper) << '\n';
                }
            }
        }
        if (!output || !comparisonCsv)
        {
            throw std::runtime_error{ "Unable to write baseline comparison summary" };
        }
    }
} // namespace Atlas::Benchmark
