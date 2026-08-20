#include "atlas/Scheduler/SchedulerResult.h"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <exception>
#include <sstream>
#include <stdexcept>
#include <string>

TEST_CASE("SchedulerResult prints successful execution details", "[UNIT]")
{
    const Atlas::SchedulerResult result{ .status = Atlas::SchedulerStatus::Success,
                                         .executedTaskCount = 3U,
                                         .exception = nullptr,
                                         .executionTime = std::chrono::milliseconds{ 12 } };
    std::ostringstream output;

    output << result;

    REQUIRE(output.str() == "SchedulerResult{status=Success, executedTaskCount=3, executionTime=12ms, exception=none}");
}

TEST_CASE("SchedulerResult prints captured exception details", "[UNIT]")
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

    const Atlas::SchedulerResult result{ .status = Atlas::SchedulerStatus::TaskFailed,
                                         .executedTaskCount = 1U,
                                         .exception = exception,
                                         .executionTime = std::chrono::milliseconds{ 2 } };
    std::ostringstream output;

    output << result;

    REQUIRE(output.str() == "SchedulerResult{status=TaskFailed, executedTaskCount=1, executionTime=2ms, exception=task failed}");
}

TEST_CASE("SchedulerResult has safe default values", "[UNIT]")
{
    const Atlas::SchedulerResult result;

    REQUIRE(result.status == Atlas::SchedulerStatus::Unknown);
    REQUIRE(result.executedTaskCount == 0U);
    REQUIRE(result.exception == nullptr);
    REQUIRE(result.executionTime == std::chrono::milliseconds{ 0 });
}

TEST_CASE("SchedulerResult prints a non-standard exception safely", "[UNIT]")
{
    const Atlas::SchedulerResult result{ .status = Atlas::SchedulerStatus::TaskFailed,
                                         .executedTaskCount = 0U,
                                         .exception = std::make_exception_ptr(42),
                                         .executionTime = std::chrono::microseconds{ 1 } };
    std::ostringstream output;

    output << result;

    REQUIRE(output.str() == "SchedulerResult{status=TaskFailed, executedTaskCount=0, executionTime=0ms, exception=unknown exception}");
}
