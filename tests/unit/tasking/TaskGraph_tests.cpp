#include "../../support/VulkanTestFactory.h"
#include "atlas/Tasking/TaskGraph.h"

#include <catch2/catch_test_macros.hpp>
#include <optional>
#include <type_traits>

namespace
{
    Atlas::TaskFunction doNothing{ [] {} };

    Atlas::TaskHandle addTask(Atlas::TaskGraph& graph, const char* name)
    {
        const std::optional<Atlas::TaskHandle> taskHandle{ graph.addTask(doNothing, Atlas::TaskOptions{ name }) };
        REQUIRE(taskHandle.has_value());
        return taskHandle.value();
    }
} // namespace

TEST_CASE("TaskGraph assigns handles from its process-unique graph identity", "[UNIT]")
{
    Atlas::TaskGraph graph;

    const Atlas::TaskHandle first{ addTask(graph, "First") };
    const Atlas::TaskHandle second{ addTask(graph, "Second") };

    REQUIRE(graph.getGraphID().isValid());
    REQUIRE(first.getGraphID() == graph.getGraphID());
    REQUIRE(second.getGraphID() == graph.getGraphID());
    REQUIRE(first.getTaskID() == Atlas::TaskId{ 1U });
    REQUIRE(second.getTaskID() == Atlas::TaskId{ 2U });
}

TEST_CASE("TaskGraph exposes read-only task queries", "[UNIT]")
{
    Atlas::TaskGraph graph;
    const Atlas::TaskHandle missing{ Atlas::TaskId{ 99U }, graph.getGraphID() };

    REQUIRE(graph.getTaskCount() == 0U);
    REQUIRE(graph.getTaskHandles().empty());
    REQUIRE_FALSE(graph.findTask(missing).has_value());

    const Atlas::TaskHandle first{ addTask(graph, "First") };
    const Atlas::TaskHandle second{ addTask(graph, "Second") };
    const std::vector<Atlas::TaskHandle> taskHandles{ graph.getTaskHandles() };
    const std::optional<std::shared_ptr<const Atlas::Task>> task{ graph.findTask(first) };

    STATIC_REQUIRE(std::is_same_v<decltype(graph.findTask(first)), std::optional<std::shared_ptr<const Atlas::Task>>>);
    REQUIRE(graph.getTaskCount() == 2U);
    REQUIRE(graph.getGraphID().isValid());
    REQUIRE(taskHandles == std::vector<Atlas::TaskHandle>{ first, second });
    REQUIRE(task.has_value());
    REQUIRE(task.value()->handle == first);
    REQUIRE(task.value()->getDependencies().empty());
    REQUIRE(task.value()->getDependents().empty());
}

TEST_CASE("TaskGraph preserves CPU metadata and rejects GPU metadata for CPU work", "[UNIT]")
{
    Atlas::TaskGraph graph;
    const Atlas::TaskOptions cpuOptions{ "CPU task", Atlas::ExecutionResource::CPU, 2U };
    const Atlas::TaskOptions gpuOptions{ "GPU task", Atlas::ExecutionResource::GPU, 5U };

    const std::optional<Atlas::TaskHandle> cpuHandle{ graph.addTask(doNothing, cpuOptions) };
    const std::optional<Atlas::TaskHandle> gpuHandle{ graph.addTask(doNothing, gpuOptions) };

    REQUIRE(cpuHandle.has_value());
    REQUIRE_FALSE(gpuHandle.has_value());

    const std::optional<std::shared_ptr<const Atlas::Task>> cpuTask{ graph.findTask(cpuHandle.value()) };
    REQUIRE(cpuTask.has_value());
    REQUIRE(cpuTask.value()->options.executionResource == Atlas::ExecutionResource::CPU);
    REQUIRE(cpuTask.value()->options.priority == 2U);
}

TEST_CASE("TaskGraph accepts sliced GPU work and rejects CPU metadata", "[UNIT]")
{
    Atlas::TaskGraph graph;
    const Atlas::SlicedVulkanDispatch firstDispatch{ Atlas::Testing::VulkanTestFactory::dispatch(), { 1U, 1U, 1U } };
    const Atlas::SlicedVulkanDispatch rejectedDispatch{ Atlas::Testing::VulkanTestFactory::dispatch(), { 1U, 1U, 1U } };

    const std::optional<Atlas::TaskHandle> gpuHandle{ graph.addGpuTask(
        firstDispatch, Atlas::TaskOptions{ "Sliced GPU", Atlas::ExecutionResource::GPU, 3U }) };
    const std::optional<Atlas::TaskHandle> rejectedHandle{ graph.addGpuTask(
        rejectedDispatch, Atlas::TaskOptions{ "Wrong backend", Atlas::ExecutionResource::CPU }) };

    REQUIRE(gpuHandle.has_value());
    REQUIRE_FALSE(rejectedHandle.has_value());
    REQUIRE(graph.getTaskCount() == 1U);

    const std::optional<std::shared_ptr<const Atlas::Task>> gpuTask{ graph.findTask(gpuHandle.value()) };
    REQUIRE(gpuTask.has_value());
    REQUIRE(gpuTask.value()->isValid());
    REQUIRE(gpuTask.value()->slicedGpuDispatch() != nullptr);
    REQUIRE(gpuTask.value()->options.priority == 3U);
    REQUIRE(gpuTask.value()->executionInfo.completedWorkUnitCount == 0U);
    REQUIRE(gpuTask.value()->executionInfo.totalWorkUnitCount == 1U);
}

TEST_CASE("TaskGraph accepts anonymous task metadata", "[UNIT]")
{
    Atlas::TaskGraph graph;

    const std::optional<Atlas::TaskHandle> anonymousHandle{ graph.addTask(doNothing, Atlas::TaskOptions{}) };

    REQUIRE(anonymousHandle.has_value());
    REQUIRE(anonymousHandle.value().getTaskID() == Atlas::TaskId{ 1U });
    REQUIRE(graph.getTaskCount() == 1U);

    const std::optional<std::shared_ptr<const Atlas::Task>> anonymousTask{ graph.findTask(anonymousHandle.value()) };
    REQUIRE(anonymousTask.has_value());
    REQUIRE(anonymousTask.value()->isValid());
    REQUIRE(anonymousTask.value()->options.name.empty());
}

TEST_CASE("TaskGraph rejects an out-of-range execution resource without consuming a task ID", "[UNIT]")
{
    Atlas::TaskGraph graph;
    // Deliberately constructs malformed public input to exercise submission validation.
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    constexpr auto invalidResource{ static_cast<Atlas::ExecutionResource>(255U) };
    const Atlas::TaskOptions invalidOptions{ "Invalid resource", invalidResource };

    REQUIRE_FALSE(graph.addTask(doNothing, invalidOptions).has_value());
    REQUIRE(graph.getTaskCount() == 0U);

    const Atlas::TaskHandle firstValidTask{ addTask(graph, "Valid task") };

    REQUIRE(firstValidTask.getTaskID() == Atlas::TaskId{ 1U });
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
    Atlas::TaskGraph graph;

    const Atlas::TaskHandle prerequisite{ addTask(graph, "Prerequisite") };
    const Atlas::TaskHandle dependent{ addTask(graph, "Dependent") };

    REQUIRE(graph.addDependency(dependent, prerequisite));

    const std::optional<std::shared_ptr<const Atlas::Task>> prerequisiteTask{ graph.findTask(prerequisite) };
    const std::optional<std::shared_ptr<const Atlas::Task>> dependentTask{ graph.findTask(dependent) };

    REQUIRE(prerequisiteTask.has_value());
    REQUIRE(dependentTask.has_value());
    REQUIRE(prerequisiteTask.value()->getDependents().size() == 1U);
    REQUIRE(prerequisiteTask.value()->getDependents().front() == dependent);
    REQUIRE(dependentTask.value()->getDependencies().size() == 1U);
    REQUIRE(dependentTask.value()->getDependencies().front() == prerequisite);
}

TEST_CASE("TaskGraph rejects a two-task dependency cycle", "[UNIT]")
{
    Atlas::TaskGraph graph;

    const Atlas::TaskHandle first{ addTask(graph, "First") };
    const Atlas::TaskHandle second{ addTask(graph, "Second") };

    REQUIRE(graph.addDependency(second, first));
    REQUIRE_FALSE(graph.addDependency(first, second));
}

TEST_CASE("TaskGraph rejects a longer dependency cycle", "[UNIT]")
{
    Atlas::TaskGraph graph;

    const Atlas::TaskHandle first{ addTask(graph, "First") };
    const Atlas::TaskHandle second{ addTask(graph, "Second") };
    const Atlas::TaskHandle third{ addTask(graph, "Third") };

    REQUIRE(graph.addDependency(second, first));
    REQUIRE(graph.addDependency(third, second));
    REQUIRE_FALSE(graph.addDependency(first, third));
}

TEST_CASE("TaskGraph rejects invalid dependency edges", "[UNIT]")
{
    Atlas::TaskGraph graph;
    Atlas::TaskGraph otherGraph;

    const Atlas::TaskHandle first{ addTask(graph, "First") };
    const Atlas::TaskHandle second{ addTask(graph, "Second") };
    const Atlas::TaskHandle other{ addTask(otherGraph, "Other") };
    const Atlas::TaskHandle invalid{ Atlas::INVALID_TASK_ID, graph.getGraphID() };
    const Atlas::TaskHandle missing{ Atlas::TaskId{ 99U }, graph.getGraphID() };

    REQUIRE(first.getTaskID() == other.getTaskID());
    REQUIRE_FALSE(first.getGraphID() == other.getGraphID());
    REQUIRE_FALSE(graph.findTask(other).has_value());

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
    Atlas::TaskGraph graph;

    REQUIRE_FALSE(graph.isFinalisedGraph());
    REQUIRE_FALSE(graph.finishTaskGraph());
    REQUIRE_FALSE(graph.isFinalisedGraph());
}

TEST_CASE("TaskGraph finishes a valid directed acyclic graph", "[UNIT]")
{
    Atlas::TaskGraph graph;

    const Atlas::TaskHandle root{ addTask(graph, "Root") };
    const Atlas::TaskHandle middle{ addTask(graph, "Middle") };
    const Atlas::TaskHandle leaf{ addTask(graph, "Leaf") };

    REQUIRE(graph.addDependency(middle, root));
    REQUIRE(graph.addDependency(leaf, middle));
    REQUIRE(graph.finishTaskGraph());
    REQUIRE(graph.isFinalisedGraph());
    REQUIRE(graph.finishTaskGraph());

    const std::optional<std::shared_ptr<const Atlas::Task>> rootTask{ graph.findTask(root) };
    const std::optional<std::shared_ptr<const Atlas::Task>> middleTask{ graph.findTask(middle) };
    const std::optional<std::shared_ptr<const Atlas::Task>> leafTask{ graph.findTask(leaf) };

    REQUIRE(rootTask.has_value());
    REQUIRE(middleTask.has_value());
    REQUIRE(leafTask.has_value());
    REQUIRE(rootTask.value()->executionInfo.state == Atlas::TaskState::Ready);
    REQUIRE(middleTask.value()->executionInfo.state == Atlas::TaskState::Blocked);
    REQUIRE(leafTask.value()->executionInfo.state == Atlas::TaskState::Blocked);
    REQUIRE(rootTask.value()->executionInfo.exception == nullptr);
    REQUIRE(middleTask.value()->executionInfo.exception == nullptr);
    REQUIRE(leafTask.value()->executionInfo.exception == nullptr);
}

TEST_CASE("TaskGraph rejects structural changes after finalisation", "[UNIT]")
{
    Atlas::TaskGraph graph;

    const Atlas::TaskHandle root{ addTask(graph, "Root") };
    const Atlas::TaskHandle firstDependent{ addTask(graph, "First dependent") };
    const Atlas::TaskHandle secondDependent{ addTask(graph, "Second dependent") };

    REQUIRE(graph.addDependency(firstDependent, root));
    REQUIRE(graph.finishTaskGraph());

    REQUIRE_FALSE(graph.addTask(doNothing, Atlas::TaskOptions{ "Late task" }).has_value());
    REQUIRE_FALSE(graph.addDependency(secondDependent, root));
    REQUIRE(graph.getTaskCount() == 3U);

    const std::optional<std::shared_ptr<const Atlas::Task>> secondTask{ graph.findTask(secondDependent) };
    REQUIRE(secondTask.has_value());
    REQUIRE(secondTask.value()->getDependencies().empty());
    REQUIRE(graph.isFinalisedGraph());
}

TEST_CASE("TaskGraph remains editable after unsuccessful finalisation", "[UNIT]")
{
    Atlas::TaskGraph graph;

    REQUIRE_FALSE(graph.finishTaskGraph());

    addTask(graph, "Root");

    REQUIRE(graph.finishTaskGraph());
    REQUIRE(graph.isFinalisedGraph());
}
