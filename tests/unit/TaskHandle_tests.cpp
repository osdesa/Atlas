#include "atlas/Tasking/TaskHandle.h"

#include <catch2/catch_test_macros.hpp>
#include <string_view>

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
