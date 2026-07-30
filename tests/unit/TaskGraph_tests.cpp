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

TEST_CASE("TaskGraph does not finish when it is empty", "[UNIT]")
{
    Atlas::TaskGraph graph{ 1U };

    REQUIRE_FALSE(graph.isFinalisedGraph());
    REQUIRE_FALSE(graph.finishTaskGraph());
    REQUIRE_FALSE(graph.isFinalisedGraph());
}

TEST_CASE("TaskGraph finishes a valid directed acyclic graph", "[UNIT]")
{
    Atlas::TaskGraph graph{ 1U };

    const Atlas::TaskHandle root{ addTask(graph, "Root") };
    const Atlas::TaskHandle middle{ addTask(graph, "Middle") };
    const Atlas::TaskHandle leaf{ addTask(graph, "Leaf") };

    REQUIRE(graph.addDependency(middle, root));
    REQUIRE(graph.addDependency(leaf, middle));
    REQUIRE(graph.finishTaskGraph());
    REQUIRE(graph.isFinalisedGraph());
}

TEST_CASE("TaskGraph does not finish when any component contains a cycle", "[UNIT]")
{
    Atlas::TaskGraph graph{ 1U };

    addTask(graph, "Root");
    const Atlas::TaskHandle first{ addTask(graph, "First") };
    const Atlas::TaskHandle second{ addTask(graph, "Second") };
    const std::optional<std::shared_ptr<Atlas::Task>> firstTask{ graph.findTask(first) };
    const std::optional<std::shared_ptr<Atlas::Task>> secondTask{ graph.findTask(second) };

    REQUIRE(firstTask.has_value());
    REQUIRE(secondTask.has_value());
    REQUIRE(firstTask.value()->addDependency(second));
    REQUIRE(secondTask.value()->addDependency(first));
    REQUIRE_FALSE(graph.finishTaskGraph());
    REQUIRE_FALSE(graph.isFinalisedGraph());
}

TEST_CASE("TaskGraph rejects structural changes after finalisation", "[UNIT]")
{
    Atlas::TaskGraph graph{ 1U };

    const Atlas::TaskHandle root{ addTask(graph, "Root") };
    const Atlas::TaskHandle firstDependent{ addTask(graph, "First dependent") };
    const Atlas::TaskHandle secondDependent{ addTask(graph, "Second dependent") };

    REQUIRE(graph.addDependency(firstDependent, root));
    REQUIRE(graph.finishTaskGraph());

    REQUIRE_FALSE(graph.addTask(doNothing, { .name = "Late task" }).has_value());
    REQUIRE_FALSE(graph.addDependency(secondDependent, root));
    REQUIRE(graph.getTaskCount() == 3U);

    const std::optional<std::shared_ptr<Atlas::Task>> secondTask{ graph.findTask(secondDependent) };
    REQUIRE(secondTask.has_value());
    REQUIRE(secondTask.value()->getDependencies().empty());
    REQUIRE(graph.isFinalisedGraph());
}

TEST_CASE("TaskGraph remains editable after unsuccessful finalisation", "[UNIT]")
{
    Atlas::TaskGraph graph{ 1U };

    REQUIRE_FALSE(graph.finishTaskGraph());

    addTask(graph, "Root");

    REQUIRE(graph.finishTaskGraph());
    REQUIRE(graph.isFinalisedGraph());
}
