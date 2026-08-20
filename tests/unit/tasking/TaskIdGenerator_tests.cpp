#include "atlas/Tasking/TaskIdGenerator.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <limits>
#include <optional>

TEST_CASE("TaskIdGenerator assigns sequential handles", "[UNIT]")
{
    const Atlas::GraphId graphId{ Atlas::GraphId::create() };
    Atlas::TaskIdGenerator generator{ graphId };

    const std::optional<Atlas::TaskHandle> first{ generator.next() };
    const std::optional<Atlas::TaskHandle> second{ generator.next() };

    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    REQUIRE(first.value().getGraphID() == graphId);
    REQUIRE(first.value().getTaskID() == Atlas::TaskId{ 1U });
    REQUIRE(second.value().getTaskID() == Atlas::TaskId{ 2U });
}

TEST_CASE("TaskIdGenerator allocates the maximum valid task ID once", "[UNIT]")
{
    const Atlas::GraphId graphId{ Atlas::GraphId::create() };
    Atlas::TaskIdGenerator generator{ graphId, Atlas::TaskId{ std::numeric_limits<std::uint32_t>::max() } };

    const std::optional<Atlas::TaskHandle> finalHandle{ generator.next() };

    REQUIRE(finalHandle.has_value());
    REQUIRE(finalHandle.value().getGraphID() == graphId);
    REQUIRE(finalHandle.value().getTaskID() == Atlas::TaskId{ std::numeric_limits<std::uint32_t>::max() });
    REQUIRE_FALSE(generator.next().has_value());
}

TEST_CASE("TaskIdGenerator rejects an invalid initial task ID", "[UNIT]")
{
    Atlas::TaskIdGenerator generator{ Atlas::GraphId::create(), Atlas::INVALID_TASK_ID };

    REQUIRE_FALSE(generator.next().has_value());
}

TEST_CASE("TaskIdGenerator rejects an invalid graph identity", "[UNIT]")
{
    Atlas::TaskIdGenerator generator{ Atlas::GraphId{} };

    REQUIRE_FALSE(generator.next().has_value());
}
