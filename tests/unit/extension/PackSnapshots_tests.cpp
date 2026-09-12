#include "../../../apps/atlas_studio_runner/PackSnapshots.h"
#include "../../support/TaskPackTestPack.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <fstream>

TEST_CASE("Runner snapshots retain exact identity and clean up after ownership ends", "[UNIT]")
{
    Atlas::Testing::TaskPackTestPack pack;
    Atlas::TaskPackRegistry registry;
    const auto inspected = registry.inspectDirectory(pack.directory);
    std::filesystem::path copied;
    {
        Atlas::Studio::PackSnapshots snapshots;
        copied = snapshots.copy(inspected, registry);
        REQUIRE(copied != pack.directory);
        REQUIRE(registry.inspectDirectory(copied).digest == inspected.digest);
        Atlas::TaskPackRegistry loaded;
        REQUIRE(loaded.loadDirectory(copied).digest == inspected.digest);
    }
    REQUIRE_FALSE(std::filesystem::exists(copied));
    REQUIRE(std::filesystem::exists(pack.directory));
}

TEST_CASE("Runner snapshots reject changed inspection input and changed copied identity", "[UNIT]")
{
    Atlas::Testing::TaskPackTestPack pack;
    Atlas::TaskPackRegistry registry;
    const auto inspected = registry.inspectDirectory(pack.directory);
    Atlas::Studio::PackSnapshots snapshots;
    pack.writeManifest(pack.manifest + " ");
    REQUIRE_THROWS_WITH(snapshots.copy(inspected, registry), "Task pack changed after inspection");
    pack.writeManifest(pack.manifest);
    const auto copied = snapshots.copy(inspected, registry);
    std::ofstream changed{ copied / "manifest.json", std::ios::app };
    changed << ' ';
    changed.close();
    REQUIRE_THROWS_WITH(snapshots.verify(inspected.digest, registry), "Task-pack snapshot digest mismatch after copy");
}
