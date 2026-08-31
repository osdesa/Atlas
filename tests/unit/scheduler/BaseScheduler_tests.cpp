#include "atlas/Scheduler/BaseScheduler.h"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <optional>
#include <stdexcept>

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
    const std::optional<Atlas::TaskHandle> task{ graph.addCpuTask([] {}, Atlas::TaskOptions{ "Root" }) };

    REQUIRE(task.has_value());
    REQUIRE(graph.finishTaskGraph());

    TestScheduler scheduler{ graph };
    const Atlas::SchedulerResult result{ scheduler.execute() };

    REQUIRE(&scheduler.taskGraph() == &graph);
    REQUIRE(result.status == Atlas::SchedulerStatus::Success);
    REQUIRE(result.executedTaskCount == 1U);
}
