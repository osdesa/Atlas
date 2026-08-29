#include "atlas/Executor/VulkanExecutor.h"

/** @file main.cpp @brief Runs and verifies standalone Vulkan vector addition. */
#include "atlas/Tasking/GraphId.h"
#include "atlas/Tasking/TaskId.h"
#include "atlas/Vulkan/VulkanRuntime.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
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

        std::vector<float> left(elementCount);
        std::vector<float> right(elementCount);
        std::vector<float> output(elementCount, 0.0F);
        for (std::size_t index{ 0U }; index < elementCount; ++index)
        {
            left.at(index) = static_cast<float>(index);
            right.at(index) = static_cast<float>(index * 2U);
        }
        runtime.upload(leftBuffer, std::as_bytes(std::span{ left }));
        runtime.upload(rightBuffer, std::as_bytes(std::span{ right }));

        Atlas::VulkanExecutor executor{ runtime };
        const Atlas::TaskHandle handle{ Atlas::TaskId{ 1U }, Atlas::GraphId::create() };
        const Atlas::VulkanDispatch dispatch{ pipeline,
                                              { { 0U, leftBuffer, Atlas::BufferAccess::ReadOnly },
                                                { 1U, rightBuffer, Atlas::BufferAccess::ReadOnly },
                                                { 2U, outputBuffer, Atlas::BufferAccess::WriteOnly } },
                                              { 4U, 1U, 1U } };
        if (!executor.submit(handle, dispatch))
        {
            std::cerr << "Vulkan dispatch submission failed\n";
            return EXIT_FAILURE;
        }
        const std::optional<Atlas::TaskCompletion> completion{ executor.waitForCompletion() };
        if (!completion.has_value() || !completion->succeeded())
        {
            std::cerr << "Vulkan dispatch execution failed\n";
            return EXIT_FAILURE;
        }

        runtime.download(outputBuffer, std::as_writable_bytes(std::span{ output }));
        const bool valid{ std::equal(output.begin(), output.end(), left.begin(),
                                     [&right, index = std::size_t{ 0U }](const float value, const float leftValue) mutable
                                     { return value == leftValue + right.at(index++); }) };
        if (!valid)
        {
            std::cerr << "Vulkan output verification failed\n";
            return EXIT_FAILURE;
        }
        std::cout << "Verified " << elementCount << " vector additions on " << runtime.deviceInfo().name << '\n';
        return EXIT_SUCCESS;
    }
    catch (const std::exception& exception)
    {
        std::cerr << exception.what() << '\n';
        return EXIT_FAILURE;
    }
}
