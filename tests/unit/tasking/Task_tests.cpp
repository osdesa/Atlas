#include "atlas/Tasking/Task.h"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdint>
#include <type_traits>

namespace
{
    const Atlas::TaskFunction dummyTaskFunction{ []() {} };

    const Atlas::GraphId TEST_GRAPH_ID{ Atlas::GraphId::create() };
    const Atlas::TaskHandle INVALID_TASK_HANDLE{ Atlas::INVALID_TASK_ID, TEST_GRAPH_ID };
    const Atlas::TaskHandle VALID_TASK_HANDLE{ Atlas::TaskId{ 1U }, TEST_GRAPH_ID };

    const std::string TASK_NAME{ "TEST_TASK" };
} // namespace

TEST_CASE("Task cannot be default constructed, copied, or moved", "[UNIT]")
{
    STATIC_REQUIRE_FALSE(std::is_default_constructible_v<Atlas::Task>);
    STATIC_REQUIRE_FALSE(std::is_copy_constructible_v<Atlas::Task>);
    STATIC_REQUIRE_FALSE(std::is_copy_assignable_v<Atlas::Task>);
    STATIC_REQUIRE_FALSE(std::is_move_constructible_v<Atlas::Task>);
    STATIC_REQUIRE_FALSE(std::is_move_assignable_v<Atlas::Task>);
}

TEST_CASE("Task preserves a valid handle's task ID", "[UNIT]")
{
    const Atlas::Task task{ VALID_TASK_HANDLE, dummyTaskFunction, Atlas::TaskOptions{ TASK_NAME } };

    REQUIRE(task.isValid());
    REQUIRE(task.handle.getTaskID() == Atlas::TaskId{ 1U });
}

TEST_CASE("Task starts with default execution information", "[UNIT]")
{
    const Atlas::Task task{ VALID_TASK_HANDLE, dummyTaskFunction, Atlas::TaskOptions{ TASK_NAME } };

    REQUIRE(task.executionInfo.state == Atlas::TaskState::Unknown);
    REQUIRE(task.executionInfo.exception == nullptr);
    REQUIRE(task.executionInfo.executionDuration == std::chrono::microseconds{ 0 });
}

TEST_CASE("Task exposes its callable work", "[UNIT]")
{
    bool executed{ false };
    const Atlas::TaskFunction taskFunction{ [&executed] { executed = true; } };
    const Atlas::Task task{ VALID_TASK_HANDLE, taskFunction, Atlas::TaskOptions{ TASK_NAME } };

    REQUIRE(task.cpuFunction() != nullptr);
    task.cpuFunction()->operator()();

    REQUIRE(executed);
}

TEST_CASE("Task preserves and exposes immutable task metadata", "[UNIT]")
{
    const Atlas::TaskOptions options{ TASK_NAME, Atlas::ExecutionResource::CPU, 4U };
    const Atlas::Task task{ VALID_TASK_HANDLE, dummyTaskFunction, options };

    STATIC_REQUIRE(std::is_const_v<decltype(task.handle)>);
    STATIC_REQUIRE(std::is_const_v<decltype(task.options)>);
    REQUIRE(task.cpuFunction() != nullptr);
    REQUIRE(task.gpuDispatch() == nullptr);
    REQUIRE(task.options.name == TASK_NAME);
    REQUIRE(task.options.executionResource == Atlas::ExecutionResource::CPU);
    REQUIRE(task.options.priority == 4U);
}

TEST_CASE("Task constructed with an invalid handle is invalid", "[UNIT]")
{
    const Atlas::Task task{ INVALID_TASK_HANDLE, dummyTaskFunction, Atlas::TaskOptions{ TASK_NAME } };

    REQUIRE_FALSE(task.isValid());
    REQUIRE(task.handle.getTaskID() == Atlas::INVALID_TASK_ID);
}

TEST_CASE("Task constructed without a name is valid", "[UNIT]")
{
    const Atlas::Task task{ VALID_TASK_HANDLE, dummyTaskFunction, Atlas::TaskOptions{} };

    REQUIRE(task.isValid());
    REQUIRE(task.options.name.empty());
}

TEST_CASE("Task removes dependencies so they can be added again", "[UNIT]")
{
    Atlas::Task task{ VALID_TASK_HANDLE, dummyTaskFunction, Atlas::TaskOptions{ TASK_NAME } };
    const Atlas::TaskHandle dependency{ Atlas::TaskId{ 2U }, TEST_GRAPH_ID };

    REQUIRE(task.addDependency(dependency));
    REQUIRE_FALSE(task.addDependency(dependency));

    task.removeDependency(dependency);

    REQUIRE(task.addDependency(dependency));
}

TEST_CASE("Task rejects duplicate dependents", "[UNIT]")
{
    Atlas::Task task{ VALID_TASK_HANDLE, dummyTaskFunction, Atlas::TaskOptions{ TASK_NAME } };
    const Atlas::TaskHandle dependent{ Atlas::TaskId{ 2U }, TEST_GRAPH_ID };

    REQUIRE(task.addDependent(dependent));
    REQUIRE_FALSE(task.addDependent(dependent));
}
