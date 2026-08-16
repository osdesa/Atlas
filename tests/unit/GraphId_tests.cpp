#include "atlas/Tasking/GraphId.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <type_traits>

TEST_CASE("GraphId defaults to the invalid identifier", "[UNIT]")
{
    const Atlas::GraphId graphId;

    REQUIRE_FALSE(graphId.isValid());
    REQUIRE(graphId.getValue() == Atlas::INVALID_GRAPH_ID_VALUE);
}

TEST_CASE("GraphId allocates opaque process-unique identities", "[UNIT]")
{
    const Atlas::GraphId first{ Atlas::GraphId::create() };
    const Atlas::GraphId second{ Atlas::GraphId::create() };

    STATIC_REQUIRE_FALSE(std::is_constructible_v<Atlas::GraphId, std::uint64_t>);
    REQUIRE(first.isValid());
    REQUIRE(second.isValid());
    REQUIRE_FALSE(first == second);
    REQUIRE(first == Atlas::GraphId{ first });
}
