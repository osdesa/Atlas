#include "../../support/UnusedVulkanDispatchExecutor.h"
#include "../../support/VulkanTestFactory.h"
#include "atlas/Executor/CpuExecutor.h"
#include "atlas/Executor/SynchronousCpuExecutor.h"
#include "atlas/Executor/VulkanDispatchExecutor.h"
#include "atlas/Scheduler/KahnScheduler.h"
#include "atlas/Tasking/TaskGraph.h"

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdint>
#include <exception>
#include <future>
#include <latch>
#include <optional>
#include <semaphore>
#include <stdexcept>
#include <utility>

/**
 * @file KahnSchedulerCancellation_tests.cpp
 * @brief Verifies fail-stop cancellation and its concurrency boundaries.
 */

namespace
{
    Atlas::TaskHandle requireHandle(const std::optional<Atlas::TaskHandle>& handle)
    {
        REQUIRE(handle.has_value());
        return handle.value();
    }

    Atlas::SlicedVulkanDispatch slicedDispatch(const std::uint32_t logicalX)
    {
        Atlas::Testing::VulkanTestFactory::Resources resources{ Atlas::Testing::VulkanTestFactory::resources() };
        Atlas::VulkanDispatch logicalDispatch{ resources.pipeline,
                                               { { 0U, resources.buffers.front(), Atlas::BufferAccess::ReadWrite } },
                                               { logicalX, 1U, 1U } };
        return Atlas::SlicedVulkanDispatch{ std::move(logicalDispatch), { 1U, 1U, 1U } };
    }

    class ControlledCpuExecutor final : public Atlas::CpuExecutor
    {
      public:
        ControlledCpuExecutor() : CpuExecutor{ 1U } {}

        bool submit(const Atlas::TaskHandle handle, Atlas::TaskFunction)
        {
            submittedHandle = handle;
            submissionCount.fetch_add(1U);
            submitted.count_down();
            return true;
        }

        bool submit(const Atlas::TaskHandle handle, Atlas::TaskFunction, Atlas::CompletionChannel& completionChannel) override
        {
            submittedHandle = handle;
            channel = &completionChannel;
            submissionCount.fetch_add(1U);
            submitted.count_down();
            return true;
        }

        std::optional<Atlas::TaskCompletion> waitForCompletion()
        {
            if (standaloneCompletionReturned)
            {
                return std::nullopt;
            }
            completionReady.acquire();
            standaloneCompletionReturned = true;
            return Atlas::TaskCompletion{ submittedHandle.value(), completionException, completionDuration,
                                          Atlas::ExecutionResource::CPU };
        }

        void shutdown() noexcept override {}

        void waitUntilSubmitted()
        {
            submitted.wait();
        }

        void complete(std::exception_ptr exception = nullptr,
                      const std::chrono::microseconds duration = std::chrono::microseconds{ 3 })
        {
            completionException = std::move(exception);
            completionDuration = duration;
            if (channel == nullptr)
            {
                completionReady.release();
                return;
            }
            channel->publish(Atlas::TaskCompletion{ submittedHandle.value(), completionException, completionDuration,
                                                    Atlas::ExecutionResource::CPU });
        }

        std::atomic<std::size_t> submissionCount{ 0U };

      private:
        std::latch submitted{ 1U };
        std::binary_semaphore completionReady{ 0 };
        Atlas::CompletionChannel* channel{ nullptr };
        std::optional<Atlas::TaskHandle> submittedHandle;
        std::exception_ptr completionException{ nullptr };
        std::chrono::microseconds completionDuration{ 0 };
        bool standaloneCompletionReturned{ false };
    };

    class ControlledVulkanDispatchExecutor final : public Atlas::VulkanDispatchExecutor
    {
      public:
        ControlledVulkanDispatchExecutor() : VulkanDispatchExecutor{ 1U } {}

        bool submit(Atlas::TaskHandle, Atlas::VulkanDispatch)
        {
            return false;
        }

        bool submit(const Atlas::TaskHandle handle, Atlas::VulkanDispatch dispatch,
                    Atlas::CompletionChannel& completionChannel) override
        {
            submittedHandle = handle;
            submittedWorkUnitIndex = dispatch.workUnitIndex();
            channel = &completionChannel;
            submissionCount.fetch_add(1U);
            submitted.count_down();
            return true;
        }

        std::optional<Atlas::TaskCompletion> waitForCompletion()
        {
            return std::nullopt;
        }

        void shutdown() noexcept override {}

        void waitUntilSubmitted()
        {
            submitted.wait();
        }

        void complete(std::exception_ptr exception = nullptr,
                      const std::chrono::microseconds duration = std::chrono::microseconds{ 5 })
        {
            REQUIRE(channel != nullptr);
            channel->publish(Atlas::TaskCompletion{ submittedHandle.value(), std::move(exception), duration,
                                                    Atlas::ExecutionResource::GPU, submittedWorkUnitIndex });
        }

        void signalProducerFailure()
        {
            REQUIRE(channel != nullptr);
            channel->signalProducerFailure(Atlas::ExecutionResource::GPU);
        }

        std::atomic<std::size_t> submissionCount{ 0U };

      private:
        std::latch submitted{ 1U };
        Atlas::CompletionChannel* channel{ nullptr };
        std::optional<Atlas::TaskHandle> submittedHandle;
        std::size_t submittedWorkUnitIndex{ 0U };
    };
} // namespace

TEST_CASE("KahnScheduler validates cancellation requests", "[UNIT]")
{
    Atlas::TaskGraph graph;
    Atlas::TaskGraph otherGraph;
    const Atlas::TaskHandle target{ requireHandle(graph.addCpuTask([] {})) };
    const Atlas::TaskHandle crossGraph{ requireHandle(otherGraph.addCpuTask([] {})) };
    const Atlas::TaskHandle unknown{ Atlas::TaskId{ 99U }, graph.getGraphID() };
    REQUIRE(graph.finishTaskGraph());

    Atlas::SynchronousCpuExecutor executor;
    Atlas::KahnScheduler scheduler{ graph, executor, Atlas::Test::unusedVulkanDispatchExecutor };

    REQUIRE_FALSE(scheduler.requestCancellation(crossGraph));
    REQUIRE_FALSE(scheduler.requestCancellation(unknown));
    REQUIRE(scheduler.requestCancellation(target));
    REQUIRE_FALSE(scheduler.requestCancellation(target));

    const Atlas::SchedulerResult result{ scheduler.execute() };
    REQUIRE(result.status == Atlas::SchedulerStatus::Cancelled);
    REQUIRE(graph.snapshotTask(target).value().executionInfo.state == Atlas::TaskState::Cancelled);
    REQUIRE_FALSE(scheduler.requestCancellation(target));
}

TEST_CASE("KahnScheduler cancels ready and blocked work before submission", "[UNIT]")
{
    Atlas::TaskGraph graph;
    bool cpuExecuted{ false };
    const Atlas::TaskHandle cpu{ requireHandle(graph.addCpuTask([&cpuExecuted] { cpuExecuted = true; })) };
    const Atlas::TaskHandle gpu{ requireHandle(graph.addGpuTask(Atlas::Testing::VulkanTestFactory::dispatch())) };
    const Atlas::TaskHandle sliced{ requireHandle(graph.addGpuTask(slicedDispatch(2U))) };
    const Atlas::TaskHandle blocked{ requireHandle(graph.addCpuTask([&cpuExecuted] { cpuExecuted = true; })) };
    REQUIRE(graph.addDependency(blocked, cpu));
    REQUIRE(graph.finishTaskGraph());

    Atlas::SynchronousCpuExecutor cpuExecutor;
    ControlledVulkanDispatchExecutor gpuExecutor;
    Atlas::KahnScheduler scheduler{ graph, cpuExecutor, gpuExecutor };
    REQUIRE(scheduler.requestCancellation(blocked));
    REQUIRE(scheduler.requestCancellation(sliced));
    REQUIRE(scheduler.requestCancellation(gpu));
    REQUIRE(scheduler.requestCancellation(cpu));

    const Atlas::SchedulerResult result{ scheduler.execute() };

    REQUIRE(result.status == Atlas::SchedulerStatus::Cancelled);
    REQUIRE(result.executedTaskCount == 0U);
    REQUIRE_FALSE(cpuExecuted);
    REQUIRE(gpuExecutor.submissionCount.load() == 0U);
    REQUIRE(graph.snapshotTask(cpu).value().executionInfo.state == Atlas::TaskState::Cancelled);
    REQUIRE(graph.snapshotTask(gpu).value().executionInfo.state == Atlas::TaskState::Cancelled);
    REQUIRE(graph.snapshotTask(sliced).value().executionInfo.state == Atlas::TaskState::Cancelled);
    REQUIRE(graph.snapshotTask(blocked).value().executionInfo.state == Atlas::TaskState::Cancelled);
}

TEST_CASE("KahnScheduler lets running CPU completion win cancellation", "[UNIT][CONCURRENCY]")
{
    Atlas::TaskGraph graph;
    const Atlas::TaskHandle task{ requireHandle(graph.addCpuTask([] {})) };
    REQUIRE(graph.finishTaskGraph());

    ControlledCpuExecutor executor;
    Atlas::KahnScheduler scheduler{ graph, executor, Atlas::Test::unusedVulkanDispatchExecutor };
    auto execution{ std::async(std::launch::async, [&scheduler] { return scheduler.execute(); }) };

    executor.waitUntilSubmitted();
    REQUIRE(scheduler.requestCancellation(task));
    executor.complete();
    const Atlas::SchedulerResult result{ execution.get() };

    REQUIRE(result.status == Atlas::SchedulerStatus::Success);
    REQUIRE(result.executedTaskCount == 1U);
    REQUIRE(graph.snapshotTask(task).value().executionInfo.state == Atlas::TaskState::Success);
}

TEST_CASE("KahnScheduler lets running ordinary GPU completion win cancellation", "[UNIT][CONCURRENCY]")
{
    Atlas::TaskGraph graph;
    const Atlas::TaskHandle task{ requireHandle(graph.addGpuTask(Atlas::Testing::VulkanTestFactory::dispatch())) };
    REQUIRE(graph.finishTaskGraph());

    Atlas::SynchronousCpuExecutor cpuExecutor;
    ControlledVulkanDispatchExecutor gpuExecutor;
    Atlas::KahnScheduler scheduler{ graph, cpuExecutor, gpuExecutor };
    auto execution{ std::async(std::launch::async, [&scheduler] { return scheduler.execute(); }) };

    gpuExecutor.waitUntilSubmitted();
    REQUIRE(scheduler.requestCancellation(task));
    gpuExecutor.complete();
    const Atlas::SchedulerResult result{ execution.get() };

    REQUIRE(result.status == Atlas::SchedulerStatus::Success);
    REQUIRE(result.executedTaskCount == 1U);
    REQUIRE(graph.snapshotTask(task).value().executionInfo.state == Atlas::TaskState::Success);
}

TEST_CASE("KahnScheduler cancels running sliced GPU work at a completed boundary", "[UNIT][CONCURRENCY]")
{
    Atlas::TaskGraph graph;
    const Atlas::TaskHandle task{ requireHandle(graph.addGpuTask(slicedDispatch(2U))) };
    const Atlas::TaskHandle dependent{ requireHandle(graph.addCpuTask([] {})) };
    REQUIRE(graph.addDependency(dependent, task));
    REQUIRE(graph.finishTaskGraph());

    Atlas::SynchronousCpuExecutor cpuExecutor;
    ControlledVulkanDispatchExecutor gpuExecutor;
    Atlas::KahnScheduler scheduler{ graph, cpuExecutor, gpuExecutor };
    auto execution{ std::async(std::launch::async, [&scheduler] { return scheduler.execute(); }) };

    gpuExecutor.waitUntilSubmitted();
    REQUIRE(scheduler.requestCancellation(task));
    gpuExecutor.complete(nullptr, std::chrono::microseconds{ 7 });
    const Atlas::SchedulerResult result{ execution.get() };

    REQUIRE(result.status == Atlas::SchedulerStatus::Cancelled);
    REQUIRE(result.executedTaskCount == 0U);
    const Atlas::TaskExecutionInfo progress{ graph.snapshotTask(task).value().executionInfo };
    REQUIRE(progress.state == Atlas::TaskState::Cancelled);
    REQUIRE(progress.completedWorkUnitCount == 1U);
    REQUIRE(progress.totalWorkUnitCount == 2U);
    REQUIRE(progress.executionDuration == std::chrono::microseconds{ 7 });
    REQUIRE(gpuExecutor.submissionCount.load() == 1U);
    REQUIRE(graph.snapshotTask(dependent).value().executionInfo.state == Atlas::TaskState::Blocked);
}

TEST_CASE("KahnScheduler lets a final sliced GPU work unit win cancellation", "[UNIT][CONCURRENCY]")
{
    Atlas::TaskGraph graph;
    const Atlas::TaskHandle task{ requireHandle(graph.addGpuTask(slicedDispatch(1U))) };
    REQUIRE(graph.finishTaskGraph());

    Atlas::SynchronousCpuExecutor cpuExecutor;
    ControlledVulkanDispatchExecutor gpuExecutor;
    Atlas::KahnScheduler scheduler{ graph, cpuExecutor, gpuExecutor };
    auto execution{ std::async(std::launch::async, [&scheduler] { return scheduler.execute(); }) };

    gpuExecutor.waitUntilSubmitted();
    REQUIRE(scheduler.requestCancellation(task));
    gpuExecutor.complete();
    const Atlas::SchedulerResult result{ execution.get() };

    REQUIRE(result.status == Atlas::SchedulerStatus::Success);
    REQUIRE(result.executedTaskCount == 1U);
    REQUIRE(graph.snapshotTask(task).value().executionInfo.state == Atlas::TaskState::Success);
    REQUIRE(graph.snapshotTask(task).value().executionInfo.completedWorkUnitCount == 1U);
}

TEST_CASE("KahnScheduler drains accepted work after sliced cancellation", "[UNIT][CONCURRENCY]")
{
    Atlas::TaskGraph graph;
    const Atlas::TaskHandle cpu{ requireHandle(graph.addCpuTask([] {})) };
    const Atlas::TaskHandle sliced{ requireHandle(graph.addGpuTask(slicedDispatch(2U))) };
    const Atlas::TaskHandle dependent{ requireHandle(graph.addCpuTask([] {})) };
    REQUIRE(graph.addDependency(dependent, sliced));
    REQUIRE(graph.finishTaskGraph());

    ControlledCpuExecutor cpuExecutor;
    ControlledVulkanDispatchExecutor gpuExecutor;
    Atlas::KahnScheduler scheduler{ graph, cpuExecutor, gpuExecutor };
    auto execution{ std::async(std::launch::async, [&scheduler] { return scheduler.execute(); }) };

    cpuExecutor.waitUntilSubmitted();
    gpuExecutor.waitUntilSubmitted();
    REQUIRE(scheduler.requestCancellation(sliced));
    gpuExecutor.complete();
    cpuExecutor.complete();
    const Atlas::SchedulerResult result{ execution.get() };

    REQUIRE(result.status == Atlas::SchedulerStatus::Cancelled);
    REQUIRE(result.executedTaskCount == 1U);
    REQUIRE(graph.snapshotTask(cpu).value().executionInfo.state == Atlas::TaskState::Success);
    REQUIRE(graph.snapshotTask(sliced).value().executionInfo.state == Atlas::TaskState::Cancelled);
    REQUIRE(graph.snapshotTask(dependent).value().executionInfo.state == Atlas::TaskState::Blocked);
}

TEST_CASE("KahnScheduler applies task failure before cancellation", "[UNIT][CONCURRENCY]")
{
    Atlas::TaskGraph graph;
    const Atlas::TaskHandle cpu{ requireHandle(graph.addCpuTask([] {})) };
    const Atlas::TaskHandle sliced{ requireHandle(graph.addGpuTask(slicedDispatch(2U))) };
    REQUIRE(graph.finishTaskGraph());
    const std::exception_ptr taskFailure{ std::make_exception_ptr(std::runtime_error{ "CPU failed while draining" }) };

    ControlledCpuExecutor cpuExecutor;
    ControlledVulkanDispatchExecutor gpuExecutor;
    Atlas::KahnScheduler scheduler{ graph, cpuExecutor, gpuExecutor };
    auto execution{ std::async(std::launch::async, [&scheduler] { return scheduler.execute(); }) };

    cpuExecutor.waitUntilSubmitted();
    gpuExecutor.waitUntilSubmitted();
    REQUIRE(scheduler.requestCancellation(sliced));
    gpuExecutor.complete();
    cpuExecutor.complete(taskFailure);
    const Atlas::SchedulerResult result{ execution.get() };

    REQUIRE(result.status == Atlas::SchedulerStatus::TaskFailed);
    REQUIRE(result.exception == taskFailure);
    REQUIRE(graph.snapshotTask(cpu).value().executionInfo.state == Atlas::TaskState::Failure);
    REQUIRE(graph.snapshotTask(sliced).value().executionInfo.state == Atlas::TaskState::Cancelled);
}

TEST_CASE("KahnScheduler applies executor failure before task failure and cancellation", "[UNIT][CONCURRENCY]")
{
    Atlas::TaskGraph graph;
    const Atlas::TaskHandle cpu{ requireHandle(graph.addCpuTask([] {})) };
    const Atlas::TaskHandle sliced{ requireHandle(graph.addGpuTask(slicedDispatch(2U))) };
    REQUIRE(graph.finishTaskGraph());
    const std::exception_ptr taskFailure{ std::make_exception_ptr(std::runtime_error{ "CPU failed while draining" }) };

    ControlledCpuExecutor cpuExecutor;
    ControlledVulkanDispatchExecutor gpuExecutor;
    Atlas::KahnScheduler scheduler{ graph, cpuExecutor, gpuExecutor };
    auto execution{ std::async(std::launch::async, [&scheduler] { return scheduler.execute(); }) };

    cpuExecutor.waitUntilSubmitted();
    gpuExecutor.waitUntilSubmitted();
    REQUIRE(scheduler.requestCancellation(sliced));
    gpuExecutor.complete();
    cpuExecutor.complete(taskFailure);
    gpuExecutor.signalProducerFailure();
    const Atlas::SchedulerResult result{ execution.get() };

    REQUIRE(result.status == Atlas::SchedulerStatus::ExecutorUnavailable);
    REQUIRE(result.exception == taskFailure);
    REQUIRE(graph.snapshotTask(cpu).value().executionInfo.state == Atlas::TaskState::Failure);
    REQUIRE(graph.snapshotTask(sliced).value().executionInfo.state == Atlas::TaskState::Cancelled);
}
