#include "ResultWriter.h"

#include "atlas/Scheduler/SchedulerStatus.h"

#include <array>
#include <iomanip>
#include <locale>
#include <nlohmann/json.hpp>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

/**
 * @file ResultWriter.cpp
 * @brief Implements deterministic benchmark JSON Lines and CSV serialization.
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

namespace Atlas::Benchmark
{
    namespace
    {
        using Json = nlohmann::json;

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

        Json optionalNumber(const std::optional<double> value)
        {
            return value.has_value() ? Json{ value.value() } : Json{ nullptr };
        }

        Json optionalDuration(const std::optional<std::chrono::microseconds> value)
        {
            return value.has_value() ? Json{ value->count() } : Json{ nullptr };
        }

        Json dimensionsJson(const DispatchDimensions dimensions)
        {
            return Json{ { "x", dimensions.x }, { "y", dimensions.y }, { "z", dimensions.z } };
        }

        Json manifestJson(const ExperimentManifest& manifest)
        {
            Json policy{ { "type", toString(manifest.policy.kind) } };
            if (manifest.policy.kind == PolicyKind::RoundRobin)
            {
                policy["quantum"] = manifest.policy.quantum;
            }
            Json dependencies{ { "shape", toString(manifest.dependencies.shape) } };
            if (manifest.dependencies.shape == DependencyShape::Layered)
            {
                dependencies["layers"] = manifest.dependencies.layers;
            }
            if (manifest.dependencies.shape == DependencyShape::Random)
            {
                dependencies["edge_probability"] = manifest.dependencies.edgeProbability;
            }
            return Json{
                { "schema_version", manifest.schemaVersion },
                { "experiment_id", manifest.experimentId },
                { "seeds", manifest.seeds },
                { "warmup_runs", manifest.warmupRuns },
                { "repetitions", manifest.repetitions },
                { "worker_count", manifest.workerCount },
                { "policy", std::move(policy) },
                { "workload",
                  { { "cpu", { { "task_count", manifest.cpu.taskCount }, { "iterations", manifest.cpu.iterations } } },
                    { "gpu",
                      { { "task_count", manifest.gpu.taskCount },
                        { "workgroups", dimensionsJson(manifest.gpu.workgroups) },
                        { "slice_workgroups",
                          manifest.gpu.sliced ? dimensionsJson(manifest.gpu.sliceWorkgroups) : Json{ nullptr } } } },
                    { "dependencies", std::move(dependencies) },
                    { "priorities",
                      { { "assignment", toString(manifest.priorities.assignment) }, { "values", manifest.priorities.values } } },
                    { "bursts", { { "count", manifest.bursts.count } } } } }
            };
        }

        Json distributionJson(const DurationDistribution& distribution)
        {
            return Json{ { "mean_us", optionalNumber(distribution.mean) },
                         { "p50_us", optionalNumber(distribution.p50) },
                         { "p95_us", optionalNumber(distribution.p95) },
                         { "max_us", optionalNumber(distribution.maximum) } };
        }

        Json taskJson(const TaskMeasurement& task)
        {
            return Json{ { "index", task.index },
                         { "name", task.name },
                         { "resource", resourceName(task.resource) },
                         { "priority", task.priority },
                         { "burst", task.burstIndex },
                         { "state", taskStateName(task.state) },
                         { "execution_us", task.executionDuration.count() },
                         { "ready_wait_us", task.readyWaitDuration.count() },
                         { "response_us", optionalDuration(task.responseDuration) },
                         { "selection_bypasses", task.selectionBypassCount },
                         { "completed_work_units", task.completedWorkUnitCount },
                         { "total_work_units", task.totalWorkUnitCount } };
        }

        Json runJson(const RunRecord& record, const ExperimentManifest& manifest)
        {
            Json tasks{ Json::array() };
            for (const TaskMeasurement& task : record.tasks)
            {
                tasks.push_back(taskJson(task));
            }
            return Json{
                { "result_schema_version", 1U },
                { "experiment_id", record.experimentId },
                { "seed", record.seed },
                { "repetition", record.repetition },
                { "manifest", manifestJson(manifest) },
                { "environment",
                  { { "atlas_version", ATLAS_BENCHMARK_ATLAS_VERSION },
                    { "git_revision", ATLAS_BENCHMARK_GIT_REVISION },
                    { "compiler", ATLAS_BENCHMARK_COMPILER },
                    { "build_type", ATLAS_BENCHMARK_BUILD_TYPE },
                    { "operating_system", ATLAS_BENCHMARK_SYSTEM },
                    { "worker_count", manifest.workerCount },
                    { "gpu_device", record.gpuDeviceName.has_value() ? Json{ record.gpuDeviceName.value() } : Json{ nullptr } },
                    { "gpu_api_version",
                      record.gpuApiVersion.has_value() ? Json{ record.gpuApiVersion.value() } : Json{ nullptr } } } },
                { "result",
                  { { "status", toString(record.schedulerResult.status) },
                    { "executed_task_count", record.schedulerResult.executedTaskCount },
                    { "completion_time_us", record.schedulerResult.executionTime.count() },
                    { "scheduler_active_us", record.schedulerResult.schedulerActiveDuration.count() },
                    { "immediate_slice_switch_us", record.schedulerResult.immediateSliceSwitchDuration.count() },
                    { "immediate_slice_switch_count", record.schedulerResult.immediateSliceSwitchCount } } },
                { "metrics",
                  { { "throughput_tasks_per_second", record.metrics.throughputTasksPerSecond },
                    { "response_latency", distributionJson(record.metrics.responseLatency) },
                    { "ready_wait", distributionJson(record.metrics.readyWait) },
                    { "scheduler_active_fraction", optionalNumber(record.metrics.schedulerActiveFraction) },
                    { "immediate_slice_switch_mean_us", optionalNumber(record.metrics.immediateSliceSwitchMeanMicroseconds) },
                    { "cpu_busy_fraction", optionalNumber(record.metrics.cpuBusyFraction) },
                    { "gpu_host_busy_fraction", optionalNumber(record.metrics.gpuHostBusyFraction) },
                    { "gpu_timestamp_supported", false },
                    { "gpu_timestamp_busy_fraction", nullptr },
                    { "cpu_jain_fairness", optionalNumber(record.metrics.cpuJainFairness) },
                    { "gpu_jain_fairness", optionalNumber(record.metrics.gpuJainFairness) } } },
                { "tasks", std::move(tasks) }
            };
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

        std::string csvOptionalDuration(const std::optional<std::chrono::microseconds> value)
        {
            return value.has_value() ? std::to_string(value->count()) : std::string{};
        }

        void requireOpen(const std::ofstream& stream, const std::filesystem::path& path)
        {
            if (!stream)
            {
                throw std::runtime_error{ "Unable to open benchmark output: " + path.string() };
            }
        }
    } // namespace

    ResultWriter::ResultWriter(const std::filesystem::path& outputDirectory, const ExperimentManifest& manifest, const bool overwrite)
        : resolvedManifest{ manifest }
    {
        std::filesystem::create_directories(outputDirectory);
        const std::array paths{ outputDirectory / "manifest.resolved.json", outputDirectory / "runs.jsonl",
                                outputDirectory / "runs.csv", outputDirectory / "tasks.csv" };
        if (!overwrite)
        {
            for (const std::filesystem::path& path : paths)
            {
                if (std::filesystem::exists(path))
                {
                    throw std::runtime_error{ "Benchmark output already exists; pass --overwrite to replace it: " + path.string() };
                }
            }
        }

        std::ofstream resolved{ paths.at(0) };
        requireOpen(resolved, paths.at(0));
        resolved.imbue(std::locale::classic());
        resolved << manifestJson(manifest).dump(2) << '\n';

        jsonLines.open(paths.at(1));
        runCsv.open(paths.at(2));
        taskCsv.open(paths.at(3));
        requireOpen(jsonLines, paths.at(1));
        requireOpen(runCsv, paths.at(2));
        requireOpen(taskCsv, paths.at(3));
        jsonLines.imbue(std::locale::classic());
        runCsv.imbue(std::locale::classic());
        taskCsv.imbue(std::locale::classic());
        runCsv << "result_schema_version,experiment_id,seed,repetition,status,executed_task_count,completion_time_us,"
                  "throughput_tasks_per_second,response_mean_us,response_p50_us,response_p95_us,response_max_us,"
                  "ready_wait_mean_us,ready_wait_p50_us,ready_wait_p95_us,ready_wait_max_us,scheduler_active_us,"
                  "scheduler_active_fraction,immediate_slice_switch_us,immediate_slice_switch_count,"
                  "immediate_slice_switch_mean_us,cpu_busy_fraction,gpu_host_busy_fraction,gpu_timestamp_supported,"
                  "gpu_timestamp_busy_fraction,cpu_jain_fairness,gpu_jain_fairness\n";
        taskCsv << "result_schema_version,experiment_id,seed,repetition,task_index,name,resource,priority,burst,state,"
                   "execution_us,ready_wait_us,response_us,selection_bypasses,completed_work_units,total_work_units\n";
    }

    void ResultWriter::append(const RunRecord& record)
    {
        jsonLines << runJson(record, resolvedManifest).dump() << '\n';
        runCsv << "1," << csvEscape(record.experimentId) << ',' << record.seed << ',' << record.repetition << ','
               << toString(record.schedulerResult.status) << ',' << record.schedulerResult.executedTaskCount << ','
               << record.schedulerResult.executionTime.count() << ',' << std::setprecision(17)
               << record.metrics.throughputTasksPerSecond << ',' << csvOptional(record.metrics.responseLatency.mean) << ','
               << csvOptional(record.metrics.responseLatency.p50) << ',' << csvOptional(record.metrics.responseLatency.p95) << ','
               << csvOptional(record.metrics.responseLatency.maximum) << ',' << csvOptional(record.metrics.readyWait.mean) << ','
               << csvOptional(record.metrics.readyWait.p50) << ',' << csvOptional(record.metrics.readyWait.p95) << ','
               << csvOptional(record.metrics.readyWait.maximum) << ',' << record.schedulerResult.schedulerActiveDuration.count() << ','
               << csvOptional(record.metrics.schedulerActiveFraction) << ','
               << record.schedulerResult.immediateSliceSwitchDuration.count() << ','
               << record.schedulerResult.immediateSliceSwitchCount << ','
               << csvOptional(record.metrics.immediateSliceSwitchMeanMicroseconds) << ','
               << csvOptional(record.metrics.cpuBusyFraction) << ',' << csvOptional(record.metrics.gpuHostBusyFraction) << ",false,,"
               << csvOptional(record.metrics.cpuJainFairness) << ',' << csvOptional(record.metrics.gpuJainFairness) << '\n';

        for (const TaskMeasurement& task : record.tasks)
        {
            taskCsv << "1," << csvEscape(record.experimentId) << ',' << record.seed << ',' << record.repetition << ',' << task.index
                    << ',' << csvEscape(task.name) << ',' << resourceName(task.resource) << ',' << task.priority << ','
                    << task.burstIndex << ',' << taskStateName(task.state) << ',' << task.executionDuration.count() << ','
                    << task.readyWaitDuration.count() << ',' << csvOptionalDuration(task.responseDuration) << ','
                    << task.selectionBypassCount << ',' << task.completedWorkUnitCount << ',' << task.totalWorkUnitCount << '\n';
        }
        if (!jsonLines || !runCsv || !taskCsv)
        {
            throw std::runtime_error{ "Unable to write benchmark results" };
        }
    }
} // namespace Atlas::Benchmark
