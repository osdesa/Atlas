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
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
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
 * The runner accepts one validated built-in-kernel graph, executes it once,
 * emits bounded version-one JSONL to standard output, and reserves standard
 * error for diagnostics. All Vulkan resources remain process-owned.
 */

namespace
{
    using Json = nlohmann::json;

    struct Dimensions
    {
        std::uint32_t x{ 1U };
        std::uint32_t y{ 1U };
        std::uint32_t z{ 1U };
    };

    const char* stateName(const Atlas::TaskState value) noexcept
    {
        static constexpr const char* names[] = {
            "unknown", "ready", "running", "success", "failure", "blocked", "paused", "cancelled"
        };
        return names[static_cast<std::size_t>(value)];
    }

    Dimensions dimensions(const Json& value, const std::string& path)
    {
        if (!value.is_object() || !value.contains("x") || !value.contains("y") || !value.contains("z") ||
            !value.at("x").is_number_unsigned() || !value.at("y").is_number_unsigned() || !value.at("z").is_number_unsigned())
        {
            throw std::runtime_error{ path + " must contain unsigned x, y, and z dimensions" };
        }
        const auto result =
            Dimensions{ value.at("x").get<std::uint32_t>(), value.at("y").get<std::uint32_t>(), value.at("z").get<std::uint32_t>() };
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
        std::string kernel;
        std::uint32_t priority{ 0U };
        std::uint64_t iterations{ 1U };
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
        bool synchronousCpu{ false };
        std::uint32_t workerCount{ 1U };
        std::string policy{ "fifo" };
        std::size_t quantum{ 1U };
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
        Json root;
        input >> root;
        rejectUnknown(root, { "schema_version", "graph_id", "seed", "runtime", "cpu_executor", "policy", "trace", "nodes", "edges" },
                      "graph");
        if (required(root, "schema_version", "graph").get<std::uint32_t>() != 1U)
        {
            throw std::runtime_error{ "only atlas-studio-graph schema version 1 is supported" };
        }
        Config config;
        if (root.contains("graph_id"))
        {
            config.id = root.at("graph_id").get<std::string>();
        }
        if (root.contains("seed"))
        {
            config.seed = root.at("seed").get<std::uint64_t>();
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
            config.workerCount = root.at("cpu_executor").value("worker_count", 1U);
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
                config.quantum = required(root.at("policy"), "quantum", "graph.policy").get<std::size_t>();
                if (config.quantum == 0U)
                {
                    throw std::runtime_error{ "graph.policy.quantum must be positive" };
                }
            }
            else if (config.policy != "fifo" && config.policy != "static_priority")
            {
                throw std::runtime_error{ "graph.policy.type is unsupported" };
            }
        }
        if (root.contains("trace"))
        {
            rejectUnknown(root.at("trace"), { "enabled", "capacity" }, "graph.trace");
            config.tracing = root.at("trace").value("enabled", true);
            config.traceCapacity = root.at("trace").value("capacity", 65'536U);
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
            rejectUnknown(node, { "id", "name", "resource", "kernel", "priority", "slice_workgroups" }, nodePath);
            NodeConfig parsed;
            parsed.id = required(node, "id", nodePath).get<std::string>();
            if (parsed.id.empty())
                throw std::runtime_error{ nodePath + ".id must not be empty" };
            if (std::any_of(config.nodes.begin(), config.nodes.end(), [&](const NodeConfig& prior) { return prior.id == parsed.id; }))
                throw std::runtime_error{ "graph.nodes contains duplicate id '" + parsed.id + "'" };
            parsed.name = node.value("name", parsed.id);
            parsed.resource = required(node, "resource", nodePath).get<std::string>();
            parsed.priority = node.value("priority", 0U);
            const Json& kernel = required(node, "kernel", nodePath);
            rejectUnknown(kernel, { "type", "iterations", "workgroups", "element_count", "left_value", "right_value" },
                          nodePath + ".kernel");
            parsed.kernel = required(kernel, "type", nodePath + ".kernel").get<std::string>();
            if (parsed.resource == "cpu" && parsed.kernel == "cpu_burn")
            {
                parsed.iterations = kernel.value("iterations", 1U);
                if (parsed.iterations == 0U)
                {
                    throw std::runtime_error{ nodePath + ".kernel.iterations must be positive" };
                }
            }
            else if (parsed.resource == "gpu" && parsed.kernel == "gpu_increment")
            {
                parsed.workgroups = dimensions(required(kernel, "workgroups", nodePath + ".kernel"), nodePath + ".kernel.workgroups");
            }
            else if (parsed.resource == "gpu" && parsed.kernel == "vector_add")
            {
                parsed.elementCount = kernel.value("element_count", 256U);
                parsed.leftValue = kernel.value("left_value", 4.0F);
                parsed.rightValue = kernel.value("right_value", 7.0F);
                if (parsed.elementCount == 0U)
                {
                    throw std::runtime_error{ nodePath + ".kernel.element_count must be positive" };
                }
                if (parsed.elementCount > std::numeric_limits<std::uint64_t>::max() - 63U)
                    throw std::runtime_error{ nodePath + ".kernel.element_count is too large" };
                const std::uint64_t groups = (parsed.elementCount + 63U) / 64U;
                if (groups > std::numeric_limits<std::uint32_t>::max())
                    throw std::runtime_error{ nodePath + ".kernel.element_count exceeds Vulkan dispatch limits" };
                parsed.workgroups = Dimensions{ static_cast<std::uint32_t>(groups), 1U, 1U };
            }
            else
            {
                throw std::runtime_error{ nodePath + " has an unsupported resource/kernel combination" };
            }
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
        return config;
    }

    class StudioTrace final
    {
      public:
        explicit StudioTrace(const std::size_t capacity) : buffer{ capacity }, session{ buffer }, consumer{ [this] { consume(); } }
        {
            std::cout << R"({"record_type":"header","studio_schema_version":1,"trace_schema_version":1})" << std::endl;
        }
        ~StudioTrace()
        {
            finish("abandoned", nullptr);
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
            std::lock_guard lock{ outputMutex };
            std::cout << record.dump() << std::endl;
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
    try
    {
        if (argc != 5 || std::string_view{ argv[1] } != "--config" || std::string_view{ argv[3] } != "--control")
        {
            throw std::invalid_argument{ "Usage: atlas_studio_runner --config <graph.json> --control <cancel-file>" };
        }
        // The config path is intentionally selected by the local user through the documented CLI.
        // codeql[cpp/path-injection]
        const Config config = loadConfig(argv[2]);
        StudioTrace trace{ config.tracing ? config.traceCapacity : 1U };
        Atlas::VulkanRuntime runtime{ Atlas::VulkanRuntimeOptions{
            .enableValidation = config.validation, .deviceSelector = {}, .validationCallback = {} } };
        Atlas::TaskGraph graph;
        std::vector<Atlas::TaskHandle> handles;
        std::vector<std::unique_ptr<VectorAddResources>> vectorResources;
        std::vector<Atlas::VulkanDispatch> gpuDispatches;
        std::vector<std::optional<Atlas::SlicedVulkanDispatch>> slicedDispatches;
        handles.reserve(config.nodes.size());
        gpuDispatches.reserve(config.nodes.size());
        slicedDispatches.reserve(config.nodes.size());
        auto cpuResults = std::make_shared<std::vector<std::uint64_t>>(config.nodes.size(), 0U);
        for (std::size_t index = 0; index < config.nodes.size(); ++index)
        {
            const NodeConfig& node = config.nodes.at(index);
            std::optional<Atlas::TaskHandle> handle;
            const Atlas::TaskOptions options{ node.name,
                                              node.resource == "cpu" ? Atlas::ExecutionResource::CPU : Atlas::ExecutionResource::GPU,
                                              node.priority };
            if (node.kernel == "cpu_burn")
            {
                handle = graph.addCpuTask([cpuResults, index, seed = config.seed, iterations = node.iterations]
                                          { cpuResults->at(index) = runCpuKernel(seed ^ (index + 1U), iterations); }, options);
            }
            else if (node.kernel == "gpu_increment")
            {
                const Atlas::VulkanComputePipeline pipeline{ runtime.createComputePipeline(
                    Atlas::ComputeShader{ shaderWords(ATLAS_STUDIO_BENCHMARK_SPIRV_PATH),
                                          "main",
                                          { { 0U, Atlas::BufferAccess::ReadOnly }, { 1U, Atlas::BufferAccess::ReadWrite } } }) };
                const Atlas::VulkanBuffer dimensionsBuffer{ runtime.createBuffer(4U * sizeof(std::uint32_t)) };
                const std::uint64_t count = checkedProduct(node.workgroups, node.id + ".workgroups");
                const Atlas::VulkanBuffer output{ runtime.createBuffer(
                    checkedBytes(count, sizeof(std::uint32_t), node.id + " output buffer")) };
                const std::vector<std::uint32_t> dimensionsData{ node.workgroups.x, node.workgroups.y, node.workgroups.z, 0U };
                runtime.upload(dimensionsBuffer, std::as_bytes(std::span{ dimensionsData }));
                gpuDispatches.emplace_back(pipeline,
                                           std::vector<Atlas::BufferBinding>{ { 0U, dimensionsBuffer, Atlas::BufferAccess::ReadOnly },
                                                                              { 1U, output, Atlas::BufferAccess::ReadWrite } },
                                           Atlas::DispatchDimensions{ node.workgroups.x, node.workgroups.y, node.workgroups.z });
                if (node.slice.has_value())
                {
                    slicedDispatches.emplace_back(Atlas::SlicedVulkanDispatch{
                        gpuDispatches.back(), Atlas::DispatchDimensions{ node.slice->x, node.slice->y, node.slice->z } });
                    handle = graph.addGpuTask(slicedDispatches.back().value(), options);
                }
                else
                {
                    slicedDispatches.emplace_back(std::nullopt);
                    handle = graph.addGpuTask(gpuDispatches.back(), options);
                }
            }
            else
            {
                vectorResources.push_back(std::make_unique<VectorAddResources>(runtime, node));
                if (node.slice.has_value())
                {
                    handle = graph.addGpuTask(
                        Atlas::SlicedVulkanDispatch{ vectorResources.back()->dispatch,
                                                     Atlas::DispatchDimensions{ node.slice->x, node.slice->y, node.slice->z } },
                        options);
                }
                else
                {
                    handle = graph.addGpuTask(vectorResources.back()->dispatch, options);
                }
            }
            if (!handle.has_value())
                throw std::runtime_error{ "unable to add studio task" };
            handles.push_back(handle.value());
            trace.write(Json{ { "record_type", "task" },
                              { "node_id", node.id },
                              { "task_id", handles.back().getTaskID().getValue() },
                              { "name", node.name },
                              { "resource", node.resource },
                              { "priority", node.priority } });
        }
        for (const auto& [from, to] : config.edges)
        {
            const auto fromIndex =
                std::find_if(config.nodes.begin(), config.nodes.end(), [&](const NodeConfig& node) { return node.id == from; });
            const auto toIndex =
                std::find_if(config.nodes.begin(), config.nodes.end(), [&](const NodeConfig& node) { return node.id == to; });
            if (fromIndex == config.nodes.end() || toIndex == config.nodes.end())
                throw std::runtime_error{ "edge references unknown node" };
            if (!graph.addDependency(handles.at(static_cast<std::size_t>(toIndex - config.nodes.begin())),
                                     handles.at(static_cast<std::size_t>(fromIndex - config.nodes.begin()))))
                throw std::runtime_error{ "invalid or duplicate graph edge" };
        }
        if (!graph.finishTaskGraph())
            throw std::runtime_error{ "studio graph is cyclic or invalid" };

        std::unique_ptr<Atlas::CpuExecutor> cpu;
        if (config.synchronousCpu)
            cpu = std::make_unique<Atlas::SynchronousCpuExecutor>();
        else
            cpu = std::make_unique<Atlas::WorkerpoolExecutor>(config.workerCount);
        Atlas::VulkanExecutor gpu{ runtime };
        std::unique_ptr<Atlas::SchedulingPolicy> schedulingPolicy = policy(config);
        Atlas::KahnScheduler scheduler{ graph, *cpu, gpu, *schedulingPolicy, config.tracing ? trace.sessionPtr() : nullptr };
        const std::filesystem::path controlPath{ argv[4] };
        std::jthread control{ [&](std::stop_token stop)
                              {
                                  while (!stop.stop_requested())
                                  {
                                      if (std::filesystem::exists(controlPath))
                                      {
                                          for (const auto handle : handles)
                                              static_cast<void>(scheduler.requestCancellation(handle));
                                          break;
                                      }
                                      std::this_thread::sleep_for(std::chrono::milliseconds{ 50 });
                                  }
                              } };
        const Atlas::SchedulerResult result = scheduler.execute();
        control.request_stop();
        for (const auto& resource : vectorResources)
        {
            if (result.status == Atlas::SchedulerStatus::Success)
                resource->verify();
        }
        const Json resultRecord = resultJson(result, runtime, config, graph, handles);
        trace.finish(result.status == Atlas::SchedulerStatus::Success ? "success" : "failed", &resultRecord);
        return result.status == Atlas::SchedulerStatus::Success ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    catch (const std::exception& error)
    {
        std::cerr << "atlas_studio_runner: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
