#include "../../../src/Vulkan/VulkanInternal.h"
#include "../../support/TaskPackTestPack.h"
#include "atlas/Executor/SynchronousCpuExecutor.h"
#include "atlas/Executor/VulkanExecutor.h"
#include "atlas/Extension/TaskPack.h"
#include "atlas/Scheduler/KahnScheduler.h"
#include "atlas/Vulkan/VulkanError.h"
#include "atlas/Vulkan/VulkanRuntime.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

TEST_CASE("Custom GPU work retains its module through ordinary and sliced execution and readback", "[FEATURE][VULKAN]")
{
    Atlas::Testing::TaskPackTestPack pack;
    Atlas::VulkanRuntime runtime;
    Atlas::TaskGraph graph;
    const bool sliced{ GENERATE(false, true) };
    Atlas::CustomTaskInstance instance = [&]
    {
        Atlas::TaskPackRegistry registry;
        const auto& manifest{ registry.loadDirectory(pack.directory) };
        Atlas::CustomTaskCreateInfo input;
        input.vulkanRuntime = &runtime;
        if (sliced)
            input.sliceDimensions = Atlas::DispatchDimensions{ 1U, 1U, 1U };
        return registry.createTask(manifest.packId, manifest.digest, "gpu_vector", input);
    }();

    const auto handle{ instance.addToGraph(graph) };
    REQUIRE(handle.has_value());
    REQUIRE(graph.finishTaskGraph());
    Atlas::SynchronousCpuExecutor cpuExecutor;
    Atlas::VulkanExecutor gpuExecutor{ runtime };
    Atlas::KahnScheduler scheduler{ graph, cpuExecutor, gpuExecutor };
    REQUIRE(scheduler.execute().status == Atlas::SchedulerStatus::Success);
    REQUIRE(graph.snapshotTask(*handle)->executionInfo.completedWorkUnitCount == (sliced ? 4U : 1U));
    REQUIRE(instance.collectSummary().canonicalJson == R"({"ok":true})");
}

TEST_CASE("Custom GPU device loss fails scheduling and prevents result download", "[FEATURE][VULKAN]")
{
    Atlas::Testing::TaskPackTestPack pack;
    Atlas::TaskPackRegistry registry;
    const auto& manifest{ registry.loadDirectory(pack.directory) };
    Atlas::VulkanRuntime runtime;
    Atlas::TaskGraph graph;
    Atlas::CustomTaskCreateInfo input;
    input.vulkanRuntime = &runtime;
    auto instance{ registry.createTask(manifest.packId, manifest.digest, "gpu_vector", input) };
    REQUIRE(instance.addToGraph(graph).has_value());
    REQUIRE(graph.finishTaskGraph());
    const auto context{ Atlas::Detail::VulkanTestingAccess::context(runtime) };
    context->executorFaultInjector = [pointer = context.get()](const Atlas::Detail::VulkanExecutorFaultPoint point)
    {
        if (point == Atlas::Detail::VulkanExecutorFaultPoint::BeforeExecution)
            pointer->checkDeviceResult(VK_ERROR_DEVICE_LOST, "injected custom GPU device loss");
    };
    Atlas::SynchronousCpuExecutor cpu;
    Atlas::VulkanExecutor gpu{ runtime };
    Atlas::KahnScheduler scheduler{ graph, cpu, gpu };
    REQUIRE(scheduler.execute().status == Atlas::SchedulerStatus::ExecutorUnavailable);
    REQUIRE_THROWS_AS(instance.collectSummary(), Atlas::VulkanError);
}
