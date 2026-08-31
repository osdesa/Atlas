#include "../../support/UnusedVulkanDispatchExecutor.h"
#include "atlas/Executor/SynchronousCpuExecutor.h"
#include "atlas/Scheduler/KahnScheduler.h"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
    class ScriptedCpuExecutor final : public Atlas::CpuExecutor
    {
      public:
        explicit ScriptedCpuExecutor(std::uint32_t capacity, std::vector<std::optional<Atlas::TaskCompletion>> completions = {},
                                     std::optional<std::size_t> rejectedSubmissionIndex = std::nullopt,
                                     bool requireSubmittedHandle = true)
            : CpuExecutor{ capacity }, scriptedCompletions{ std::move(completions) }, rejectionIndex{ rejectedSubmissionIndex },
              waitForSubmittedHandle{ requireSubmittedHandle },
              publisher{ [this](const std::stop_token stopToken) { publishCompletions(stopToken); } }
        {
        }

        bool submit(Atlas::TaskHandle handle, Atlas::TaskFunction, Atlas::CompletionChannel& channel) override
        {
            std::lock_guard lock{ stateMutex };
            if (rejectionIndex.has_value() && submittedHandles.size() == rejectionIndex.value())
            {
                return false;
            }

            submittedHandles.emplace_back(handle);
            completionChannel = &channel;
            stateChanged.notify_all();
            return true;
        }

        std::optional<Atlas::TaskCompletion> waitForCompletion()
        {
            if (!submissionsBeforeFirstWait.has_value())
            {
                submissionsBeforeFirstWait = submittedHandles.size();
            }

            if (nextCompletionIndex == scriptedCompletions.size())
            {
                return std::nullopt;
            }

            return std::move(scriptedCompletions.at(nextCompletionIndex++));
        }

        void shutdown() noexcept override
        {
            publisher.request_stop();
            stateChanged.notify_all();
            if (publisher.joinable())
            {
                publisher.join();
            }
        }

        const std::vector<Atlas::TaskHandle>& submissions() const noexcept
        {
            return submittedHandles;
        }

      private:
        void publishCompletions(const std::stop_token stopToken)
        {
            std::unique_lock lock{ stateMutex };
            if (scriptedCompletions.empty())
            {
                stateChanged.wait(lock, stopToken, [this] { return !submittedHandles.empty(); });
                if (!stopToken.stop_requested() && completionChannel != nullptr)
                {
                    completionChannel->signalProducerFailure(Atlas::ExecutionResource::CPU);
                }
                return;
            }

            while (!stopToken.stop_requested() && nextCompletionIndex < scriptedCompletions.size())
            {
                stateChanged.wait(lock, stopToken,
                                  [this]
                                  {
                                      const auto& completion{ scriptedCompletions.at(nextCompletionIndex) };
                                      return completionChannel != nullptr &&
                                             (!waitForSubmittedHandle || !completion.has_value() ||
                                              std::find(submittedHandles.begin(), submittedHandles.end(), completion->handle) !=
                                                  submittedHandles.end());
                                  });
                if (stopToken.stop_requested() || completionChannel == nullptr)
                {
                    return;
                }
                std::optional<Atlas::TaskCompletion> completion{ std::move(scriptedCompletions.at(nextCompletionIndex++)) };
                Atlas::CompletionChannel* const channel{ completionChannel };
                lock.unlock();
                if (completion.has_value())
                {
                    channel->publish(std::move(completion.value()));
                }
                else
                {
                    channel->signalProducerFailure(Atlas::ExecutionResource::CPU);
                }
                lock.lock();
            }
        }

        std::vector<std::optional<Atlas::TaskCompletion>> scriptedCompletions;
        std::optional<std::size_t> rejectionIndex;
        bool waitForSubmittedHandle{ true };
        std::vector<Atlas::TaskHandle> submittedHandles;
        std::optional<std::size_t> submissionsBeforeFirstWait;
        std::size_t nextCompletionIndex{ 0U };
        std::mutex stateMutex;
        std::condition_variable_any stateChanged;
        Atlas::CompletionChannel* completionChannel{ nullptr };
        std::jthread publisher;
    };

    class DuplicateCompletionCpuExecutor final : public Atlas::CpuExecutor
    {
      public:
        DuplicateCompletionCpuExecutor() : CpuExecutor{ 2U } {}

        bool submit(Atlas::TaskHandle handle, Atlas::TaskFunction, Atlas::CompletionChannel& channel) override
        {
            submittedHandles.emplace_back(handle);
            if (submittedHandles.size() == maxConcurrency())
            {
                channel.publish(Atlas::TaskCompletion{ submittedHandles.at(0U), nullptr, std::chrono::microseconds{ 2 } });
                channel.publish(Atlas::TaskCompletion{ submittedHandles.at(1U), nullptr, std::chrono::microseconds{ 5 } });
                channel.publish(Atlas::TaskCompletion{ submittedHandles.at(0U), nullptr, std::chrono::microseconds{ 3 } });
            }
            return true;
        }

        void shutdown() noexcept override {}

      private:
        std::vector<Atlas::TaskHandle> submittedHandles;
    };

    Atlas::TaskHandle addTask(Atlas::TaskGraph& graph, Atlas::TaskFunction function, const char* name)
    {
        const std::optional<Atlas::TaskHandle> handle{ graph.addCpuTask(std::move(function), Atlas::TaskOptions{ name }) };
        REQUIRE(handle.has_value());
        return handle.value();
    }
} // namespace

TEST_CASE("KahnScheduler requires a task graph and cannot be copied or moved", "[UNIT]")
{
    STATIC_REQUIRE_FALSE(std::is_default_constructible_v<Atlas::KahnScheduler>);
    STATIC_REQUIRE_FALSE(std::is_constructible_v<Atlas::KahnScheduler, const Atlas::TaskGraph&>);
    STATIC_REQUIRE(
        std::is_constructible_v<Atlas::KahnScheduler, const Atlas::TaskGraph&, Atlas::CpuExecutor&, Atlas::VulkanDispatchExecutor&>);
    STATIC_REQUIRE_FALSE(std::is_copy_constructible_v<Atlas::KahnScheduler>);
    STATIC_REQUIRE_FALSE(std::is_copy_assignable_v<Atlas::KahnScheduler>);
    STATIC_REQUIRE_FALSE(std::is_move_constructible_v<Atlas::KahnScheduler>);
    STATIC_REQUIRE_FALSE(std::is_move_assignable_v<Atlas::KahnScheduler>);
}

TEST_CASE("KahnScheduler executes a single task successfully", "[UNIT]")
{
    Atlas::TaskGraph graph;
    bool executed{ false };
    Atlas::TaskState observedExecutionState{ Atlas::TaskState::Unknown };
    std::shared_ptr<const Atlas::Task> runtimeTask;

    const Atlas::TaskHandle handle{ addTask(
        graph,
        [&executed, &observedExecutionState, &runtimeTask]
        {
            observedExecutionState = runtimeTask->executionInfo.state;
            executed = true;
        },
        "Root") };
    REQUIRE(graph.finishTaskGraph());
    runtimeTask = graph.findTask(handle).value();

    Atlas::SynchronousCpuExecutor executor;
    Atlas::KahnScheduler scheduler{ graph, executor, Atlas::Test::unusedVulkanDispatchExecutor };
    const Atlas::SchedulerResult result{ scheduler.execute() };

    REQUIRE(executed);
    REQUIRE(observedExecutionState == Atlas::TaskState::Running);
    REQUIRE(result.status == Atlas::SchedulerStatus::Success);
    REQUIRE(result.executedTaskCount == 1U);
    REQUIRE(result.exception == nullptr);
    REQUIRE(result.executionTime >= std::chrono::milliseconds{ 0 });

    const std::optional<std::shared_ptr<const Atlas::Task>> task{ graph.findTask(handle) };
    REQUIRE(task.has_value());
    REQUIRE(task.value()->executionInfo.state == Atlas::TaskState::Success);
    REQUIRE(task.value()->executionInfo.exception == nullptr);
    REQUIRE(task.value()->executionInfo.executionDuration >= std::chrono::microseconds{ 0 });
    REQUIRE(task.value()->executionInfo.executionDuration <= result.executionTime);
    REQUIRE(task.value()->executionInfo.responseDuration.has_value());
    REQUIRE(task.value()->executionInfo.responseDuration.value() <= result.executionTime);
    REQUIRE(result.schedulerActiveDuration <= result.executionTime);
}

TEST_CASE("KahnScheduler treats an empty task function as successful work", "[UNIT]")
{
    Atlas::TaskGraph graph;

    addTask(graph, Atlas::TaskFunction{}, "Empty");
    REQUIRE(graph.finishTaskGraph());

    Atlas::SynchronousCpuExecutor executor;
    Atlas::KahnScheduler scheduler{ graph, executor, Atlas::Test::unusedVulkanDispatchExecutor };
    const Atlas::SchedulerResult result{ scheduler.execute() };

    REQUIRE(result.status == Atlas::SchedulerStatus::Success);
    REQUIRE(result.executedTaskCount == 1U);
    REQUIRE(result.exception == nullptr);
}

TEST_CASE("KahnScheduler executes every independent root task", "[UNIT]")
{
    Atlas::TaskGraph graph;
    std::size_t executionCount{ 0U };

    addTask(graph, [&executionCount] { ++executionCount; }, "First root");
    addTask(graph, [&executionCount] { ++executionCount; }, "Second root");
    REQUIRE(graph.finishTaskGraph());

    Atlas::SynchronousCpuExecutor executor;
    Atlas::KahnScheduler scheduler{ graph, executor, Atlas::Test::unusedVulkanDispatchExecutor };
    const Atlas::SchedulerResult result{ scheduler.execute() };

    REQUIRE(result.status == Atlas::SchedulerStatus::Success);
    REQUIRE(result.executedTaskCount == 2U);
    REQUIRE(executionCount == 2U);
}

TEST_CASE("KahnScheduler selects independent ready tasks in FIFO order", "[UNIT]")
{
    Atlas::TaskGraph graph;
    std::vector<int> executionOrder;

    addTask(graph, [&executionOrder] { executionOrder.emplace_back(1); }, "First");
    addTask(graph, [&executionOrder] { executionOrder.emplace_back(2); }, "Second");
    addTask(graph, [&executionOrder] { executionOrder.emplace_back(3); }, "Third");
    REQUIRE(graph.finishTaskGraph());

    Atlas::SynchronousCpuExecutor executor;
    Atlas::KahnScheduler scheduler{ graph, executor, Atlas::Test::unusedVulkanDispatchExecutor };
    const Atlas::SchedulerResult result{ scheduler.execute() };

    REQUIRE(result.status == Atlas::SchedulerStatus::Success);
    REQUIRE(result.executedTaskCount == 3U);
    REQUIRE(executionOrder == std::vector<int>{ 1, 2, 3 });
}

TEST_CASE("KahnScheduler fills executor capacity and accepts out-of-order completions", "[UNIT]")
{
    Atlas::TaskGraph graph;
    const Atlas::TaskHandle firstHandle{ addTask(graph, [] {}, "First") };
    const Atlas::TaskHandle secondHandle{ addTask(graph, [] {}, "Second") };
    const Atlas::TaskHandle thirdHandle{ addTask(graph, [] {}, "Third") };
    REQUIRE(graph.finishTaskGraph());

    ScriptedCpuExecutor executor{ 2U,
                                  { Atlas::TaskCompletion{ secondHandle, nullptr, std::chrono::microseconds{ 2 } },
                                    Atlas::TaskCompletion{ firstHandle, nullptr, std::chrono::microseconds{ 3 } },
                                    Atlas::TaskCompletion{ thirdHandle, nullptr, std::chrono::microseconds{ 5 } } } };
    Atlas::KahnScheduler scheduler{ graph, executor, Atlas::Test::unusedVulkanDispatchExecutor };

    const Atlas::SchedulerResult result{ scheduler.execute() };

    REQUIRE(result.status == Atlas::SchedulerStatus::Success);
    REQUIRE(result.executedTaskCount == 3U);
    REQUIRE(executor.submissions() == std::vector<Atlas::TaskHandle>{ firstHandle, secondHandle, thirdHandle });
    REQUIRE(graph.findTask(firstHandle).value()->executionInfo.state == Atlas::TaskState::Success);
    REQUIRE(graph.findTask(secondHandle).value()->executionInfo.state == Atlas::TaskState::Success);
    REQUIRE(graph.findTask(thirdHandle).value()->executionInfo.state == Atlas::TaskState::Success);
}

TEST_CASE("KahnScheduler refills capacity with newly unblocked work", "[UNIT]")
{
    Atlas::TaskGraph graph;
    const Atlas::TaskHandle firstRootHandle{ addTask(graph, [] {}, "First root") };
    const Atlas::TaskHandle secondRootHandle{ addTask(graph, [] {}, "Second root") };
    const Atlas::TaskHandle dependentHandle{ addTask(graph, [] {}, "Dependent") };
    REQUIRE(graph.addDependency(dependentHandle, firstRootHandle));
    REQUIRE(graph.finishTaskGraph());

    ScriptedCpuExecutor executor{ 2U,
                                  { Atlas::TaskCompletion{ firstRootHandle, nullptr, std::chrono::microseconds{ 2 } },
                                    Atlas::TaskCompletion{ dependentHandle, nullptr, std::chrono::microseconds{ 3 } },
                                    Atlas::TaskCompletion{ secondRootHandle, nullptr, std::chrono::microseconds{ 5 } } } };
    Atlas::KahnScheduler scheduler{ graph, executor, Atlas::Test::unusedVulkanDispatchExecutor };

    const Atlas::SchedulerResult result{ scheduler.execute() };

    REQUIRE(result.status == Atlas::SchedulerStatus::Success);
    REQUIRE(result.executedTaskCount == 3U);
    REQUIRE(executor.submissions() == std::vector<Atlas::TaskHandle>{ firstRootHandle, secondRootHandle, dependentHandle });
    REQUIRE(graph.findTask(firstRootHandle).value()->executionInfo.state == Atlas::TaskState::Success);
    REQUIRE(graph.findTask(secondRootHandle).value()->executionInfo.state == Atlas::TaskState::Success);
    REQUIRE(graph.findTask(dependentHandle).value()->executionInfo.state == Atlas::TaskState::Success);
}

TEST_CASE("KahnScheduler rejects an executor with zero concurrency", "[UNIT]")
{
    Atlas::TaskGraph graph;
    const Atlas::TaskHandle handle{ addTask(graph, [] {}, "Never submitted") };
    REQUIRE(graph.finishTaskGraph());

    ScriptedCpuExecutor executor{ 0U };
    Atlas::KahnScheduler scheduler{ graph, executor, Atlas::Test::unusedVulkanDispatchExecutor };

    const Atlas::SchedulerResult result{ scheduler.execute() };

    REQUIRE(result.status == Atlas::SchedulerStatus::ExecutorUnavailable);
    REQUIRE(result.executedTaskCount == 0U);
    REQUIRE(executor.submissions().empty());
    REQUIRE(graph.findTask(handle).value()->executionInfo.state == Atlas::TaskState::Ready);
}

TEST_CASE("KahnScheduler captures task exceptions", "[UNIT]")
{
    Atlas::TaskGraph graph;
    bool dependentExecuted{ false };

    const Atlas::TaskHandle handle{ addTask(graph, [] { throw std::runtime_error{ "task failed" }; }, "Failing") };
    const Atlas::TaskHandle dependentHandle{ addTask(graph, [&dependentExecuted] { dependentExecuted = true; }, "Dependent") };
    REQUIRE(graph.addDependency(dependentHandle, handle));
    REQUIRE(graph.finishTaskGraph());

    Atlas::SynchronousCpuExecutor executor;
    Atlas::KahnScheduler scheduler{ graph, executor, Atlas::Test::unusedVulkanDispatchExecutor };
    const Atlas::SchedulerResult result{ scheduler.execute() };

    CHECK(result.status == Atlas::SchedulerStatus::TaskFailed);
    CHECK(result.executedTaskCount == 0U);
    REQUIRE(result.exception != nullptr);

    const std::optional<std::shared_ptr<const Atlas::Task>> task{ graph.findTask(handle) };
    REQUIRE(task.has_value());
    REQUIRE(task.value()->executionInfo.state == Atlas::TaskState::Failure);
    REQUIRE(task.value()->executionInfo.exception == result.exception);
    REQUIRE(task.value()->executionInfo.executionDuration >= std::chrono::microseconds{ 0 });
    REQUIRE(task.value()->executionInfo.executionDuration <= result.executionTime);
    REQUIRE_FALSE(dependentExecuted);

    const std::optional<std::shared_ptr<const Atlas::Task>> dependentTask{ graph.findTask(dependentHandle) };
    REQUIRE(dependentTask.has_value());
    REQUIRE(dependentTask.value()->executionInfo.state == Atlas::TaskState::Blocked);
    REQUIRE(dependentTask.value()->executionInfo.exception == nullptr);

    bool caughtRuntimeError{ false };
    try
    {
        std::rethrow_exception(result.exception);
    }
    catch (const std::runtime_error&)
    {
        caughtRuntimeError = true;
    }

    REQUIRE(caughtRuntimeError);
}

TEST_CASE("KahnScheduler restores a task when the CPU executor rejects submission", "[UNIT]")
{
    Atlas::TaskGraph graph;
    bool executed{ false };

    const Atlas::TaskHandle handle{ addTask(graph, [&executed] { executed = true; }, "Rejected") };
    REQUIRE(graph.finishTaskGraph());

    Atlas::SynchronousCpuExecutor executor;
    executor.shutdown();
    Atlas::KahnScheduler scheduler{ graph, executor, Atlas::Test::unusedVulkanDispatchExecutor };

    const Atlas::SchedulerResult result{ scheduler.execute() };

    REQUIRE_FALSE(executed);
    REQUIRE(result.status == Atlas::SchedulerStatus::ExecutorUnavailable);
    REQUIRE(result.executedTaskCount == 0U);
    REQUIRE(result.exception == nullptr);

    const std::optional<std::shared_ptr<const Atlas::Task>> task{ graph.findTask(handle) };
    REQUIRE(task.has_value());
    REQUIRE(task.value()->executionInfo.state == Atlas::TaskState::Ready);
    REQUIRE(task.value()->executionInfo.exception == nullptr);
    REQUIRE(task.value()->executionInfo.executionDuration == std::chrono::microseconds{ 0 });
}

TEST_CASE("KahnScheduler drains accepted work after a later submission is rejected", "[UNIT]")
{
    Atlas::TaskGraph graph;
    const Atlas::TaskHandle acceptedHandle{ addTask(graph, [] {}, "Accepted") };
    const Atlas::TaskHandle rejectedHandle{ addTask(graph, [] {}, "Rejected") };
    REQUIRE(graph.finishTaskGraph());

    ScriptedCpuExecutor executor{ 2U, { Atlas::TaskCompletion{ acceptedHandle, nullptr, std::chrono::microseconds{ 11 } } }, 1U };
    Atlas::KahnScheduler scheduler{ graph, executor, Atlas::Test::unusedVulkanDispatchExecutor };

    const Atlas::SchedulerResult result{ scheduler.execute() };

    REQUIRE(result.status == Atlas::SchedulerStatus::ExecutorUnavailable);
    REQUIRE(result.executedTaskCount == 1U);
    REQUIRE(result.exception == nullptr);
    REQUIRE(executor.submissions() == std::vector<Atlas::TaskHandle>{ acceptedHandle });
    REQUIRE(graph.findTask(acceptedHandle).value()->executionInfo.state == Atlas::TaskState::Success);
    REQUIRE(graph.findTask(acceptedHandle).value()->executionInfo.executionDuration == std::chrono::microseconds{ 11 });
    REQUIRE(graph.findTask(rejectedHandle).value()->executionInfo.state == Atlas::TaskState::Ready);
}

TEST_CASE("KahnScheduler stops submissions after failure and drains accepted outcomes", "[UNIT]")
{
    Atlas::TaskGraph graph;
    const Atlas::TaskHandle failedHandle{ addTask(graph, [] {}, "Failure") };
    const Atlas::TaskHandle successfulHandle{ addTask(graph, [] {}, "Accepted success") };
    const Atlas::TaskHandle unsubmittedHandle{ addTask(graph, [] {}, "Not submitted") };
    REQUIRE(graph.finishTaskGraph());

    const std::exception_ptr firstException{ std::make_exception_ptr(std::runtime_error{ "first failure" }) };
    ScriptedCpuExecutor executor{ 2U,
                                  { Atlas::TaskCompletion{ failedHandle, firstException, std::chrono::microseconds{ 13 } },
                                    Atlas::TaskCompletion{ successfulHandle, nullptr, std::chrono::microseconds{ 17 } } } };
    Atlas::KahnScheduler scheduler{ graph, executor, Atlas::Test::unusedVulkanDispatchExecutor };

    const Atlas::SchedulerResult result{ scheduler.execute() };

    REQUIRE(result.status == Atlas::SchedulerStatus::TaskFailed);
    REQUIRE(result.executedTaskCount == 1U);
    REQUIRE(result.exception == firstException);
    REQUIRE(executor.submissions() == std::vector<Atlas::TaskHandle>{ failedHandle, successfulHandle });
    REQUIRE(graph.findTask(failedHandle).value()->executionInfo.state == Atlas::TaskState::Failure);
    REQUIRE(graph.findTask(failedHandle).value()->executionInfo.exception == firstException);
    REQUIRE(graph.findTask(successfulHandle).value()->executionInfo.state == Atlas::TaskState::Success);
    REQUIRE(graph.findTask(unsubmittedHandle).value()->executionInfo.state == Atlas::TaskState::Ready);
}

TEST_CASE("KahnScheduler preserves the first exception while draining multiple failures", "[UNIT]")
{
    Atlas::TaskGraph graph;
    const Atlas::TaskHandle firstHandle{ addTask(graph, [] {}, "First failure") };
    const Atlas::TaskHandle secondHandle{ addTask(graph, [] {}, "Second failure") };
    REQUIRE(graph.finishTaskGraph());

    const std::exception_ptr firstException{ std::make_exception_ptr(std::runtime_error{ "first failure" }) };
    const std::exception_ptr secondException{ std::make_exception_ptr(std::runtime_error{ "second failure" }) };
    ScriptedCpuExecutor executor{ 2U,
                                  { Atlas::TaskCompletion{ firstHandle, firstException, std::chrono::microseconds{ 7 } },
                                    Atlas::TaskCompletion{ secondHandle, secondException, std::chrono::microseconds{ 11 } } } };
    Atlas::KahnScheduler scheduler{ graph, executor, Atlas::Test::unusedVulkanDispatchExecutor };

    const Atlas::SchedulerResult result{ scheduler.execute() };

    REQUIRE(result.status == Atlas::SchedulerStatus::TaskFailed);
    REQUIRE(result.executedTaskCount == 0U);
    REQUIRE(result.exception == firstException);
    REQUIRE(graph.findTask(firstHandle).value()->executionInfo.state == Atlas::TaskState::Failure);
    REQUIRE(graph.findTask(firstHandle).value()->executionInfo.exception == firstException);
    REQUIRE(graph.findTask(firstHandle).value()->executionInfo.executionDuration == std::chrono::microseconds{ 7 });
    REQUIRE(graph.findTask(secondHandle).value()->executionInfo.state == Atlas::TaskState::Failure);
    REQUIRE(graph.findTask(secondHandle).value()->executionInfo.exception == secondException);
    REQUIRE(graph.findTask(secondHandle).value()->executionInfo.executionDuration == std::chrono::microseconds{ 11 });
}

TEST_CASE("KahnScheduler drains valid work after an unknown completion", "[UNIT]")
{
    Atlas::TaskGraph graph;
    const Atlas::TaskHandle rootHandle{ addTask(graph, [] {}, "Root") };
    const Atlas::TaskHandle dependentHandle{ addTask(graph, [] {}, "Dependent") };
    REQUIRE(graph.addDependency(dependentHandle, rootHandle));
    REQUIRE(graph.finishTaskGraph());

    ScriptedCpuExecutor executor{ 2U,
                                  { Atlas::TaskCompletion{ dependentHandle, nullptr, std::chrono::microseconds{ 7 } },
                                    Atlas::TaskCompletion{ rootHandle, nullptr, std::chrono::microseconds{ 19 } } },
                                  std::nullopt,
                                  false };
    Atlas::KahnScheduler scheduler{ graph, executor, Atlas::Test::unusedVulkanDispatchExecutor };

    const Atlas::SchedulerResult result{ scheduler.execute() };

    REQUIRE(result.status == Atlas::SchedulerStatus::ExecutorUnavailable);
    REQUIRE(result.executedTaskCount == 1U);
    REQUIRE(result.exception == nullptr);
    REQUIRE(graph.findTask(rootHandle).value()->executionInfo.state == Atlas::TaskState::Success);
    REQUIRE(graph.findTask(rootHandle).value()->executionInfo.executionDuration == std::chrono::microseconds{ 19 });
    REQUIRE(graph.findTask(dependentHandle).value()->executionInfo.state == Atlas::TaskState::Blocked);
}

TEST_CASE("KahnScheduler rejects duplicate completions while preserving accepted outcomes", "[UNIT]")
{
    Atlas::TaskGraph graph;
    const Atlas::TaskHandle firstHandle{ addTask(graph, [] {}, "First") };
    const Atlas::TaskHandle secondHandle{ addTask(graph, [] {}, "Second") };
    REQUIRE(graph.finishTaskGraph());

    DuplicateCompletionCpuExecutor executor;
    Atlas::KahnScheduler scheduler{ graph, executor, Atlas::Test::unusedVulkanDispatchExecutor };

    const Atlas::SchedulerResult result{ scheduler.execute() };

    REQUIRE(result.status == Atlas::SchedulerStatus::ExecutorUnavailable);
    REQUIRE(result.executedTaskCount == 2U);
    REQUIRE(graph.findTask(firstHandle).value()->executionInfo.state == Atlas::TaskState::Success);
    REQUIRE(graph.findTask(firstHandle).value()->executionInfo.executionDuration == std::chrono::microseconds{ 2 });
    REQUIRE(graph.findTask(secondHandle).value()->executionInfo.state == Atlas::TaskState::Success);
}

TEST_CASE("KahnScheduler rejects an extra completion after accepted work drains", "[UNIT]")
{
    Atlas::TaskGraph graph;
    const Atlas::TaskHandle handle{ addTask(graph, [] {}, "Completed once") };
    REQUIRE(graph.finishTaskGraph());

    ScriptedCpuExecutor executor{ 1U,
                                  { Atlas::TaskCompletion{ handle, nullptr, std::chrono::microseconds{ 13 } },
                                    Atlas::TaskCompletion{ handle, nullptr, std::chrono::microseconds{ 17 } } } };
    Atlas::KahnScheduler scheduler{ graph, executor, Atlas::Test::unusedVulkanDispatchExecutor };

    const Atlas::SchedulerResult result{ scheduler.execute() };

    REQUIRE(result.status == Atlas::SchedulerStatus::ExecutorUnavailable);
    REQUIRE(result.executedTaskCount == 1U);
    REQUIRE(result.exception == nullptr);
    REQUIRE(graph.findTask(handle).value()->executionInfo.state == Atlas::TaskState::Success);
    REQUIRE(graph.findTask(handle).value()->executionInfo.exception == nullptr);
    REQUIRE(graph.findTask(handle).value()->executionInfo.executionDuration == std::chrono::microseconds{ 13 });
}

TEST_CASE("KahnScheduler fails every unresolved task when completions end early", "[UNIT]")
{
    Atlas::TaskGraph graph;
    const Atlas::TaskHandle firstHandle{ addTask(graph, [] {}, "First") };
    const Atlas::TaskHandle secondHandle{ addTask(graph, [] {}, "Second") };
    REQUIRE(graph.finishTaskGraph());

    ScriptedCpuExecutor executor{ 2U };
    Atlas::KahnScheduler scheduler{ graph, executor, Atlas::Test::unusedVulkanDispatchExecutor };

    const Atlas::SchedulerResult result{ scheduler.execute() };

    REQUIRE(result.status == Atlas::SchedulerStatus::ExecutorUnavailable);
    REQUIRE(result.executedTaskCount == 0U);
    REQUIRE(graph.findTask(firstHandle).value()->executionInfo.state == Atlas::TaskState::Failure);
    REQUIRE(graph.findTask(secondHandle).value()->executionInfo.state == Atlas::TaskState::Failure);
}

TEST_CASE("KahnScheduler reports accepted work without a completion as an executor failure", "[UNIT]")
{
    Atlas::TaskGraph graph;
    const Atlas::TaskHandle rootHandle{ addTask(graph, [] {}, "Root") };
    const Atlas::TaskHandle dependentHandle{ addTask(graph, [] {}, "Dependent") };
    REQUIRE(graph.addDependency(dependentHandle, rootHandle));
    REQUIRE(graph.finishTaskGraph());

    ScriptedCpuExecutor executor{ 1U };
    Atlas::KahnScheduler scheduler{ graph, executor, Atlas::Test::unusedVulkanDispatchExecutor };

    const Atlas::SchedulerResult result{ scheduler.execute() };

    REQUIRE(result.status == Atlas::SchedulerStatus::ExecutorUnavailable);
    REQUIRE(result.executedTaskCount == 0U);
    REQUIRE(result.exception == nullptr);

    const std::shared_ptr<const Atlas::Task> root{ graph.findTask(rootHandle).value() };
    const std::shared_ptr<const Atlas::Task> dependent{ graph.findTask(dependentHandle).value() };
    REQUIRE(root->executionInfo.state == Atlas::TaskState::Failure);
    REQUIRE(root->executionInfo.exception == nullptr);
    REQUIRE(root->executionInfo.executionDuration == std::chrono::microseconds{ 0 });
    REQUIRE(dependent->executionInfo.state == Atlas::TaskState::Blocked);
}

TEST_CASE("KahnScheduler rejects a completion attributed to the wrong task", "[UNIT]")
{
    Atlas::TaskGraph graph;
    const Atlas::TaskHandle rootHandle{ addTask(graph, [] {}, "Root") };
    const Atlas::TaskHandle dependentHandle{ addTask(graph, [] {}, "Dependent") };
    REQUIRE(graph.addDependency(dependentHandle, rootHandle));
    REQUIRE(graph.finishTaskGraph());

    ScriptedCpuExecutor executor{
        2U, { Atlas::TaskCompletion{ dependentHandle, nullptr, std::chrono::microseconds{ 7 } }, std::nullopt }, std::nullopt, false
    };
    Atlas::KahnScheduler scheduler{ graph, executor, Atlas::Test::unusedVulkanDispatchExecutor };

    const Atlas::SchedulerResult result{ scheduler.execute() };

    REQUIRE(result.status == Atlas::SchedulerStatus::ExecutorUnavailable);
    REQUIRE(result.executedTaskCount == 0U);
    REQUIRE(result.exception == nullptr);

    const std::shared_ptr<const Atlas::Task> root{ graph.findTask(rootHandle).value() };
    const std::shared_ptr<const Atlas::Task> dependent{ graph.findTask(dependentHandle).value() };
    REQUIRE(root->executionInfo.state == Atlas::TaskState::Failure);
    REQUIRE(root->executionInfo.exception == nullptr);
    REQUIRE(root->executionInfo.executionDuration == std::chrono::microseconds{ 0 });
    REQUIRE(dependent->executionInfo.state == Atlas::TaskState::Blocked);
    REQUIRE(dependent->executionInfo.executionDuration == std::chrono::microseconds{ 0 });
}

TEST_CASE("KahnScheduler skips a queued task that is no longer ready", "[UNIT]")
{
    Atlas::TaskGraph graph;
    bool executed{ false };

    const Atlas::TaskHandle handle{ addTask(graph, [&executed] { executed = true; }, "No longer ready") };
    REQUIRE(graph.finishTaskGraph());

    const std::optional<std::shared_ptr<const Atlas::Task>> task{ graph.findTask(handle) };
    REQUIRE(task.has_value());
    task.value()->executionInfo.state = Atlas::TaskState::Blocked;

    Atlas::SynchronousCpuExecutor executor;
    Atlas::KahnScheduler scheduler{ graph, executor, Atlas::Test::unusedVulkanDispatchExecutor };
    const Atlas::SchedulerResult result{ scheduler.execute() };

    REQUIRE_FALSE(executed);
    REQUIRE(result.status == Atlas::SchedulerStatus::InvalidGraph);
    REQUIRE(result.executedTaskCount == 0U);
    REQUIRE(task.value()->executionInfo.state == Atlas::TaskState::Blocked);
    REQUIRE(task.value()->executionInfo.exception == nullptr);
}

TEST_CASE("KahnScheduler does not execute a completed task again", "[UNIT]")
{
    Atlas::TaskGraph graph;
    std::size_t executionCount{ 0U };

    addTask(graph, [&executionCount] { ++executionCount; }, "Repeatable");
    REQUIRE(graph.finishTaskGraph());

    Atlas::SynchronousCpuExecutor executor;
    Atlas::KahnScheduler scheduler{ graph, executor, Atlas::Test::unusedVulkanDispatchExecutor };
    const Atlas::SchedulerResult firstResult{ scheduler.execute() };
    const Atlas::SchedulerResult secondResult{ scheduler.execute() };

    REQUIRE(firstResult.status == Atlas::SchedulerStatus::Success);
    REQUIRE(secondResult.status == Atlas::SchedulerStatus::InvalidGraph);
    REQUIRE(firstResult.executedTaskCount == 1U);
    REQUIRE(secondResult.executedTaskCount == 0U);
    REQUIRE(executionCount == 1U);
}
