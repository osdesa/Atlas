#include "../../support/StandaloneExecutorHarness.h"
#include "atlas/Executor/WorkerpoolExecutor.h"

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <future>
#include <latch>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <unordered_set>
#include <vector>

namespace
{
    using ExecutorHarness = Atlas::Test::StandaloneExecutorHarness<Atlas::WorkerpoolExecutor>;
    const Atlas::GraphId TEST_GRAPH_ID{ Atlas::GraphId::create() };
    const Atlas::TaskHandle INVALID_TASK_HANDLE{ Atlas::TaskId{}, TEST_GRAPH_ID };
    const Atlas::TaskHandle FIRST_TASK_HANDLE{ Atlas::TaskId{ 1U }, TEST_GRAPH_ID };
    const Atlas::TaskHandle SECOND_TASK_HANDLE{ Atlas::TaskId{ 2U }, TEST_GRAPH_ID };
    const Atlas::TaskHandle THIRD_TASK_HANDLE{ Atlas::TaskId{ 3U }, TEST_GRAPH_ID };
} // namespace

TEST_CASE("WorkerpoolExecutor validates and reports its fixed worker count", "[UNIT][CONCURRENCY]")
{
    STATIC_REQUIRE_FALSE(std::is_default_constructible_v<Atlas::WorkerpoolExecutor>);
    STATIC_REQUIRE_FALSE(std::is_copy_constructible_v<Atlas::WorkerpoolExecutor>);
    STATIC_REQUIRE_FALSE(std::is_move_constructible_v<Atlas::WorkerpoolExecutor>);

    REQUIRE_THROWS_AS(Atlas::WorkerpoolExecutor{ 0U }, std::invalid_argument);

    ExecutorHarness executor{ 3U };
    REQUIRE(executor.maxConcurrency() == 3U);
}

TEST_CASE("WorkerpoolExecutor rejects invalid task handles", "[UNIT][CONCURRENCY]")
{
    ExecutorHarness executor{ 1U };
    bool executed{ false };

    REQUIRE_THROWS_AS(executor.submit(INVALID_TASK_HANDLE, [&executed] { executed = true; }), std::invalid_argument);
    REQUIRE_FALSE(executed);
    REQUIRE_FALSE(executor.waitForCompletion().has_value());
}

TEST_CASE("WorkerpoolExecutor executes accepted and empty callables", "[UNIT][CONCURRENCY]")
{
    ExecutorHarness executor{ 1U };
    bool executed{ false };

    REQUIRE(executor.submit(FIRST_TASK_HANDLE, [&executed] { executed = true; }));
    REQUIRE(executor.submit(SECOND_TASK_HANDLE, Atlas::TaskFunction{}));

    const std::optional<Atlas::TaskCompletion> firstCompletion{ executor.waitForCompletion() };
    const std::optional<Atlas::TaskCompletion> secondCompletion{ executor.waitForCompletion() };

    REQUIRE(executed);
    REQUIRE(firstCompletion.has_value());
    REQUIRE(firstCompletion->handle == FIRST_TASK_HANDLE);
    REQUIRE(firstCompletion->succeeded());
    REQUIRE(firstCompletion->executionDuration >= std::chrono::microseconds{ 0 });
    REQUIRE(secondCompletion.has_value());
    REQUIRE(secondCompletion->handle == SECOND_TASK_HANDLE);
    REQUIRE(secondCompletion->succeeded());
    REQUIRE_FALSE(executor.waitForCompletion().has_value());
}

TEST_CASE("WorkerpoolExecutor isolates callable failures and continues working", "[UNIT][CONCURRENCY]")
{
    ExecutorHarness executor{ 1U };

    REQUIRE(executor.submit(FIRST_TASK_HANDLE, [] { throw std::runtime_error{ "worker failed" }; }));
    REQUIRE(executor.submit(SECOND_TASK_HANDLE, [] { throw 7; }));

    const std::optional<Atlas::TaskCompletion> standardFailure{ executor.waitForCompletion() };
    const std::optional<Atlas::TaskCompletion> nonStandardFailure{ executor.waitForCompletion() };

    REQUIRE(standardFailure.has_value());
    REQUIRE(standardFailure->handle == FIRST_TASK_HANDLE);
    REQUIRE_FALSE(standardFailure->succeeded());
    REQUIRE(nonStandardFailure.has_value());
    REQUIRE(nonStandardFailure->handle == SECOND_TASK_HANDLE);
    REQUIRE_FALSE(nonStandardFailure->succeeded());

    bool caughtExpectedFailure{ false };
    try
    {
        std::rethrow_exception(standardFailure->exception);
    }
    catch (const std::runtime_error& error)
    {
        caughtExpectedFailure = std::string_view{ error.what() } == "worker failed";
    }
    REQUIRE(caughtExpectedFailure);

    REQUIRE(executor.submit(THIRD_TASK_HANDLE, [] {}));
    const std::optional<Atlas::TaskCompletion> laterSuccess{ executor.waitForCompletion() };
    REQUIRE(laterSuccess.has_value());
    REQUIRE(laterSuccess->handle == THIRD_TASK_HANDLE);
    REQUIRE(laterSuccess->succeeded());
}

TEST_CASE("WorkerpoolExecutor produces exactly one attributed completion per accepted item", "[UNIT][CONCURRENCY]")
{
    constexpr std::uint32_t taskCount{ 64U };
    ExecutorHarness executor{ 4U };
    std::vector<Atlas::TaskHandle> submittedHandles;
    submittedHandles.reserve(taskCount);

    for (std::uint32_t taskIndex{ 1U }; taskIndex <= taskCount; ++taskIndex)
    {
        const Atlas::TaskHandle handle{ Atlas::TaskId{ taskIndex }, TEST_GRAPH_ID };
        submittedHandles.emplace_back(handle);
        REQUIRE(executor.submit(handle, [] {}));
    }

    std::unordered_set<Atlas::TaskHandle, Atlas::TaskHandle::Hash> completedHandles;
    for (std::uint32_t completionIndex{ 0U }; completionIndex < taskCount; ++completionIndex)
    {
        const std::optional<Atlas::TaskCompletion> completion{ executor.waitForCompletion() };
        REQUIRE(completion.has_value());
        REQUIRE(completion->succeeded());
        REQUIRE(completion->executionDuration >= std::chrono::microseconds{ 0 });
        REQUIRE(completedHandles.emplace(completion->handle).second);
    }

    REQUIRE(completedHandles.size() == submittedHandles.size());
    for (const Atlas::TaskHandle handle : submittedHandles)
    {
        REQUIRE(completedHandles.contains(handle));
    }
    REQUIRE_FALSE(executor.waitForCompletion().has_value());
}

TEST_CASE("WorkerpoolExecutor survives repeated high-volume lifecycle rounds", "[UNIT][CONCURRENCY]")
{
    constexpr std::uint32_t roundCount{ 4U };
    constexpr std::uint32_t taskCount{ 512U };

    for (std::uint32_t roundIndex{ 0U }; roundIndex < roundCount; ++roundIndex)
    {
        const Atlas::GraphId graphId{ Atlas::GraphId::create() };
        std::atomic_uint32_t executedTaskCount{ 0U };
        ExecutorHarness executor{ 4U };

        for (std::uint32_t taskIndex{ 1U }; taskIndex <= taskCount; ++taskIndex)
        {
            const Atlas::TaskHandle handle{ Atlas::TaskId{ taskIndex }, graphId };
            REQUIRE(executor.submit(handle, [&executedTaskCount] { ++executedTaskCount; }));
        }

        std::unordered_set<Atlas::TaskHandle, Atlas::TaskHandle::Hash> completedHandles;
        for (std::uint32_t completionIndex{ 0U }; completionIndex < taskCount; ++completionIndex)
        {
            const std::optional<Atlas::TaskCompletion> completion{ executor.waitForCompletion() };
            REQUIRE(completion.has_value());
            REQUIRE(completion->succeeded());
            REQUIRE(completedHandles.emplace(completion->handle).second);
        }

        REQUIRE(executedTaskCount.load() == taskCount);
        REQUIRE(completedHandles.size() == taskCount);
        REQUIRE_FALSE(executor.waitForCompletion().has_value());
    }
}

TEST_CASE("WorkerpoolExecutor runs at least two tasks concurrently", "[UNIT][CONCURRENCY]")
{
    ExecutorHarness executor{ 2U };
    std::latch bothStarted{ 2 };
    std::latch releaseWorkers{ 1 };

    const auto blockingWork = [&bothStarted, &releaseWorkers]
    {
        bothStarted.count_down();
        releaseWorkers.wait();
    };

    REQUIRE(executor.submit(FIRST_TASK_HANDLE, blockingWork));
    REQUIRE(executor.submit(SECOND_TASK_HANDLE, blockingWork));

    bothStarted.wait();
    releaseWorkers.count_down();

    REQUIRE(executor.waitForCompletion().has_value());
    REQUIRE(executor.waitForCompletion().has_value());
}

TEST_CASE("WorkerpoolExecutor returns completions in completion rather than submission order", "[UNIT][CONCURRENCY]")
{
    ExecutorHarness executor{ 2U };
    std::latch firstTaskStarted{ 1 };
    std::latch releaseFirstTask{ 1 };

    REQUIRE(executor.submit(FIRST_TASK_HANDLE,
                            [&firstTaskStarted, &releaseFirstTask]
                            {
                                firstTaskStarted.count_down();
                                releaseFirstTask.wait();
                            }));
    firstTaskStarted.wait();
    REQUIRE(executor.submit(SECOND_TASK_HANDLE, [] {}));

    const std::optional<Atlas::TaskCompletion> secondCompletion{ executor.waitForCompletion() };
    REQUIRE(secondCompletion.has_value());
    REQUIRE(secondCompletion->handle == SECOND_TASK_HANDLE);

    releaseFirstTask.count_down();
    const std::optional<Atlas::TaskCompletion> firstCompletion{ executor.waitForCompletion() };
    REQUIRE(firstCompletion.has_value());
    REQUIRE(firstCompletion->handle == FIRST_TASK_HANDLE);
}

TEST_CASE("WorkerpoolExecutor completion retrieval waits for accepted work", "[UNIT][CONCURRENCY]")
{
    ExecutorHarness executor{ 1U };
    std::latch taskStarted{ 1 };
    std::latch releaseTask{ 1 };

    REQUIRE(executor.submit(FIRST_TASK_HANDLE,
                            [&taskStarted, &releaseTask]
                            {
                                taskStarted.count_down();
                                releaseTask.wait();
                            }));
    taskStarted.wait();

    std::future<std::optional<Atlas::TaskCompletion>> completionFuture{ std::async(std::launch::async, [&executor]
                                                                                   { return executor.waitForCompletion(); }) };
    REQUIRE(completionFuture.wait_for(std::chrono::microseconds{ 0 }) == std::future_status::timeout);

    releaseTask.count_down();
    const std::optional<Atlas::TaskCompletion> completion{ completionFuture.get() };
    REQUIRE(completion.has_value());
    REQUIRE(completion->handle == FIRST_TASK_HANDLE);
}

TEST_CASE("WorkerpoolExecutor shutdown drains work and rejects later submissions", "[UNIT][CONCURRENCY]")
{
    ExecutorHarness executor{ 1U };
    std::latch firstTaskStarted{ 1 };
    std::latch releaseFirstTask{ 1 };
    std::atomic_bool secondTaskExecuted{ false };

    REQUIRE(executor.submit(FIRST_TASK_HANDLE,
                            [&firstTaskStarted, &releaseFirstTask]
                            {
                                firstTaskStarted.count_down();
                                releaseFirstTask.wait();
                            }));
    REQUIRE(executor.submit(SECOND_TASK_HANDLE, [&secondTaskExecuted] { secondTaskExecuted = true; }));
    firstTaskStarted.wait();

    std::future<void> shutdownFuture{ std::async(std::launch::async, [&executor] { executor.shutdown(); }) };
    releaseFirstTask.count_down();
    shutdownFuture.get();
    executor.shutdown();

    REQUIRE(secondTaskExecuted.load());
    REQUIRE_FALSE(executor.submit(FIRST_TASK_HANDLE, [] {}));
    REQUIRE(executor.waitForCompletion().has_value());
    REQUIRE(executor.waitForCompletion().has_value());
    REQUIRE_FALSE(executor.waitForCompletion().has_value());
}

TEST_CASE("WorkerpoolExecutor destruction waits for accepted work", "[UNIT][CONCURRENCY]")
{
    std::atomic_bool executed{ false };
    {
        ExecutorHarness executor{ 1U };
        REQUIRE(executor.submit(FIRST_TASK_HANDLE, [&executed] { executed = true; }));
    }

    REQUIRE(executed.load());
}
