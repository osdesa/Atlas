#include "../../support/VulkanTestFactory.h"
#include "atlas/Executor/GpuExecutor.h"
#include "atlas/Executor/SynchronousCpuExecutor.h"
#include "atlas/Executor/WorkerpoolExecutor.h"
#include "atlas/Scheduler/KahnScheduler.h"
#include "atlas/Tasking/TaskGraph.h"

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <exception>
#include <functional>
#include <optional>
#include <semaphore>
#include <stdexcept>
#include <utility>
#include <vector>

namespace
{
    class ImmediateGpuExecutor final : public Atlas::GpuExecutor
    {
      public:
        explicit ImmediateGpuExecutor(const Atlas::ExecutionResource reportedResource = Atlas::ExecutionResource::GPU,
                                      std::exception_ptr outcome = nullptr,
                                      std::function<void(Atlas::TaskHandle)> submissionCallback = {})
            : GpuExecutor{ 1U }, completionResource{ reportedResource }, completionException{ std::move(outcome) },
              onSubmission{ std::move(submissionCallback) }
        {
        }

        bool submit(const Atlas::TaskHandle handle, Atlas::VulkanDispatch) override
        {
            if (!accepting)
            {
                return false;
            }
            standaloneCompletion =
                Atlas::TaskCompletion{ handle, completionException, std::chrono::microseconds{ 1 }, completionResource };
            submitted.emplace_back(handle);
            if (onSubmission)
            {
                onSubmission(handle);
            }
            return true;
        }

        bool submit(const Atlas::TaskHandle handle, Atlas::VulkanDispatch, Atlas::CompletionChannel& channel) override
        {
            if (!accepting)
            {
                return false;
            }
            submitted.emplace_back(handle);
            if (onSubmission)
            {
                onSubmission(handle);
            }
            channel.publish(Atlas::TaskCompletion{ handle, completionException, std::chrono::microseconds{ 1 }, completionResource });
            return true;
        }

        std::optional<Atlas::TaskCompletion> waitForCompletion() override
        {
            return std::exchange(standaloneCompletion, std::nullopt);
        }

        void shutdown() noexcept override
        {
            accepting = false;
        }

        std::vector<Atlas::TaskHandle> submitted;

      private:
        Atlas::ExecutionResource completionResource;
        std::exception_ptr completionException;
        std::function<void(Atlas::TaskHandle)> onSubmission;
        std::optional<Atlas::TaskCompletion> standaloneCompletion;
        bool accepting{ true };
    };

    class FailingGpuProducer final : public Atlas::GpuExecutor
    {
      public:
        FailingGpuProducer() : GpuExecutor{ 1U } {}

        bool submit(Atlas::TaskHandle, Atlas::VulkanDispatch) override
        {
            return false;
        }

        bool submit(Atlas::TaskHandle, Atlas::VulkanDispatch, Atlas::CompletionChannel& channel) override
        {
            channel.signalProducerFailure(Atlas::ExecutionResource::GPU);
            return true;
        }

        std::optional<Atlas::TaskCompletion> waitForCompletion() override
        {
            return std::nullopt;
        }

        void shutdown() noexcept override {}
    };

    class ClosingGpuProducer final : public Atlas::GpuExecutor
    {
      public:
        ClosingGpuProducer() : GpuExecutor{ 1U } {}

        bool submit(Atlas::TaskHandle, Atlas::VulkanDispatch) override
        {
            return false;
        }

        bool submit(Atlas::TaskHandle, Atlas::VulkanDispatch, Atlas::CompletionChannel& channel) override
        {
            channel.close();
            return true;
        }

        std::optional<Atlas::TaskCompletion> waitForCompletion() override
        {
            return std::nullopt;
        }

        void shutdown() noexcept override {}
    };

    class ZeroCapacityCpuExecutor final : public Atlas::CpuExecutor
    {
      public:
        ZeroCapacityCpuExecutor() : CpuExecutor{ 0U } {}

        bool submit(Atlas::TaskHandle, Atlas::TaskFunction) override
        {
            return false;
        }

        std::optional<Atlas::TaskCompletion> waitForCompletion() override
        {
            return std::nullopt;
        }

        void shutdown() noexcept override {}
    };

    class ZeroCapacityGpuExecutor final : public Atlas::GpuExecutor
    {
      public:
        ZeroCapacityGpuExecutor() : GpuExecutor{ 0U } {}

        bool submit(Atlas::TaskHandle, Atlas::VulkanDispatch) override
        {
            return false;
        }

        std::optional<Atlas::TaskCompletion> waitForCompletion() override
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
} // namespace

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
    ImmediateGpuExecutor gpuExecutor;
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
    Atlas::KahnScheduler scheduler{ graph, cpuExecutor };
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
    Atlas::KahnScheduler scheduler{ graph, cpuExecutor };
    const Atlas::SchedulerResult result{ scheduler.execute() };

    REQUIRE(result.status == Atlas::SchedulerStatus::ExecutorUnavailable);
    REQUIRE(result.executedTaskCount == 0U);
    REQUIRE(graph.findTask(cpuTask).value()->executionInfo.state == Atlas::TaskState::Ready);
    REQUIRE(graph.findTask(gpuTask).value()->executionInfo.state == Atlas::TaskState::Blocked);
}

TEST_CASE("KahnScheduler ignores an unused zero-capacity CPU backend for a GPU-only graph", "[UNIT]")
{
    Atlas::TaskGraph graph;
    const Atlas::TaskHandle gpuTask{ requireHandle(graph.addGpuTask(Atlas::Testing::VulkanTestFactory::dispatch())) };
    REQUIRE(graph.finishTaskGraph());

    ZeroCapacityCpuExecutor cpuExecutor;
    ImmediateGpuExecutor gpuExecutor;
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
    ZeroCapacityGpuExecutor gpuExecutor;
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
    ImmediateGpuExecutor gpuExecutor{ Atlas::ExecutionResource::CPU };
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
    ImmediateGpuExecutor gpuExecutor{ Atlas::ExecutionResource::GPU, failure };
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
    ImmediateGpuExecutor gpuExecutor{ Atlas::ExecutionResource::GPU, nullptr,
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
    ImmediateGpuExecutor gpuExecutor{ Atlas::ExecutionResource::GPU, nullptr,
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
