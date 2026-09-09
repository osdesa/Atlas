#include "BuiltinMetadata.h"
#include "PackSnapshots.h"
#include "atlas/Executor/SynchronousCpuExecutor.h"
#include "atlas/Executor/VulkanExecutor.h"
#include "atlas/Executor/WorkerpoolExecutor.h"
#include "atlas/Profiling/Trace.h"
#include "atlas/Scheduler/FifoSchedulingPolicy.h"
#include "atlas/Scheduler/KahnScheduler.h"
#include "atlas/Scheduler/RoundRobinSchedulingPolicy.h"
#include "atlas/Scheduler/StaticPrioritySchedulingPolicy.h"
#include "atlas/Tasking/TaskGraph.h"
#include "atlas/Vulkan/VulkanRuntime.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

/**
 * @file main.cpp
 * @brief Implements the strict process-boundary runner used by Atlas Studio.
 *
 * The runner accepts one validated descriptor-based graph, executes it once,
 * emits bounded version-two JSONL to standard output, and reserves standard
 * error for diagnostics. All Vulkan resources remain process-owned.
 */

namespace
{
    using Json = nlohmann::json;

    /** @brief Produces valid UTF-8 diagnostics bounded to 4 KiB even for arbitrary native error bytes. */
    std::string boundedMessage(const std::string_view message)
    {
        std::string text = Json::parse(Json(message).dump(-1, ' ', false, Json::error_handler_t::replace)).get<std::string>();
        if (text.empty())
            return "Runner operation failed";
        if (text.size() > 4096U)
        {
            std::size_t end = 4096U;
            while ((static_cast<unsigned char>(text[end]) & 0xc0U) == 0x80U)
                --end;
            text.resize(end);
        }
        return text;
    }

    struct Dimensions
    {
        std::uint32_t x{ 1U };
        std::uint32_t y{ 1U };
        std::uint32_t z{ 1U };
    };

    /** @brief Rejects signed, floating, boolean, and overflowing document integers before conversion. */
    template <typename Integer>
    Integer unsignedValue(const Json& value, const std::string& path, const Integer minimum = 0,
                          const Integer maximum = std::numeric_limits<Integer>::max())
    {
        if (!value.is_number_unsigned() || value.get<std::uint64_t>() < minimum || value.get<std::uint64_t>() > maximum)
            throw std::runtime_error{ path + " must be an unsigned integer within bounds" };
        return value.get<Integer>();
    }

    const char* stateName(const Atlas::TaskState value) noexcept
    {
        static constexpr const char* names[] = {
            "unknown", "ready", "running", "success", "failure", "blocked", "paused", "cancelled"
        };
        return names[static_cast<std::size_t>(value)];
    }

    Dimensions dimensions(const Json& value, const std::string& path)
    {
        if (!value.is_object() || value.size() != 3U || !value.contains("x") || !value.contains("y") || !value.contains("z") ||
            !value.at("x").is_number_unsigned() || !value.at("y").is_number_unsigned() || !value.at("z").is_number_unsigned())
        {
            throw std::runtime_error{ path + " must contain unsigned x, y, and z dimensions" };
        }
        const auto result =
            Dimensions{ unsignedValue<std::uint32_t>(value.at("x"), path, 1U), unsignedValue<std::uint32_t>(value.at("y"), path, 1U),
                        unsignedValue<std::uint32_t>(value.at("z"), path, 1U) };
        if (result.x == 0U || result.y == 0U || result.z == 0U)
        {
            throw std::runtime_error{ path + " dimensions must be non-zero" };
        }
        return result;
    }

    std::vector<std::uint32_t> shaderWords(const char* path)
    {
        std::ifstream input{ path, std::ios::binary };
        if (!input)
        {
            throw std::runtime_error{ std::string{ "unable to open compiled shader: " } + path };
        }
        const std::vector<char> bytes{ std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{} };
        if (bytes.empty() || bytes.size() % sizeof(std::uint32_t) != 0U)
        {
            throw std::runtime_error{ std::string{ "compiled shader is malformed: " } + path };
        }
        std::vector<std::uint32_t> words(bytes.size() / sizeof(std::uint32_t));
        std::memcpy(words.data(), bytes.data(), bytes.size());
        return words;
    }

    void rejectUnknown(const Json& object, const std::vector<std::string_view>& allowed, const std::string& path)
    {
        if (!object.is_object())
        {
            throw std::runtime_error{ path + " must be an object" };
        }
        for (const auto& [key, value] : object.items())
        {
            static_cast<void>(value);
            if (std::find(allowed.begin(), allowed.end(), key) == allowed.end())
            {
                throw std::runtime_error{ path + " contains unknown field '" + key + "'" };
            }
        }
    }

    struct NodeConfig
    {
        std::string id;
        std::string name;
        std::string resource;
        std::string taskId;
        std::string packId;
        Json parameters;
        std::uint32_t priority{ 0U };
        std::uint64_t elementCount{ 0U };
        float leftValue{ 0.0F };
        float rightValue{ 0.0F };
        Dimensions workgroups;
        std::optional<Dimensions> slice;
    };

    struct Config
    {
        std::string id{ "studio-run" };
        std::uint64_t seed{ 1U };
        bool validation{ false };
        bool tracing{ true };
        std::size_t traceCapacity{ 65'536U };
        bool synchronousCpu{ true };
        std::uint32_t workerCount{ 1U };
        std::string policy{ "fifo" };
        std::size_t quantum{ 1U };
        Json packs = Json::array();
        std::vector<NodeConfig> nodes;
        std::vector<std::pair<std::string, std::string>> edges;
    };

    std::size_t checkedBytes(const std::uint64_t elements, const std::size_t elementSize, const std::string& path)
    {
        if (elements > std::numeric_limits<std::size_t>::max() / elementSize)
        {
            throw std::runtime_error{ path + " is too large" };
        }
        return static_cast<std::size_t>(elements) * elementSize;
    }

    std::uint64_t checkedProduct(const Dimensions& value, const std::string& path)
    {
        const std::uint64_t first = static_cast<std::uint64_t>(value.x) * value.y;
        if (first > std::numeric_limits<std::uint64_t>::max() / value.z)
            throw std::runtime_error{ path + " product is too large" };
        return first * value.z;
    }

    std::uint64_t runCpuKernel(std::uint64_t value, const std::uint64_t iterations) noexcept
    {
        for (std::uint64_t iteration{ 0U }; iteration < iterations; ++iteration)
        {
            value ^= value >> 12U;
            value ^= value << 25U;
            value ^= value >> 27U;
            value *= 2'685'821'657'736'338'717ULL;
        }
        return value;
    }

    const Json& required(const Json& object, const char* key, const std::string& path)
    {
        if (!object.contains(key))
        {
            throw std::runtime_error{ path + " is missing required field '" + key + "'" };
        }
        return object.at(key);
    }

    Config loadConfig(const std::string& path)
    {
        std::ifstream input{ path };
        if (!input)
        {
            throw std::runtime_error{ "unable to open studio graph: " + path };
        }
        if (std::filesystem::file_size(path) > 16U * 1024U * 1024U)
            throw std::runtime_error{ "Studio graph exceeds 16 MiB" };
        Json root;
        input >> root;
        input >> std::ws;
        if (!input.eof())
            throw std::runtime_error{ "Trailing content after studio graph" };
        rejectUnknown(
            root, { "schema_version", "graph_id", "seed", "runtime", "cpu_executor", "policy", "trace", "nodes", "edges", "packs" },
            "graph");
        if (unsignedValue<std::uint32_t>(required(root, "schema_version", "graph"), "schema_version") != 2U)
        {
            throw std::runtime_error{ "only atlas-studio-graph schema version 2 is supported" };
        }
        Config config;
        config.packs = required(root, "packs", "graph");
        if (!config.packs.is_array() || config.packs.size() > 128U)
            throw std::runtime_error{ "graph.packs must be a bounded array" };
        std::set<std::string> packIds;
        for (const auto& pack : config.packs)
        {
            rejectUnknown(pack, { "pack_id", "version", "digest" }, "graph.packs[]");
            const auto id = required(pack, "pack_id", "pack").get<std::string>();
            const auto version = required(pack, "version", "pack").get<std::string>();
            const auto digest = required(pack, "digest", "pack").get<std::string>();
            if (id.empty() || id.size() > 128U || id == "atlas.builtin" || !packIds.insert(id).second || version.empty() ||
                version.size() > 128U || digest.size() != 64U || digest.find_first_not_of("0123456789abcdef") != std::string::npos)
                throw std::runtime_error{ "Invalid or duplicate graph pack identity" };
        }
        if (root.contains("graph_id"))
        {
            config.id = root.at("graph_id").get<std::string>();
        }
        if (config.id.empty() || config.id.size() > 128U || config.id.find('\0') != std::string::npos)
            throw std::runtime_error{ "Invalid graph_id" };
        if (root.contains("seed"))
        {
            config.seed = unsignedValue<std::uint64_t>(root.at("seed"), "graph.seed");
        }
        if (root.contains("runtime"))
        {
            rejectUnknown(root.at("runtime"), { "validation" }, "graph.runtime");
            config.validation = root.at("runtime").value("validation", false);
        }
        if (root.contains("cpu_executor"))
        {
            rejectUnknown(root.at("cpu_executor"), { "mode", "worker_count" }, "graph.cpu_executor");
            const std::string mode = root.at("cpu_executor").value("mode", "synchronous");
            config.synchronousCpu = mode == "synchronous";
            if (!config.synchronousCpu && mode != "worker_pool")
            {
                throw std::runtime_error{ "graph.cpu_executor.mode must be synchronous or worker_pool" };
            }
            config.workerCount =
                unsignedValue<std::uint32_t>(root.at("cpu_executor").value("worker_count", Json(1U)), "worker_count", 1U);
            if (config.workerCount == 0U)
            {
                throw std::runtime_error{ "graph.cpu_executor.worker_count must be positive" };
            }
        }
        if (root.contains("policy"))
        {
            rejectUnknown(root.at("policy"), { "type", "quantum" }, "graph.policy");
            config.policy = required(root.at("policy"), "type", "graph.policy").get<std::string>();
            if (config.policy == "round_robin")
            {
                config.quantum = unsignedValue<std::size_t>(required(root.at("policy"), "quantum", "graph.policy"), "quantum", 1U);
                if (config.quantum == 0U)
                {
                    throw std::runtime_error{ "graph.policy.quantum must be positive" };
                }
            }
            else if (config.policy != "fifo" && config.policy != "static_priority")
            {
                throw std::runtime_error{ "graph.policy.type is unsupported" };
            }
            if (root.at("policy").contains("quantum"))
                static_cast<void>(unsignedValue<std::size_t>(root.at("policy").at("quantum"), "quantum", 1U));
        }
        if (root.contains("trace"))
        {
            rejectUnknown(root.at("trace"), { "enabled", "capacity" }, "graph.trace");
            config.tracing = root.at("trace").value("enabled", true);
            config.traceCapacity =
                unsignedValue<std::size_t>(root.at("trace").value("capacity", Json(65'536U)), "trace.capacity", 1U, 1'000'000U);
            if (config.tracing && config.traceCapacity == 0U)
            {
                throw std::runtime_error{ "graph.trace.capacity must be positive" };
            }
        }
        const Json nodes = required(root, "nodes", "graph");
        if (!nodes.is_array() || nodes.empty() || nodes.size() > 10'000U)
        {
            throw std::runtime_error{ "graph.nodes must contain between 1 and 10000 nodes" };
        }
        for (std::size_t index = 0; index < nodes.size(); ++index)
        {
            const std::string nodePath = "graph.nodes[" + std::to_string(index) + "]";
            const Json& node = nodes.at(index);
            rejectUnknown(node, { "id", "name", "resource", "pack_id", "task_id", "parameters", "priority", "slice_workgroups" },
                          nodePath);
            NodeConfig parsed;
            parsed.id = required(node, "id", nodePath).get<std::string>();
            if (parsed.id.empty())
                throw std::runtime_error{ nodePath + ".id must not be empty" };
            if (std::any_of(config.nodes.begin(), config.nodes.end(), [&](const NodeConfig& prior) { return prior.id == parsed.id; }))
                throw std::runtime_error{ "graph.nodes contains duplicate id '" + parsed.id + "'" };
            parsed.name = required(node, "name", nodePath).get<std::string>();
            parsed.resource = required(node, "resource", nodePath).get<std::string>();
            parsed.priority = unsignedValue<std::uint32_t>(required(node, "priority", nodePath), nodePath + ".priority");
            parsed.packId = required(node, "pack_id", nodePath).get<std::string>();
            parsed.taskId = required(node, "task_id", nodePath).get<std::string>();
            parsed.parameters = required(node, "parameters", nodePath);
            if (parsed.id.size() > 128U || parsed.name.empty() || parsed.name.size() > 4096U ||
                parsed.id.find('\0') != std::string::npos || parsed.name.find('\0') != std::string::npos || parsed.packId.empty() ||
                parsed.packId.size() > 128U || parsed.taskId.empty() || parsed.taskId.size() > 128U ||
                (parsed.resource != "cpu" && parsed.resource != "gpu") || !parsed.parameters.is_object() ||
                parsed.parameters.dump().size() > 65536U || (parsed.packId != "atlas.builtin" && !packIds.contains(parsed.packId)))
                throw std::runtime_error{ nodePath + " has invalid task identity or parameters" };
            if (node.contains("slice_workgroups") && !node.at("slice_workgroups").is_null())
            {
                parsed.slice = dimensions(node.at("slice_workgroups"), nodePath + ".slice_workgroups");
            }
            config.nodes.push_back(std::move(parsed));
        }
        const Json edges = required(root, "edges", "graph");
        if (!edges.is_array() || edges.size() > 50'000U)
        {
            throw std::runtime_error{ "graph.edges must be an array of at most 50000 edges" };
        }
        for (const Json& edge : edges)
        {
            rejectUnknown(edge, { "from", "to" }, "graph.edges[]");
            config.edges.emplace_back(required(edge, "from", "graph.edges[]").get<std::string>(),
                                      required(edge, "to", "graph.edges[]").get<std::string>());
        }
        std::map<std::string, std::size_t> indices;
        for (std::size_t i = 0; i < config.nodes.size(); ++i)
            indices.emplace(config.nodes[i].id, i);
        std::vector<std::vector<std::size_t>> successors(config.nodes.size());
        std::vector<std::size_t> indegree(config.nodes.size());
        std::set<std::pair<std::string, std::string>> seen;
        for (const auto& edge : config.edges)
        {
            if (!indices.contains(edge.first) || !indices.contains(edge.second) || edge.first == edge.second ||
                !seen.insert(edge).second)
                throw std::runtime_error{ "Invalid, duplicate, or unknown dependency" };
            successors[indices.at(edge.first)].push_back(indices.at(edge.second));
            ++indegree[indices.at(edge.second)];
        }
        std::vector<std::size_t> ready;
        for (std::size_t i = 0; i < indegree.size(); ++i)
            if (indegree[i] == 0U)
                ready.push_back(i);
        for (std::size_t i = 0; i < ready.size(); ++i)
            for (const auto next : successors[ready[i]])
                if (--indegree[next] == 0U)
                    ready.push_back(next);
        if (ready.size() != config.nodes.size())
            throw std::runtime_error{ "Studio graph contains a cycle" };
        for (const auto& id : packIds)
            if (std::none_of(config.nodes.begin(), config.nodes.end(), [&](const NodeConfig& node) { return node.packId == id; }))
                throw std::runtime_error{ "Graph declares an unused pack" };
        return config;
    }

    class StudioTrace final
    {
      public:
        explicit StudioTrace(const std::size_t capacity, const Json& packs)
            : buffer{ capacity }, session{ buffer }, consumer{ [this] { consume(); } }
        {
            write(Json{
                { "record_type", "header" }, { "studio_schema_version", 2 }, { "trace_schema_version", 1 }, { "packs", packs } });
        }
        ~StudioTrace()
        {
            buffer.close();
            if (consumer.joinable())
                consumer.join();
        }
        Atlas::TraceSession* sessionPtr() noexcept
        {
            return &session;
        }
        void finish(const std::string_view status, const Json* result)
        {
            if (finished.exchange(true))
            {
                return;
            }
            buffer.close();
            if (consumer.joinable())
            {
                consumer.join();
            }
            if (result != nullptr)
            {
                write(*result);
            }
            Json footer{ { "record_type", "footer" },
                         { "status", status },
                         { "accepted_events", buffer.acceptedEventCount() },
                         { "dropped_events", buffer.droppedEventCount() },
                         { "complete", true } };
            std::cout << footer.dump() << std::endl;
        }
        void write(const Json& record)
        {
            const auto encoded = record.dump(-1, ' ', false, Json::error_handler_t::replace);
            if (encoded.size() > 16U * 1024U * 1024U)
                throw std::runtime_error{ "Studio output record exceeds 16 MiB" };
            std::lock_guard lock{ outputMutex };
            std::cout << encoded << std::endl;
        }

      private:
        static const char* kind(const Atlas::TraceEventKind value) noexcept
        {
            static constexpr const char* names[] = {
                "scheduler_started", "scheduler_finished",   "task_ready",          "policy_decision",        "task_selected",
                "task_resumed",      "submission_requested", "submission_accepted", "submission_rejected",    "backend_started",
                "backend_finished",  "completion_observed",  "task_paused",         "cancellation_requested", "cancellation_applied",
                "task_succeeded",    "task_failed",          "policy_failed",       "infrastructure_failed"
            };
            return names[static_cast<std::size_t>(value)];
        }
        static const char* source(const Atlas::TraceEventSource value) noexcept
        {
            return value == Atlas::TraceEventSource::Scheduler     ? "scheduler"
                   : value == Atlas::TraceEventSource::CpuExecutor ? "cpu_executor"
                                                                   : "vulkan_executor";
        }
        void consume() noexcept
        {
            while (const auto event = buffer.waitPop())
            {
                Json record{ { "record_type", "event" },
                             { "sequence", event->sequence },
                             { "timestamp_ns", event->timestampNanoseconds },
                             { "kind", kind(event->kind) },
                             { "source", source(event->source) },
                             { "priority", event->priority },
                             { "previous_state", stateName(event->previousState) },
                             { "state", stateName(event->state) },
                             { "host_duration_ns", event->hostDurationNanoseconds } };
                if (event->hasTask)
                {
                    record["graph_id"] = event->graphId;
                    record["task_id"] = event->taskId;
                }
                if (event->hasResource)
                {
                    record["resource"] = event->resource == Atlas::ExecutionResource::CPU ? "cpu" : "gpu";
                }
                if (event->workUnitIndex != Atlas::noTraceIndex)
                    record["work_unit_index"] = event->workUnitIndex;
                if (event->workerIndex != Atlas::noTraceIndex)
                    record["worker_index"] = event->workerIndex;
                if (event->readyCount != Atlas::noTraceIndex)
                    record["ready_count"] = event->readyCount;
                if (event->selectedIndex != Atlas::noTraceIndex)
                    record["selected_index"] = event->selectedIndex;
                if (event->hasDeviceDuration)
                    record["device_duration_ns"] = event->deviceDurationNanoseconds;
                write(record);
            }
        }
        Atlas::BoundedTraceBuffer buffer;
        Atlas::TraceSession session;
        std::mutex outputMutex;
        std::atomic_bool finished{ false };
        std::jthread consumer;
    };

    std::unique_ptr<Atlas::SchedulingPolicy> policy(const Config& config)
    {
        if (config.policy == "fifo")
            return std::make_unique<Atlas::FifoSchedulingPolicy>();
        if (config.policy == "round_robin")
            return std::make_unique<Atlas::RoundRobinSchedulingPolicy>(config.quantum);
        return std::make_unique<Atlas::StaticPrioritySchedulingPolicy>();
    }

    struct VectorAddResources final
    {
        VectorAddResources(Atlas::VulkanRuntime& runtimeContext, const NodeConfig& config)
            : count{ static_cast<std::size_t>(config.elementCount) }, runtime{ runtimeContext },
              left{ runtimeContext.createBuffer(checkedBytes(config.elementCount, sizeof(float), "vector_add buffer")) },
              right{ runtimeContext.createBuffer(checkedBytes(config.elementCount, sizeof(float), "vector_add buffer")) },
              output{ runtimeContext.createBuffer(checkedBytes(config.elementCount, sizeof(float), "vector_add buffer")) },
              pipeline{ runtimeContext.createComputePipeline(Atlas::ComputeShader{ shaderWords(ATLAS_STUDIO_VECTOR_ADD_SPIRV_PATH),
                                                                                   "main",
                                                                                   { { 0U, Atlas::BufferAccess::ReadOnly },
                                                                                     { 1U, Atlas::BufferAccess::ReadOnly },
                                                                                     { 2U, Atlas::BufferAccess::WriteOnly } } }) },
              dispatch{ pipeline,
                        { { 0U, left, Atlas::BufferAccess::ReadOnly },
                          { 1U, right, Atlas::BufferAccess::ReadOnly },
                          { 2U, output, Atlas::BufferAccess::WriteOnly } },
                        Atlas::DispatchDimensions{ config.workgroups.x, config.workgroups.y, config.workgroups.z } },
              leftValue{ config.leftValue }, rightValue{ config.rightValue }
        {
            std::vector<float> values(count, leftValue);
            runtime.upload(left, std::as_bytes(std::span{ values }));
            values.assign(count, rightValue);
            runtime.upload(right, std::as_bytes(std::span{ values }));
        }
        void verify() const
        {
            std::vector<float> values(count);
            runtime.download(output, std::as_writable_bytes(std::span{ values }));
            if (!std::all_of(values.begin(), values.end(), [this](const float value) { return value == leftValue + rightValue; }))
            {
                throw std::runtime_error{ "vector_add output validation failed" };
            }
        }
        std::size_t count;
        Atlas::VulkanRuntime& runtime;
        Atlas::VulkanBuffer left, right, output;
        Atlas::VulkanComputePipeline pipeline;
        Atlas::VulkanDispatch dispatch;
        float leftValue, rightValue;
    };

    /** @brief Constructs internal descriptors from the same trusted metadata used by Studio forms. */
    Atlas::CustomTaskDescriptor builtinDescriptor(const std::string& id)
    {
        for (const auto& metadata : Json::parse(builtinMetadata))
        {
            if (metadata.at("task_id") != id)
                continue;
            Atlas::CustomTaskDescriptor descriptor;
            descriptor.packId = "atlas.builtin";
            descriptor.taskId = id;
            descriptor.displayName = metadata.at("name");
            descriptor.resource = metadata.at("resource") == "cpu" ? Atlas::ExecutionResource::CPU : Atlas::ExecutionResource::GPU;
            descriptor.supportsSlicing = metadata.at("supports_slicing");
            for (const auto* key : { "parameters", "summaries" })
            {
                auto& fields = std::string_view{ key } == "parameters" ? descriptor.parameters : descriptor.summaries;
                for (const auto& value : metadata.at(key))
                {
                    Atlas::TaskPackFieldDescriptor field;
                    field.id = value.at("id");
                    field.displayName = value.value("name", field.id);
                    field.required = value.value("required", true);
                    const std::string type = value.at("type");
                    if (type == "unsigned_integer")
                    {
                        field.type = Atlas::TaskPackFieldType::UnsignedInteger;
                        if (value.contains("minimum"))
                            field.minimumUnsigned = value.at("minimum").get<std::uint64_t>();
                        if (value.contains("maximum"))
                            field.maximumUnsigned = value.at("maximum").get<std::uint64_t>();
                        if (value.contains("default"))
                            field.defaultValue = value.at("default").get<std::uint64_t>();
                    }
                    else if (type == "number")
                    {
                        field.type = Atlas::TaskPackFieldType::Number;
                        if (value.contains("minimum"))
                            field.minimumNumber = value.at("minimum").get<double>();
                        if (value.contains("maximum"))
                            field.maximumNumber = value.at("maximum").get<double>();
                        if (value.contains("default"))
                            field.defaultValue = value.at("default").get<double>();
                    }
                    else if (type == "boolean")
                        field.type = Atlas::TaskPackFieldType::Boolean;
                    else
                        throw std::logic_error{ "Unsupported internal built-in field" };
                    fields.push_back(std::move(field));
                }
            }
            return descriptor;
        }
        throw std::runtime_error{ "Unknown atlas.builtin task: " + id };
    }

    /** @brief Common prepared-node adapter. Closures retain payload and summary ownership through execution.
     * add is invoked exactly once after every node has prepared; summary only after successful execution.
     */
    struct PreparedTask
    {
        std::function<std::optional<Atlas::TaskHandle>(Atlas::TaskGraph&, Atlas::TaskOptions)> add;
        std::function<Atlas::CustomTaskSummary()> summary;
    };

    PreparedTask prepareBuiltin(NodeConfig node, const Atlas::CustomTaskDescriptor& descriptor, Atlas::VulkanRuntime& runtime,
                                const std::uint64_t seed, const std::size_t index)
    {
        const Json parameters = Json::parse(descriptor.canonicalizeParameters(node.parameters.dump()));
        if (node.taskId == "cpu_burn")
        {
            auto value = std::make_shared<std::uint64_t>(0U);
            const auto iterations = parameters.at("iterations").get<std::uint64_t>();
            return { [value, seed, index, iterations](Atlas::TaskGraph& graph, Atlas::TaskOptions options)
                     {
                         return graph.addCpuTask([value, seed, index, iterations]
                                                 { *value = runCpuKernel(seed ^ (index + 1U), iterations); }, std::move(options));
                     },
                     [value, descriptor] { return descriptor.validateSummary(Json{ { "value", *value } }.dump()); } };
        }
        std::optional<Atlas::VulkanDispatch> dispatch;
        std::function<Atlas::CustomTaskSummary()> summary;
        if (node.taskId == "gpu_increment")
        {
            node.workgroups = { parameters.at("workgroups_x"), parameters.at("workgroups_y"), parameters.at("workgroups_z") };
            const auto count = checkedProduct(node.workgroups, "gpu_increment workgroups");
            if (count > (256U * 1024U * 1024U - 16U) / sizeof(std::uint32_t))
                throw std::runtime_error{ "gpu_increment allocation exceeds 256 MiB" };
            const auto pipeline = runtime.createComputePipeline(
                Atlas::ComputeShader{ shaderWords(ATLAS_STUDIO_BENCHMARK_SPIRV_PATH),
                                      "main",
                                      { { 0U, Atlas::BufferAccess::ReadOnly }, { 1U, Atlas::BufferAccess::ReadWrite } } });
            const auto dimensionsBuffer = runtime.createBuffer(4U * sizeof(std::uint32_t));
            const auto output = runtime.createBuffer(checkedBytes(count, sizeof(std::uint32_t), "gpu_increment"));
            const std::vector<std::uint32_t> dimensionsData{ node.workgroups.x, node.workgroups.y, node.workgroups.z, 0U };
            runtime.upload(dimensionsBuffer, std::as_bytes(std::span{ dimensionsData }));
            std::vector<std::uint32_t> zeros(static_cast<std::size_t>(count), 0U);
            runtime.upload(output, std::as_bytes(std::span{ zeros }));
            dispatch.emplace(pipeline,
                             std::vector<Atlas::BufferBinding>{ { 0U, dimensionsBuffer, Atlas::BufferAccess::ReadOnly },
                                                                { 1U, output, Atlas::BufferAccess::ReadWrite } },
                             Atlas::DispatchDimensions{ node.workgroups.x, node.workgroups.y, node.workgroups.z });
            summary = [&runtime, output, count, descriptor]
            {
                std::vector<std::uint32_t> values(static_cast<std::size_t>(count));
                runtime.download(output, std::as_writable_bytes(std::span{ values }));
                if (!std::all_of(values.begin(), values.end(), [](const auto value) { return value == 1013904223U; }))
                    throw std::runtime_error{ "gpu_increment output validation failed" };
                return descriptor.validateSummary(R"({"ok":true})");
            };
        }
        else
        {
            node.elementCount = parameters.at("element_count");
            node.leftValue = parameters.at("left_value");
            node.rightValue = parameters.at("right_value");
            node.workgroups = { static_cast<std::uint32_t>((node.elementCount + 63U) / 64U), 1U, 1U };
            auto resources = std::make_shared<VectorAddResources>(runtime, node);
            dispatch = resources->dispatch;
            summary = [resources, descriptor]
            {
                resources->verify();
                return descriptor.validateSummary(R"({"ok":true})");
            };
        }
        if (node.slice)
        {
            Atlas::SlicedVulkanDispatch sliced{ *dispatch, { node.slice->x, node.slice->y, node.slice->z } };
            return { [sliced](Atlas::TaskGraph& graph, Atlas::TaskOptions options)
                     { return graph.addGpuTask(sliced, std::move(options)); }, std::move(summary) };
        }
        return { [work = *dispatch](Atlas::TaskGraph& graph, Atlas::TaskOptions options)
                 { return graph.addGpuTask(work, std::move(options)); }, std::move(summary) };
    }

    Json resultJson(const Atlas::SchedulerResult& result, const Atlas::VulkanRuntime& runtime, const Config& config,
                    const Atlas::TaskGraph& graph, const std::vector<Atlas::TaskHandle>& handles)
    {
        Json resultRecord{ { "record_type", "result" },
                           { "status", std::string{ Atlas::toString(result.status) } },
                           { "executed_task_count", result.executedTaskCount },
                           { "execution_time_ns", result.executionTime.count() },
                           { "scheduler_active_ns", result.schedulerActiveDuration.count() },
                           { "immediate_slice_switch_ns", result.immediateSliceSwitchDuration.count() },
                           { "immediate_slice_switch_count", result.immediateSliceSwitchCount },
                           { "device", runtime.deviceInfo().name },
                           { "timestamp_supported", runtime.timestampCapabilities().supported } };
        Json tasks = Json::array();
        for (std::size_t index = 0; index < handles.size(); ++index)
        {
            const auto snapshot = graph.snapshotTask(handles.at(index));
            if (!snapshot.has_value())
                continue;
            const auto& info = snapshot->executionInfo;
            Json task{ { "node_id", config.nodes.at(index).id },
                       { "task_id", handles.at(index).getTaskID().getValue() },
                       { "state", stateName(info.state) },
                       { "execution_duration_ns", info.executionDuration.count() },
                       { "completed_work_units", info.completedWorkUnitCount },
                       { "total_work_units", info.totalWorkUnitCount },
                       { "ready_wait_ns", info.readyWaitDuration.count() },
                       { "selection_bypass_count", info.selectionBypassCount } };
            if (info.responseDuration.has_value())
                task["response_duration_ns"] = info.responseDuration->count();
            if (info.deviceExecutionDuration.has_value())
                task["device_execution_duration_ns"] = info.deviceExecutionDuration->count();
            tasks.push_back(std::move(task));
        }
        resultRecord["tasks"] = std::move(tasks);
        return resultRecord;
    }
} // namespace

int main(int argc, char** argv)
{
    std::unique_ptr<StudioTrace> trace;
    std::string phase = "preflight";
    try
    {
        std::string configPath, controlPath;
        std::vector<std::filesystem::path> packPaths;
        for (int i = 1; i < argc; ++i)
        {
            const std::string_view option{ argv[i] };
            if (i + 1 >= argc)
                throw std::invalid_argument{ "Missing runner argument value" };
            const std::string value{ argv[++i] };
            if (option == "--config" && configPath.empty())
                configPath = value;
            else if (option == "--control" && controlPath.empty())
                controlPath = value;
            else if (option == "--task-pack" && packPaths.size() < 128U)
                packPaths.emplace_back(value);
            else
                throw std::invalid_argument{ "Unknown or duplicate runner argument" };
        }
        if (configPath.empty() || controlPath.empty())
            throw std::invalid_argument{
                "Usage: atlas_studio_runner --config <graph.json> --control <cancel-file> [--task-pack <trusted-directory>]..."
            };
        const Config config = loadConfig(configPath);
        Atlas::Studio::PackSnapshots snapshots;
        Atlas::TaskPackRegistry registry;
        std::vector<Atlas::TaskPackManifest> available;
        for (const auto& path : packPaths)
            available.push_back(registry.inspectDirectory(path));
        std::map<std::string, std::string> digests;
        std::vector<std::filesystem::path> selected;
        // Resolve and verify every snapshot before executing any native loader.
        for (const auto& requested : config.packs)
        {
            const std::string id = requested.at("pack_id"), digest = requested.at("digest");
            const auto found = std::find_if(
                available.begin(), available.end(), [&](const auto& pack)
                { return pack.packId == id && pack.digest == digest && pack.version == requested.at("version").get<std::string>(); });
            if (found == available.end())
                throw std::runtime_error{ "Missing exact task pack: " + id + " digest " + digest };
            selected.push_back(snapshots.copy(*found, registry));
            digests.emplace(id, digest);
        }
        for (const auto& path : selected)
        {
            const auto& loaded = registry.loadDirectory(path);
            if (!digests.contains(loaded.packId) || digests.at(loaded.packId) != loaded.digest)
                throw std::runtime_error{ "Loaded snapshot identity differs from graph provenance" };
        }
        std::vector<Atlas::CustomTaskDescriptor> descriptors;
        for (const auto& node : config.nodes)
        {
            Atlas::CustomTaskDescriptor descriptor;
            if (node.packId == "atlas.builtin")
                descriptor = builtinDescriptor(node.taskId);
            else
            {
                const auto* found = registry.findTask(node.packId, digests.at(node.packId), node.taskId);
                if (found == nullptr)
                    throw std::runtime_error{ "Unknown exact task descriptor: " + node.packId + "/" + node.taskId };
                descriptor = *found;
            }
            if ((descriptor.resource == Atlas::ExecutionResource::CPU ? "cpu" : "gpu") != node.resource ||
                (node.slice && !descriptor.supportsSlicing))
                throw std::runtime_error{ "Task resource or slicing does not match descriptor: " + node.id };
            static_cast<void>(descriptor.canonicalizeParameters(node.parameters.dump()));
            descriptors.push_back(std::move(descriptor));
        }
        Atlas::VulkanRuntime runtime{ Atlas::VulkanRuntimeOptions{
            .enableValidation = config.validation, .deviceSelector = {}, .validationCallback = {} } };
        Atlas::TaskGraph graph;
        std::vector<PreparedTask> prepared;
        for (std::size_t i = 0; i < config.nodes.size(); ++i)
        {
            const auto& node = config.nodes[i];
            if (node.packId == "atlas.builtin")
                prepared.push_back(prepareBuiltin(node, descriptors[i], runtime, config.seed, i));
            else
            {
                Atlas::CustomTaskCreateInfo info;
                info.parameterJson = node.parameters.dump();
                info.graphSeed = config.seed;
                info.stableNodeIndex = i;
                info.vulkanRuntime = &runtime;
                if (node.slice)
                    info.sliceDimensions = Atlas::DispatchDimensions{ node.slice->x, node.slice->y, node.slice->z };
                auto instance = std::make_shared<Atlas::CustomTaskInstance>(
                    registry.createTask(node.packId, digests.at(node.packId), node.taskId, info));
                prepared.push_back({ [instance](Atlas::TaskGraph& target, Atlas::TaskOptions options)
                                     { return instance->addToGraph(target, std::move(options)); },
                                     [instance] { return instance->collectSummary(); } });
            }
        }
        std::vector<Atlas::TaskHandle> handles;
        std::map<std::string, std::size_t> indices;
        for (std::size_t i = 0; i < config.nodes.size(); ++i)
        {
            const auto& node = config.nodes[i];
            const auto handle = prepared[i].add(graph, Atlas::TaskOptions{ node.name, descriptors[i].resource, node.priority });
            if (!handle)
                throw std::runtime_error{ "Unable to add prepared task" };
            handles.push_back(*handle);
            indices.emplace(node.id, i);
        }
        for (const auto& [from, to] : config.edges)
            if (!graph.addDependency(handles[indices.at(to)], handles[indices.at(from)]))
                throw std::runtime_error{ "Invalid graph dependency" };
        if (!graph.finishTaskGraph())
            throw std::runtime_error{ "Studio graph is cyclic or invalid" };
        std::unique_ptr<Atlas::CpuExecutor> cpu;
        if (config.synchronousCpu)
            cpu = std::make_unique<Atlas::SynchronousCpuExecutor>();
        else
            cpu = std::make_unique<Atlas::WorkerpoolExecutor>(config.workerCount);
        Atlas::VulkanExecutor gpu{ runtime };
        auto schedulingPolicy = policy(config);
        trace = std::make_unique<StudioTrace>(config.tracing ? config.traceCapacity : 1U, config.packs);
        phase = "execution";
        for (std::size_t i = 0; i < handles.size(); ++i)
        {
            const auto& node = config.nodes[i];
            trace->write(Json{ { "record_type", "task" },
                               { "node_id", node.id },
                               { "task_id", handles[i].getTaskID().getValue() },
                               { "name", node.name },
                               { "resource", node.resource },
                               { "priority", node.priority },
                               { "pack_id", node.packId },
                               { "pack_task_id", node.taskId } });
        }
        Atlas::KahnScheduler scheduler{ graph, *cpu, gpu, *schedulingPolicy, config.tracing ? trace->sessionPtr() : nullptr };
        if (std::filesystem::exists(controlPath))
            for (const auto handle : handles)
                static_cast<void>(scheduler.requestCancellation(handle));
        std::jthread control{ [&](std::stop_token stop)
                              {
                                  while (!stop.stop_requested())
                                  {
                                      std::error_code error;
                                      if (std::filesystem::exists(controlPath, error))
                                      {
                                          for (const auto handle : handles)
                                              static_cast<void>(scheduler.requestCancellation(handle));
                                          break;
                                      }
                                      std::this_thread::sleep_for(std::chrono::milliseconds{ 50 });
                                  }
                              } };
        const auto result = scheduler.execute();
        control.request_stop();
        control.join();
        bool summaryFailed = false;
        phase = "summary";
        for (std::size_t i = 0; i < prepared.size(); ++i)
        {
            if (graph.snapshotTask(handles[i])->executionInfo.state != Atlas::TaskState::Success)
                continue;
            try
            {
                const auto summary = prepared[i].summary();
                trace->write(Json{ { "record_type", "task_summary" },
                                   { "node_id", config.nodes[i].id },
                                   { "task_id", handles[i].getTaskID().getValue() },
                                   { "summary", Json::parse(summary.canonicalJson) } });
            }
            catch (const std::exception& error)
            {
                summaryFailed = true;
                trace->write(Json{ { "record_type", "error" },
                                   { "studio_schema_version", 2 },
                                   { "phase", "summary" },
                                   { "message", boundedMessage(config.nodes[i].id + ": " + error.what()) } });
            }
        }
        const auto resultRecord = resultJson(result, runtime, config, graph, handles);
        const bool success = result.status == Atlas::SchedulerStatus::Success && !summaryFailed;
        trace->finish(success ? "success" : "failed", &resultRecord);
        return success ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    catch (const std::exception& error)
    {
        const Json record{
            { "record_type", "error" }, { "studio_schema_version", 2 }, { "phase", phase }, { "message", boundedMessage(error.what()) }
        };
        if (trace)
        {
            trace->write(record);
            trace->finish("failed", nullptr);
        }
        else
        {
            std::cout << record.dump(-1, ' ', false, Json::error_handler_t::replace) << std::endl;
        }
        std::cerr << "atlas_studio_runner: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
