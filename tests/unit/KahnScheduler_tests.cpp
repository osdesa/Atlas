#include "atlas/Scheduler/KahnScheduler.h"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <exception>
#include <memory>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
    Atlas::TaskHandle addTask(Atlas::TaskGraph& graph, Atlas::TaskFunction function, const char* name)
    {
        const std::optional<Atlas::TaskHandle> handle{ graph.addTask(std::move(function), Atlas::TaskOptions{ name }) };
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
    Atlas::TaskGraph graph;
    bool executed{ false };
    Atlas::TaskState observedExecutionState{ Atlas::TaskState::Unknown };
    std::shared_ptr<const Atlas::Task> runtimeTask;

    const Atlas::TaskHandle handle{ addTask(
        graph,
        [&executed, &observedExecutionState, &runtimeTask]
        {
            observedExecutionState = runtimeTask->state;
            executed = true;
        },
        "Root") };
    REQUIRE(graph.finishTaskGraph());
    runtimeTask = graph.findTask(handle).value();

    Atlas::KahnScheduler scheduler{ graph };
    const Atlas::SchedulerResult result{ scheduler.execute() };

    REQUIRE(executed);
    REQUIRE(observedExecutionState == Atlas::TaskState::Running);
    REQUIRE(result.status == Atlas::SchedulerStatus::Success);
    REQUIRE(result.executedTaskCount == 1U);
    REQUIRE(result.exception == nullptr);
    REQUIRE(result.executionTime >= std::chrono::milliseconds{ 0 });

    const std::optional<std::shared_ptr<const Atlas::Task>> task{ graph.findTask(handle) };
    REQUIRE(task.has_value());
    REQUIRE(task.value()->state == Atlas::TaskState::Success);
    REQUIRE(task.value()->result.has_value());
    REQUIRE(task.value()->result->handle == handle);
    REQUIRE(task.value()->result->state == Atlas::TaskState::Success);
    REQUIRE(task.value()->result->exception == nullptr);
}

TEST_CASE("KahnScheduler treats an empty task function as successful work", "[UNIT]")
{
    Atlas::TaskGraph graph;

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
    Atlas::TaskGraph graph;
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

TEST_CASE("KahnScheduler selects independent ready tasks in FIFO order", "[UNIT]")
{
    Atlas::TaskGraph graph;
    std::vector<int> executionOrder;

    addTask(graph, [&executionOrder] { executionOrder.emplace_back(1); }, "First");
    addTask(graph, [&executionOrder] { executionOrder.emplace_back(2); }, "Second");
    addTask(graph, [&executionOrder] { executionOrder.emplace_back(3); }, "Third");
    REQUIRE(graph.finishTaskGraph());

    Atlas::KahnScheduler scheduler{ graph };
    const Atlas::SchedulerResult result{ scheduler.execute() };

    REQUIRE(result.status == Atlas::SchedulerStatus::Success);
    REQUIRE(result.executedTaskCount == 3U);
    REQUIRE(executionOrder == std::vector<int>{ 1, 2, 3 });
}

TEST_CASE("KahnScheduler captures task exceptions", "[UNIT]")
{
    Atlas::TaskGraph graph;
    bool dependentExecuted{ false };

    const Atlas::TaskHandle handle{ addTask(graph, [] { throw std::runtime_error{ "task failed" }; }, "Failing") };
    const Atlas::TaskHandle dependentHandle{ addTask(graph, [&dependentExecuted] { dependentExecuted = true; }, "Dependent") };
    REQUIRE(graph.addDependency(dependentHandle, handle));
    REQUIRE(graph.finishTaskGraph());

    Atlas::KahnScheduler scheduler{ graph };
    const Atlas::SchedulerResult result{ scheduler.execute() };

    CHECK(result.status == Atlas::SchedulerStatus::TaskFailed);
    CHECK(result.executedTaskCount == 0U);
    REQUIRE(result.exception != nullptr);

    const std::optional<std::shared_ptr<const Atlas::Task>> task{ graph.findTask(handle) };
    REQUIRE(task.has_value());
    REQUIRE(task.value()->state == Atlas::TaskState::Failure);
    REQUIRE(task.value()->result.has_value());
    REQUIRE(task.value()->result->handle == handle);
    REQUIRE(task.value()->result->state == Atlas::TaskState::Failure);
    REQUIRE(task.value()->result->exception == result.exception);
    REQUIRE_FALSE(dependentExecuted);

    const std::optional<std::shared_ptr<const Atlas::Task>> dependentTask{ graph.findTask(dependentHandle) };
    REQUIRE(dependentTask.has_value());
    REQUIRE(dependentTask.value()->state == Atlas::TaskState::Blocked);
    REQUIRE_FALSE(dependentTask.value()->result.has_value());

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

TEST_CASE("KahnScheduler skips a queued task that is no longer ready", "[UNIT]")
{
    Atlas::TaskGraph graph;
    bool executed{ false };

    const Atlas::TaskHandle handle{ addTask(graph, [&executed] { executed = true; }, "Cancelled") };
    REQUIRE(graph.finishTaskGraph());

    const std::optional<std::shared_ptr<const Atlas::Task>> task{ graph.findTask(handle) };
    REQUIRE(task.has_value());
    task.value()->state = Atlas::TaskState::Cancelled;

    Atlas::KahnScheduler scheduler{ graph };
    const Atlas::SchedulerResult result{ scheduler.execute() };

    REQUIRE_FALSE(executed);
    REQUIRE(result.status == Atlas::SchedulerStatus::InvalidGraph);
    REQUIRE(result.executedTaskCount == 0U);
    REQUIRE(task.value()->state == Atlas::TaskState::Cancelled);
    REQUIRE_FALSE(task.value()->result.has_value());
}

TEST_CASE("KahnScheduler does not execute a completed task again", "[UNIT]")
{
    Atlas::TaskGraph graph;
    std::size_t executionCount{ 0U };

    addTask(graph, [&executionCount] { ++executionCount; }, "Repeatable");
    REQUIRE(graph.finishTaskGraph());

    Atlas::KahnScheduler scheduler{ graph };
    const Atlas::SchedulerResult firstResult{ scheduler.execute() };
    const Atlas::SchedulerResult secondResult{ scheduler.execute() };

    REQUIRE(firstResult.status == Atlas::SchedulerStatus::Success);
    REQUIRE(secondResult.status == Atlas::SchedulerStatus::InvalidGraph);
    REQUIRE(firstResult.executedTaskCount == 1U);
    REQUIRE(secondResult.executedTaskCount == 0U);
    REQUIRE(executionCount == 1U);
}
