#include "../../support/TaskPackTestPack.h"
#include "atlas/Executor/SynchronousCpuExecutor.h"
#include "atlas/Executor/VulkanExecutor.h"
#include "atlas/Extension/TaskPack.h"
#include "atlas/Scheduler/KahnScheduler.h"
#include "atlas/Vulkan/VulkanRuntime.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Custom GPU preparation creates Atlas-owned sliced work and bounded readback summaries", "[FEATURE][VULKAN]")
{
    Atlas::Testing::TaskPackTestPack pack;
    Atlas::TaskPackRegistry registry;
    const Atlas::TaskPackManifest& manifest{ registry.loadDirectory(pack.directory) };
    Atlas::VulkanRuntime runtime;
    Atlas::CustomTaskInstance instance{ registry.createTask(
        manifest.packId, manifest.digest, "gpu_vector",
        Atlas::CustomTaskCreateInfo{ .vulkanRuntime = &runtime, .sliceDimensions = Atlas::DispatchDimensions{ 1U, 1U, 1U } }) };

    Atlas::TaskGraph graph;
    REQUIRE(instance.addToGraph(graph).has_value());
    REQUIRE(graph.finishTaskGraph());
    Atlas::SynchronousCpuExecutor cpuExecutor;
    Atlas::VulkanExecutor gpuExecutor{ runtime };
    Atlas::KahnScheduler scheduler{ graph, cpuExecutor, gpuExecutor };
    REQUIRE(scheduler.execute().status == Atlas::SchedulerStatus::Success);
    REQUIRE(instance.collectSummary().canonicalJson == R"({"ok":true})");
}
