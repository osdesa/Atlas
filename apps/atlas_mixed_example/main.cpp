#include "atlas/Executor/SynchronousCpuExecutor.h"

/** @file main.cpp @brief Runs and verifies a CPU-to-Vulkan-to-CPU task graph. */
#include "atlas/Executor/VulkanExecutor.h"
#include "atlas/Scheduler/KahnScheduler.h"
#include "atlas/Tasking/TaskGraph.h"
#include "atlas/Vulkan/VulkanRuntime.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <span>
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
} // namespace

int main()
{
    try
    {
        constexpr std::size_t elementCount{ 256U };
        Atlas::VulkanRuntime runtime;
        const Atlas::VulkanBuffer leftBuffer{ runtime.createBuffer(elementCount * sizeof(float)) };
        const Atlas::VulkanBuffer rightBuffer{ runtime.createBuffer(elementCount * sizeof(float)) };
        const Atlas::VulkanBuffer outputBuffer{ runtime.createBuffer(elementCount * sizeof(float)) };
        const Atlas::VulkanComputePipeline pipeline{ runtime.createComputePipeline(
            Atlas::ComputeShader{ readShader(), "main", { 0U, 1U, 2U } }) };
        const Atlas::VulkanDispatch dispatch{ pipeline,
                                              { { 0U, leftBuffer, Atlas::BufferAccess::ReadOnly },
                                                { 1U, rightBuffer, Atlas::BufferAccess::ReadOnly },
                                                { 2U, outputBuffer, Atlas::BufferAccess::WriteOnly } },
                                              { 4U, 1U, 1U } };

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
        const auto compute{ graph.addGpuTask(dispatch, Atlas::TaskOptions{ "Compute", Atlas::ExecutionResource::GPU }) };
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
        Atlas::KahnScheduler scheduler{ graph, cpuExecutor, gpuExecutor };
        const Atlas::SchedulerResult result{ scheduler.execute() };
        if (result.status != Atlas::SchedulerStatus::Success || !verified)
        {
            std::cerr << "Mixed graph execution failed\n";
            return EXIT_FAILURE;
        }
        std::cout << "Verified CPU -> Vulkan -> CPU graph on " << runtime.deviceInfo().name << '\n';
        return EXIT_SUCCESS;
    }
    catch (const std::exception& exception)
    {
        std::cerr << exception.what() << '\n';
        return EXIT_FAILURE;
    }
}
