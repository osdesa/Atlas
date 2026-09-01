#include "../../support/UnusedVulkanDispatchExecutor.h"
#include "atlas/Executor/SynchronousCpuExecutor.h"
#include "atlas/Profiling/Trace.h"
#include "atlas/Scheduler/KahnScheduler.h"
#include "atlas/Tasking/TaskGraph.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <stdexcept>
#include <vector>

TEST_CASE("BoundedTraceBuffer validates capacity and reports overflow", "[UNIT][PROFILING]")
{
    REQUIRE_THROWS_AS(Atlas::BoundedTraceBuffer{ 0U }, std::invalid_argument);
    Atlas::BoundedTraceBuffer buffer{ 1U };

    REQUIRE(buffer.tryPublish(Atlas::TraceEvent{ .kind = Atlas::TraceEventKind::SchedulerStarted }));
    REQUIRE_FALSE(buffer.tryPublish(Atlas::TraceEvent{ .kind = Atlas::TraceEventKind::SchedulerFinished }));
    REQUIRE(buffer.acceptedEventCount() == 1U);
    REQUIRE(buffer.droppedEventCount() == 1U);

    const auto event{ buffer.tryPop() };
    REQUIRE(event.has_value());
    REQUIRE(event->kind == Atlas::TraceEventKind::SchedulerStarted);
    REQUIRE_FALSE(buffer.tryPop().has_value());
}

TEST_CASE("TraceSession attaches unique sequence and monotonic timestamps", "[UNIT][PROFILING]")
{
    Atlas::BoundedTraceBuffer buffer{ 2U };
    Atlas::TraceSession session{ buffer };

    if constexpr (!Atlas::profilingEnabled)
    {
        REQUIRE_FALSE(session.emit(Atlas::TraceEvent{}));
        REQUIRE(buffer.acceptedEventCount() == 0U);
        return;
    }
    REQUIRE(session.emit(Atlas::TraceEvent{ .kind = Atlas::TraceEventKind::SchedulerStarted }));
    REQUIRE(session.emit(Atlas::TraceEvent{ .kind = Atlas::TraceEventKind::SchedulerFinished }));
    const auto first{ buffer.tryPop() };
    const auto second{ buffer.tryPop() };
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    REQUIRE(first->sequence != second->sequence);
    REQUIRE(first->timestampNanoseconds <= second->timestampNanoseconds);
}

TEST_CASE("Closing a trace buffer drains accepted events then ends the consumer", "[UNIT][PROFILING]")
{
    Atlas::BoundedTraceBuffer buffer{ 1U };
    REQUIRE(buffer.tryPublish(Atlas::TraceEvent{ .kind = Atlas::TraceEventKind::TaskReady }));
    buffer.close();
    REQUIRE(buffer.waitPop().has_value());
    REQUIRE_FALSE(buffer.waitPop().has_value());
    REQUIRE_FALSE(buffer.tryPublish(Atlas::TraceEvent{}));
}

TEST_CASE("KahnScheduler traces one CPU task lifecycle in causal order", "[UNIT][PROFILING]")
{
    if constexpr (!Atlas::profilingEnabled)
    {
        return;
    }
    Atlas::TaskGraph graph;
    REQUIRE(graph.addCpuTask([] {}).has_value());
    REQUIRE(graph.finishTaskGraph());
    Atlas::SynchronousCpuExecutor cpuExecutor;
    Atlas::Test::UnusedVulkanDispatchExecutor gpuExecutor;
    Atlas::BoundedTraceBuffer buffer{ 64U };
    Atlas::TraceSession session{ buffer };
    Atlas::KahnScheduler scheduler{ graph, cpuExecutor, gpuExecutor, &session };

    REQUIRE(scheduler.execute().status == Atlas::SchedulerStatus::Success);
    std::vector<Atlas::TraceEventKind> kinds;
    while (const auto event = buffer.tryPop())
    {
        kinds.push_back(event->kind);
    }
    const auto position = [&kinds](const Atlas::TraceEventKind kind)
    { return std::find(kinds.begin(), kinds.end(), kind) - kinds.begin(); };
    REQUIRE(position(Atlas::TraceEventKind::SchedulerStarted) < position(Atlas::TraceEventKind::TaskReady));
    REQUIRE(position(Atlas::TraceEventKind::TaskReady) < position(Atlas::TraceEventKind::TaskSelected));
    REQUIRE(position(Atlas::TraceEventKind::TaskSelected) < position(Atlas::TraceEventKind::SubmissionRequested));
    REQUIRE(position(Atlas::TraceEventKind::SubmissionRequested) < position(Atlas::TraceEventKind::BackendStarted));
    REQUIRE(position(Atlas::TraceEventKind::BackendStarted) < position(Atlas::TraceEventKind::BackendFinished));
    REQUIRE(position(Atlas::TraceEventKind::BackendFinished) < position(Atlas::TraceEventKind::CompletionObserved));
    REQUIRE(position(Atlas::TraceEventKind::CompletionObserved) < position(Atlas::TraceEventKind::TaskSucceeded));
    REQUIRE(position(Atlas::TraceEventKind::TaskSucceeded) < position(Atlas::TraceEventKind::SchedulerFinished));
    REQUIRE(buffer.droppedEventCount() == 0U);
}
