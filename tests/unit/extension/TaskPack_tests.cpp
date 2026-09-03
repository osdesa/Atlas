#include "../../support/TaskPackTestPack.h"
#include "../../support/UnusedVulkanDispatchExecutor.h"
#include "atlas/Executor/SynchronousCpuExecutor.h"
#include "atlas/Executor/WorkerpoolExecutor.h"
#include "atlas/Extension/TaskPack.h"
#include "atlas/Scheduler/KahnScheduler.h"

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <string>

TEST_CASE("Task-pack inspection is deterministic and does not require native loading", "[UNIT]")
{
    Atlas::Testing::TaskPackTestPack pack;
    Atlas::TaskPackRegistry registry;
    const Atlas::TaskPackManifest first{ registry.inspectDirectory(pack.directory) };
    const Atlas::TaskPackManifest second{ registry.inspectDirectory(pack.directory) };

    REQUIRE(first.packId == "test.pack");
    REQUIRE(first.digest.size() == 64U);
    REQUIRE(first.digest == second.digest);
    REQUIRE(first.tasks.size() == 3U);
    REQUIRE(first.tasks.front().qualifiedId() == "test.pack/cpu_success");
    REQUIRE(registry.findTask(first.packId, first.digest, "cpu_success") == nullptr);

    std::ofstream changed{ pack.directory / "shaders" / "vector_add.spv", std::ios::binary | std::ios::app };
    changed.put('\0');
    changed.close();
    REQUIRE(registry.inspectDirectory(pack.directory).digest != first.digest);
}

TEST_CASE("Task-pack inspection rejects traversal and symlinks", "[UNIT]")
{
    Atlas::Testing::TaskPackTestPack pack;
    Atlas::TaskPackRegistry registry;
    std::string traversal{ pack.manifest };
    const std::size_t path{ traversal.find("bin/" + pack.libraryName) };
    REQUIRE(path != std::string::npos);
    traversal.replace(path, std::string{ "bin/" }.size() + pack.libraryName.size(), "../outside.so");
    pack.writeManifest(traversal);
    REQUIRE_THROWS_AS(registry.inspectDirectory(pack.directory), std::invalid_argument);

    pack.writeManifest(pack.manifest);
    std::error_code symlinkError;
    std::filesystem::create_symlink(pack.directory / "shaders" / "vector_add.spv", pack.directory / "linked.spv", symlinkError);
    if (symlinkError)
    {
        SUCCEED("This host does not permit test symlink creation");
        return;
    }
    REQUIRE_THROWS_AS(registry.inspectDirectory(pack.directory), std::invalid_argument);
}

TEST_CASE("Task-pack inspection rejects overflowing and inapplicable typed fields", "[UNIT]")
{
    Atlas::Testing::TaskPackTestPack pack;
    Atlas::TaskPackRegistry registry;
    const auto replaceOnce = [&pack](const std::string_view from, const std::string_view to)
    {
        std::string changed{ pack.manifest };
        const std::size_t offset{ changed.find(from) };
        REQUIRE(offset != std::string::npos);
        changed.replace(offset, from.size(), to);
        pack.writeManifest(changed);
    };

    replaceOnce(R"("schema_version":1)", R"("schema_version":4294967297)");
    REQUIRE_THROWS_AS(registry.inspectDirectory(pack.directory), std::invalid_argument);

    replaceOnce(R"("type":"unsigned_integer","minimum":1)", R"("type":"boolean","minimum":1)");
    REQUIRE_THROWS_AS(registry.inspectDirectory(pack.directory), std::invalid_argument);

    replaceOnce(R"("result_bindings":[2])", R"("result_bindings":[0])");
    REQUIRE_THROWS_AS(registry.inspectDirectory(pack.directory), std::invalid_argument);
}

TEST_CASE("Loaded CPU custom tasks retain modules, execute once, and validate summaries", "[UNIT]")
{
    Atlas::Testing::TaskPackTestPack pack;
    std::string digest;
    Atlas::CustomTaskInstance instance = [&]
    {
        Atlas::TaskPackRegistry registry;
        const Atlas::TaskPackManifest& manifest{ registry.loadDirectory(pack.directory) };
        digest = manifest.digest;
        REQUIRE(registry.findTask(manifest.packId, manifest.digest, "cpu_success") != nullptr);
        Atlas::CustomTaskCreateInfo createInfo;
        createInfo.parameterJson = R"({"amount":5})";
        return registry.createTask(manifest.packId, manifest.digest, "cpu_success", createInfo);
    }();

    Atlas::TaskGraph graph;
    const auto handle{ instance.addToGraph(graph) };
    REQUIRE(handle.has_value());
    REQUIRE_FALSE(instance.addToGraph(graph).has_value());
    REQUIRE_THROWS_AS(instance.collectSummary(), std::logic_error);
    REQUIRE(graph.finishTaskGraph());

    Atlas::SynchronousCpuExecutor executor;
    Atlas::KahnScheduler scheduler{ graph, executor, Atlas::Test::unusedVulkanDispatchExecutor };
    REQUIRE(scheduler.execute().status == Atlas::SchedulerStatus::Success);
    const Atlas::CustomTaskSummary summary{ instance.collectSummary() };
    REQUIRE(summary.canonicalJson == R"({"value":42})");
    REQUIRE(summary.fields.size() == 1U);
    REQUIRE_THROWS_AS(instance.collectSummary(), std::logic_error);
}

TEST_CASE("Custom CPU callback errors follow normal TaskFailed behavior", "[UNIT]")
{
    Atlas::Testing::TaskPackTestPack pack;
    Atlas::TaskPackRegistry registry;
    const Atlas::TaskPackManifest& manifest{ registry.loadDirectory(pack.directory) };
    Atlas::CustomTaskCreateInfo invalidInfo;
    invalidInfo.parameterJson = R"({"amount":0})";
    REQUIRE_THROWS_AS(registry.createTask(manifest.packId, manifest.digest, "cpu_success", invalidInfo), std::invalid_argument);

    Atlas::CustomTaskInstance instance{ registry.createTask(manifest.packId, manifest.digest, "cpu_error", {}) };
    Atlas::TaskGraph graph;
    REQUIRE(instance.addToGraph(graph).has_value());
    REQUIRE(graph.finishTaskGraph());
    Atlas::SynchronousCpuExecutor executor;
    Atlas::KahnScheduler scheduler{ graph, executor, Atlas::Test::unusedVulkanDispatchExecutor };
    const Atlas::SchedulerResult result{ scheduler.execute() };
    REQUIRE(result.status == Atlas::SchedulerStatus::TaskFailed);
    REQUIRE(result.exception != nullptr);
}

TEST_CASE("Task-pack loading cross-checks native task metadata", "[UNIT]")
{
    Atlas::Testing::TaskPackTestPack pack;
    std::string mismatched{ pack.manifest };
    const std::size_t taskId{ mismatched.find("cpu_success") };
    REQUIRE(taskId != std::string::npos);
    mismatched.replace(taskId, std::string{ "cpu_success" }.size(), "cpu_changed");
    pack.writeManifest(mismatched);

    Atlas::TaskPackRegistry registry;
    REQUIRE_THROWS_AS(registry.loadDirectory(pack.directory), std::runtime_error);
}

TEST_CASE("Worker-pool custom CPU nodes use independent prepared contexts", "[UNIT][CONCURRENCY]")
{
    Atlas::Testing::TaskPackTestPack pack;
    Atlas::TaskPackRegistry registry;
    const Atlas::TaskPackManifest& manifest{ registry.loadDirectory(pack.directory) };
    Atlas::CustomTaskInstance first{ registry.createTask(manifest.packId, manifest.digest, "cpu_success", {}) };
    Atlas::CustomTaskInstance second{ registry.createTask(manifest.packId, manifest.digest, "cpu_success", {}) };
    Atlas::TaskGraph graph;
    REQUIRE(first.addToGraph(graph).has_value());
    REQUIRE(second.addToGraph(graph).has_value());
    REQUIRE(graph.finishTaskGraph());

    Atlas::WorkerpoolExecutor executor{ 2U };
    Atlas::KahnScheduler scheduler{ graph, executor, Atlas::Test::unusedVulkanDispatchExecutor };
    REQUIRE(scheduler.execute().status == Atlas::SchedulerStatus::Success);
    REQUIRE(first.collectSummary().canonicalJson == R"({"value":42})");
    REQUIRE(second.collectSummary().canonicalJson == R"({"value":42})");
}
