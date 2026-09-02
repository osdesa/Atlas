#include "../../support/UnusedVulkanDispatchExecutor.h"
#include "../../support/VulkanTestFactory.h"
#include "atlas/Executor/SynchronousCpuExecutor.h"
#include "atlas/Executor/WorkerpoolExecutor.h"
#include "atlas/Scheduler/FifoSchedulingPolicy.h"
#include "atlas/Scheduler/KahnScheduler.h"
#include "atlas/Scheduler/RoundRobinSchedulingPolicy.h"
#include "atlas/Scheduler/StaticPrioritySchedulingPolicy.h"
#include "atlas/Tasking/TaskGraph.h"

#include <algorithm>
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <optional>
#include <random>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

/** @file KahnSchedulerStress_tests.cpp @brief Deterministic generated and large-graph robustness tests. */

namespace
{
    constexpr std::uint64_t defaultStressSeed{ 684'453U };
    constexpr std::size_t defaultStressRounds{ 128U };

    std::uint64_t environmentUnsigned(const char* const name, const std::uint64_t fallback)
    {
        const char* const text{ std::getenv(name) };
        if (text == nullptr)
        {
            return fallback;
        }

        const std::string_view value{ text };
        std::uint64_t parsed{ 0U };
        const auto result{ std::from_chars(value.data(), value.data() + value.size(), parsed) };
        if (result.ec != std::errc{} || result.ptr != value.data() + value.size())
        {
            throw std::runtime_error{ std::string{ name } + " must be an unsigned decimal integer" };
        }
        return parsed;
    }

    struct Submission
    {
        Atlas::TaskHandle handle;
        std::size_t workUnitIndex{ 0U };
    };

    class RecordingCpuExecutor final : public Atlas::CpuExecutor
    {
      public:
        explicit RecordingCpuExecutor(std::vector<Submission>& recorded) : CpuExecutor{ 1U }, submissions{ recorded } {}

        bool submit(const Atlas::TaskHandle handle, Atlas::TaskFunction function, Atlas::CompletionChannel& channel) override
        {
            submissions.push_back({ handle, 0U });
            std::exception_ptr exception;
            try
            {
                if (function)
                {
                    function();
                }
            }
            catch (...)
            {
                exception = std::current_exception();
            }
            return channel.publish(Atlas::TaskCompletion{ handle, exception });
        }

        void shutdown() noexcept override {}

      private:
        std::vector<Submission>& submissions;
    };

    class RecordingGpuExecutor final : public Atlas::VulkanDispatchExecutor
    {
      public:
        explicit RecordingGpuExecutor(std::vector<Submission>& recorded) : VulkanDispatchExecutor{ 1U }, submissions{ recorded } {}

        bool submit(const Atlas::TaskHandle handle, Atlas::VulkanDispatch dispatch, Atlas::CompletionChannel& channel) override
        {
            submissions.push_back({ handle, dispatch.workUnitIndex() });
            return channel.publish(
                Atlas::TaskCompletion{ handle, nullptr, {}, Atlas::ExecutionResource::GPU, dispatch.workUnitIndex() });
        }

        void shutdown() noexcept override {}

      private:
        std::vector<Submission>& submissions;
    };

    struct GeneratedGraph
    {
        Atlas::TaskGraph graph;
        std::vector<Atlas::TaskHandle> handles;
        std::vector<std::vector<std::size_t>> dependencies;
    };

    void generateGraph(GeneratedGraph& generated, const std::uint64_t seed, const std::size_t taskCount)
    {
        std::mt19937_64 random{ seed };
        generated.handles.reserve(taskCount);
        generated.dependencies.resize(taskCount);

        Atlas::Testing::VulkanTestFactory::Resources resources{ Atlas::Testing::VulkanTestFactory::resources() };
        const Atlas::VulkanDispatch dispatch{ resources.pipeline,
                                              { { 0U, resources.buffers.front(), Atlas::BufferAccess::ReadWrite } },
                                              { 4U, 1U, 1U } };

        for (std::size_t index{ 0U }; index < taskCount; ++index)
        {
            const std::uint32_t priority{ static_cast<std::uint32_t>(random() % 8U) };
            std::optional<Atlas::TaskHandle> handle;
            if ((random() % 3U) == 0U)
            {
                if ((random() % 2U) == 0U)
                {
                    handle = generated.graph.addGpuTask(
                        Atlas::SlicedVulkanDispatch{ dispatch, { 1U, 1U, 1U } },
                        Atlas::TaskOptions{ "generated sliced GPU", Atlas::ExecutionResource::GPU, priority });
                }
                else
                {
                    handle = generated.graph.addGpuTask(
                        dispatch, Atlas::TaskOptions{ "generated GPU", Atlas::ExecutionResource::GPU, priority });
                }
            }
            else
            {
                handle =
                    generated.graph.addCpuTask([] {}, Atlas::TaskOptions{ "generated CPU", Atlas::ExecutionResource::CPU, priority });
            }
            REQUIRE(handle.has_value());
            generated.handles.emplace_back(handle.value());
        }

        if (taskCount >= 3U)
        {
            REQUIRE(generated.graph.addDependency(generated.handles.at(1U), generated.handles.at(0U)));
            REQUIRE(generated.graph.addDependency(generated.handles.at(2U), generated.handles.at(1U)));
            generated.dependencies.at(1U).emplace_back(0U);
            generated.dependencies.at(2U).emplace_back(1U);
            REQUIRE(generated.graph.addDependency(generated.handles.at(0U), generated.handles.at(2U)));
            REQUIRE_FALSE(generated.graph.finishTaskGraph());
            REQUIRE(generated.graph.removeDependency(generated.handles.at(0U), generated.handles.at(2U)));
        }

        for (std::size_t dependent{ 3U }; dependent < taskCount; ++dependent)
        {
            const std::size_t requestedParents{ static_cast<std::size_t>(random() % 4U) };
            for (std::size_t parentAttempt{ 0U }; parentAttempt < requestedParents; ++parentAttempt)
            {
                const std::size_t dependency{ static_cast<std::size_t>(random() % dependent) };
                if (std::find(generated.dependencies.at(dependent).begin(), generated.dependencies.at(dependent).end(), dependency) !=
                    generated.dependencies.at(dependent).end())
                {
                    continue;
                }
                REQUIRE(generated.graph.addDependency(generated.handles.at(dependent), generated.handles.at(dependency)));
                generated.dependencies.at(dependent).emplace_back(dependency);
                REQUIRE_FALSE(generated.graph.addDependency(generated.handles.at(dependent), generated.handles.at(dependency)));
            }
        }
        REQUIRE(generated.graph.finishTaskGraph());
    }

    void requireDependencyOrder(const GeneratedGraph& generated, const std::span<const Submission> submissions)
    {
        std::unordered_map<Atlas::TaskHandle, std::size_t, Atlas::TaskHandle::Hash> firstSubmission;
        std::unordered_map<Atlas::TaskHandle, std::size_t, Atlas::TaskHandle::Hash> lastSubmission;
        for (std::size_t index{ 0U }; index < submissions.size(); ++index)
        {
            firstSubmission.try_emplace(submissions[index].handle, index);
            lastSubmission.insert_or_assign(submissions[index].handle, index);
        }

        for (std::size_t dependent{ 0U }; dependent < generated.handles.size(); ++dependent)
        {
            REQUIRE(firstSubmission.contains(generated.handles.at(dependent)));
            for (const std::size_t dependency : generated.dependencies.at(dependent))
            {
                REQUIRE(lastSubmission.at(generated.handles.at(dependency)) < firstSubmission.at(generated.handles.at(dependent)));
            }
        }
    }

    void executeGeneratedGraph(const std::uint64_t seed)
    {
        const std::size_t taskCount{ 1U + static_cast<std::size_t>(seed % 64U) };
        GeneratedGraph generated;
        generateGraph(generated, seed, taskCount);
        std::vector<Submission> submissions;
        RecordingCpuExecutor cpuExecutor{ submissions };
        RecordingGpuExecutor gpuExecutor{ submissions };

        Atlas::SchedulerResult result;
        switch (seed % 3U)
        {
        case 0U:
        {
            const Atlas::FifoSchedulingPolicy policy;
            Atlas::KahnScheduler scheduler{ generated.graph, cpuExecutor, gpuExecutor, policy };
            result = scheduler.execute();
            break;
        }
        case 1U:
        {
            const Atlas::RoundRobinSchedulingPolicy policy{ 1U + static_cast<std::size_t>(seed % 4U) };
            Atlas::KahnScheduler scheduler{ generated.graph, cpuExecutor, gpuExecutor, policy };
            result = scheduler.execute();
            break;
        }
        default:
        {
            const Atlas::StaticPrioritySchedulingPolicy policy;
            Atlas::KahnScheduler scheduler{ generated.graph, cpuExecutor, gpuExecutor, policy };
            result = scheduler.execute();
            break;
        }
        }

        REQUIRE(result.status == Atlas::SchedulerStatus::Success);
        REQUIRE(result.executedTaskCount == taskCount);
        requireDependencyOrder(generated, submissions);
        for (const Atlas::TaskHandle handle : generated.handles)
        {
            const auto task{ generated.graph.snapshotTask(handle) };
            REQUIRE(task.has_value());
            REQUIRE(task.value().executionInfo.state == Atlas::TaskState::Success);
            REQUIRE(task.value().executionInfo.completedWorkUnitCount == task.value().executionInfo.totalWorkUnitCount);
        }
    }
} // namespace

TEST_CASE("KahnScheduler satisfies generated DAG invariants for replayable seeds", "[UNIT][STRESS]")
{
    const std::uint64_t masterSeed{ environmentUnsigned("ATLAS_STRESS_SEED", defaultStressSeed) };
    const std::uint64_t configuredRounds{ environmentUnsigned("ATLAS_STRESS_ROUNDS", defaultStressRounds) };
    REQUIRE(configuredRounds > 0U);
    REQUIRE(configuredRounds <= 100'000U);

    std::mt19937_64 seeds{ masterSeed };
    for (std::uint64_t round{ 0U }; round < configuredRounds; ++round)
    {
        const std::uint64_t seed{ seeds() };
        CAPTURE(masterSeed, round, seed);
        executeGeneratedGraph(seed);
    }
}

TEST_CASE("KahnScheduler completes a very large independent graph", "[UNIT][STRESS][CONCURRENCY]")
{
    constexpr std::size_t taskCount{ 10'000U };
    Atlas::TaskGraph graph;
    std::atomic_size_t executed{ 0U };
    for (std::size_t index{ 0U }; index < taskCount; ++index)
    {
        REQUIRE(graph.addCpuTask([&executed] { executed.fetch_add(1U, std::memory_order_relaxed); }).has_value());
    }
    REQUIRE(graph.finishTaskGraph());

    Atlas::WorkerpoolExecutor cpuExecutor{ 4U };
    Atlas::KahnScheduler scheduler{ graph, cpuExecutor, Atlas::Test::unusedVulkanDispatchExecutor };
    const Atlas::SchedulerResult result{ scheduler.execute() };

    REQUIRE(result.status == Atlas::SchedulerStatus::Success);
    REQUIRE(result.executedTaskCount == taskCount);
    REQUIRE(executed.load(std::memory_order_relaxed) == taskCount);
}

TEST_CASE("TaskGraph finalises and executes a large sparse dependency chain", "[UNIT][STRESS]")
{
    constexpr std::size_t taskCount{ 4'096U };
    Atlas::TaskGraph graph;
    std::vector<Atlas::TaskHandle> handles;
    handles.reserve(taskCount);
    for (std::size_t index{ 0U }; index < taskCount; ++index)
    {
        const std::optional<Atlas::TaskHandle> handle{ graph.addCpuTask([] {}) };
        REQUIRE(handle.has_value());
        handles.emplace_back(handle.value());
        if (index != 0U)
        {
            REQUIRE(graph.addDependency(handles.at(index), handles.at(index - 1U)));
        }
    }
    REQUIRE(graph.finishTaskGraph());

    Atlas::SynchronousCpuExecutor cpuExecutor;
    Atlas::KahnScheduler scheduler{ graph, cpuExecutor, Atlas::Test::unusedVulkanDispatchExecutor };
    const Atlas::SchedulerResult result{ scheduler.execute() };
    REQUIRE(result.status == Atlas::SchedulerStatus::Success);
    REQUIRE(result.executedTaskCount == taskCount);
}
