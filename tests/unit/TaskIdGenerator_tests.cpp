#include "atlas/Tasking/TaskIdGenerator.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <limits>
#include <optional>

TEST_CASE("TaskIdGenerator assigns sequential handles", "[UNIT]")
{
    Atlas::TaskIdGenerator generator{ 7U };

    const std::optional<Atlas::TaskHandle> first{ generator.next() };
    const std::optional<Atlas::TaskHandle> second{ generator.next() };

    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    REQUIRE(first.value().getGraphID() == 7U);
    REQUIRE(first.value().getTaskID() == 1U);
    REQUIRE(second.value().getTaskID() == 2U);
}

TEST_CASE("TaskIdGenerator allocates the maximum valid task ID once", "[UNIT]")
{
    Atlas::TaskIdGenerator generator{ 7U, std::numeric_limits<std::uint32_t>::max() };

    const std::optional<Atlas::TaskHandle> finalHandle{ generator.next() };

    REQUIRE(finalHandle.has_value());
    REQUIRE(finalHandle.value().getGraphID() == 7U);
    REQUIRE(finalHandle.value().getTaskID() == std::numeric_limits<std::uint32_t>::max());
    REQUIRE_FALSE(generator.next().has_value());
}

TEST_CASE("TaskIdGenerator rejects an invalid initial task ID", "[UNIT]")
{
    Atlas::TaskIdGenerator generator{ 7U, Atlas::INVALID_TASK_ID };

    REQUIRE_FALSE(generator.next().has_value());
}
