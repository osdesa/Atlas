#include "../../support/StandaloneExecutorHarness.h"
#include "atlas/Executor/SynchronousCpuExecutor.h"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <exception>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <vector>

namespace
{
    using ExecutorHarness = Atlas::Test::StandaloneExecutorHarness<Atlas::SynchronousCpuExecutor>;
    const Atlas::GraphId TEST_GRAPH_ID{ Atlas::GraphId::create() };
    const Atlas::TaskHandle INVALID_TASK_HANDLE{ Atlas::TaskId{}, TEST_GRAPH_ID };
    const Atlas::TaskHandle FIRST_TASK_HANDLE{ Atlas::TaskId{ 1U }, TEST_GRAPH_ID };
    const Atlas::TaskHandle SECOND_TASK_HANDLE{ Atlas::TaskId{ 2U }, TEST_GRAPH_ID };
} // namespace

TEST_CASE("SynchronousCpuExecutor has exclusive executor state", "[UNIT]")
{
    STATIC_REQUIRE(std::is_default_constructible_v<Atlas::SynchronousCpuExecutor>);
    STATIC_REQUIRE_FALSE(std::is_copy_constructible_v<Atlas::SynchronousCpuExecutor>);
    STATIC_REQUIRE_FALSE(std::is_copy_assignable_v<Atlas::SynchronousCpuExecutor>);
    STATIC_REQUIRE_FALSE(std::is_move_constructible_v<Atlas::SynchronousCpuExecutor>);
    STATIC_REQUIRE_FALSE(std::is_move_assignable_v<Atlas::SynchronousCpuExecutor>);
    STATIC_REQUIRE(std::has_virtual_destructor_v<Atlas::CpuExecutor>);

    ExecutorHarness executor;
    REQUIRE(executor.maxConcurrency() == 1U);
}

TEST_CASE("SynchronousCpuExecutor executes accepted work before submit returns", "[UNIT]")
{
    ExecutorHarness executor;
    bool executed{ false };

    const bool accepted{ executor.submit(FIRST_TASK_HANDLE, [&executed] { executed = true; }) };

    REQUIRE(accepted);
    REQUIRE(executed);

    const std::optional<Atlas::TaskCompletion> completion{ executor.waitForCompletion() };
    REQUIRE(completion.has_value());
    REQUIRE(completion->handle == FIRST_TASK_HANDLE);
    REQUIRE(completion->succeeded());
    REQUIRE(completion->exception == nullptr);
    REQUIRE(completion->executionDuration >= std::chrono::microseconds{ 0 });
    REQUIRE_FALSE(executor.waitForCompletion().has_value());
}

TEST_CASE("SynchronousCpuExecutor treats an empty task function as successful work", "[UNIT]")
{
    ExecutorHarness executor;

    REQUIRE(executor.submit(FIRST_TASK_HANDLE, Atlas::TaskFunction{}));

    const std::optional<Atlas::TaskCompletion> completion{ executor.waitForCompletion() };
    REQUIRE(completion.has_value());
    REQUIRE(completion->handle == FIRST_TASK_HANDLE);
    REQUIRE(completion->succeeded());
    REQUIRE(completion->exception == nullptr);
}

TEST_CASE("SynchronousCpuExecutor captures callable failures", "[UNIT]")
{
    ExecutorHarness executor;

    REQUIRE(executor.submit(FIRST_TASK_HANDLE, [] { throw std::runtime_error{ "CPU task failed" }; }));

    const std::optional<Atlas::TaskCompletion> completion{ executor.waitForCompletion() };
    REQUIRE(completion.has_value());
    REQUIRE(completion->handle == FIRST_TASK_HANDLE);
    REQUIRE_FALSE(completion->succeeded());
    REQUIRE(completion->exception != nullptr);
    REQUIRE(completion->executionDuration >= std::chrono::microseconds{ 0 });

    bool caughtExpectedFailure{ false };
    try
    {
        std::rethrow_exception(completion->exception);
    }
    catch (const std::runtime_error& error)
    {
        caughtExpectedFailure = std::string_view{ error.what() } == "CPU task failed";
    }

    REQUIRE(caughtExpectedFailure);
}

TEST_CASE("SynchronousCpuExecutor continues after a non-standard callable failure", "[UNIT]")
{
    ExecutorHarness executor;
    bool laterWorkExecuted{ false };

    REQUIRE(executor.submit(FIRST_TASK_HANDLE, [] { throw 7; }));
    REQUIRE(executor.submit(SECOND_TASK_HANDLE, [&laterWorkExecuted] { laterWorkExecuted = true; }));
    REQUIRE(laterWorkExecuted);

    const std::optional<Atlas::TaskCompletion> failedCompletion{ executor.waitForCompletion() };
    const std::optional<Atlas::TaskCompletion> successfulCompletion{ executor.waitForCompletion() };

    REQUIRE(failedCompletion.has_value());
    REQUIRE_FALSE(failedCompletion->succeeded());
    REQUIRE(failedCompletion->exception != nullptr);
    REQUIRE(successfulCompletion.has_value());
    REQUIRE(successfulCompletion->handle == SECOND_TASK_HANDLE);
    REQUIRE(successfulCompletion->succeeded());

    bool caughtExpectedFailure{ false };
    try
    {
        std::rethrow_exception(failedCompletion->exception);
    }
    catch (int value)
    {
        caughtExpectedFailure = value == 7;
    }

    REQUIRE(caughtExpectedFailure);
}

TEST_CASE("SynchronousCpuExecutor retains completions in submission order", "[UNIT]")
{
    ExecutorHarness executor;
    std::vector<int> executionOrder;

    REQUIRE(executor.submit(FIRST_TASK_HANDLE, [&executionOrder] { executionOrder.emplace_back(1); }));
    REQUIRE(executor.submit(SECOND_TASK_HANDLE, [&executionOrder] { executionOrder.emplace_back(2); }));
    REQUIRE(executionOrder == std::vector<int>{ 1, 2 });

    const std::optional<Atlas::TaskCompletion> firstCompletion{ executor.waitForCompletion() };
    const std::optional<Atlas::TaskCompletion> secondCompletion{ executor.waitForCompletion() };

    REQUIRE(firstCompletion.has_value());
    REQUIRE(secondCompletion.has_value());
    REQUIRE(firstCompletion->handle == FIRST_TASK_HANDLE);
    REQUIRE(secondCompletion->handle == SECOND_TASK_HANDLE);
    REQUIRE_FALSE(executor.waitForCompletion().has_value());
}

TEST_CASE("SynchronousCpuExecutor preserves each queued task outcome", "[UNIT]")
{
    ExecutorHarness executor;

    REQUIRE(executor.submit(FIRST_TASK_HANDLE, [] {}));
    REQUIRE(executor.submit(SECOND_TASK_HANDLE, [] { throw std::runtime_error{ "second task failed" }; }));

    const std::optional<Atlas::TaskCompletion> firstCompletion{ executor.waitForCompletion() };
    const std::optional<Atlas::TaskCompletion> secondCompletion{ executor.waitForCompletion() };

    REQUIRE(firstCompletion.has_value());
    REQUIRE(firstCompletion->handle == FIRST_TASK_HANDLE);
    REQUIRE(firstCompletion->succeeded());
    REQUIRE(secondCompletion.has_value());
    REQUIRE(secondCompletion->handle == SECOND_TASK_HANDLE);
    REQUIRE_FALSE(secondCompletion->succeeded());
}

TEST_CASE("SynchronousCpuExecutor rejects invalid task handles", "[UNIT]")
{
    ExecutorHarness executor;
    bool workExecuted{ false };

    REQUIRE_THROWS_AS(executor.submit(INVALID_TASK_HANDLE, [&workExecuted] { workExecuted = true; }), std::invalid_argument);
    REQUIRE_FALSE(workExecuted);
    REQUIRE_FALSE(executor.waitForCompletion().has_value());

    executor.shutdown();

    REQUIRE_THROWS_AS(executor.submit(INVALID_TASK_HANDLE, [] {}), std::invalid_argument);
}

TEST_CASE("SynchronousCpuExecutor rejects submissions after shutdown without discarding completions", "[UNIT]")
{
    ExecutorHarness executor;
    bool rejectedWorkExecuted{ false };

    REQUIRE(executor.submit(FIRST_TASK_HANDLE, [] {}));

    executor.shutdown();
    executor.shutdown();

    REQUIRE_FALSE(executor.submit(SECOND_TASK_HANDLE, [&rejectedWorkExecuted] { rejectedWorkExecuted = true; }));
    REQUIRE_FALSE(rejectedWorkExecuted);

    const std::optional<Atlas::TaskCompletion> acceptedCompletion{ executor.waitForCompletion() };
    REQUIRE(acceptedCompletion.has_value());
    REQUIRE(acceptedCompletion->handle == FIRST_TASK_HANDLE);
    REQUIRE(acceptedCompletion->succeeded());
    REQUIRE_FALSE(executor.waitForCompletion().has_value());
}
