#include <atlas/atlas.hpp>
#include <catch2/catch_test_macros.hpp>
#include <string_view>

TEST_CASE("Atlas reports its version", "[smoke]")
{
    const bool version_matches{atlas::version() == std::string_view{"0.1.0"}};

    REQUIRE(version_matches);
}
