#include "atlas/Scheduler/BaseScheduler.h"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <exception>
#include <optional>
#include <stdexcept>
#include <string_view>

namespace
{
    class TestScheduler final : public Atlas::BaseScheduler
    {
      public:
        explicit TestScheduler(const Atlas::TaskGraph& graph) : BaseScheduler{ graph } {}

        TestScheduler(const TestScheduler&) = delete;
        TestScheduler& operator=(const TestScheduler&) = delete;
        TestScheduler(TestScheduler&&) = delete;
        TestScheduler& operator=(TestScheduler&&) = delete;

        Atlas::SchedulerResult execute() override
        {
            return { .status = Atlas::SchedulerStatus::Success,
                     .executedTaskCount = startingGraph.getTaskCount(),
                     .exception = nullptr,
                     .executionTime = std::chrono::milliseconds{ 0 } };
        }

        const Atlas::TaskGraph& taskGraph() const noexcept
        {
            return startingGraph;
        }
    };
} // namespace

TEST_CASE("BaseScheduler rejects a task graph that is not finalised", "[UNIT]")
{
    const Atlas::TaskGraph graph;

    REQUIRE_THROWS_AS(TestScheduler{ graph }, std::invalid_argument);
}

TEST_CASE("BaseScheduler accepts and retains a finalised task graph", "[UNIT]")
{
    Atlas::TaskGraph graph;
    const std::optional<Atlas::TaskHandle> task{ graph.addTask([] {}, Atlas::TaskOptions{ "Root" }) };

    REQUIRE(task.has_value());
    REQUIRE(graph.finishTaskGraph());

    TestScheduler scheduler{ graph };
    const Atlas::SchedulerResult result{ scheduler.execute() };

    REQUIRE(&scheduler.taskGraph() == &graph);
    REQUIRE(result.status == Atlas::SchedulerStatus::Success);
    REQUIRE(result.executedTaskCount == 1U);
}

TEST_CASE("BaseScheduler executes one task function successfully", "[UNIT]")
{
    Atlas::TaskGraph graph;
    REQUIRE(graph.addTask([] {}, Atlas::TaskOptions{ "Root" }).has_value());
    REQUIRE(graph.finishTaskGraph());

    TestScheduler scheduler{ graph };
    bool executed{ false };

    const Atlas::SchedulerResult result{ scheduler.executeFunction([&executed] { executed = true; }) };

    REQUIRE(executed);
    REQUIRE(result.status == Atlas::SchedulerStatus::Success);
    REQUIRE(result.exception == nullptr);
}

TEST_CASE("BaseScheduler captures exceptions from one task function", "[UNIT]")
{
    Atlas::TaskGraph graph;
    REQUIRE(graph.addTask([] {}, Atlas::TaskOptions{ "Root" }).has_value());
    REQUIRE(graph.finishTaskGraph());

    TestScheduler scheduler{ graph };
    const Atlas::SchedulerResult result{ scheduler.executeFunction([] { throw std::runtime_error{ "task execution failed" }; }) };

    REQUIRE(result.status == Atlas::SchedulerStatus::TaskFailed);
    REQUIRE(result.exception != nullptr);

    bool caughtExpectedException{ false };
    try
    {
        std::rethrow_exception(result.exception);
    }
    catch (const std::runtime_error& error)
    {
        caughtExpectedException = std::string_view{ error.what() } == "task execution failed";
    }

    REQUIRE(caughtExpectedException);
}

TEST_CASE("BaseScheduler treats an empty task function as successful", "[UNIT]")
{
    Atlas::TaskGraph graph;
    REQUIRE(graph.addTask([] {}, Atlas::TaskOptions{ "Root" }).has_value());
    REQUIRE(graph.finishTaskGraph());

    TestScheduler scheduler{ graph };
    const Atlas::SchedulerResult result{ scheduler.executeFunction(Atlas::TaskFunction{}) };

    REQUIRE(result.status == Atlas::SchedulerStatus::Success);
    REQUIRE(result.exception == nullptr);
}
