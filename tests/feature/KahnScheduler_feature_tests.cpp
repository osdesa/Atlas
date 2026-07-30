#include "atlas/Scheduler/KahnScheduler.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

namespace
{
    Atlas::TaskHandle addRecordingTask(Atlas::TaskGraph& graph, std::vector<std::string>& executionOrder, const char* name)
    {
        const std::optional<Atlas::TaskHandle> handle{ graph.addTask([&executionOrder, name] { executionOrder.emplace_back(name); },
                                                                     { .name = name }) };
        REQUIRE(handle.has_value());
        return handle.value();
    }

    std::size_t positionOf(const std::vector<std::string>& executionOrder, const std::string& taskName)
    {
        const auto position{ std::find(executionOrder.begin(), executionOrder.end(), taskName) };
        REQUIRE(position != executionOrder.end());
        return static_cast<std::size_t>(std::distance(executionOrder.begin(), position));
    }
} // namespace

SCENARIO("KahnScheduler executes a dependency chain", "[FEATURE]")
{
    GIVEN("a finalised graph containing three tasks in a dependency chain")
    {
        Atlas::TaskGraph graph{ 1U };
        std::vector<std::string> executionOrder;

        const Atlas::TaskHandle first{ addRecordingTask(graph, executionOrder, "First") };
        const Atlas::TaskHandle second{ addRecordingTask(graph, executionOrder, "Second") };
        const Atlas::TaskHandle third{ addRecordingTask(graph, executionOrder, "Third") };

        REQUIRE(graph.addDependency(second, first));
        REQUIRE(graph.addDependency(third, second));
        REQUIRE(graph.finishTaskGraph());

        WHEN("the graph is executed by the Kahn scheduler")
        {
            Atlas::KahnScheduler scheduler{ graph };
            const Atlas::SchedulerResult result{ scheduler.execute() };

            THEN("all tasks complete in dependency order")
            {
                REQUIRE(result.status == Atlas::SchedulerStatus::Success);
                REQUIRE(result.executedTaskCount == 3U);
                REQUIRE(executionOrder == std::vector<std::string>{ "First", "Second", "Third" });
            }
        }
    }
}

SCENARIO("KahnScheduler executes a fan-out and fan-in graph", "[FEATURE]")
{
    GIVEN("a finalised diamond graph with two parallel branches")
    {
        Atlas::TaskGraph graph{ 1U };
        std::vector<std::string> executionOrder;

        const Atlas::TaskHandle root{ addRecordingTask(graph, executionOrder, "Root") };
        const Atlas::TaskHandle firstBranch{ addRecordingTask(graph, executionOrder, "First branch") };
        const Atlas::TaskHandle secondBranch{ addRecordingTask(graph, executionOrder, "Second branch") };
        const Atlas::TaskHandle leaf{ addRecordingTask(graph, executionOrder, "Leaf") };

        REQUIRE(graph.addDependency(firstBranch, root));
        REQUIRE(graph.addDependency(secondBranch, root));
        REQUIRE(graph.addDependency(leaf, firstBranch));
        REQUIRE(graph.addDependency(leaf, secondBranch));
        REQUIRE(graph.finishTaskGraph());

        WHEN("the graph is executed by the Kahn scheduler")
        {
            Atlas::KahnScheduler scheduler{ graph };
            const Atlas::SchedulerResult result{ scheduler.execute() };

            THEN("the root precedes both branches and both branches precede the leaf")
            {
                REQUIRE(result.status == Atlas::SchedulerStatus::Success);
                REQUIRE(result.executedTaskCount == 4U);
                REQUIRE(executionOrder.size() == 4U);

                const std::size_t rootPosition{ positionOf(executionOrder, "Root") };
                const std::size_t firstBranchPosition{ positionOf(executionOrder, "First branch") };
                const std::size_t secondBranchPosition{ positionOf(executionOrder, "Second branch") };
                const std::size_t leafPosition{ positionOf(executionOrder, "Leaf") };

                REQUIRE(rootPosition < firstBranchPosition);
                REQUIRE(rootPosition < secondBranchPosition);
                REQUIRE(firstBranchPosition < leafPosition);
                REQUIRE(secondBranchPosition < leafPosition);
            }
        }
    }
}
