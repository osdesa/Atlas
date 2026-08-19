#include "atlas/Scheduler/SchedulerStatus.h"

#include <catch2/catch_test_macros.hpp>
#include <string_view>

TEST_CASE("SchedulerStatus has stable human-readable names", "[UNIT]")
{
    const bool successMatches{ Atlas::toString(Atlas::SchedulerStatus::Success) == std::string_view{ "Success" } };
    const bool invalidGraphMatches{ Atlas::toString(Atlas::SchedulerStatus::InvalidGraph) == std::string_view{ "InvalidGraph" } };
    const bool graphNotFinalisedMatches{ Atlas::toString(Atlas::SchedulerStatus::GraphNotFinalised) ==
                                         std::string_view{ "GraphNotFinalised" } };
    const bool taskFailedMatches{ Atlas::toString(Atlas::SchedulerStatus::TaskFailed) == std::string_view{ "TaskFailed" } };
    const bool executorUnavailableMatches{ Atlas::toString(Atlas::SchedulerStatus::ExecutorUnavailable) ==
                                           std::string_view{ "ExecutorUnavailable" } };
    const bool unknownMatches{ Atlas::toString(Atlas::SchedulerStatus::Unknown) == std::string_view{ "Unknown" } };

    REQUIRE(successMatches);
    REQUIRE(invalidGraphMatches);
    REQUIRE(graphNotFinalisedMatches);
    REQUIRE(taskFailedMatches);
    REQUIRE(executorUnavailableMatches);
    REQUIRE(unknownMatches);
}
