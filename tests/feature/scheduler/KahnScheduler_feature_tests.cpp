#include "atlas/Executor/SynchronousCpuExecutor.h"
#include "atlas/Executor/WorkerpoolExecutor.h"
#include "atlas/Scheduler/KahnScheduler.h"

#include <algorithm>
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <future>
#include <iterator>
#include <latch>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
    class FailureObservingExecutor final : public Atlas::CpuExecutor
    {
      public:
        explicit FailureObservingExecutor(std::promise<void>& failureReturned)
            : CpuExecutor{ 2U }, failureCompletionReturned{ failureReturned }, workerpool{ 2U }
        {
        }

        bool submit(Atlas::TaskHandle handle, Atlas::TaskFunction function) override
        {
            return workerpool.submit(handle, std::move(function));
        }

        std::optional<Atlas::TaskCompletion> waitForCompletion() override
        {
            std::optional<Atlas::TaskCompletion> completion{ workerpool.waitForCompletion() };
            if (completion.has_value() && !completion->succeeded() && !failureObserved)
            {
                failureObserved = true;
                failureCompletionReturned.set_value();
            }
            return completion;
        }

        void shutdown() noexcept override
        {
            workerpool.shutdown();
        }

      private:
        std::promise<void>& failureCompletionReturned;
        Atlas::WorkerpoolExecutor workerpool;
        bool failureObserved{ false };
    };

    Atlas::TaskHandle addTask(Atlas::TaskGraph& graph, Atlas::TaskFunction function, const char* name)
    {
        const std::optional<Atlas::TaskHandle> handle{ graph.addTask(std::move(function), Atlas::TaskOptions{ name }) };
        REQUIRE(handle.has_value());
        return handle.value();
    }

    Atlas::TaskHandle addRecordingTask(Atlas::TaskGraph& graph, std::vector<std::string>& executionOrder, const char* name)
    {
        const std::optional<Atlas::TaskHandle> handle{ graph.addTask([&executionOrder, name] { executionOrder.emplace_back(name); },
                                                                     Atlas::TaskOptions{ name }) };
        REQUIRE(handle.has_value());
        return handle.value();
    }

    std::size_t positionOf(const std::vector<std::string>& executionOrder, const std::string& taskName)
    {
        const auto position{ std::find(executionOrder.begin(), executionOrder.end(), taskName) };
        REQUIRE(position != executionOrder.end());
        return static_cast<std::size_t>(std::distance(executionOrder.begin(), position));
    }

    void requireSuccessfulTask(const Atlas::TaskGraph& graph, Atlas::TaskHandle handle)
    {
        const std::optional<std::shared_ptr<const Atlas::Task>> task{ graph.findTask(handle) };

        REQUIRE(task.has_value());
        REQUIRE(task.value()->executionInfo.state == Atlas::TaskState::Success);
        REQUIRE(task.value()->executionInfo.exception == nullptr);
        REQUIRE(task.value()->executionInfo.executionDuration >= std::chrono::microseconds{ 0 });
    }
} // namespace

SCENARIO("KahnScheduler executes a dependency chain", "[FEATURE]")
{
    GIVEN("a finalised graph containing three tasks in a dependency chain")
    {
        Atlas::TaskGraph graph;
        std::vector<std::string> executionOrder;

        const Atlas::TaskHandle first{ addRecordingTask(graph, executionOrder, "First") };
        const Atlas::TaskHandle second{ addRecordingTask(graph, executionOrder, "Second") };
        const Atlas::TaskHandle third{ addRecordingTask(graph, executionOrder, "Third") };

        REQUIRE(graph.addDependency(second, first));
        REQUIRE(graph.addDependency(third, second));
        REQUIRE(graph.finishTaskGraph());

        WHEN("the graph is executed by the Kahn scheduler")
        {
            Atlas::SynchronousCpuExecutor executor;
            Atlas::KahnScheduler scheduler{ graph, executor };
            const Atlas::SchedulerResult result{ scheduler.execute() };

            THEN("all tasks complete in dependency order")
            {
                REQUIRE(result.status == Atlas::SchedulerStatus::Success);
                REQUIRE(result.executedTaskCount == 3U);
                REQUIRE(executionOrder == std::vector<std::string>{ "First", "Second", "Third" });
                requireSuccessfulTask(graph, first);
                requireSuccessfulTask(graph, second);
                requireSuccessfulTask(graph, third);
            }
        }
    }
}

SCENARIO("KahnScheduler executes a fan-out and fan-in graph", "[FEATURE]")
{
    GIVEN("a finalised diamond graph with two parallel branches")
    {
        Atlas::TaskGraph graph;
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
            Atlas::SynchronousCpuExecutor executor;
            Atlas::KahnScheduler scheduler{ graph, executor };
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
                requireSuccessfulTask(graph, root);
                requireSuccessfulTask(graph, firstBranch);
                requireSuccessfulTask(graph, secondBranch);
                requireSuccessfulTask(graph, leaf);
            }
        }
    }
}

TEST_CASE("KahnScheduler keeps independent worker-pool tasks in flight", "[FEATURE]")
{
    Atlas::TaskGraph graph;
    std::latch bothStarted{ 2 };
    std::latch releaseFirst{ 1 };
    std::atomic_bool secondFinished{ false };

    const Atlas::TaskHandle firstHandle{ addTask(
        graph,
        [&bothStarted, &releaseFirst]
        {
            bothStarted.count_down();
            releaseFirst.wait();
        },
        "First root") };
    const Atlas::TaskHandle secondHandle{ addTask(
        graph,
        [&bothStarted, &secondFinished]
        {
            secondFinished = true;
            bothStarted.count_down();
        },
        "Second root") };
    REQUIRE(graph.finishTaskGraph());

    Atlas::WorkerpoolExecutor executor{ 2U };
    Atlas::KahnScheduler scheduler{ graph, executor };
    std::future<Atlas::SchedulerResult> resultFuture{ std::async(std::launch::async, [&scheduler] { return scheduler.execute(); }) };

    bothStarted.wait();
    CHECK(secondFinished.load());
    CHECK(resultFuture.wait_for(std::chrono::microseconds{ 0 }) == std::future_status::timeout);
    releaseFirst.count_down();
    const Atlas::SchedulerResult result{ resultFuture.get() };

    REQUIRE(result.status == Atlas::SchedulerStatus::Success);
    REQUIRE(result.executedTaskCount == 2U);
    requireSuccessfulTask(graph, firstHandle);
    requireSuccessfulTask(graph, secondHandle);
}

TEST_CASE("KahnScheduler waits for worker-pool prerequisites before submitting dependants", "[FEATURE]")
{
    Atlas::TaskGraph graph;
    std::latch prerequisiteStarted{ 1 };
    std::latch releasePrerequisite{ 1 };
    std::atomic_bool dependentExecuted{ false };

    const Atlas::TaskHandle prerequisiteHandle{ addTask(
        graph,
        [&prerequisiteStarted, &releasePrerequisite]
        {
            prerequisiteStarted.count_down();
            releasePrerequisite.wait();
        },
        "Prerequisite") };
    const Atlas::TaskHandle dependentHandle{ addTask(graph, [&dependentExecuted] { dependentExecuted = true; }, "Dependent") };
    REQUIRE(graph.addDependency(dependentHandle, prerequisiteHandle));
    REQUIRE(graph.finishTaskGraph());

    Atlas::WorkerpoolExecutor executor{ 2U };
    Atlas::KahnScheduler scheduler{ graph, executor };
    std::future<Atlas::SchedulerResult> resultFuture{ std::async(std::launch::async, [&scheduler] { return scheduler.execute(); }) };

    prerequisiteStarted.wait();
    CHECK_FALSE(dependentExecuted.load());
    releasePrerequisite.count_down();
    const Atlas::SchedulerResult result{ resultFuture.get() };

    REQUIRE(result.status == Atlas::SchedulerStatus::Success);
    REQUIRE(result.executedTaskCount == 2U);
    REQUIRE(dependentExecuted.load());
    requireSuccessfulTask(graph, prerequisiteHandle);
    requireSuccessfulTask(graph, dependentHandle);
}

TEST_CASE("KahnScheduler overlaps fan-out work and waits at fan-in", "[FEATURE]")
{
    Atlas::TaskGraph graph;
    std::latch bothBranchesStarted{ 2 };
    std::latch releaseBranches{ 1 };
    std::atomic_bool firstBranchFinished{ false };
    std::atomic_bool secondBranchFinished{ false };
    std::atomic_bool leafObservedBothBranches{ false };

    const Atlas::TaskHandle rootHandle{ addTask(graph, [] {}, "Root") };
    const Atlas::TaskHandle firstBranchHandle{ addTask(
        graph,
        [&bothBranchesStarted, &releaseBranches, &firstBranchFinished]
        {
            bothBranchesStarted.count_down();
            releaseBranches.wait();
            firstBranchFinished = true;
        },
        "First branch") };
    const Atlas::TaskHandle secondBranchHandle{ addTask(
        graph,
        [&bothBranchesStarted, &releaseBranches, &secondBranchFinished]
        {
            bothBranchesStarted.count_down();
            releaseBranches.wait();
            secondBranchFinished = true;
        },
        "Second branch") };
    const Atlas::TaskHandle leafHandle{ addTask(
        graph, [&firstBranchFinished, &secondBranchFinished, &leafObservedBothBranches]
        { leafObservedBothBranches = firstBranchFinished.load() && secondBranchFinished.load(); }, "Leaf") };

    REQUIRE(graph.addDependency(firstBranchHandle, rootHandle));
    REQUIRE(graph.addDependency(secondBranchHandle, rootHandle));
    REQUIRE(graph.addDependency(leafHandle, firstBranchHandle));
    REQUIRE(graph.addDependency(leafHandle, secondBranchHandle));
    REQUIRE(graph.finishTaskGraph());

    Atlas::WorkerpoolExecutor executor{ 2U };
    Atlas::KahnScheduler scheduler{ graph, executor };
    std::future<Atlas::SchedulerResult> resultFuture{ std::async(std::launch::async, [&scheduler] { return scheduler.execute(); }) };

    bothBranchesStarted.wait();
    CHECK_FALSE(leafObservedBothBranches.load());
    releaseBranches.count_down();
    const Atlas::SchedulerResult result{ resultFuture.get() };

    REQUIRE(result.status == Atlas::SchedulerStatus::Success);
    REQUIRE(result.executedTaskCount == 4U);
    REQUIRE(leafObservedBothBranches.load());
    requireSuccessfulTask(graph, rootHandle);
    requireSuccessfulTask(graph, firstBranchHandle);
    requireSuccessfulTask(graph, secondBranchHandle);
    requireSuccessfulTask(graph, leafHandle);
}

TEST_CASE("KahnScheduler drains worker-pool work after the first task failure", "[FEATURE]")
{
    Atlas::TaskGraph graph;
    std::latch drainingTaskStarted{ 1 };
    std::latch releaseDrainingTask{ 1 };
    std::atomic_bool unsubmittedTaskExecuted{ false };

    const Atlas::TaskHandle failedHandle{ addTask(
        graph,
        [&drainingTaskStarted]
        {
            drainingTaskStarted.wait();
            throw std::runtime_error{ "first worker failure" };
        },
        "Failure") };
    const Atlas::TaskHandle drainedHandle{ addTask(
        graph,
        [&drainingTaskStarted, &releaseDrainingTask]
        {
            drainingTaskStarted.count_down();
            releaseDrainingTask.wait();
        },
        "Already accepted") };
    const Atlas::TaskHandle unsubmittedHandle{ addTask(
        graph, [&unsubmittedTaskExecuted] { unsubmittedTaskExecuted = true; }, "Not accepted") };
    const Atlas::TaskHandle blockedHandle{ addTask(graph, [] {}, "Blocked dependant") };
    REQUIRE(graph.addDependency(blockedHandle, drainedHandle));
    REQUIRE(graph.finishTaskGraph());

    std::promise<void> failureCompletionReturned;
    std::future<void> failureReturnedFuture{ failureCompletionReturned.get_future() };
    FailureObservingExecutor executor{ failureCompletionReturned };
    Atlas::KahnScheduler scheduler{ graph, executor };
    std::future<Atlas::SchedulerResult> resultFuture{ std::async(std::launch::async, [&scheduler] { return scheduler.execute(); }) };

    failureReturnedFuture.wait();
    releaseDrainingTask.count_down();
    const Atlas::SchedulerResult result{ resultFuture.get() };

    REQUIRE(result.status == Atlas::SchedulerStatus::TaskFailed);
    REQUIRE(result.executedTaskCount == 1U);
    REQUIRE(result.exception != nullptr);
    REQUIRE(graph.findTask(failedHandle).value()->executionInfo.state == Atlas::TaskState::Failure);
    requireSuccessfulTask(graph, drainedHandle);
    REQUIRE_FALSE(unsubmittedTaskExecuted.load());
    REQUIRE(graph.findTask(unsubmittedHandle).value()->executionInfo.state == Atlas::TaskState::Ready);
    REQUIRE(graph.findTask(blockedHandle).value()->executionInfo.state == Atlas::TaskState::Blocked);
}
