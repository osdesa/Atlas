#include "atlas/Executor/SynchronousCpuExecutor.h"

/** @file main.cpp @brief Runs and verifies Atlas's current CPU-to-Vulkan-to-CPU workflow. */
#include "atlas/Executor/VulkanExecutor.h"
#include "atlas/Profiling/TraceJsonlWriter.h"
#include "atlas/Scheduler/FifoSchedulingPolicy.h"
#include "atlas/Scheduler/KahnScheduler.h"
#include "atlas/Tasking/TaskGraph.h"
#include "atlas/Vulkan/VulkanRuntime.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace
{
    std::vector<std::uint32_t> readShader()
    {
        std::ifstream stream{ ATLAS_VECTOR_ADD_SPIRV_PATH, std::ios::binary };
        if (!stream)
        {
            return {};
        }
        const std::vector<char> bytes{ std::istreambuf_iterator<char>{ stream }, std::istreambuf_iterator<char>{} };
        if (stream.bad() || bytes.empty() || bytes.size() % sizeof(std::uint32_t) != 0U)
        {
            return {};
        }
        std::vector<std::uint32_t> words(bytes.size() / sizeof(std::uint32_t));
        std::memcpy(words.data(), bytes.data(), bytes.size());
        return words;
    }

    std::optional<std::filesystem::path> tracePath(const int argumentCount, char** arguments)
    {
        if (argumentCount == 1)
        {
            return std::nullopt;
        }
        if (argumentCount == 3 && std::string_view{ arguments[1] } == "--trace")
        {
            return std::filesystem::path{ arguments[2] };
        }
        throw std::invalid_argument{ "Usage: atlas [--trace <jsonl-file>]" };
    }
} // namespace

int main(const int argumentCount, char** arguments)
{
    try
    {
        const std::optional<std::filesystem::path> requestedTrace{ tracePath(argumentCount, arguments) };
        if (requestedTrace.has_value() && !Atlas::profilingEnabled)
        {
            throw std::runtime_error{ "Trace output is unavailable because Atlas was built with ATLAS_ENABLE_PROFILING=OFF" };
        }
        constexpr std::size_t elementCount{ 256U };
        Atlas::VulkanRuntime runtime;
        const Atlas::VulkanBuffer leftBuffer{ runtime.createBuffer(elementCount * sizeof(float)) };
        const Atlas::VulkanBuffer rightBuffer{ runtime.createBuffer(elementCount * sizeof(float)) };
        const Atlas::VulkanBuffer outputBuffer{ runtime.createBuffer(elementCount * sizeof(float)) };
        const Atlas::VulkanComputePipeline pipeline{ runtime.createComputePipeline(
            Atlas::ComputeShader{ readShader(),
                                  "main",
                                  { { 0U, Atlas::BufferAccess::ReadOnly },
                                    { 1U, Atlas::BufferAccess::ReadOnly },
                                    { 2U, Atlas::BufferAccess::WriteOnly } } }) };
        const Atlas::VulkanDispatch logicalDispatch{ pipeline,
                                                     { { 0U, leftBuffer, Atlas::BufferAccess::ReadOnly },
                                                       { 1U, rightBuffer, Atlas::BufferAccess::ReadOnly },
                                                       { 2U, outputBuffer, Atlas::BufferAccess::WriteOnly } },
                                                     { 4U, 1U, 1U } };
        const Atlas::SlicedVulkanDispatch slicedDispatch{ logicalDispatch, { 3U, 1U, 1U } };

        std::vector<float> left(elementCount, 4.0F);
        std::vector<float> right(elementCount, 7.0F);
        std::vector<float> output(elementCount, 0.0F);
        bool verified{ false };

        Atlas::TaskGraph graph;
        const auto prepare{ graph.addCpuTask(
            [&]
            {
                runtime.upload(leftBuffer, std::as_bytes(std::span{ left }));
                runtime.upload(rightBuffer, std::as_bytes(std::span{ right }));
            },
            Atlas::TaskOptions{ "Prepare" }) };
        const auto compute{ graph.addGpuTask(slicedDispatch, Atlas::TaskOptions{ "Sliced compute", Atlas::ExecutionResource::GPU }) };
        const auto verify{ graph.addCpuTask(
            [&]
            {
                runtime.download(outputBuffer, std::as_writable_bytes(std::span{ output }));
                verified = std::all_of(output.begin(), output.end(), [](const float value) { return value == 11.0F; });
            },
            Atlas::TaskOptions{ "Verify" }) };
        if (!prepare || !compute || !verify || !graph.addDependency(compute.value(), prepare.value()) ||
            !graph.addDependency(verify.value(), compute.value()) || !graph.finishTaskGraph())
        {
            std::cerr << "Failed to build mixed graph\n";
            return EXIT_FAILURE;
        }

        Atlas::SynchronousCpuExecutor cpuExecutor;
        Atlas::VulkanExecutor gpuExecutor{ runtime };
        const Atlas::FifoSchedulingPolicy policy;
        std::unique_ptr<Atlas::TraceJsonlWriter> trace;
        if (requestedTrace.has_value())
        {
            trace = std::make_unique<Atlas::TraceJsonlWriter>(requestedTrace.value());
        }
        Atlas::KahnScheduler scheduler{ graph, cpuExecutor, gpuExecutor, policy, trace != nullptr ? &trace->session() : nullptr };
        const Atlas::SchedulerResult result{ scheduler.execute() };
        if (trace != nullptr)
        {
            trace->finish(result.status == Atlas::SchedulerStatus::Success ? "success" : "failed");
        }
        const Atlas::TaskSnapshot computeProgress{ graph.snapshotTask(compute.value()).value() };
        if (result.status != Atlas::SchedulerStatus::Success || !verified ||
            computeProgress.executionInfo.completedWorkUnitCount != 2U || computeProgress.executionInfo.totalWorkUnitCount != 2U)
        {
            std::cerr << "Mixed graph execution failed\n";
            return EXIT_FAILURE;
        }
        std::cout << "Verified CPU -> sliced Vulkan (" << computeProgress.executionInfo.completedWorkUnitCount
                  << " work units) -> CPU graph on " << runtime.deviceInfo().name << '\n';
        return EXIT_SUCCESS;
    }
    catch (const std::exception& exception)
    {
        std::cerr << exception.what() << '\n';
        return EXIT_FAILURE;
    }
}
