#include "atlas/Tasking/TaskId.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <type_traits>

TEST_CASE("TaskId defaults to the invalid identifier", "[UNIT]")
{
    const Atlas::TaskId taskId;

    REQUIRE_FALSE(taskId.isValid());
    REQUIRE(taskId == Atlas::INVALID_TASK_ID);
    REQUIRE(taskId.getValue() == Atlas::INVALID_TASK_ID_VALUE);
}

TEST_CASE("TaskId explicitly wraps a graph-local value", "[UNIT]")
{
    const Atlas::TaskId taskId{ 42U };

    STATIC_REQUIRE_FALSE(std::is_convertible_v<std::uint32_t, Atlas::TaskId>);
    REQUIRE(taskId.isValid());
    REQUIRE(taskId.getValue() == 42U);
    REQUIRE(taskId == Atlas::TaskId{ 42U });
    REQUIRE_FALSE(taskId == Atlas::TaskId{ 43U });
}
