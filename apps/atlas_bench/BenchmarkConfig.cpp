#include "BenchmarkConfig.h"

#include <algorithm>
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>

/**
 * @file BenchmarkConfig.cpp
 * @brief Implements strict version-one benchmark manifest parsing.
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

        PolicyConfig parsePolicy(const Json& value)
        {
            rejectUnknown(value, { "type", "quantum" }, "policy");
            const std::string type{ stringValue(required(value, "type", "policy"), "policy.type") };
            if (type == "fifo")
            {
                if (value.contains("quantum"))
                {
                    throw std::runtime_error{ "policy.quantum is valid only for round_robin" };
                }
                return {};
            }
            if (type == "static_priority")
            {
                if (value.contains("quantum"))
                {
                    throw std::runtime_error{ "policy.quantum is valid only for round_robin" };
                }
                return PolicyConfig{ PolicyKind::StaticPriority, 1U };
            }
            if (type == "round_robin")
            {
                const std::size_t quantum{ sizeValue(required(value, "quantum", "policy"), "policy.quantum") };
                if (quantum == 0U)
                {
                    throw std::runtime_error{ "policy.quantum must be positive" };
                }
                return PolicyConfig{ PolicyKind::RoundRobin, quantum };
            }
            throw std::runtime_error{ "policy.type is unsupported" };
        }

        DependencyConfig parseDependencies(const Json& value)
        {
            rejectUnknown(value, { "shape", "layers", "edge_probability" }, "workload.dependencies");
            const std::string shape{ stringValue(required(value, "shape", "workload.dependencies"), "workload.dependencies.shape") };
            if (shape == "independent" || shape == "chain")
            {
                if (value.size() != 1U)
                {
                    throw std::runtime_error{ "independent and chain dependencies accept only shape" };
                }
                return DependencyConfig{ shape == "independent" ? DependencyShape::Independent : DependencyShape::Chain, 1U, 0.0 };
            }
            if (shape == "layered")
            {
                const std::size_t layers{ sizeValue(required(value, "layers", "workload.dependencies"),
                                                    "workload.dependencies.layers") };
                if (layers == 0U || value.size() != 2U)
                {
                    throw std::runtime_error{ "layered dependencies require only a positive layers field" };
                }
                return DependencyConfig{ DependencyShape::Layered, layers, 0.0 };
            }
            if (shape == "random")
            {
                const Json& probabilityValue{ required(value, "edge_probability", "workload.dependencies") };
                if (!probabilityValue.is_number())
                {
                    throw std::runtime_error{ "workload.dependencies.edge_probability must be numeric" };
                }
                const double probability{ probabilityValue.get<double>() };
                if (probability < 0.0 || probability > 1.0 || value.size() != 2U)
                {
                    throw std::runtime_error{ "random dependencies require only edge_probability in [0, 1]" };
                }
                return DependencyConfig{ DependencyShape::Random, 1U, probability };
            }
            throw std::runtime_error{ "workload.dependencies.shape is unsupported" };
        }

        PriorityConfig parsePriorities(const Json& value)
        {
            rejectUnknown(value, { "assignment", "values" }, "workload.priorities");
            const std::string assignment{ stringValue(required(value, "assignment", "workload.priorities"),
                                                      "workload.priorities.assignment") };
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
                throw std::runtime_error{ "workload.priorities.assignment is unsupported" };
            }

            const Json& values{ required(value, "values", "workload.priorities") };
            if (!values.is_array() || values.empty())
            {
                throw std::runtime_error{ "workload.priorities.values must be a non-empty array" };
            }
            result.values.clear();
            result.values.reserve(values.size());
            for (std::size_t index{ 0U }; index < values.size(); ++index)
            {
                result.values.push_back(uint32Value(values.at(index), "workload.priorities.values"));
            }
            return result;
        }
    } // namespace

    ExperimentManifest loadManifest(const std::filesystem::path& path)
    {
        std::ifstream input{ path };
        if (!input)
        {
            throw std::runtime_error{ "Unable to open benchmark manifest: " + path.string() };
        }

        Json root;
        try
        {
            input >> root;
        }
        catch (const Json::exception& error)
        {
            throw std::runtime_error{ std::string{ "Unable to parse benchmark manifest: " } + error.what() };
        }
        rejectUnknown(
            root, { "schema_version", "experiment_id", "seeds", "warmup_runs", "repetitions", "worker_count", "policy", "workload" },
            "manifest");

        ExperimentManifest manifest;
        manifest.schemaVersion = uint32Value(required(root, "schema_version", "manifest"), "schema_version");
        if (manifest.schemaVersion != 1U)
        {
            throw std::runtime_error{ "Only benchmark manifest schema_version 1 is supported" };
        }
        manifest.experimentId = stringValue(required(root, "experiment_id", "manifest"), "experiment_id");
        if (manifest.experimentId.empty())
        {
            throw std::runtime_error{ "experiment_id must not be empty" };
        }

        const Json& seeds{ required(root, "seeds", "manifest") };
        if (!seeds.is_array() || seeds.empty())
        {
            throw std::runtime_error{ "seeds must be a non-empty array" };
        }
        for (const Json& seed : seeds)
        {
            manifest.seeds.push_back(unsignedInteger(seed, "seeds"));
        }
        manifest.warmupRuns = sizeValue(required(root, "warmup_runs", "manifest"), "warmup_runs");
        manifest.repetitions = sizeValue(required(root, "repetitions", "manifest"), "repetitions");
        if (manifest.repetitions == 0U)
        {
            throw std::runtime_error{ "repetitions must be positive" };
        }
        manifest.workerCount = uint32Value(required(root, "worker_count", "manifest"), "worker_count");
        if (manifest.workerCount == 0U)
        {
            throw std::runtime_error{ "worker_count must be positive" };
        }
        manifest.policy = parsePolicy(required(root, "policy", "manifest"));

        const Json& workload{ required(root, "workload", "manifest") };
        rejectUnknown(workload, { "cpu", "gpu", "dependencies", "priorities", "bursts" }, "workload");
        const Json& cpu{ required(workload, "cpu", "workload") };
        rejectUnknown(cpu, { "task_count", "iterations" }, "workload.cpu");
        manifest.cpu.taskCount = sizeValue(required(cpu, "task_count", "workload.cpu"), "workload.cpu.task_count");
        manifest.cpu.iterations = unsignedInteger(required(cpu, "iterations", "workload.cpu"), "workload.cpu.iterations");
        if (manifest.cpu.taskCount != 0U && manifest.cpu.iterations == 0U)
        {
            throw std::runtime_error{ "workload.cpu.iterations must be positive when CPU tasks are requested" };
        }

        const Json& gpu{ required(workload, "gpu", "workload") };
        rejectUnknown(gpu, { "task_count", "workgroups", "slice_workgroups" }, "workload.gpu");
        manifest.gpu.taskCount = sizeValue(required(gpu, "task_count", "workload.gpu"), "workload.gpu.task_count");
        manifest.gpu.workgroups = dimensions(required(gpu, "workgroups", "workload.gpu"), "workload.gpu.workgroups");
        const Json& slice{ required(gpu, "slice_workgroups", "workload.gpu") };
        manifest.gpu.sliced = !slice.is_null();
        if (manifest.gpu.sliced)
        {
            manifest.gpu.sliceWorkgroups = dimensions(slice, "workload.gpu.slice_workgroups");
        }

        manifest.dependencies = parseDependencies(required(workload, "dependencies", "workload"));
        manifest.priorities = parsePriorities(required(workload, "priorities", "workload"));
        const Json& bursts{ required(workload, "bursts", "workload") };
        rejectUnknown(bursts, { "count" }, "workload.bursts");
        manifest.bursts.count = sizeValue(required(bursts, "count", "workload.bursts"), "workload.bursts.count");

        const std::size_t totalTasks{ manifest.cpu.taskCount + manifest.gpu.taskCount };
        if (totalTasks == 0U || totalTasks < manifest.cpu.taskCount)
        {
            throw std::runtime_error{ "The workload must contain at least one task without overflowing task count" };
        }
        if (manifest.bursts.count == 0U || manifest.bursts.count > totalTasks)
        {
            throw std::runtime_error{ "workload.bursts.count must be in [1, total task count]" };
        }
        return manifest;
    }

    std::string toString(const PolicyKind kind)
    {
        switch (kind)
        {
        case PolicyKind::Fifo:
            return "fifo";
        case PolicyKind::RoundRobin:
            return "round_robin";
        case PolicyKind::StaticPriority:
            return "static_priority";
        }
        throw std::logic_error{ "Unknown benchmark policy kind" };
    }

    std::string toString(const DependencyShape shape)
    {
        switch (shape)
        {
        case DependencyShape::Independent:
            return "independent";
        case DependencyShape::Chain:
            return "chain";
        case DependencyShape::Layered:
            return "layered";
        case DependencyShape::Random:
            return "random";
        }
        throw std::logic_error{ "Unknown benchmark dependency shape" };
    }

    std::string toString(const PriorityAssignment assignment)
    {
        switch (assignment)
        {
        case PriorityAssignment::Cycle:
            return "cycle";
        case PriorityAssignment::Random:
            return "random";
        }
        throw std::logic_error{ "Unknown benchmark priority assignment" };
    }
} // namespace Atlas::Benchmark
