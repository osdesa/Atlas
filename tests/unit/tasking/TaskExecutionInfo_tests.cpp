#include "atlas/Tasking/TaskExecutionInfo.h"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <exception>
#include <stdexcept>
#include <string_view>

TEST_CASE("TaskExecutionInfo has safe defaults", "[UNIT]")
{
    const Atlas::TaskExecutionInfo executionInfo;

    REQUIRE(executionInfo.state == Atlas::TaskState::Unknown);
    REQUIRE(executionInfo.exception == nullptr);
    REQUIRE(executionInfo.executionDuration == std::chrono::microseconds{ 0 });
    REQUIRE(executionInfo.completedWorkUnitCount == 0U);
    REQUIRE(executionInfo.totalWorkUnitCount == 1U);
}

TEST_CASE("TaskState appends cooperative slicing and cancellation states", "[UNIT]")
{
    REQUIRE(static_cast<std::uint8_t>(Atlas::TaskState::Unknown) == 0U);
    REQUIRE(static_cast<std::uint8_t>(Atlas::TaskState::Blocked) == 5U);
    REQUIRE(static_cast<std::uint8_t>(Atlas::TaskState::Paused) == 6U);
    REQUIRE(static_cast<std::uint8_t>(Atlas::TaskState::Cancelled) == 7U);
}

TEST_CASE("TaskExecutionInfo stores failure details", "[UNIT]")
{
    std::exception_ptr exception;
    try
    {
        throw std::runtime_error{ "task failed" };
    }
    catch (...)
    {
        exception = std::current_exception();
    }

    const Atlas::TaskExecutionInfo executionInfo{ .state = Atlas::TaskState::Failure,
                                                  .exception = exception,
                                                  .executionDuration = std::chrono::microseconds{ 12 } };

    REQUIRE(executionInfo.state == Atlas::TaskState::Failure);
    REQUIRE(executionInfo.executionDuration == std::chrono::microseconds{ 12 });
    REQUIRE(executionInfo.exception != nullptr);

    bool caughtExpectedFailure{ false };
    try
    {
        std::rethrow_exception(executionInfo.exception);
    }
    catch (const std::runtime_error& error)
    {
        caughtExpectedFailure = std::string_view{ error.what() } == "task failed";
    }

    REQUIRE(caughtExpectedFailure);
}
