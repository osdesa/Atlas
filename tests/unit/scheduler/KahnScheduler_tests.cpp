#include "atlas/Executor/SynchronousCpuExecutor.h"
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
    class ScriptedCpuExecutor final : public Atlas::CpuExecutor
    {
      public:
        explicit ScriptedCpuExecutor(std::optional<Atlas::TaskCompletion> completion)
            : CpuExecutor{ 1U }, nextCompletion{ std::move(completion) }
        {
        }

        bool submit(Atlas::TaskHandle, Atlas::TaskFunction) override
        {
            return true;
        }

        std::optional<Atlas::TaskCompletion> waitForCompletion() override
        {
            return std::exchange(nextCompletion, std::nullopt);
        }

        void shutdown() noexcept override {}

      private:
        std::optional<Atlas::TaskCompletion> nextCompletion;
    };

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
    STATIC_REQUIRE_FALSE(std::is_constructible_v<Atlas::KahnScheduler, const Atlas::TaskGraph&>);
    STATIC_REQUIRE(std::is_constructible_v<Atlas::KahnScheduler, const Atlas::TaskGraph&, Atlas::CpuExecutor&>);
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
            observedExecutionState = runtimeTask->executionInfo.state;
            executed = true;
        },
        "Root") };
    REQUIRE(graph.finishTaskGraph());
    runtimeTask = graph.findTask(handle).value();

    Atlas::SynchronousCpuExecutor executor;
    Atlas::KahnScheduler scheduler{ graph, executor };
    const Atlas::SchedulerResult result{ scheduler.execute() };

    REQUIRE(executed);
    REQUIRE(observedExecutionState == Atlas::TaskState::Running);
    REQUIRE(result.status == Atlas::SchedulerStatus::Success);
    REQUIRE(result.executedTaskCount == 1U);
    REQUIRE(result.exception == nullptr);
    REQUIRE(result.executionTime >= std::chrono::milliseconds{ 0 });

    const std::optional<std::shared_ptr<const Atlas::Task>> task{ graph.findTask(handle) };
    REQUIRE(task.has_value());
    REQUIRE(task.value()->executionInfo.state == Atlas::TaskState::Success);
    REQUIRE(task.value()->executionInfo.exception == nullptr);
    REQUIRE(task.value()->executionInfo.executionDuration >= std::chrono::microseconds{ 0 });
    REQUIRE(task.value()->executionInfo.executionDuration <= result.executionTime);
}

TEST_CASE("KahnScheduler treats an empty task function as successful work", "[UNIT]")
{
    Atlas::TaskGraph graph;

    addTask(graph, Atlas::TaskFunction{}, "Empty");
    REQUIRE(graph.finishTaskGraph());

    Atlas::SynchronousCpuExecutor executor;
    Atlas::KahnScheduler scheduler{ graph, executor };
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

    Atlas::SynchronousCpuExecutor executor;
    Atlas::KahnScheduler scheduler{ graph, executor };
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

    Atlas::SynchronousCpuExecutor executor;
    Atlas::KahnScheduler scheduler{ graph, executor };
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

    Atlas::SynchronousCpuExecutor executor;
    Atlas::KahnScheduler scheduler{ graph, executor };
    const Atlas::SchedulerResult result{ scheduler.execute() };

    CHECK(result.status == Atlas::SchedulerStatus::TaskFailed);
    CHECK(result.executedTaskCount == 0U);
    REQUIRE(result.exception != nullptr);

    const std::optional<std::shared_ptr<const Atlas::Task>> task{ graph.findTask(handle) };
    REQUIRE(task.has_value());
    REQUIRE(task.value()->executionInfo.state == Atlas::TaskState::Failure);
    REQUIRE(task.value()->executionInfo.exception == result.exception);
    REQUIRE(task.value()->executionInfo.executionDuration >= std::chrono::microseconds{ 0 });
    REQUIRE(task.value()->executionInfo.executionDuration <= result.executionTime);
    REQUIRE_FALSE(dependentExecuted);

    const std::optional<std::shared_ptr<const Atlas::Task>> dependentTask{ graph.findTask(dependentHandle) };
    REQUIRE(dependentTask.has_value());
    REQUIRE(dependentTask.value()->executionInfo.state == Atlas::TaskState::Blocked);
    REQUIRE(dependentTask.value()->executionInfo.exception == nullptr);

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

TEST_CASE("KahnScheduler restores a task when the CPU executor rejects submission", "[UNIT]")
{
    Atlas::TaskGraph graph;
    bool executed{ false };

    const Atlas::TaskHandle handle{ addTask(graph, [&executed] { executed = true; }, "Rejected") };
    REQUIRE(graph.finishTaskGraph());

    Atlas::SynchronousCpuExecutor executor;
    executor.shutdown();
    Atlas::KahnScheduler scheduler{ graph, executor };

    const Atlas::SchedulerResult result{ scheduler.execute() };

    REQUIRE_FALSE(executed);
    REQUIRE(result.status == Atlas::SchedulerStatus::ExecutorUnavailable);
    REQUIRE(result.executedTaskCount == 0U);
    REQUIRE(result.exception == nullptr);

    const std::optional<std::shared_ptr<const Atlas::Task>> task{ graph.findTask(handle) };
    REQUIRE(task.has_value());
    REQUIRE(task.value()->executionInfo.state == Atlas::TaskState::Ready);
    REQUIRE(task.value()->executionInfo.exception == nullptr);
    REQUIRE(task.value()->executionInfo.executionDuration == std::chrono::microseconds{ 0 });
}

TEST_CASE("KahnScheduler reports accepted work without a completion as an executor failure", "[UNIT]")
{
    Atlas::TaskGraph graph;
    const Atlas::TaskHandle rootHandle{ addTask(graph, [] {}, "Root") };
    const Atlas::TaskHandle dependentHandle{ addTask(graph, [] {}, "Dependent") };
    REQUIRE(graph.addDependency(dependentHandle, rootHandle));
    REQUIRE(graph.finishTaskGraph());

    ScriptedCpuExecutor executor{ std::nullopt };
    Atlas::KahnScheduler scheduler{ graph, executor };

    const Atlas::SchedulerResult result{ scheduler.execute() };

    REQUIRE(result.status == Atlas::SchedulerStatus::ExecutorUnavailable);
    REQUIRE(result.executedTaskCount == 0U);
    REQUIRE(result.exception == nullptr);

    const std::shared_ptr<const Atlas::Task> root{ graph.findTask(rootHandle).value() };
    const std::shared_ptr<const Atlas::Task> dependent{ graph.findTask(dependentHandle).value() };
    REQUIRE(root->executionInfo.state == Atlas::TaskState::Failure);
    REQUIRE(root->executionInfo.exception == nullptr);
    REQUIRE(root->executionInfo.executionDuration == std::chrono::microseconds{ 0 });
    REQUIRE(dependent->executionInfo.state == Atlas::TaskState::Blocked);
}

TEST_CASE("KahnScheduler rejects a completion attributed to the wrong task", "[UNIT]")
{
    Atlas::TaskGraph graph;
    const Atlas::TaskHandle rootHandle{ addTask(graph, [] {}, "Root") };
    const Atlas::TaskHandle dependentHandle{ addTask(graph, [] {}, "Dependent") };
    REQUIRE(graph.addDependency(dependentHandle, rootHandle));
    REQUIRE(graph.finishTaskGraph());

    ScriptedCpuExecutor executor{ Atlas::TaskCompletion{ dependentHandle, nullptr, std::chrono::microseconds{ 7 } } };
    Atlas::KahnScheduler scheduler{ graph, executor };

    const Atlas::SchedulerResult result{ scheduler.execute() };

    REQUIRE(result.status == Atlas::SchedulerStatus::ExecutorUnavailable);
    REQUIRE(result.executedTaskCount == 0U);
    REQUIRE(result.exception == nullptr);

    const std::shared_ptr<const Atlas::Task> root{ graph.findTask(rootHandle).value() };
    const std::shared_ptr<const Atlas::Task> dependent{ graph.findTask(dependentHandle).value() };
    REQUIRE(root->executionInfo.state == Atlas::TaskState::Failure);
    REQUIRE(root->executionInfo.exception == nullptr);
    REQUIRE(root->executionInfo.executionDuration == std::chrono::microseconds{ 0 });
    REQUIRE(dependent->executionInfo.state == Atlas::TaskState::Blocked);
    REQUIRE(dependent->executionInfo.executionDuration == std::chrono::microseconds{ 0 });
}

TEST_CASE("KahnScheduler skips a queued task that is no longer ready", "[UNIT]")
{
    Atlas::TaskGraph graph;
    bool executed{ false };

    const Atlas::TaskHandle handle{ addTask(graph, [&executed] { executed = true; }, "No longer ready") };
    REQUIRE(graph.finishTaskGraph());

    const std::optional<std::shared_ptr<const Atlas::Task>> task{ graph.findTask(handle) };
    REQUIRE(task.has_value());
    task.value()->executionInfo.state = Atlas::TaskState::Blocked;

    Atlas::SynchronousCpuExecutor executor;
    Atlas::KahnScheduler scheduler{ graph, executor };
    const Atlas::SchedulerResult result{ scheduler.execute() };

    REQUIRE_FALSE(executed);
    REQUIRE(result.status == Atlas::SchedulerStatus::InvalidGraph);
    REQUIRE(result.executedTaskCount == 0U);
    REQUIRE(task.value()->executionInfo.state == Atlas::TaskState::Blocked);
    REQUIRE(task.value()->executionInfo.exception == nullptr);
}

TEST_CASE("KahnScheduler does not execute a completed task again", "[UNIT]")
{
    Atlas::TaskGraph graph;
    std::size_t executionCount{ 0U };

    addTask(graph, [&executionCount] { ++executionCount; }, "Repeatable");
    REQUIRE(graph.finishTaskGraph());

    Atlas::SynchronousCpuExecutor executor;
    Atlas::KahnScheduler scheduler{ graph, executor };
    const Atlas::SchedulerResult firstResult{ scheduler.execute() };
    const Atlas::SchedulerResult secondResult{ scheduler.execute() };

    REQUIRE(firstResult.status == Atlas::SchedulerStatus::Success);
    REQUIRE(secondResult.status == Atlas::SchedulerStatus::InvalidGraph);
    REQUIRE(firstResult.executedTaskCount == 1U);
    REQUIRE(secondResult.executedTaskCount == 0U);
    REQUIRE(executionCount == 1U);
}
