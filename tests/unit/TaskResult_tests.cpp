#include "atlas/Tasking/TaskResult.h"

#include <catch2/catch_test_macros.hpp>
#include <exception>
#include <stdexcept>
#include <string_view>

namespace
{
    const Atlas::GraphId TEST_GRAPH_ID{ Atlas::GraphId::create() };
    const Atlas::TaskHandle TEST_TASK_HANDLE{ Atlas::TaskId{ 1U }, TEST_GRAPH_ID };
} // namespace

TEST_CASE("TaskResult represents successful task completion", "[UNIT]")
{
    const Atlas::TaskResult result{ .handle = TEST_TASK_HANDLE, .state = Atlas::TaskState::Success, .exception = nullptr };

    REQUIRE(result.handle == TEST_TASK_HANDLE);
    REQUIRE(result.state == Atlas::TaskState::Success);
    REQUIRE(result.exception == nullptr);
}

TEST_CASE("TaskResult attributes a captured failure to its task", "[UNIT]")
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

    const Atlas::TaskResult result{ .handle = TEST_TASK_HANDLE, .state = Atlas::TaskState::Failure, .exception = exception };

    REQUIRE(result.handle == TEST_TASK_HANDLE);
    REQUIRE(result.state == Atlas::TaskState::Failure);
    REQUIRE(result.exception == exception);

    bool caughtExpectedFailure{ false };
    try
    {
        std::rethrow_exception(result.exception);
    }
    catch (const std::runtime_error& error)
    {
        caughtExpectedFailure = std::string_view{ error.what() } == "task failed";
    }

    REQUIRE(caughtExpectedFailure);
}

TEST_CASE("TaskResult can represent cancellation independently of an exception", "[UNIT]")
{
    const Atlas::TaskResult result{ .handle = TEST_TASK_HANDLE, .state = Atlas::TaskState::Cancelled, .exception = nullptr };

    REQUIRE(result.handle == TEST_TASK_HANDLE);
    REQUIRE(result.state == Atlas::TaskState::Cancelled);
    REQUIRE(result.exception == nullptr);
}
