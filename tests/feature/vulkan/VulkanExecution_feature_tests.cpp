#if defined(ATLAS_VECTOR_ADD_SPIRV_PATH)

#include "atlas/Executor/SynchronousCpuExecutor.h"
#include "atlas/Executor/VulkanExecutor.h"
#include "atlas/Scheduler/FifoSchedulingPolicy.h"
#include "atlas/Scheduler/KahnScheduler.h"
#include "atlas/Scheduler/RoundRobinSchedulingPolicy.h"
#include "atlas/Scheduler/StaticPrioritySchedulingPolicy.h"
#include "atlas/Tasking/TaskGraph.h"
#include "atlas/Vulkan/VulkanRuntime.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace
{
    std::vector<std::uint32_t> readShader()
    {
        std::ifstream stream{ ATLAS_VECTOR_ADD_SPIRV_PATH, std::ios::binary };
        REQUIRE(stream.good());
        const std::vector<char> bytes{ std::istreambuf_iterator<char>{ stream }, std::istreambuf_iterator<char>{} };
        REQUIRE(bytes.size() % sizeof(std::uint32_t) == 0U);
        std::vector<std::uint32_t> words(bytes.size() / sizeof(std::uint32_t));
        std::memcpy(words.data(), bytes.data(), bytes.size());
        return words;
    }

    std::span<const std::byte> bytesOf(const std::vector<float>& values)
    {
        return std::as_bytes(std::span{ values });
    }

    std::span<std::byte> writableBytesOf(std::vector<float>& values)
    {
        return std::as_writable_bytes(std::span{ values });
    }

    struct ComputeFixture
    {
        static constexpr std::size_t elementCount{ 320U };

        std::shared_ptr<std::vector<std::string>> validationErrors{ std::make_shared<std::vector<std::string>>() };
        Atlas::VulkanRuntime runtime{ Atlas::VulkanRuntimeOptions{
            true, {}, [errors = validationErrors](const std::string_view message) { errors->emplace_back(message); } } };
        Atlas::VulkanBuffer left{ runtime.createBuffer(elementCount * sizeof(float)) };
        Atlas::VulkanBuffer right{ runtime.createBuffer(elementCount * sizeof(float)) };
        Atlas::VulkanBuffer output{ runtime.createBuffer(elementCount * sizeof(float)) };
        Atlas::VulkanComputePipeline pipeline{ runtime.createComputePipeline(
            Atlas::ComputeShader{ readShader(), "main", { 0U, 1U, 2U } }) };

        Atlas::VulkanDispatch dispatch() const
        {
            return Atlas::VulkanDispatch{ pipeline,
                                          { { 0U, left, Atlas::BufferAccess::ReadOnly },
                                            { 1U, right, Atlas::BufferAccess::ReadOnly },
                                            { 2U, output, Atlas::BufferAccess::WriteOnly } },
                                          { static_cast<std::uint32_t>(elementCount / 64U), 1U, 1U } };
        }

        Atlas::SlicedVulkanDispatch slicedDispatch() const
        {
            return Atlas::SlicedVulkanDispatch{ dispatch(), { 2U, 1U, 1U } };
        }
    };
} // namespace

TEST_CASE("VulkanExecutor executes and reuses persistent compute resources", "[FEATURE][VULKAN_INTEGRATION]")
{
    ComputeFixture fixture;
    std::vector<float> left(ComputeFixture::elementCount);
    std::vector<float> right(ComputeFixture::elementCount);
    std::vector<float> output(ComputeFixture::elementCount, 0.0F);
    for (std::size_t index{ 0U }; index < left.size(); ++index)
    {
        left.at(index) = static_cast<float>(index);
        right.at(index) = static_cast<float>(index * 2U);
    }
    fixture.runtime.upload(fixture.left, bytesOf(left));
    fixture.runtime.upload(fixture.right, bytesOf(right));

    Atlas::VulkanExecutor executor{ fixture.runtime };
    const Atlas::GraphId graphId{ Atlas::GraphId::create() };
    const Atlas::TaskHandle first{ Atlas::TaskId{ 1U }, graphId };
    const Atlas::TaskHandle second{ Atlas::TaskId{ 2U }, graphId };
    REQUIRE(executor.maxConcurrency() == 1U);
    REQUIRE(executor.submit(first, fixture.dispatch()));
    REQUIRE(executor.submit(second, fixture.dispatch()));
    executor.shutdown();
    const std::optional<Atlas::TaskCompletion> firstCompletion{ executor.waitForCompletion() };
    const std::optional<Atlas::TaskCompletion> secondCompletion{ executor.waitForCompletion() };
    REQUIRE(firstCompletion.has_value());
    REQUIRE(secondCompletion.has_value());
    REQUIRE(firstCompletion->handle == first);
    REQUIRE(secondCompletion->handle == second);
    REQUIRE(firstCompletion->succeeded());
    REQUIRE(secondCompletion->succeeded());
    REQUIRE(firstCompletion->workUnitIndex == 0U);
    REQUIRE(secondCompletion->workUnitIndex == 0U);
    REQUIRE_FALSE(executor.waitForCompletion().has_value());

    fixture.runtime.download(fixture.output, writableBytesOf(output));
    for (std::size_t index{ 0U }; index < output.size(); ++index)
    {
        REQUIRE(output.at(index) == left.at(index) + right.at(index));
    }

    executor.shutdown();
    REQUIRE_FALSE(executor.submit(first, fixture.dispatch()));
    REQUIRE(fixture.validationErrors->empty());
}

TEST_CASE("KahnScheduler executes uneven Vulkan dispatch-base slices", "[FEATURE][VULKAN_INTEGRATION]")
{
    ComputeFixture fixture;
    std::vector<float> left(ComputeFixture::elementCount);
    std::vector<float> right(ComputeFixture::elementCount);
    std::vector<float> output(ComputeFixture::elementCount, -1.0F);
    for (std::size_t index{ 0U }; index < left.size(); ++index)
    {
        left.at(index) = static_cast<float>(index);
        right.at(index) = static_cast<float>(index * 3U);
    }
    fixture.runtime.upload(fixture.left, bytesOf(left));
    fixture.runtime.upload(fixture.right, bytesOf(right));
    fixture.runtime.upload(fixture.output, bytesOf(output));

    Atlas::TaskGraph graph;
    const std::optional<Atlas::TaskHandle> compute{ graph.addGpuTask(
        fixture.slicedDispatch(), Atlas::TaskOptions{ "Uneven sliced vector addition", Atlas::ExecutionResource::GPU }) };
    REQUIRE(compute.has_value());
    REQUIRE(graph.finishTaskGraph());

    Atlas::SynchronousCpuExecutor cpuExecutor;
    Atlas::VulkanExecutor gpuExecutor{ fixture.runtime };
    Atlas::KahnScheduler scheduler{ graph, cpuExecutor, gpuExecutor };
    const Atlas::SchedulerResult result{ scheduler.execute() };

    fixture.runtime.download(fixture.output, writableBytesOf(output));
    REQUIRE(result.status == Atlas::SchedulerStatus::Success);
    REQUIRE(result.executedTaskCount == 1U);
    const Atlas::TaskExecutionInfo& progress{ graph.findTask(compute.value()).value()->executionInfo };
    REQUIRE(progress.state == Atlas::TaskState::Success);
    REQUIRE(progress.completedWorkUnitCount == 3U);
    REQUIRE(progress.totalWorkUnitCount == 3U);
    for (std::size_t index{ 0U }; index < output.size(); ++index)
    {
        REQUIRE(output.at(index) == left.at(index) + right.at(index));
    }
    REQUIRE(fixture.validationErrors->empty());
}

TEST_CASE("KahnScheduler executes real sliced Vulkan work with every built-in policy", "[FEATURE][VULKAN_INTEGRATION]")
{
    ComputeFixture fixture;
    std::vector<float> left(ComputeFixture::elementCount);
    std::vector<float> right(ComputeFixture::elementCount);
    std::vector<float> output(ComputeFixture::elementCount, -1.0F);
    for (std::size_t index{ 0U }; index < left.size(); ++index)
    {
        left.at(index) = static_cast<float>(index + 1U);
        right.at(index) = static_cast<float>(index * 2U);
    }
    fixture.runtime.upload(fixture.left, bytesOf(left));
    fixture.runtime.upload(fixture.right, bytesOf(right));

    const auto executePolicy = [&](const Atlas::SchedulingPolicy& policy)
    {
        std::fill(output.begin(), output.end(), -1.0F);
        fixture.runtime.upload(fixture.output, bytesOf(output));
        Atlas::TaskGraph graph;
        const std::optional<Atlas::TaskHandle> compute{ graph.addGpuTask(
            fixture.slicedDispatch(), Atlas::TaskOptions{ "Policy sliced vector addition", Atlas::ExecutionResource::GPU }) };
        REQUIRE(compute.has_value());
        REQUIRE(graph.finishTaskGraph());

        Atlas::SynchronousCpuExecutor cpuExecutor;
        Atlas::VulkanExecutor gpuExecutor{ fixture.runtime };
        Atlas::KahnScheduler scheduler{ graph, cpuExecutor, gpuExecutor, policy };
        const Atlas::SchedulerResult result{ scheduler.execute() };

        fixture.runtime.download(fixture.output, writableBytesOf(output));
        REQUIRE(result.status == Atlas::SchedulerStatus::Success);
        REQUIRE(result.executedTaskCount == 1U);
        const Atlas::TaskExecutionInfo& progress{ graph.findTask(compute.value()).value()->executionInfo };
        REQUIRE(progress.state == Atlas::TaskState::Success);
        REQUIRE(progress.completedWorkUnitCount == 3U);
        REQUIRE(progress.totalWorkUnitCount == 3U);
        for (std::size_t index{ 0U }; index < output.size(); ++index)
        {
            REQUIRE(output.at(index) == left.at(index) + right.at(index));
        }
        REQUIRE(fixture.validationErrors->empty());
    };

    SECTION("FIFO")
    {
        const Atlas::FifoSchedulingPolicy policy;
        executePolicy(policy);
    }
    SECTION("round-robin")
    {
        const Atlas::RoundRobinSchedulingPolicy policy{ 2U };
        executePolicy(policy);
    }
    SECTION("static priority")
    {
        const Atlas::StaticPrioritySchedulingPolicy policy;
        executePolicy(policy);
    }
}

TEST_CASE("VulkanExecutor isolates a rejected-runtime dispatch and continues", "[FEATURE][VULKAN_INTEGRATION]")
{
    ComputeFixture fixture;
    ComputeFixture foreignFixture;
    Atlas::VulkanExecutor executor{ fixture.runtime };
    const Atlas::GraphId graphId{ Atlas::GraphId::create() };
    const Atlas::TaskHandle invalid{ Atlas::TaskId{ 1U }, graphId };
    const Atlas::TaskHandle valid{ Atlas::TaskId{ 2U }, graphId };

    REQUIRE(executor.submit(invalid, foreignFixture.dispatch()));
    REQUIRE(executor.submit(valid, fixture.dispatch()));
    const std::optional<Atlas::TaskCompletion> invalidCompletion{ executor.waitForCompletion() };
    const std::optional<Atlas::TaskCompletion> validCompletion{ executor.waitForCompletion() };

    REQUIRE(invalidCompletion.has_value());
    REQUIRE(validCompletion.has_value());
    REQUIRE(invalidCompletion->handle == invalid);
    REQUIRE_FALSE(invalidCompletion->succeeded());
    REQUIRE(validCompletion->handle == valid);
    REQUIRE(validCompletion->succeeded());
    REQUIRE(fixture.validationErrors->empty());
    REQUIRE(foreignFixture.validationErrors->empty());
}

TEST_CASE("KahnScheduler executes a real CPU Vulkan CPU dependency chain", "[FEATURE][VULKAN_INTEGRATION]")
{
    ComputeFixture fixture;
    std::vector<float> left(ComputeFixture::elementCount, 4.0F);
    std::vector<float> right(ComputeFixture::elementCount, 7.0F);
    std::vector<float> output(ComputeFixture::elementCount, 0.0F);
    bool verified{ false };

    Atlas::TaskGraph graph;
    const auto prepare{ graph.addCpuTask(
        [&]
        {
            fixture.runtime.upload(fixture.left, bytesOf(left));
            fixture.runtime.upload(fixture.right, bytesOf(right));
        },
        Atlas::TaskOptions{ "Prepare input" }) };
    const auto compute{ graph.addGpuTask(fixture.dispatch(), Atlas::TaskOptions{ "Add vectors", Atlas::ExecutionResource::GPU }) };
    const auto verify{ graph.addCpuTask(
        [&]
        {
            fixture.runtime.download(fixture.output, writableBytesOf(output));
            verified = std::all_of(output.begin(), output.end(), [](const float value) { return value == 11.0F; });
        },
        Atlas::TaskOptions{ "Verify output" }) };
    REQUIRE(prepare.has_value());
    REQUIRE(compute.has_value());
    REQUIRE(verify.has_value());
    REQUIRE(graph.addDependency(compute.value(), prepare.value()));
    REQUIRE(graph.addDependency(verify.value(), compute.value()));
    REQUIRE(graph.finishTaskGraph());

    Atlas::SynchronousCpuExecutor cpuExecutor;
    Atlas::VulkanExecutor gpuExecutor{ fixture.runtime };
    Atlas::KahnScheduler scheduler{ graph, cpuExecutor, gpuExecutor };
    const Atlas::SchedulerResult result{ scheduler.execute() };

    REQUIRE(result.status == Atlas::SchedulerStatus::Success);
    REQUIRE(result.executedTaskCount == 3U);
    REQUIRE(verified);
    REQUIRE(fixture.validationErrors->empty());
}

#endif
