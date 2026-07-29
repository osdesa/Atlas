#include "atlas/Tasking/TaskGraph.h"

#include <catch2/catch_test_macros.hpp>
#include <optional>
#include <type_traits>

namespace
{
    Atlas::TaskFunction doNothing{ [] {} };

    Atlas::TaskHandle addTask(Atlas::TaskGraph& graph, const char* name)
    {
        const std::optional<Atlas::TaskHandle> taskHandle{ graph.addTask(doNothing, { .name = name }) };
        REQUIRE(taskHandle.has_value());
        return taskHandle.value();
    }
} // namespace

TEST_CASE("TaskGraph assigns handles from its own graph", "[UNIT]")
{
    Atlas::TaskGraph graph{ 7U };

    const Atlas::TaskHandle first{ addTask(graph, "First") };
    const Atlas::TaskHandle second{ addTask(graph, "Second") };

    REQUIRE(first.getGraphID() == 7U);
    REQUIRE(second.getGraphID() == 7U);
    REQUIRE(first.getTaskID() == 1U);
    REQUIRE(second.getTaskID() == 2U);
}

TEST_CASE("TaskGraph exposes read-only task queries", "[UNIT]")
{
    Atlas::TaskGraph graph{ 7U };
    const Atlas::TaskHandle missing{ 99U, 7U };

    REQUIRE(graph.getTaskCount() == 0U);
    REQUIRE(graph.getTaskHandles().empty());
    REQUIRE_FALSE(graph.findTask(missing).has_value());

    const Atlas::TaskHandle first{ addTask(graph, "First") };
    const Atlas::TaskHandle second{ addTask(graph, "Second") };
    const std::vector<Atlas::TaskHandle> taskHandles{ graph.getTaskHandles() };
    const std::optional<std::shared_ptr<const Atlas::Task>> task{ graph.findTask(first) };

    REQUIRE(graph.getTaskCount() == 2U);
    REQUIRE(graph.getGraphID() == 7U);
    REQUIRE(taskHandles == std::vector<Atlas::TaskHandle>{ first, second });
    REQUIRE(task.has_value());
    REQUIRE(task.value()->getHandle() == first);
    REQUIRE(task.value()->getDependencies().empty());
    REQUIRE(task.value()->getDependents().empty());
}

TEST_CASE("TaskGraph cannot be copied or moved", "[UNIT]")
{
    STATIC_REQUIRE_FALSE(std::is_copy_constructible_v<Atlas::TaskGraph>);
    STATIC_REQUIRE_FALSE(std::is_copy_assignable_v<Atlas::TaskGraph>);
    STATIC_REQUIRE_FALSE(std::is_move_constructible_v<Atlas::TaskGraph>);
    STATIC_REQUIRE_FALSE(std::is_move_assignable_v<Atlas::TaskGraph>);
}

TEST_CASE("TaskGraph accepts one valid dependency edge", "[UNIT]")
{
    Atlas::TaskGraph graph{ 1U };

    const Atlas::TaskHandle prerequisite{ addTask(graph, "Prerequisite") };
    const Atlas::TaskHandle dependent{ addTask(graph, "Dependent") };

    REQUIRE(graph.addDependency(dependent, prerequisite));
}

TEST_CASE("TaskGraph rejects a two-task dependency cycle", "[UNIT]")
{
    Atlas::TaskGraph graph{ 1U };

    const Atlas::TaskHandle first{ addTask(graph, "First") };
    const Atlas::TaskHandle second{ addTask(graph, "Second") };

    REQUIRE(graph.addDependency(second, first));
    REQUIRE_FALSE(graph.addDependency(first, second));
}

TEST_CASE("TaskGraph rejects a longer dependency cycle", "[UNIT]")
{
    Atlas::TaskGraph graph{ 1U };

    const Atlas::TaskHandle first{ addTask(graph, "First") };
    const Atlas::TaskHandle second{ addTask(graph, "Second") };
    const Atlas::TaskHandle third{ addTask(graph, "Third") };

    REQUIRE(graph.addDependency(second, first));
    REQUIRE(graph.addDependency(third, second));
    REQUIRE_FALSE(graph.addDependency(first, third));
}

TEST_CASE("TaskGraph rejects invalid dependency edges", "[UNIT]")
{
    Atlas::TaskGraph graph{ 1U };
    Atlas::TaskGraph otherGraph{ 2U };

    const Atlas::TaskHandle first{ addTask(graph, "First") };
    const Atlas::TaskHandle second{ addTask(graph, "Second") };
    const Atlas::TaskHandle other{ addTask(otherGraph, "Other") };
    const Atlas::TaskHandle invalid{ Atlas::INVALID_TASK_ID, 1U };
    const Atlas::TaskHandle missing{ 99U, 1U };

    REQUIRE_FALSE(graph.addDependency(invalid, first));
    REQUIRE_FALSE(graph.addDependency(first, first));
    REQUIRE_FALSE(graph.addDependency(first, other));
    REQUIRE_FALSE(graph.addDependency(missing, first));
    REQUIRE_FALSE(graph.addDependency(first, missing));
    REQUIRE(graph.addDependency(second, first));
    REQUIRE_FALSE(graph.addDependency(second, first));
}
