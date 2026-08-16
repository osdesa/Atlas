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
