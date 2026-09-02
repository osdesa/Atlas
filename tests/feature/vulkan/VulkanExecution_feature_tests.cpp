#if defined(ATLAS_VECTOR_ADD_SPIRV_PATH)

#include "../../../src/Vulkan/VulkanInternal.h"
#include "../../support/StandaloneExecutorHarness.h"
#include "atlas/Executor/SynchronousCpuExecutor.h"
#include "atlas/Executor/VulkanExecutor.h"
#include "atlas/Scheduler/FifoSchedulingPolicy.h"
#include "atlas/Scheduler/KahnScheduler.h"
#include "atlas/Scheduler/RoundRobinSchedulingPolicy.h"
#include "atlas/Scheduler/StaticPrioritySchedulingPolicy.h"
#include "atlas/Tasking/TaskGraph.h"
#include "atlas/Vulkan/VulkanError.h"
#include "atlas/Vulkan/VulkanRuntime.h"

#include <algorithm>
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <semaphore>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
    using VulkanExecutorHarness = Atlas::Test::StandaloneExecutorHarness<Atlas::VulkanExecutor>;
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

    class RecordingVulkanDispatchExecutor final : public Atlas::VulkanDispatchExecutor
    {
      public:
        explicit RecordingVulkanDispatchExecutor(Atlas::VulkanExecutor& executor)
            : VulkanDispatchExecutor{ executor.maxConcurrency() }, vulkanExecutor{ executor }
        {
        }

        bool submit(const Atlas::TaskHandle handle, Atlas::VulkanDispatch dispatch, Atlas::CompletionChannel& channel) override
        {
            submittedWorkUnits.emplace_back(handle, dispatch.workUnitIndex());
            return vulkanExecutor.submit(handle, std::move(dispatch), channel);
        }

        void shutdown() noexcept override
        {
            vulkanExecutor.shutdown();
        }

        std::vector<std::pair<Atlas::TaskHandle, std::size_t>> submittedWorkUnits;

      private:
        Atlas::VulkanExecutor& vulkanExecutor;
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

    VulkanExecutorHarness executor{ fixture.runtime };
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
    const bool timestampSupported{ Atlas::profilingEnabled && fixture.runtime.timestampCapabilities().supported };
    REQUIRE(firstCompletion->deviceExecutionDuration.has_value() == timestampSupported);
    REQUIRE(secondCompletion->deviceExecutionDuration.has_value() == timestampSupported);
    if (timestampSupported)
    {
        REQUIRE(firstCompletion->deviceExecutionDuration->count() > 0);
        REQUIRE(secondCompletion->deviceExecutionDuration->count() > 0);
    }
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
    const Atlas::TaskExecutionInfo progress{ graph.snapshotTask(compute.value()).value().executionInfo };
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
        const Atlas::TaskExecutionInfo progress{ graph.snapshotTask(compute.value()).value().executionInfo };
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

TEST_CASE("KahnScheduler applies real Vulkan priority intervention at slice boundaries", "[FEATURE][VULKAN_INTEGRATION]")
{
    ComputeFixture fixture;
    std::vector<float> left(ComputeFixture::elementCount);
    std::vector<float> right(ComputeFixture::elementCount);
    std::vector<float> output(ComputeFixture::elementCount, -1.0F);
    for (std::size_t index{ 0U }; index < left.size(); ++index)
    {
        left.at(index) = static_cast<float>(index + 2U);
        right.at(index) = static_cast<float>(index * 4U);
    }
    fixture.runtime.upload(fixture.left, bytesOf(left));
    fixture.runtime.upload(fixture.right, bytesOf(right));
    fixture.runtime.upload(fixture.output, bytesOf(output));

    Atlas::TaskGraph graph;
    const std::optional<Atlas::TaskHandle> prerequisite{ graph.addCpuTask([] {}) };
    const std::optional<Atlas::TaskHandle> lower{ graph.addGpuTask(
        Atlas::SlicedVulkanDispatch{ fixture.dispatch(), { 3U, 1U, 1U } },
        Atlas::TaskOptions{ "Lower priority", Atlas::ExecutionResource::GPU, 9U }) };
    const std::optional<Atlas::TaskHandle> higher{ graph.addGpuTask(
        Atlas::SlicedVulkanDispatch{ fixture.dispatch(), { 5U, 1U, 1U } },
        Atlas::TaskOptions{ "Higher priority", Atlas::ExecutionResource::GPU, 1U }) };
    REQUIRE(prerequisite.has_value());
    REQUIRE(lower.has_value());
    REQUIRE(higher.has_value());
    REQUIRE(graph.addDependency(higher.value(), prerequisite.value()));
    REQUIRE(graph.finishTaskGraph());

    Atlas::SynchronousCpuExecutor cpuExecutor;
    Atlas::VulkanExecutor vulkanExecutor{ fixture.runtime };
    RecordingVulkanDispatchExecutor gpuExecutor{ vulkanExecutor };
    Atlas::StaticPrioritySchedulingPolicy policy;
    Atlas::KahnScheduler scheduler{ graph, cpuExecutor, gpuExecutor, policy };
    const Atlas::SchedulerResult result{ scheduler.execute() };

    fixture.runtime.download(fixture.output, writableBytesOf(output));
    REQUIRE(result.status == Atlas::SchedulerStatus::Success);
    REQUIRE(result.executedTaskCount == 3U);
    REQUIRE(gpuExecutor.submittedWorkUnits == std::vector<std::pair<Atlas::TaskHandle, std::size_t>>{
                                                  { lower.value(), 0U }, { higher.value(), 0U }, { lower.value(), 1U } });
    const Atlas::TaskExecutionInfo lowerInfo{ graph.snapshotTask(lower.value()).value().executionInfo };
    REQUIRE(lowerInfo.completedWorkUnitCount == 2U);
    REQUIRE(lowerInfo.selectionBypassCount == 1U);
    REQUIRE(lowerInfo.readyWaitDuration >= std::chrono::microseconds{ 0 });
    for (std::size_t index{ 0U }; index < output.size(); ++index)
    {
        REQUIRE(output.at(index) == left.at(index) + right.at(index));
    }
    REQUIRE(fixture.validationErrors->empty());
}

TEST_CASE("VulkanExecutor isolates a rejected-runtime dispatch and continues", "[FEATURE][VULKAN_INTEGRATION]")
{
    ComputeFixture fixture;
    ComputeFixture foreignFixture;
    VulkanExecutorHarness executor{ fixture.runtime };
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

TEST_CASE("VulkanExecutor latches device loss and drains accepted dispatches", "[FEATURE][VULKAN_INTEGRATION][CONCURRENCY]")
{
    ComputeFixture fixture;
    const std::shared_ptr<Atlas::Detail::VulkanContext> context{ Atlas::Detail::VulkanTestingAccess::context(fixture.runtime) };
    std::binary_semaphore faultReached{ 0 };
    std::binary_semaphore releaseFault{ 0 };
    std::atomic_bool injected{ false };
    Atlas::Detail::VulkanContext* const contextPointer{ context.get() };
    context->executorFaultInjector = [&, contextPointer](const Atlas::Detail::VulkanExecutorFaultPoint point)
    {
        if (point == Atlas::Detail::VulkanExecutorFaultPoint::BeforeExecution && !injected.exchange(true))
        {
            faultReached.release();
            releaseFault.acquire();
            contextPointer->checkDeviceResult(VK_ERROR_DEVICE_LOST, "injected Vulkan dispatch");
        }
    };

    VulkanExecutorHarness executor{ fixture.runtime };
    const Atlas::GraphId graphId{ Atlas::GraphId::create() };
    const Atlas::TaskHandle first{ Atlas::TaskId{ 1U }, graphId };
    const Atlas::TaskHandle second{ Atlas::TaskId{ 2U }, graphId };
    const Atlas::TaskHandle later{ Atlas::TaskId{ 3U }, graphId };

    REQUIRE(executor.submit(first, fixture.dispatch()));
    faultReached.acquire();
    REQUIRE(executor.submit(second, fixture.dispatch()));
    releaseFault.release();

    const std::optional<Atlas::TaskCompletion> firstCompletion{ executor.waitForCompletion() };
    const std::optional<Atlas::TaskCompletion> secondCompletion{ executor.waitForCompletion() };
    REQUIRE(firstCompletion.has_value());
    REQUIRE(secondCompletion.has_value());
    REQUIRE(firstCompletion->handle == first);
    REQUIRE(secondCompletion->handle == second);
    REQUIRE_FALSE(firstCompletion->succeeded());
    REQUIRE_FALSE(secondCompletion->succeeded());

    const auto requireDeviceLoss = [](const std::exception_ptr& exception)
    {
        REQUIRE(exception != nullptr);
        try
        {
            std::rethrow_exception(exception);
        }
        catch (const Atlas::VulkanError& error)
        {
            REQUIRE(error.result() == VK_ERROR_DEVICE_LOST);
            return;
        }
        catch (...)
        {
        }
        FAIL("Expected VulkanError carrying VK_ERROR_DEVICE_LOST");
    };
    requireDeviceLoss(firstCompletion->exception);
    requireDeviceLoss(secondCompletion->exception);
    REQUIRE_THROWS_AS(executor.submit(later, fixture.dispatch()), Atlas::VulkanError);
    REQUIRE_THROWS_AS(fixture.runtime.createBuffer(sizeof(std::uint32_t)), Atlas::VulkanError);
    executor.shutdown();
    context->executorFaultInjector = {};
    REQUIRE(fixture.validationErrors->empty());
}

TEST_CASE("VulkanExecutor isolates transient failures at every injected boundary", "[FEATURE][VULKAN_INTEGRATION]")
{
    ComputeFixture fixture;
    const std::shared_ptr<Atlas::Detail::VulkanContext> context{ Atlas::Detail::VulkanTestingAccess::context(fixture.runtime) };
    const Atlas::GraphId graphId{ Atlas::GraphId::create() };
    std::uint32_t taskId{ 1U };

    const auto exerciseBoundary = [&](const Atlas::Detail::VulkanExecutorFaultPoint boundary)
    {
        std::atomic_bool injected{ false };
        context->executorFaultInjector = [&](const Atlas::Detail::VulkanExecutorFaultPoint point)
        {
            if (point == boundary && !injected.exchange(true))
            {
                throw std::runtime_error{ "injected transient Vulkan executor failure" };
            }
        };

        VulkanExecutorHarness executor{ fixture.runtime };
        const Atlas::TaskHandle failed{ Atlas::TaskId{ taskId++ }, graphId };
        const Atlas::TaskHandle recovered{ Atlas::TaskId{ taskId++ }, graphId };
        REQUIRE(executor.submit(failed, fixture.dispatch()));
        const std::optional<Atlas::TaskCompletion> failedCompletion{ executor.waitForCompletion() };
        REQUIRE(failedCompletion.has_value());
        REQUIRE(failedCompletion->handle == failed);
        REQUIRE_FALSE(failedCompletion->succeeded());
        REQUIRE(injected.load());

        context->executorFaultInjector = {};
        REQUIRE(executor.submit(recovered, fixture.dispatch()));
        const std::optional<Atlas::TaskCompletion> recoveredCompletion{ executor.waitForCompletion() };
        REQUIRE(recoveredCompletion.has_value());
        REQUIRE(recoveredCompletion->handle == recovered);
        REQUIRE(recoveredCompletion->succeeded());
    };

    exerciseBoundary(Atlas::Detail::VulkanExecutorFaultPoint::BeforeExecution);
    exerciseBoundary(Atlas::Detail::VulkanExecutorFaultPoint::BeforeQueueSubmit);
    exerciseBoundary(Atlas::Detail::VulkanExecutorFaultPoint::AfterFenceWait);
    if constexpr (Atlas::profilingEnabled)
    {
        if (fixture.runtime.timestampCapabilities().supported)
        {
            exerciseBoundary(Atlas::Detail::VulkanExecutorFaultPoint::BeforeTimestampReadback);
        }
    }
    context->executorFaultInjector = {};
    REQUIRE(fixture.validationErrors->empty());
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
