#include "atlas/Tasking/TaskHandle.h"

#include <catch2/catch_test_macros.hpp>
#include <type_traits>
#include <unordered_map>

TEST_CASE("Default Construction is not allowed", "[UNIT]")
{
    STATIC_REQUIRE_FALSE(std::is_default_constructible_v<Atlas::TaskHandle>);
}

TEST_CASE("0 is an invalid TaskID", "[UNIT]")
{
    const Atlas::GraphId graphId{ Atlas::GraphId::create() };
    const Atlas::TaskHandle taskHandle{ Atlas::INVALID_TASK_ID, graphId };

    REQUIRE(!taskHandle.isValid());
    REQUIRE(taskHandle.getTaskID() == Atlas::INVALID_TASK_ID);
}

TEST_CASE("TaskHandle is valid when constructed with a non-zero TaskID", "[UNIT]")
{
    const Atlas::GraphId graphId{ Atlas::GraphId::create() };
    const Atlas::TaskHandle taskHandle{ Atlas::TaskId{ 1U }, graphId };

    REQUIRE(taskHandle.isValid());
    REQUIRE(taskHandle.getTaskID() == Atlas::TaskId{ 1U });
}

TEST_CASE("TaskHandle identity includes both task and graph IDs", "[UNIT]")
{
    const Atlas::GraphId firstGraph{ Atlas::GraphId::create() };
    const Atlas::GraphId secondGraph{ Atlas::GraphId::create() };
    const Atlas::TaskHandle handle{ Atlas::TaskId{ 1U }, firstGraph };

    REQUIRE(handle == Atlas::TaskHandle{ Atlas::TaskId{ 1U }, firstGraph });
    REQUIRE_FALSE(handle == Atlas::TaskHandle{ Atlas::TaskId{ 2U }, firstGraph });
    REQUIRE_FALSE(handle == Atlas::TaskHandle{ Atlas::TaskId{ 1U }, secondGraph });
}

TEST_CASE("TaskHandle hash supports unordered associative containers", "[UNIT]")
{
    const Atlas::GraphId firstGraph{ Atlas::GraphId::create() };
    const Atlas::GraphId secondGraph{ Atlas::GraphId::create() };
    const Atlas::TaskHandle first{ Atlas::TaskId{ 1U }, firstGraph };
    const Atlas::TaskHandle sameTaskInAnotherGraph{ Atlas::TaskId{ 1U }, secondGraph };
    std::unordered_map<Atlas::TaskHandle, int, Atlas::TaskHandle::Hash> values;

    values.emplace(first, 10);
    values.emplace(sameTaskInAnotherGraph, 20);

    STATIC_REQUIRE(noexcept(Atlas::TaskHandle::Hash{}(first)));
    REQUIRE(values.at(first) == 10);
    REQUIRE(values.at(sameTaskInAnotherGraph) == 20);
}
