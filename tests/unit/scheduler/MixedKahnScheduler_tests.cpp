#include "../../support/UnusedVulkanDispatchExecutor.h"
#include "../../support/VulkanTestFactory.h"
#include "atlas/Executor/SynchronousCpuExecutor.h"
#include "atlas/Executor/VulkanDispatchExecutor.h"
#include "atlas/Executor/WorkerpoolExecutor.h"
#include "atlas/Scheduler/KahnScheduler.h"
#include "atlas/Scheduler/RoundRobinSchedulingPolicy.h"
#include "atlas/Scheduler/StaticPrioritySchedulingPolicy.h"
#include "atlas/Tasking/TaskGraph.h"

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <exception>
#include <functional>
#include <optional>
#include <semaphore>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <vector>

namespace
{
    class ImmediateVulkanDispatchExecutor final : public Atlas::VulkanDispatchExecutor
    {
      public:
        explicit ImmediateVulkanDispatchExecutor(const Atlas::ExecutionResource reportedResource = Atlas::ExecutionResource::GPU,
                                                 std::exception_ptr outcome = nullptr,
                                                 std::function<void(Atlas::TaskHandle)> submissionCallback = {})
            : VulkanDispatchExecutor{ 1U }, completionResource{ reportedResource }, completionException{ std::move(outcome) },
              onSubmission{ std::move(submissionCallback) }
        {
        }

        bool submit(const Atlas::TaskHandle handle, Atlas::VulkanDispatch dispatch)
        {
            if (!accepting)
            {
                return false;
            }
            standaloneCompletion = Atlas::TaskCompletion{ handle, completionException, std::chrono::microseconds{ 1 },
                                                          completionResource, dispatch.workUnitIndex() };
            submitted.emplace_back(handle);
            submittedWorkUnits.emplace_back(handle, dispatch.workUnitIndex());
            if (onSubmission)
            {
                onSubmission(handle);
            }
            return true;
        }

        bool submit(const Atlas::TaskHandle handle, Atlas::VulkanDispatch dispatch, Atlas::CompletionChannel& channel) override
        {
            if (!accepting)
            {
                return false;
            }
            submitted.emplace_back(handle);
            submittedWorkUnits.emplace_back(handle, dispatch.workUnitIndex());
            if (onSubmission)
            {
                onSubmission(handle);
            }
            channel.publish(Atlas::TaskCompletion{ handle, completionException, std::chrono::microseconds{ 1 }, completionResource,
                                                   dispatch.workUnitIndex() });
            return true;
        }

        std::optional<Atlas::TaskCompletion> waitForCompletion()
        {
            return std::exchange(standaloneCompletion, std::nullopt);
        }

        void shutdown() noexcept override
        {
            accepting = false;
        }

        std::vector<Atlas::TaskHandle> submitted;
        std::vector<std::pair<Atlas::TaskHandle, std::size_t>> submittedWorkUnits;

      private:
        Atlas::ExecutionResource completionResource;
        std::exception_ptr completionException;
        std::function<void(Atlas::TaskHandle)> onSubmission;
        std::optional<Atlas::TaskCompletion> standaloneCompletion;
        bool accepting{ true };
    };

    struct ScriptedGpuOutcome
    {
        bool accepted{ true };
        std::exception_ptr exception{ nullptr };
        std::chrono::microseconds duration{ 1 };
        std::vector<std::size_t> reportedWorkUnitIndices;
    };

    class ScriptedVulkanDispatchExecutor final : public Atlas::VulkanDispatchExecutor
    {
      public:
        explicit ScriptedVulkanDispatchExecutor(std::vector<ScriptedGpuOutcome> outcomes,
                                                std::function<void(Atlas::TaskHandle, std::size_t)> beforeCompletionCallback = {})
            : VulkanDispatchExecutor{ 1U }, scriptedOutcomes{ std::move(outcomes) },
              beforeCompletion{ std::move(beforeCompletionCallback) }
        {
        }

        bool submit(Atlas::TaskHandle, Atlas::VulkanDispatch)
        {
            return false;
        }

        bool submit(const Atlas::TaskHandle handle, Atlas::VulkanDispatch dispatch, Atlas::CompletionChannel& channel) override
        {
            const std::size_t attempt{ submissionAttempts++ };
            submittedWorkUnits.emplace_back(handle, dispatch.workUnitIndex());
            if (attempt >= scriptedOutcomes.size() || !scriptedOutcomes.at(attempt).accepted)
            {
                return false;
            }

            const ScriptedGpuOutcome& outcome{ scriptedOutcomes.at(attempt) };
            if (beforeCompletion)
            {
                beforeCompletion(handle, dispatch.workUnitIndex());
            }
            if (outcome.reportedWorkUnitIndices.empty())
            {
                channel.publish(Atlas::TaskCompletion{ handle, outcome.exception, outcome.duration, Atlas::ExecutionResource::GPU,
                                                       dispatch.workUnitIndex() });
            }
            else
            {
                for (const std::size_t reportedIndex : outcome.reportedWorkUnitIndices)
                {
                    channel.publish(Atlas::TaskCompletion{ handle, outcome.exception, outcome.duration, Atlas::ExecutionResource::GPU,
                                                           reportedIndex });
                }
            }
            return true;
        }

        std::optional<Atlas::TaskCompletion> waitForCompletion()
        {
            return std::nullopt;
        }

        void shutdown() noexcept override {}

        std::vector<std::pair<Atlas::TaskHandle, std::size_t>> submittedWorkUnits;

      private:
        std::vector<ScriptedGpuOutcome> scriptedOutcomes;
        std::function<void(Atlas::TaskHandle, std::size_t)> beforeCompletion;
        std::size_t submissionAttempts{ 0U };
    };

    class DeferredCpuExecutor final : public Atlas::CpuExecutor
    {
      public:
        DeferredCpuExecutor() : CpuExecutor{ 1U } {}

        bool submit(Atlas::TaskHandle, Atlas::TaskFunction)
        {
            return false;
        }

        bool submit(const Atlas::TaskHandle handle, Atlas::TaskFunction, Atlas::CompletionChannel& channel) override
        {
            if (pendingHandle.has_value())
            {
                return false;
            }
            pendingHandle = handle;
            pendingChannel = &channel;
            return true;
        }

        bool publishPendingCompletion()
        {
            if (!pendingHandle.has_value() || pendingChannel == nullptr)
            {
                return false;
            }
            const bool published{ pendingChannel->publish(Atlas::TaskCompletion{
                pendingHandle.value(), nullptr, std::chrono::microseconds{ 1 }, Atlas::ExecutionResource::CPU }) };
            pendingHandle.reset();
            pendingChannel = nullptr;
            return published;
        }

        std::optional<Atlas::TaskCompletion> waitForCompletion()
        {
            return std::nullopt;
        }

        void shutdown() noexcept override {}

      private:
        std::optional<Atlas::TaskHandle> pendingHandle;
        Atlas::CompletionChannel* pendingChannel{ nullptr };
    };

    class FailingGpuProducer final : public Atlas::VulkanDispatchExecutor
    {
      public:
        FailingGpuProducer() : VulkanDispatchExecutor{ 1U } {}

        bool submit(Atlas::TaskHandle, Atlas::VulkanDispatch)
        {
            return false;
        }

        bool submit(Atlas::TaskHandle, Atlas::VulkanDispatch, Atlas::CompletionChannel& channel) override
        {
            channel.signalProducerFailure(Atlas::ExecutionResource::GPU);
            return true;
        }

        std::optional<Atlas::TaskCompletion> waitForCompletion()
        {
            return std::nullopt;
        }

        void shutdown() noexcept override {}
    };

    class ClosingGpuProducer final : public Atlas::VulkanDispatchExecutor
    {
      public:
        ClosingGpuProducer() : VulkanDispatchExecutor{ 1U } {}

        bool submit(Atlas::TaskHandle, Atlas::VulkanDispatch)
        {
            return false;
        }

        bool submit(Atlas::TaskHandle, Atlas::VulkanDispatch, Atlas::CompletionChannel& channel) override
        {
            channel.close();
            return true;
        }

        std::optional<Atlas::TaskCompletion> waitForCompletion()
        {
            return std::nullopt;
        }

        void shutdown() noexcept override {}
    };

    class ZeroCapacityCpuExecutor final : public Atlas::CpuExecutor
    {
      public:
        ZeroCapacityCpuExecutor() : CpuExecutor{ 0U } {}

        bool submit(Atlas::TaskHandle, Atlas::TaskFunction)
        {
            return false;
        }

        bool submit(Atlas::TaskHandle, Atlas::TaskFunction, Atlas::CompletionChannel&) override
        {
            return false;
        }

        std::optional<Atlas::TaskCompletion> waitForCompletion()
        {
            return std::nullopt;
        }

        void shutdown() noexcept override {}
    };

    class ZeroCapacityVulkanDispatchExecutor final : public Atlas::VulkanDispatchExecutor
    {
      public:
        ZeroCapacityVulkanDispatchExecutor() : VulkanDispatchExecutor{ 0U } {}

        bool submit(Atlas::TaskHandle, Atlas::VulkanDispatch)
        {
            return false;
        }

        bool submit(Atlas::TaskHandle, Atlas::VulkanDispatch, Atlas::CompletionChannel&) override
        {
            return false;
        }

        std::optional<Atlas::TaskCompletion> waitForCompletion()
        {
            return std::nullopt;
        }

        void shutdown() noexcept override {}
    };

    Atlas::TaskHandle requireHandle(const std::optional<Atlas::TaskHandle>& handle)
    {
        REQUIRE(handle.has_value());
        return handle.value();
    }

    Atlas::SlicedVulkanDispatch slicedDispatch(const std::uint32_t logicalX, const std::uint32_t sliceX = 1U)
    {
        Atlas::Testing::VulkanTestFactory::Resources resources{ Atlas::Testing::VulkanTestFactory::resources() };
        Atlas::VulkanDispatch logicalDispatch{ resources.pipeline,
                                               { { 0U, resources.buffers.front(), Atlas::BufferAccess::ReadWrite } },
                                               { logicalX, 1U, 1U } };
        return Atlas::SlicedVulkanDispatch{ std::move(logicalDispatch), { sliceX, 1U, 1U } };
    }
} // namespace

TEST_CASE("KahnScheduler submits sliced GPU work in order and releases dependants after the final unit", "[UNIT]")
{
    Atlas::TaskGraph graph;
    bool dependentExecuted{ false };
    const Atlas::TaskHandle sliced{ requireHandle(graph.addGpuTask(slicedDispatch(3U))) };
    const Atlas::TaskHandle dependent{ requireHandle(graph.addCpuTask([&dependentExecuted] { dependentExecuted = true; })) };
    REQUIRE(graph.addDependency(dependent, sliced));
    REQUIRE(graph.finishTaskGraph());

    Atlas::SynchronousCpuExecutor cpuExecutor;
    ScriptedVulkanDispatchExecutor gpuExecutor{ { { true, nullptr, std::chrono::microseconds{ 2 }, {} },
                                                  { true, nullptr, std::chrono::microseconds{ 3 }, {} },
                                                  { true, nullptr, std::chrono::microseconds{ 5 }, {} } } };
    Atlas::KahnScheduler scheduler{ graph, cpuExecutor, gpuExecutor };
    const Atlas::SchedulerResult result{ scheduler.execute() };

    REQUIRE(result.status == Atlas::SchedulerStatus::Success);
    REQUIRE(result.executedTaskCount == 2U);
    REQUIRE(dependentExecuted);
    REQUIRE(gpuExecutor.submittedWorkUnits ==
            std::vector<std::pair<Atlas::TaskHandle, std::size_t>>{ { sliced, 0U }, { sliced, 1U }, { sliced, 2U } });
    const Atlas::TaskExecutionInfo& progress{ graph.findTask(sliced).value()->executionInfo };
    REQUIRE(progress.state == Atlas::TaskState::Success);
    REQUIRE(progress.completedWorkUnitCount == 3U);
    REQUIRE(progress.totalWorkUnitCount == 3U);
    REQUIRE(progress.executionDuration == std::chrono::microseconds{ 10 });
    REQUIRE(progress.responseDuration.has_value());
    REQUIRE(result.immediateSliceSwitchCount == 2U);
    REQUIRE(result.immediateSliceSwitchDuration <= result.schedulerActiveDuration);
}

TEST_CASE("KahnScheduler interleaves sliced GPU tasks at work-unit boundaries", "[UNIT]")
{
    Atlas::TaskGraph graph;
    const Atlas::TaskHandle first{ requireHandle(graph.addGpuTask(slicedDispatch(2U))) };
    const Atlas::TaskHandle second{ requireHandle(graph.addGpuTask(slicedDispatch(2U))) };
    REQUIRE(graph.finishTaskGraph());

    Atlas::SynchronousCpuExecutor cpuExecutor;
    ScriptedVulkanDispatchExecutor gpuExecutor{ std::vector<ScriptedGpuOutcome>(4U) };
    Atlas::KahnScheduler scheduler{ graph, cpuExecutor, gpuExecutor };
    const Atlas::SchedulerResult result{ scheduler.execute() };

    REQUIRE(result.status == Atlas::SchedulerStatus::Success);
    REQUIRE(result.executedTaskCount == 2U);
    REQUIRE(gpuExecutor.submittedWorkUnits ==
            std::vector<std::pair<Atlas::TaskHandle, std::size_t>>{ { first, 0U }, { second, 0U }, { first, 1U }, { second, 1U } });
    const Atlas::TaskExecutionInfo& firstInfo{ graph.findTask(first).value()->executionInfo };
    const Atlas::TaskExecutionInfo& secondInfo{ graph.findTask(second).value()->executionInfo };
    REQUIRE(firstInfo.selectionBypassCount == 1U);
    REQUIRE(secondInfo.selectionBypassCount == 2U);
    REQUIRE(firstInfo.readyWaitDuration >= std::chrono::microseconds{ 0 });
    REQUIRE(secondInfo.readyWaitDuration >= std::chrono::microseconds{ 0 });
    REQUIRE(firstInfo.readyWaitDuration <= result.executionTime);
    REQUIRE(secondInfo.readyWaitDuration <= result.executionTime);
    REQUIRE(firstInfo.responseDuration.has_value());
    REQUIRE(secondInfo.responseDuration.has_value());
    REQUIRE(result.immediateSliceSwitchCount == 0U);
}

TEST_CASE("KahnScheduler applies a round-robin work-unit quantum", "[UNIT]")
{
    Atlas::TaskGraph graph;
    const Atlas::TaskHandle first{ requireHandle(graph.addGpuTask(slicedDispatch(4U))) };
    const Atlas::TaskHandle second{ requireHandle(graph.addGpuTask(slicedDispatch(2U))) };
    REQUIRE(graph.finishTaskGraph());

    Atlas::SynchronousCpuExecutor cpuExecutor;
    ScriptedVulkanDispatchExecutor gpuExecutor{ std::vector<ScriptedGpuOutcome>(6U) };
    Atlas::RoundRobinSchedulingPolicy policy{ 2U };
    Atlas::KahnScheduler scheduler{ graph, cpuExecutor, gpuExecutor, policy };
    const Atlas::SchedulerResult result{ scheduler.execute() };

    REQUIRE(result.status == Atlas::SchedulerStatus::Success);
    REQUIRE(gpuExecutor.submittedWorkUnits ==
            std::vector<std::pair<Atlas::TaskHandle, std::size_t>>{
                { first, 0U }, { first, 1U }, { second, 0U }, { second, 1U }, { first, 2U }, { first, 3U } });
}

TEST_CASE("KahnScheduler applies stable static priority at sliced boundaries", "[UNIT]")
{
    Atlas::TaskGraph graph;
    const Atlas::TaskHandle first{ requireHandle(
        graph.addGpuTask(slicedDispatch(4U), Atlas::TaskOptions{ "Lower priority", Atlas::ExecutionResource::GPU, 8U })) };
    const Atlas::TaskHandle second{ requireHandle(
        graph.addGpuTask(slicedDispatch(2U), Atlas::TaskOptions{ "Higher priority", Atlas::ExecutionResource::GPU, 2U })) };
    REQUIRE(graph.finishTaskGraph());

    Atlas::SynchronousCpuExecutor cpuExecutor;
    ScriptedVulkanDispatchExecutor gpuExecutor{ std::vector<ScriptedGpuOutcome>(6U) };
    Atlas::StaticPrioritySchedulingPolicy policy;
    Atlas::KahnScheduler scheduler{ graph, cpuExecutor, gpuExecutor, policy };
    const Atlas::SchedulerResult result{ scheduler.execute() };

    REQUIRE(result.status == Atlas::SchedulerStatus::Success);
    REQUIRE(gpuExecutor.submittedWorkUnits ==
            std::vector<std::pair<Atlas::TaskHandle, std::size_t>>{
                { second, 0U }, { second, 1U }, { first, 0U }, { first, 1U }, { first, 2U }, { first, 3U } });
}

TEST_CASE("KahnScheduler lets newly ready priority work intervene after an active GPU slice", "[UNIT]")
{
    Atlas::TaskGraph graph;
    const Atlas::TaskHandle prerequisite{ requireHandle(graph.addCpuTask([] {})) };
    const Atlas::TaskHandle lower{ requireHandle(
        graph.addGpuTask(slicedDispatch(2U), Atlas::TaskOptions{ "Lower priority", Atlas::ExecutionResource::GPU, 9U })) };
    const Atlas::TaskHandle higher{ requireHandle(
        graph.addGpuTask(slicedDispatch(1U), Atlas::TaskOptions{ "Higher priority", Atlas::ExecutionResource::GPU, 1U })) };
    REQUIRE(graph.addDependency(higher, prerequisite));
    REQUIRE(graph.finishTaskGraph());

    DeferredCpuExecutor cpuExecutor;
    bool lowerWasRunningWhenHigherBecameEligible{ false };
    ScriptedVulkanDispatchExecutor gpuExecutor{ std::vector<ScriptedGpuOutcome>(3U),
                                                [&](const Atlas::TaskHandle handle, const std::size_t workUnitIndex)
                                                {
                                                    if (handle == lower && workUnitIndex == 0U)
                                                    {
                                                        lowerWasRunningWhenHigherBecameEligible =
                                                            graph.findTask(lower).value()->executionInfo.state ==
                                                            Atlas::TaskState::Running;
                                                        REQUIRE(cpuExecutor.publishPendingCompletion());
                                                    }
                                                } };
    Atlas::StaticPrioritySchedulingPolicy policy;
    Atlas::KahnScheduler scheduler{ graph, cpuExecutor, gpuExecutor, policy };
    const Atlas::SchedulerResult result{ scheduler.execute() };

    REQUIRE(result.status == Atlas::SchedulerStatus::Success);
    REQUIRE(lowerWasRunningWhenHigherBecameEligible);
    REQUIRE(gpuExecutor.submittedWorkUnits ==
            std::vector<std::pair<Atlas::TaskHandle, std::size_t>>{ { lower, 0U }, { higher, 0U }, { lower, 1U } });
    const Atlas::TaskExecutionInfo& lowerInfo{ graph.findTask(lower).value()->executionInfo };
    REQUIRE(lowerInfo.completedWorkUnitCount == 2U);
    REQUIRE(lowerInfo.selectionBypassCount == 1U);
    REQUIRE(lowerInfo.readyWaitDuration >= std::chrono::microseconds{ 0 });
}

TEST_CASE("KahnScheduler measures a finite strict-priority backlog exactly", "[UNIT]")
{
    Atlas::TaskGraph graph;
    const Atlas::TaskHandle lower{ requireHandle(
        graph.addGpuTask(slicedDispatch(1U), Atlas::TaskOptions{ "Lower priority", Atlas::ExecutionResource::GPU, 9U })) };
    const Atlas::TaskHandle firstHigher{ requireHandle(
        graph.addGpuTask(slicedDispatch(1U), Atlas::TaskOptions{ "First higher", Atlas::ExecutionResource::GPU, 1U })) };
    const Atlas::TaskHandle secondHigher{ requireHandle(
        graph.addGpuTask(slicedDispatch(1U), Atlas::TaskOptions{ "Second higher", Atlas::ExecutionResource::GPU, 2U })) };
    const Atlas::TaskHandle thirdHigher{ requireHandle(
        graph.addGpuTask(slicedDispatch(1U), Atlas::TaskOptions{ "Third higher", Atlas::ExecutionResource::GPU, 3U })) };
    REQUIRE(graph.finishTaskGraph());

    Atlas::SynchronousCpuExecutor cpuExecutor;
    ScriptedVulkanDispatchExecutor gpuExecutor{ std::vector<ScriptedGpuOutcome>(4U) };
    Atlas::StaticPrioritySchedulingPolicy policy;
    Atlas::KahnScheduler scheduler{ graph, cpuExecutor, gpuExecutor, policy };
    const Atlas::SchedulerResult result{ scheduler.execute() };

    REQUIRE(result.status == Atlas::SchedulerStatus::Success);
    REQUIRE(gpuExecutor.submittedWorkUnits == std::vector<std::pair<Atlas::TaskHandle, std::size_t>>{
                                                  { firstHigher, 0U }, { secondHigher, 0U }, { thirdHigher, 0U }, { lower, 0U } });
    REQUIRE(graph.findTask(firstHigher).value()->executionInfo.selectionBypassCount == 0U);
    REQUIRE(graph.findTask(secondHigher).value()->executionInfo.selectionBypassCount == 1U);
    REQUIRE(graph.findTask(thirdHigher).value()->executionInfo.selectionBypassCount == 2U);
    REQUIRE(graph.findTask(lower).value()->executionInfo.selectionBypassCount == 3U);
}

TEST_CASE("KahnScheduler preserves sliced progress and duration when a later unit fails", "[UNIT]")
{
    Atlas::TaskGraph graph;
    const Atlas::TaskHandle sliced{ requireHandle(graph.addGpuTask(slicedDispatch(3U))) };
    const Atlas::TaskHandle dependent{ requireHandle(graph.addCpuTask([] {})) };
    REQUIRE(graph.addDependency(dependent, sliced));
    REQUIRE(graph.finishTaskGraph());
    const std::exception_ptr failure{ std::make_exception_ptr(std::runtime_error{ "second slice failed" }) };

    Atlas::SynchronousCpuExecutor cpuExecutor;
    ScriptedVulkanDispatchExecutor gpuExecutor{ { { true, nullptr, std::chrono::microseconds{ 7 }, {} },
                                                  { true, failure, std::chrono::microseconds{ 11 }, {} } } };
    Atlas::KahnScheduler scheduler{ graph, cpuExecutor, gpuExecutor };
    const Atlas::SchedulerResult result{ scheduler.execute() };

    REQUIRE(result.status == Atlas::SchedulerStatus::TaskFailed);
    REQUIRE(result.executedTaskCount == 0U);
    REQUIRE(result.exception == failure);
    const Atlas::TaskExecutionInfo& progress{ graph.findTask(sliced).value()->executionInfo };
    REQUIRE(progress.state == Atlas::TaskState::Failure);
    REQUIRE(progress.completedWorkUnitCount == 1U);
    REQUIRE(progress.totalWorkUnitCount == 3U);
    REQUIRE(progress.executionDuration == std::chrono::microseconds{ 18 });
    REQUIRE(graph.findTask(dependent).value()->executionInfo.state == Atlas::TaskState::Blocked);
}

TEST_CASE("KahnScheduler rejects mismatched sliced work-unit attribution", "[UNIT]")
{
    Atlas::TaskGraph graph;
    const Atlas::TaskHandle sliced{ requireHandle(graph.addGpuTask(slicedDispatch(3U))) };
    const Atlas::TaskHandle dependent{ requireHandle(graph.addCpuTask([] {})) };
    REQUIRE(graph.addDependency(dependent, sliced));
    REQUIRE(graph.finishTaskGraph());

    Atlas::SynchronousCpuExecutor cpuExecutor;
    ScriptedVulkanDispatchExecutor gpuExecutor{ { { true, nullptr, std::chrono::microseconds{ 3 }, { 1U } } } };
    Atlas::KahnScheduler scheduler{ graph, cpuExecutor, gpuExecutor };
    const Atlas::SchedulerResult result{ scheduler.execute() };

    REQUIRE(result.status == Atlas::SchedulerStatus::ExecutorUnavailable);
    REQUIRE(result.executedTaskCount == 0U);
    REQUIRE(graph.findTask(sliced).value()->executionInfo.state == Atlas::TaskState::Failure);
    REQUIRE(graph.findTask(sliced).value()->executionInfo.completedWorkUnitCount == 0U);
    REQUIRE(graph.findTask(dependent).value()->executionInfo.state == Atlas::TaskState::Blocked);
}

TEST_CASE("KahnScheduler rejects duplicate or stale sliced completions", "[UNIT]")
{
    Atlas::TaskGraph graph;
    const Atlas::TaskHandle sliced{ requireHandle(graph.addGpuTask(slicedDispatch(2U))) };
    const Atlas::TaskHandle dependent{ requireHandle(graph.addCpuTask([] {})) };
    REQUIRE(graph.addDependency(dependent, sliced));
    REQUIRE(graph.finishTaskGraph());

    Atlas::SynchronousCpuExecutor cpuExecutor;
    ScriptedVulkanDispatchExecutor gpuExecutor{ { { true, nullptr, std::chrono::microseconds{ 3 }, { 0U, 0U } } } };
    Atlas::KahnScheduler scheduler{ graph, cpuExecutor, gpuExecutor };
    const Atlas::SchedulerResult result{ scheduler.execute() };

    REQUIRE(result.status == Atlas::SchedulerStatus::ExecutorUnavailable);
    REQUIRE(result.executedTaskCount == 0U);
    REQUIRE(graph.findTask(sliced).value()->executionInfo.state == Atlas::TaskState::Paused);
    REQUIRE(graph.findTask(sliced).value()->executionInfo.completedWorkUnitCount == 1U);
    REQUIRE(graph.findTask(dependent).value()->executionInfo.state == Atlas::TaskState::Blocked);
}

TEST_CASE("KahnScheduler restores Paused when a later sliced submission is rejected", "[UNIT]")
{
    Atlas::TaskGraph graph;
    const Atlas::TaskHandle sliced{ requireHandle(graph.addGpuTask(slicedDispatch(2U))) };
    REQUIRE(graph.finishTaskGraph());

    Atlas::SynchronousCpuExecutor cpuExecutor;
    ScriptedVulkanDispatchExecutor gpuExecutor{ { { true, nullptr, std::chrono::microseconds{ 13 }, {} },
                                                  { false, nullptr, std::chrono::microseconds{ 1 }, {} } } };
    Atlas::KahnScheduler scheduler{ graph, cpuExecutor, gpuExecutor };
    const Atlas::SchedulerResult result{ scheduler.execute() };

    REQUIRE(result.status == Atlas::SchedulerStatus::ExecutorUnavailable);
    REQUIRE(result.executedTaskCount == 0U);
    const Atlas::TaskExecutionInfo& progress{ graph.findTask(sliced).value()->executionInfo };
    REQUIRE(progress.state == Atlas::TaskState::Paused);
    REQUIRE(progress.completedWorkUnitCount == 1U);
    REQUIRE(progress.executionDuration == std::chrono::microseconds{ 13 });
}

TEST_CASE("KahnScheduler executes a device-independent CPU GPU CPU chain", "[UNIT]")
{
    Atlas::TaskGraph graph;
    std::vector<int> executionOrder;
    const Atlas::TaskHandle prepare{ requireHandle(graph.addCpuTask([&executionOrder] { executionOrder.emplace_back(1); })) };
    const Atlas::TaskHandle compute{ requireHandle(graph.addGpuTask(Atlas::Testing::VulkanTestFactory::dispatch())) };
    const Atlas::TaskHandle verify{ requireHandle(graph.addCpuTask([&executionOrder] { executionOrder.emplace_back(3); })) };
    REQUIRE(graph.addDependency(compute, prepare));
    REQUIRE(graph.addDependency(verify, compute));
    REQUIRE(graph.finishTaskGraph());

    Atlas::SynchronousCpuExecutor cpuExecutor;
    ImmediateVulkanDispatchExecutor gpuExecutor;
    Atlas::KahnScheduler scheduler{ graph, cpuExecutor, gpuExecutor };
    const Atlas::SchedulerResult result{ scheduler.execute() };

    REQUIRE(result.status == Atlas::SchedulerStatus::Success);
    REQUIRE(result.executedTaskCount == 3U);
    REQUIRE(executionOrder == std::vector<int>{ 1, 3 });
    REQUIRE(gpuExecutor.submitted == std::vector<Atlas::TaskHandle>{ compute });
}

TEST_CASE("KahnScheduler requires a GPU executor for explicit GPU work", "[UNIT]")
{
    Atlas::TaskGraph graph;
    const Atlas::TaskHandle gpuTask{ requireHandle(graph.addGpuTask(Atlas::Testing::VulkanTestFactory::dispatch())) };
    REQUIRE(graph.finishTaskGraph());

    Atlas::SynchronousCpuExecutor cpuExecutor;
    Atlas::KahnScheduler scheduler{ graph, cpuExecutor, Atlas::Test::unusedVulkanDispatchExecutor };
    const Atlas::SchedulerResult result{ scheduler.execute() };

    REQUIRE(result.status == Atlas::SchedulerStatus::ExecutorUnavailable);
    REQUIRE(graph.findTask(gpuTask).value()->executionInfo.state == Atlas::TaskState::Ready);
}

TEST_CASE("KahnScheduler requires a GPU executor for blocked GPU work", "[UNIT]")
{
    Atlas::TaskGraph graph;
    const Atlas::TaskHandle cpuTask{ requireHandle(graph.addCpuTask([] {})) };
    const Atlas::TaskHandle gpuTask{ requireHandle(graph.addGpuTask(Atlas::Testing::VulkanTestFactory::dispatch())) };
    REQUIRE(graph.addDependency(gpuTask, cpuTask));
    REQUIRE(graph.finishTaskGraph());

    Atlas::SynchronousCpuExecutor cpuExecutor;
    Atlas::KahnScheduler scheduler{ graph, cpuExecutor, Atlas::Test::unusedVulkanDispatchExecutor };
    const Atlas::SchedulerResult result{ scheduler.execute() };

    REQUIRE(result.status == Atlas::SchedulerStatus::ExecutorUnavailable);
    REQUIRE(result.executedTaskCount == 1U);
    REQUIRE(graph.findTask(cpuTask).value()->executionInfo.state == Atlas::TaskState::Success);
    REQUIRE(graph.findTask(gpuTask).value()->executionInfo.state == Atlas::TaskState::Ready);
}

TEST_CASE("KahnScheduler ignores an unused zero-capacity CPU backend for a GPU-only graph", "[UNIT]")
{
    Atlas::TaskGraph graph;
    const Atlas::TaskHandle gpuTask{ requireHandle(graph.addGpuTask(Atlas::Testing::VulkanTestFactory::dispatch())) };
    REQUIRE(graph.finishTaskGraph());

    ZeroCapacityCpuExecutor cpuExecutor;
    ImmediateVulkanDispatchExecutor gpuExecutor;
    Atlas::KahnScheduler scheduler{ graph, cpuExecutor, gpuExecutor };
    const Atlas::SchedulerResult result{ scheduler.execute() };

    REQUIRE(result.status == Atlas::SchedulerStatus::Success);
    REQUIRE(result.executedTaskCount == 1U);
    REQUIRE(graph.findTask(gpuTask).value()->executionInfo.state == Atlas::TaskState::Success);
}

TEST_CASE("KahnScheduler ignores an unused zero-capacity GPU backend for a CPU-only graph", "[UNIT]")
{
    Atlas::TaskGraph graph;
    bool executed{ false };
    const Atlas::TaskHandle cpuTask{ requireHandle(graph.addCpuTask([&executed] { executed = true; })) };
    REQUIRE(graph.finishTaskGraph());

    Atlas::SynchronousCpuExecutor cpuExecutor;
    ZeroCapacityVulkanDispatchExecutor gpuExecutor;
    Atlas::KahnScheduler scheduler{ graph, cpuExecutor, gpuExecutor };
    const Atlas::SchedulerResult result{ scheduler.execute() };

    REQUIRE(result.status == Atlas::SchedulerStatus::Success);
    REQUIRE(result.executedTaskCount == 1U);
    REQUIRE(executed);
    REQUIRE(graph.findTask(cpuTask).value()->executionInfo.state == Atlas::TaskState::Success);
}

TEST_CASE("KahnScheduler rejects a GPU completion reported as CPU work", "[UNIT]")
{
    Atlas::TaskGraph graph;
    const Atlas::TaskHandle gpuTask{ requireHandle(graph.addGpuTask(Atlas::Testing::VulkanTestFactory::dispatch())) };
    REQUIRE(graph.finishTaskGraph());

    Atlas::SynchronousCpuExecutor cpuExecutor;
    ImmediateVulkanDispatchExecutor gpuExecutor{ Atlas::ExecutionResource::CPU };
    Atlas::KahnScheduler scheduler{ graph, cpuExecutor, gpuExecutor };
    const Atlas::SchedulerResult result{ scheduler.execute() };

    REQUIRE(result.status == Atlas::SchedulerStatus::ExecutorUnavailable);
    REQUIRE(graph.findTask(gpuTask).value()->executionInfo.state == Atlas::TaskState::Failure);
}

TEST_CASE("KahnScheduler preserves a GPU dispatch exception and blocks dependants", "[UNIT]")
{
    Atlas::TaskGraph graph;
    const Atlas::TaskHandle gpuTask{ requireHandle(graph.addGpuTask(Atlas::Testing::VulkanTestFactory::dispatch())) };
    const Atlas::TaskHandle dependent{ requireHandle(graph.addCpuTask([] {})) };
    REQUIRE(graph.addDependency(dependent, gpuTask));
    REQUIRE(graph.finishTaskGraph());
    const std::exception_ptr failure{ std::make_exception_ptr(std::runtime_error{ "dispatch failed" }) };

    Atlas::SynchronousCpuExecutor cpuExecutor;
    ImmediateVulkanDispatchExecutor gpuExecutor{ Atlas::ExecutionResource::GPU, failure };
    Atlas::KahnScheduler scheduler{ graph, cpuExecutor, gpuExecutor };
    const Atlas::SchedulerResult result{ scheduler.execute() };

    REQUIRE(result.status == Atlas::SchedulerStatus::TaskFailed);
    REQUIRE(result.exception == failure);
    REQUIRE(graph.findTask(gpuTask).value()->executionInfo.state == Atlas::TaskState::Failure);
    REQUIRE(graph.findTask(dependent).value()->executionInfo.state == Atlas::TaskState::Blocked);
}

TEST_CASE("KahnScheduler releases mixed fan-out and cross-resource fan-in", "[UNIT]")
{
    Atlas::TaskGraph graph;
    std::atomic<unsigned int> completedBranches{ 0U };
    bool joined{ false };
    const Atlas::TaskHandle root{ requireHandle(graph.addCpuTask([] {})) };
    const Atlas::TaskHandle cpuBranch{ requireHandle(graph.addCpuTask([&completedBranches] { completedBranches.fetch_or(1U); })) };
    const Atlas::TaskHandle gpuBranch{ requireHandle(graph.addGpuTask(Atlas::Testing::VulkanTestFactory::dispatch())) };
    const Atlas::TaskHandle join{ requireHandle(
        graph.addCpuTask([&completedBranches, &joined] { joined = completedBranches.load() == 3U; })) };
    REQUIRE(graph.addDependency(cpuBranch, root));
    REQUIRE(graph.addDependency(gpuBranch, root));
    REQUIRE(graph.addDependency(join, cpuBranch));
    REQUIRE(graph.addDependency(join, gpuBranch));
    REQUIRE(graph.finishTaskGraph());

    Atlas::WorkerpoolExecutor cpuExecutor{ 2U };
    ImmediateVulkanDispatchExecutor gpuExecutor{ Atlas::ExecutionResource::GPU, nullptr,
                                                 [gpuBranch, &completedBranches](const Atlas::TaskHandle handle)
                                                 {
                                                     if (handle == gpuBranch)
                                                     {
                                                         completedBranches.fetch_or(2U);
                                                     }
                                                 } };
    Atlas::KahnScheduler scheduler{ graph, cpuExecutor, gpuExecutor };
    const Atlas::SchedulerResult result{ scheduler.execute() };

    REQUIRE(result.status == Atlas::SchedulerStatus::Success);
    REQUIRE(result.executedTaskCount == 4U);
    REQUIRE(joined);
}

TEST_CASE("KahnScheduler processes GPU progress while CPU work remains in flight", "[UNIT][CONCURRENCY]")
{
    Atlas::TaskGraph graph;
    std::binary_semaphore allowCpuCompletion{ 0 };
    const Atlas::TaskHandle cpuRoot{ requireHandle(graph.addCpuTask([&allowCpuCompletion] { allowCpuCompletion.acquire(); })) };
    const Atlas::TaskHandle firstGpu{ requireHandle(graph.addGpuTask(Atlas::Testing::VulkanTestFactory::dispatch())) };
    const Atlas::TaskHandle secondGpu{ requireHandle(graph.addGpuTask(Atlas::Testing::VulkanTestFactory::dispatch())) };
    REQUIRE(graph.addDependency(secondGpu, firstGpu));
    REQUIRE(graph.finishTaskGraph());

    Atlas::WorkerpoolExecutor cpuExecutor{ 1U };
    ImmediateVulkanDispatchExecutor gpuExecutor{ Atlas::ExecutionResource::GPU, nullptr,
                                                 [secondGpu, &allowCpuCompletion](const Atlas::TaskHandle handle)
                                                 {
                                                     if (handle == secondGpu)
                                                     {
                                                         allowCpuCompletion.release();
                                                     }
                                                 } };
    Atlas::KahnScheduler scheduler{ graph, cpuExecutor, gpuExecutor };
    const Atlas::SchedulerResult result{ scheduler.execute() };

    REQUIRE(result.status == Atlas::SchedulerStatus::Success);
    REQUIRE(result.executedTaskCount == 3U);
    REQUIRE(gpuExecutor.submitted == std::vector<Atlas::TaskHandle>{ firstGpu, secondGpu });
    REQUIRE(graph.findTask(cpuRoot).value()->executionInfo.state == Atlas::TaskState::Success);
}

TEST_CASE("KahnScheduler treats a completion producer failure as infrastructure failure", "[UNIT]")
{
    Atlas::TaskGraph graph;
    const Atlas::TaskHandle gpuTask{ requireHandle(graph.addGpuTask(Atlas::Testing::VulkanTestFactory::dispatch())) };
    REQUIRE(graph.finishTaskGraph());

    Atlas::SynchronousCpuExecutor cpuExecutor;
    FailingGpuProducer gpuExecutor;
    Atlas::KahnScheduler scheduler{ graph, cpuExecutor, gpuExecutor };
    const Atlas::SchedulerResult result{ scheduler.execute() };

    REQUIRE(result.status == Atlas::SchedulerStatus::ExecutorUnavailable);
    REQUIRE(result.executedTaskCount == 0U);
    REQUIRE(graph.findTask(gpuTask).value()->executionInfo.state == Atlas::TaskState::Failure);
}

TEST_CASE("KahnScheduler terminates when a completion producer closes the channel", "[UNIT]")
{
    Atlas::TaskGraph graph;
    const Atlas::TaskHandle gpuTask{ requireHandle(graph.addGpuTask(Atlas::Testing::VulkanTestFactory::dispatch())) };
    REQUIRE(graph.finishTaskGraph());

    Atlas::SynchronousCpuExecutor cpuExecutor;
    ClosingGpuProducer gpuExecutor;
    Atlas::KahnScheduler scheduler{ graph, cpuExecutor, gpuExecutor };
    const Atlas::SchedulerResult result{ scheduler.execute() };

    REQUIRE(result.status == Atlas::SchedulerStatus::ExecutorUnavailable);
    REQUIRE(result.executedTaskCount == 0U);
    REQUIRE(graph.findTask(gpuTask).value()->executionInfo.state == Atlas::TaskState::Failure);
}
