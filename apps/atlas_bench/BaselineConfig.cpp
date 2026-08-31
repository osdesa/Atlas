#include "BaselineConfig.h"

#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
#include <string_view>
#include <unordered_set>

/**
 * @file BaselineConfig.cpp
 * @brief Implements strict additive comparison-suite configuration parsing.
 */

namespace Atlas::Benchmark
{
    namespace
    {
        using Json = nlohmann::json;

        void requireObject(const Json& value, const std::string_view path)
        {
            if (!value.is_object())
            {
                throw std::runtime_error{ std::string{ path } + " must be an object" };
            }
        }

        void rejectUnknown(const Json& object, const std::set<std::string, std::less<>>& allowed, const std::string_view path)
        {
            requireObject(object, path);
            for (const auto& [key, value] : object.items())
            {
                static_cast<void>(value);
                if (!allowed.contains(key))
                {
                    throw std::runtime_error{ std::string{ path } + " contains unknown field '" + key + "'" };
                }
            }
        }

        const Json& required(const Json& object, const std::string_view key, const std::string_view path)
        {
            const auto entry{ object.find(key) };
            if (entry == object.end())
            {
                throw std::runtime_error{ std::string{ path } + " is missing required field '" + std::string{ key } + "'" };
            }
            return entry.value();
        }

        std::uint64_t unsignedInteger(const Json& value, const std::string_view path)
        {
            if (!value.is_number_unsigned())
            {
                throw std::runtime_error{ std::string{ path } + " must be an unsigned integer" };
            }
            return value.get<std::uint64_t>();
        }

        std::size_t sizeValue(const Json& value, const std::string_view path)
        {
            const std::uint64_t parsed{ unsignedInteger(value, path) };
            if (parsed > std::numeric_limits<std::size_t>::max())
            {
                throw std::runtime_error{ std::string{ path } + " exceeds this platform's size range" };
            }
            return static_cast<std::size_t>(parsed);
        }

        std::uint32_t uint32Value(const Json& value, const std::string_view path)
        {
            const std::uint64_t parsed{ unsignedInteger(value, path) };
            if (parsed > std::numeric_limits<std::uint32_t>::max())
            {
                throw std::runtime_error{ std::string{ path } + " exceeds uint32 range" };
            }
            return static_cast<std::uint32_t>(parsed);
        }

        std::string stringValue(const Json& value, const std::string_view path)
        {
            if (!value.is_string())
            {
                throw std::runtime_error{ std::string{ path } + " must be a string" };
            }
            return value.get<std::string>();
        }

        std::optional<std::string> optionalString(const Json& object, const std::string_view key, const std::string_view path)
        {
            const auto entry{ object.find(key) };
            if (entry == object.end())
            {
                return std::nullopt;
            }
            return stringValue(entry.value(), std::string{ path } + "." + std::string{ key });
        }

        DispatchDimensions dimensions(const Json& value, const std::string_view path)
        {
            rejectUnknown(value, { "x", "y", "z" }, path);
            DispatchDimensions result{ uint32Value(required(value, "x", path), std::string{ path } + ".x"),
                                       uint32Value(required(value, "y", path), std::string{ path } + ".y"),
                                       uint32Value(required(value, "z", path), std::string{ path } + ".z") };
            if (!result.isValid())
            {
                throw std::runtime_error{ std::string{ path } + " dimensions must be non-zero" };
            }
            return result;
        }

        PolicyConfig parsePolicy(const Json& value, const std::string_view path)
        {
            rejectUnknown(value, { "type", "quantum" }, path);
            const std::string type{ stringValue(required(value, "type", path), std::string{ path } + ".type") };
            if (type == "fifo")
            {
                if (value.contains("quantum"))
                {
                    throw std::runtime_error{ std::string{ path } + ".quantum is valid only for round_robin" };
                }
                return {};
            }
            if (type == "static_priority")
            {
                if (value.contains("quantum"))
                {
                    throw std::runtime_error{ std::string{ path } + ".quantum is valid only for round_robin" };
                }
                return PolicyConfig{ PolicyKind::StaticPriority, 1U };
            }
            if (type == "round_robin")
            {
                const std::size_t quantum{ sizeValue(required(value, "quantum", path), std::string{ path } + ".quantum") };
                if (quantum == 0U)
                {
                    throw std::runtime_error{ std::string{ path } + ".quantum must be positive" };
                }
                return PolicyConfig{ PolicyKind::RoundRobin, quantum };
            }
            throw std::runtime_error{ std::string{ path } + ".type is unsupported" };
        }

        DependencyConfig parseDependencies(const Json& value, const std::string_view path)
        {
            rejectUnknown(value, { "shape", "layers", "edge_probability" }, path);
            const std::string shape{ stringValue(required(value, "shape", path), std::string{ path } + ".shape") };
            if (shape == "independent" || shape == "chain")
            {
                if (value.size() != 1U)
                {
                    throw std::runtime_error{ std::string{ path } + " accepts only shape for independent and chain" };
                }
                return DependencyConfig{ shape == "independent" ? DependencyShape::Independent : DependencyShape::Chain, 1U, 0.0 };
            }
            if (shape == "layered")
            {
                const std::size_t layers{ sizeValue(required(value, "layers", path), std::string{ path } + ".layers") };
                if (layers == 0U || value.size() != 2U)
                {
                    throw std::runtime_error{ std::string{ path } + " layered shape requires only positive layers" };
                }
                return DependencyConfig{ DependencyShape::Layered, layers, 0.0 };
            }
            if (shape == "random")
            {
                const Json& probabilityValue{ required(value, "edge_probability", path) };
                if (!probabilityValue.is_number())
                {
                    throw std::runtime_error{ std::string{ path } + ".edge_probability must be numeric" };
                }
                const double probability{ probabilityValue.get<double>() };
                if (probability < 0.0 || probability > 1.0 || value.size() != 2U)
                {
                    throw std::runtime_error{ std::string{ path } + " random shape requires edge_probability in [0, 1]" };
                }
                return DependencyConfig{ DependencyShape::Random, 1U, probability };
            }
            throw std::runtime_error{ std::string{ path } + ".shape is unsupported" };
        }

        PriorityConfig parsePriorities(const Json& value, const std::string_view path)
        {
            rejectUnknown(value, { "assignment", "values" }, path);
            const std::string assignment{ stringValue(required(value, "assignment", path), std::string{ path } + ".assignment") };
            PriorityConfig result;
            if (assignment == "cycle")
            {
                result.assignment = PriorityAssignment::Cycle;
            }
            else if (assignment == "random")
            {
                result.assignment = PriorityAssignment::Random;
            }
            else
            {
                throw std::runtime_error{ std::string{ path } + ".assignment is unsupported" };
            }

            const Json& values{ required(value, "values", path) };
            if (!values.is_array() || values.empty())
            {
                throw std::runtime_error{ std::string{ path } + ".values must be a non-empty array" };
            }
            result.values.clear();
            result.values.reserve(values.size());
            for (const Json& priority : values)
            {
                result.values.push_back(uint32Value(priority, std::string{ path } + ".values"));
            }
            return result;
        }

        BaselineWorkloadConfig parseWorkload(const Json& value, const std::string_view path)
        {
            rejectUnknown(value, { "cpu", "gpu", "dependencies", "priorities", "bursts" }, path);
            BaselineWorkloadConfig result;

            const std::string cpuPath{ std::string{ path } + ".cpu" };
            const Json& cpu{ required(value, "cpu", path) };
            rejectUnknown(cpu, { "task_count", "iterations" }, cpuPath);
            result.cpu.taskCount = sizeValue(required(cpu, "task_count", cpuPath), cpuPath + ".task_count");
            result.cpu.iterations = unsignedInteger(required(cpu, "iterations", cpuPath), cpuPath + ".iterations");
            if (result.cpu.taskCount != 0U && result.cpu.iterations == 0U)
            {
                throw std::runtime_error{ cpuPath + ".iterations must be positive when CPU tasks are requested" };
            }

            const std::string gpuPath{ std::string{ path } + ".gpu" };
            const Json& gpu{ required(value, "gpu", path) };
            rejectUnknown(gpu, { "task_count", "workgroups" }, gpuPath);
            result.gpu.taskCount = sizeValue(required(gpu, "task_count", gpuPath), gpuPath + ".task_count");
            result.gpu.workgroups = dimensions(required(gpu, "workgroups", gpuPath), gpuPath + ".workgroups");
            result.gpu.sliced = false;

            result.dependencies = parseDependencies(required(value, "dependencies", path), std::string{ path } + ".dependencies");
            result.priorities = parsePriorities(required(value, "priorities", path), std::string{ path } + ".priorities");

            const std::string burstsPath{ std::string{ path } + ".bursts" };
            const Json& bursts{ required(value, "bursts", path) };
            rejectUnknown(bursts, { "count" }, burstsPath);
            result.bursts.count = sizeValue(required(bursts, "count", burstsPath), burstsPath + ".count");

            const std::size_t totalTasks{ result.cpu.taskCount + result.gpu.taskCount };
            if (totalTasks == 0U || totalTasks < result.cpu.taskCount)
            {
                throw std::runtime_error{ std::string{ path } + " must contain at least one task without overflow" };
            }
            if (result.bursts.count == 0U || result.bursts.count > totalTasks)
            {
                throw std::runtime_error{ burstsPath + ".count must be in [1, total task count]" };
            }
            return result;
        }

        BaselineVariant parseVariant(const Json& value, const std::string_view path)
        {
            requireObject(value, path);
            const std::string mode{ stringValue(required(value, "execution", path), std::string{ path } + ".execution") };
            BaselineVariant result;
            result.variantId = stringValue(required(value, "variant_id", path), std::string{ path } + ".variant_id");
            if (result.variantId.empty())
            {
                throw std::runtime_error{ std::string{ path } + ".variant_id must not be empty" };
            }
            if (mode == "direct")
            {
                rejectUnknown(value, { "variant_id", "execution" }, path);
                result.executionMode = ExecutionMode::Direct;
                return result;
            }
            if (mode != "scheduled")
            {
                throw std::runtime_error{ std::string{ path } + ".execution is unsupported" };
            }

            rejectUnknown(value, { "variant_id", "execution", "policy", "slice_workgroups" }, path);
            result.executionMode = ExecutionMode::Scheduled;
            result.policy = parsePolicy(required(value, "policy", path), std::string{ path } + ".policy");
            const Json& slice{ required(value, "slice_workgroups", path) };
            result.sliced = !slice.is_null();
            if (result.sliced)
            {
                result.sliceWorkgroups = dimensions(slice, std::string{ path } + ".slice_workgroups");
            }
            return result;
        }

        Json readJson(const std::filesystem::path& path, const std::string_view description)
        {
            std::ifstream input{ path };
            if (!input)
            {
                throw std::runtime_error{ "Unable to open " + std::string{ description } + ": " + path.string() };
            }
            Json root;
            try
            {
                input >> root;
            }
            catch (const Json::exception& error)
            {
                throw std::runtime_error{ "Unable to parse " + std::string{ description } + ": " + error.what() };
            }
            return root;
        }
    } // namespace

    BaselineSuite loadBaselineSuite(const std::filesystem::path& path)
    {
        const Json root = readJson(path, "baseline suite");
        rejectUnknown(root, { "schema_version", "suite_id", "seeds", "warmup_runs", "repetitions", "worker_count", "cases" }, "suite");
        BaselineSuite suite;
        suite.schemaVersion = uint32Value(required(root, "schema_version", "suite"), "suite.schema_version");
        if (suite.schemaVersion != 1U)
        {
            throw std::runtime_error{ "Only baseline suite schema_version 1 is supported" };
        }
        suite.suiteId = stringValue(required(root, "suite_id", "suite"), "suite.suite_id");
        if (suite.suiteId.empty())
        {
            throw std::runtime_error{ "suite.suite_id must not be empty" };
        }

        const Json& seeds{ required(root, "seeds", "suite") };
        if (!seeds.is_array() || seeds.empty())
        {
            throw std::runtime_error{ "suite.seeds must be a non-empty array" };
        }
        for (const Json& seed : seeds)
        {
            suite.seeds.push_back(unsignedInteger(seed, "suite.seeds"));
        }
        suite.warmupRuns = sizeValue(required(root, "warmup_runs", "suite"), "suite.warmup_runs");
        suite.repetitions = sizeValue(required(root, "repetitions", "suite"), "suite.repetitions");
        if (suite.repetitions == 0U)
        {
            throw std::runtime_error{ "suite.repetitions must be positive" };
        }
        suite.workerCount = uint32Value(required(root, "worker_count", "suite"), "suite.worker_count");
        if (suite.workerCount == 0U)
        {
            throw std::runtime_error{ "suite.worker_count must be positive" };
        }

        const Json& cases{ required(root, "cases", "suite") };
        if (!cases.is_array() || cases.empty())
        {
            throw std::runtime_error{ "suite.cases must be a non-empty array" };
        }
        std::unordered_set<std::string> caseIds;
        for (std::size_t caseIndex{ 0U }; caseIndex < cases.size(); ++caseIndex)
        {
            const Json& value{ cases.at(caseIndex) };
            const std::string casePath{ "suite.cases[" + std::to_string(caseIndex) + "]" };
            rejectUnknown(value, { "case_id", "workload", "reference_variant", "variants" }, casePath);
            BaselineCase comparisonCase;
            comparisonCase.caseId = stringValue(required(value, "case_id", casePath), casePath + ".case_id");
            if (comparisonCase.caseId.empty() || !caseIds.insert(comparisonCase.caseId).second)
            {
                throw std::runtime_error{ casePath + ".case_id must be non-empty and unique" };
            }
            comparisonCase.workload = parseWorkload(required(value, "workload", casePath), casePath + ".workload");
            comparisonCase.referenceVariant =
                stringValue(required(value, "reference_variant", casePath), casePath + ".reference_variant");

            const Json& variants{ required(value, "variants", casePath) };
            if (!variants.is_array() || variants.size() < 2U)
            {
                throw std::runtime_error{ casePath + ".variants must contain at least two entries" };
            }
            std::unordered_set<std::string> variantIds;
            for (std::size_t variantIndex{ 0U }; variantIndex < variants.size(); ++variantIndex)
            {
                BaselineVariant variant{ parseVariant(variants.at(variantIndex),
                                                      casePath + ".variants[" + std::to_string(variantIndex) + "]") };
                if (!variantIds.insert(variant.variantId).second)
                {
                    throw std::runtime_error{ casePath + ".variant_id values must be unique" };
                }
                if (variant.sliced && comparisonCase.workload.gpu.taskCount == 0U)
                {
                    throw std::runtime_error{ casePath + " cannot slice a CPU-only workload" };
                }
                comparisonCase.variants.push_back(std::move(variant));
            }
            if (!variantIds.contains(comparisonCase.referenceVariant))
            {
                throw std::runtime_error{ casePath + ".reference_variant does not name a variant" };
            }
            suite.cases.push_back(std::move(comparisonCase));
        }
        return suite;
    }

    EnvironmentMetadata loadEnvironmentMetadata(const std::filesystem::path& path)
    {
        const Json root = readJson(path, "baseline environment metadata");
        rejectUnknown(root,
                      { "schema_version", "environment_id", "cpu_model", "physical_memory_bytes", "os_version", "gpu_driver",
                        "power_profile", "notes" },
                      "environment");
        EnvironmentMetadata result;
        result.schemaVersion = uint32Value(required(root, "schema_version", "environment"), "environment.schema_version");
        if (result.schemaVersion != 1U)
        {
            throw std::runtime_error{ "Only baseline environment schema_version 1 is supported" };
        }
        result.environmentId = stringValue(required(root, "environment_id", "environment"), "environment.environment_id");
        if (result.environmentId.empty())
        {
            throw std::runtime_error{ "environment.environment_id must not be empty" };
        }
        result.cpuModel = optionalString(root, "cpu_model", "environment");
        result.osVersion = optionalString(root, "os_version", "environment");
        result.gpuDriver = optionalString(root, "gpu_driver", "environment");
        result.powerProfile = optionalString(root, "power_profile", "environment");
        result.notes = optionalString(root, "notes", "environment");
        if (root.contains("physical_memory_bytes"))
        {
            result.physicalMemoryBytes = unsignedInteger(root.at("physical_memory_bytes"), "environment.physical_memory_bytes");
        }
        return result;
    }

    std::string toString(const ExecutionMode mode)
    {
        switch (mode)
        {
        case ExecutionMode::Direct:
            return "direct";
        case ExecutionMode::Scheduled:
            return "scheduled";
        }
        throw std::logic_error{ "Unknown baseline execution mode" };
    }

    ExperimentManifest makeExperimentManifest(const BaselineSuite& suite, const BaselineCase& comparisonCase,
                                              const BaselineVariant& variant)
    {
        ExperimentManifest result;
        result.experimentId = suite.suiteId + "/" + comparisonCase.caseId + "/" + variant.variantId;
        result.seeds = suite.seeds;
        result.warmupRuns = suite.warmupRuns;
        result.repetitions = suite.repetitions;
        result.workerCount = suite.workerCount;
        result.policy = variant.policy;
        result.cpu = comparisonCase.workload.cpu;
        result.gpu = comparisonCase.workload.gpu;
        result.gpu.sliced = variant.sliced;
        result.gpu.sliceWorkgroups = variant.sliceWorkgroups;
        result.dependencies = comparisonCase.workload.dependencies;
        result.priorities = comparisonCase.workload.priorities;
        result.bursts = comparisonCase.workload.bursts;
        return result;
    }
} // namespace Atlas::Benchmark
