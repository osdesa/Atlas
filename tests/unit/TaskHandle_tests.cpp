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
    const Atlas::TaskHandle taskHandle{ Atlas::INVALID_TASK_ID, 0U };

    REQUIRE(!taskHandle.isValid());
    REQUIRE(taskHandle.getTaskID() == 0U);
}

TEST_CASE("TaskHandle is valid when constructed with a non-zero TaskID", "[UNIT]")
{
    const Atlas::TaskHandle taskHandle{ 1U, 0U };

    REQUIRE(taskHandle.isValid());
    REQUIRE(taskHandle.getTaskID() == 1U);
}

TEST_CASE("TaskHandle identity includes both task and graph IDs", "[UNIT]")
{
    const Atlas::TaskHandle handle{ 1U, 7U };

    REQUIRE(handle == Atlas::TaskHandle{ 1U, 7U });
    REQUIRE_FALSE(handle == Atlas::TaskHandle{ 2U, 7U });
    REQUIRE_FALSE(handle == Atlas::TaskHandle{ 1U, 8U });
}

TEST_CASE("TaskHandle hash supports unordered associative containers", "[UNIT]")
{
    const Atlas::TaskHandle first{ 1U, 7U };
    const Atlas::TaskHandle sameTaskInAnotherGraph{ 1U, 8U };
    std::unordered_map<Atlas::TaskHandle, int, Atlas::TaskHandle::Hash> values;

    values.emplace(first, 10);
    values.emplace(sameTaskInAnotherGraph, 20);

    STATIC_REQUIRE(noexcept(Atlas::TaskHandle::Hash{}(first)));
    REQUIRE(values.at(first) == 10);
    REQUIRE(values.at(sameTaskInAnotherGraph) == 20);
}
