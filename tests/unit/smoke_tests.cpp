#include "atlas/atlas.h"

#include <catch2/catch_test_macros.hpp>
#include <string_view>

TEST_CASE("Atlas reports its version", "[UNIT]")
{
    const bool version_matches{ Atlas::version() == std::string_view{ "0.1.0" } };

    REQUIRE(version_matches);
}
