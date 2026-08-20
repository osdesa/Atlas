#include "atlas/Tasking/TaskOptions.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("TaskOptions defaults to CPU execution at the highest priority", "[UNIT]")
{
    const Atlas::TaskOptions options{ "CPU task" };

    REQUIRE(options.name == "CPU task");
    REQUIRE(options.executionResource == Atlas::ExecutionResource::CPU);
    REQUIRE(options.priority == 0U);
}

TEST_CASE("TaskOptions represents GPU task intent without backend types", "[UNIT]")
{
    const Atlas::TaskOptions options{ "GPU task", Atlas::ExecutionResource::GPU, 7U };

    REQUIRE(options.name == "GPU task");
    REQUIRE(options.executionResource == Atlas::ExecutionResource::GPU);
    REQUIRE(options.priority == 7U);
}

TEST_CASE("TaskOptions supports anonymous tasks", "[UNIT]")
{
    const Atlas::TaskOptions options;

    REQUIRE(options.name.empty());
    REQUIRE(options.isValid());
    REQUIRE(options.executionResource == Atlas::ExecutionResource::CPU);
    REQUIRE(options.priority == 0U);
}

TEST_CASE("TaskOptions rejects an out-of-range execution resource", "[UNIT]")
{
    // Deliberately constructs malformed public input to exercise validation.
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    constexpr auto invalidResource{ static_cast<Atlas::ExecutionResource>(255U) };
    const Atlas::TaskOptions options{ "Invalid resource", invalidResource };

    REQUIRE_FALSE(options.isValid());
}

TEST_CASE("TaskOptions priorities use ascending numeric order", "[UNIT]")
{
    const Atlas::TaskOptions higherPriority{ "Higher priority", Atlas::ExecutionResource::CPU, 1U };
    const Atlas::TaskOptions lowerPriority{ "Lower priority", Atlas::ExecutionResource::CPU, 9U };

    REQUIRE(higherPriority.priority < lowerPriority.priority);
}
