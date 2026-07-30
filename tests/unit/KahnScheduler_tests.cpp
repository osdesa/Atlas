#include "atlas/Scheduler/KahnScheduler.h"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <exception>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace
{
    Atlas::TaskHandle addTask(Atlas::TaskGraph& graph, Atlas::TaskFunction function, const char* name)
    {
        const std::optional<Atlas::TaskHandle> handle{ graph.addTask(std::move(function), { .name = name }) };
        REQUIRE(handle.has_value());
        return handle.value();
    }
} // namespace

TEST_CASE("KahnScheduler requires a task graph and cannot be copied or moved", "[UNIT]")
{
    STATIC_REQUIRE_FALSE(std::is_default_constructible_v<Atlas::KahnScheduler>);
    STATIC_REQUIRE_FALSE(std::is_copy_constructible_v<Atlas::KahnScheduler>);
    STATIC_REQUIRE_FALSE(std::is_copy_assignable_v<Atlas::KahnScheduler>);
    STATIC_REQUIRE_FALSE(std::is_move_constructible_v<Atlas::KahnScheduler>);
    STATIC_REQUIRE_FALSE(std::is_move_assignable_v<Atlas::KahnScheduler>);
}

TEST_CASE("KahnScheduler executes a single task successfully", "[UNIT]")
{
    Atlas::TaskGraph graph{ 1U };
    bool executed{ false };

    addTask(graph, [&executed] { executed = true; }, "Root");
    REQUIRE(graph.finishTaskGraph());

    Atlas::KahnScheduler scheduler{ graph };
    const Atlas::SchedulerResult result{ scheduler.execute() };

    REQUIRE(executed);
    REQUIRE(result.status == Atlas::SchedulerStatus::Success);
    REQUIRE(result.executedTaskCount == 1U);
    REQUIRE(result.exception == nullptr);
    REQUIRE(result.executionTime >= std::chrono::milliseconds{ 0 });
}

TEST_CASE("KahnScheduler treats an empty task function as successful work", "[UNIT]")
{
    Atlas::TaskGraph graph{ 1U };

    addTask(graph, Atlas::TaskFunction{}, "Empty");
    REQUIRE(graph.finishTaskGraph());

    Atlas::KahnScheduler scheduler{ graph };
    const Atlas::SchedulerResult result{ scheduler.execute() };

    REQUIRE(result.status == Atlas::SchedulerStatus::Success);
    REQUIRE(result.executedTaskCount == 1U);
    REQUIRE(result.exception == nullptr);
}

TEST_CASE("KahnScheduler executes every independent root task", "[UNIT]")
{
    Atlas::TaskGraph graph{ 1U };
    std::size_t executionCount{ 0U };

    addTask(graph, [&executionCount] { ++executionCount; }, "First root");
    addTask(graph, [&executionCount] { ++executionCount; }, "Second root");
    REQUIRE(graph.finishTaskGraph());

    Atlas::KahnScheduler scheduler{ graph };
    const Atlas::SchedulerResult result{ scheduler.execute() };

    REQUIRE(result.status == Atlas::SchedulerStatus::Success);
    REQUIRE(result.executedTaskCount == 2U);
    REQUIRE(executionCount == 2U);
}

TEST_CASE("KahnScheduler captures task exceptions", "[UNIT]")
{
    Atlas::TaskGraph graph{ 1U };

    addTask(graph, [] { throw std::runtime_error{ "task failed" }; }, "Failing");
    REQUIRE(graph.finishTaskGraph());

    Atlas::KahnScheduler scheduler{ graph };
    const Atlas::SchedulerResult result{ scheduler.execute() };

    CHECK(result.status == Atlas::SchedulerStatus::TaskFailed);
    CHECK(result.executedTaskCount == 0U);
    REQUIRE(result.exception != nullptr);

    bool caughtRuntimeError{ false };
    try
    {
        std::rethrow_exception(result.exception);
    }
    catch (const std::runtime_error&)
    {
        caughtRuntimeError = true;
    }

    REQUIRE(caughtRuntimeError);
}

TEST_CASE("KahnScheduler rebuilds its execution state for every run", "[UNIT]")
{
    Atlas::TaskGraph graph{ 1U };
    std::size_t executionCount{ 0U };

    addTask(graph, [&executionCount] { ++executionCount; }, "Repeatable");
    REQUIRE(graph.finishTaskGraph());

    Atlas::KahnScheduler scheduler{ graph };
    const Atlas::SchedulerResult firstResult{ scheduler.execute() };
    const Atlas::SchedulerResult secondResult{ scheduler.execute() };

    REQUIRE(firstResult.status == Atlas::SchedulerStatus::Success);
    REQUIRE(secondResult.status == Atlas::SchedulerStatus::Success);
    REQUIRE(firstResult.executedTaskCount == 1U);
    REQUIRE(secondResult.executedTaskCount == 1U);
    REQUIRE(executionCount == 2U);
}
