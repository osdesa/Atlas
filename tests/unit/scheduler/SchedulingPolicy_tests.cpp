#include "atlas/Executor/SynchronousCpuExecutor.h"
#include "atlas/Scheduler/FifoSchedulingPolicy.h"
#include "atlas/Scheduler/KahnScheduler.h"
#include "atlas/Scheduler/RoundRobinSchedulingPolicy.h"
#include "atlas/Scheduler/StaticPrioritySchedulingPolicy.h"
#include "atlas/Tasking/TaskGraph.h"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace
{
    Atlas::TaskHandle makeHandle(const std::uint32_t value)
    {
        static const Atlas::GraphId graphId{ Atlas::GraphId::create() };
        return Atlas::TaskHandle{ Atlas::TaskId{ value }, graphId };
    }

    class ThrowingPolicy final : public Atlas::SchedulingPolicy
    {
      public:
        explicit ThrowingPolicy(std::exception_ptr failureValue) : failure{ std::move(failureValue) } {}

        std::unique_ptr<Atlas::SchedulingPolicy> clone() const override
        {
            return std::make_unique<ThrowingPolicy>(failure);
        }

        std::size_t selectNext(std::span<const Atlas::SchedulingCandidate>) override
        {
            std::rethrow_exception(failure);
        }

      private:
        std::exception_ptr failure;
    };

    class InvalidIndexPolicy final : public Atlas::SchedulingPolicy
    {
      public:
        std::unique_ptr<Atlas::SchedulingPolicy> clone() const override
        {
            return std::make_unique<InvalidIndexPolicy>();
        }

        std::size_t selectNext(const std::span<const Atlas::SchedulingCandidate> candidates) override
        {
            return candidates.size();
        }
    };

    class NullClonePolicy final : public Atlas::SchedulingPolicy
    {
      public:
        std::unique_ptr<Atlas::SchedulingPolicy> clone() const override
        {
            return nullptr;
        }

        std::size_t selectNext(std::span<const Atlas::SchedulingCandidate>) override
        {
            return 0U;
        }
    };

    class FailAfterFirstSelectionPolicy final : public Atlas::SchedulingPolicy
    {
      public:
        explicit FailAfterFirstSelectionPolicy(std::exception_ptr failureValue) : failure{ std::move(failureValue) } {}

        std::unique_ptr<Atlas::SchedulingPolicy> clone() const override
        {
            return std::make_unique<FailAfterFirstSelectionPolicy>(failure);
        }

        std::size_t selectNext(std::span<const Atlas::SchedulingCandidate>) override
        {
            if (selected)
            {
                std::rethrow_exception(failure);
            }
            selected = true;
            return 0U;
        }

      private:
        std::exception_ptr failure;
        bool selected{ false };
    };

    class ImmediateCpuExecutor final : public Atlas::CpuExecutor
    {
      public:
        ImmediateCpuExecutor() : CpuExecutor{ 2U } {}

        bool submit(const Atlas::TaskHandle handle, Atlas::TaskFunction function) override
        {
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
            completions.emplace_back(Atlas::TaskCompletion{ handle, exception });
            return true;
        }

        std::optional<Atlas::TaskCompletion> waitForCompletion() override
        {
            if (completions.empty())
            {
                return std::nullopt;
            }
            Atlas::TaskCompletion completion{ std::move(completions.front()) };
            completions.pop_front();
            return completion;
        }

        void shutdown() noexcept override {}

      private:
        std::deque<Atlas::TaskCompletion> completions;
    };

    class MissingCompletionCpuExecutor final : public Atlas::CpuExecutor
    {
      public:
        MissingCompletionCpuExecutor() : CpuExecutor{ 2U } {}

        bool submit(Atlas::TaskHandle, Atlas::TaskFunction) override
        {
            return true;
        }

        std::optional<Atlas::TaskCompletion> waitForCompletion() override
        {
            return std::nullopt;
        }

        void shutdown() noexcept override {}
    };

    class CloneCountingPolicy final : public Atlas::SchedulingPolicy
    {
      public:
        CloneCountingPolicy() : cloneCounter{ std::make_shared<std::size_t>(0U) } {}

        explicit CloneCountingPolicy(std::shared_ptr<std::size_t> counter) : cloneCounter{ std::move(counter) } {}

        std::unique_ptr<Atlas::SchedulingPolicy> clone() const override
        {
            ++*cloneCounter;
            return std::make_unique<CloneCountingPolicy>(cloneCounter);
        }

        std::size_t selectNext(std::span<const Atlas::SchedulingCandidate>) override
        {
            return 0U;
        }

        std::size_t cloneCount() const noexcept
        {
            return *cloneCounter;
        }

      private:
        std::shared_ptr<std::size_t> cloneCounter;
    };

    class UnusedGpuExecutor final : public Atlas::GpuExecutor
    {
      public:
        UnusedGpuExecutor() : GpuExecutor{ 1U } {}

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
} // namespace

TEST_CASE("FIFO scheduling selects stable enqueue order", "[UNIT]")
{
    Atlas::FifoSchedulingPolicy policy;
    const std::vector<Atlas::SchedulingCandidate> candidates{ { makeHandle(1U), 8U }, { makeHandle(2U), 1U } };

    REQUIRE(policy.selectNext(candidates) == 0U);
    REQUIRE_THROWS_AS(policy.selectNext({}), std::invalid_argument);
    REQUIRE(dynamic_cast<Atlas::FifoSchedulingPolicy*>(policy.clone().get()) != nullptr);
}

TEST_CASE("Round-robin scheduling retains a task for its work-unit quantum", "[UNIT]")
{
    const Atlas::TaskHandle first{ makeHandle(1U) };
    const Atlas::TaskHandle second{ makeHandle(2U) };
    Atlas::RoundRobinSchedulingPolicy policy{ 2U };

    REQUIRE(policy.getQuantum() == 2U);
    REQUIRE(policy.selectNext(std::vector<Atlas::SchedulingCandidate>{ { first, 0U }, { second, 0U } }) == 0U);
    REQUIRE(policy.selectNext(std::vector<Atlas::SchedulingCandidate>{ { second, 0U }, { first, 0U } }) == 1U);
    REQUIRE(policy.selectNext(std::vector<Atlas::SchedulingCandidate>{ { second, 0U }, { first, 0U } }) == 0U);

    std::unique_ptr<Atlas::SchedulingPolicy> clone{ policy.clone() };
    REQUIRE(clone->selectNext(std::vector<Atlas::SchedulingCandidate>{ { first, 0U }, { second, 0U } }) == 0U);
    REQUIRE_THROWS_AS(Atlas::RoundRobinSchedulingPolicy{ 0U }, std::invalid_argument);

    Atlas::RoundRobinSchedulingPolicy singleUnitQuantum;
    REQUIRE(singleUnitQuantum.selectNext(std::vector<Atlas::SchedulingCandidate>{ { first, 0U }, { second, 0U } }) == 0U);
    REQUIRE(singleUnitQuantum.selectNext(std::vector<Atlas::SchedulingCandidate>{ { second, 0U }, { first, 0U } }) == 0U);
}

TEST_CASE("Static-priority scheduling selects lower values with stable ties", "[UNIT]")
{
    Atlas::StaticPrioritySchedulingPolicy policy;
    const std::vector<Atlas::SchedulingCandidate> candidates{ { makeHandle(1U), 9U },
                                                              { makeHandle(2U), 2U },
                                                              { makeHandle(3U), 2U },
                                                              { makeHandle(4U), std::numeric_limits<std::uint32_t>::max() } };

    REQUIRE(policy.selectNext(candidates) == 1U);
    REQUIRE_THROWS_AS(policy.selectNext({}), std::invalid_argument);
    REQUIRE(dynamic_cast<Atlas::StaticPrioritySchedulingPolicy*>(policy.clone().get()) != nullptr);
}

TEST_CASE("KahnScheduler reports a thrown policy failure", "[UNIT]")
{
    Atlas::TaskGraph graph;
    REQUIRE(graph.addCpuTask([] {}).has_value());
    REQUIRE(graph.finishTaskGraph());
    const std::exception_ptr failure{ std::make_exception_ptr(std::runtime_error{ "policy failed" }) };
    ThrowingPolicy policy{ failure };
    Atlas::SynchronousCpuExecutor executor;
    Atlas::KahnScheduler scheduler{ graph, executor, policy };

    const Atlas::SchedulerResult result{ scheduler.execute() };

    REQUIRE(result.status == Atlas::SchedulerStatus::PolicyError);
    REQUIRE(result.executedTaskCount == 0U);
    REQUIRE(result.exception == failure);
}

TEST_CASE("KahnScheduler applies stable static priority to CPU-only work", "[UNIT]")
{
    Atlas::TaskGraph graph;
    std::vector<int> order;
    REQUIRE(graph.addCpuTask([&order] { order.emplace_back(1); }, Atlas::TaskOptions{ "Lower", Atlas::ExecutionResource::CPU, 8U })
                .has_value());
    REQUIRE(graph
                .addCpuTask([&order] { order.emplace_back(2); },
                            Atlas::TaskOptions{ "First equal priority", Atlas::ExecutionResource::CPU, 2U })
                .has_value());
    REQUIRE(graph
                .addCpuTask([&order] { order.emplace_back(3); },
                            Atlas::TaskOptions{ "Second equal priority", Atlas::ExecutionResource::CPU, 2U })
                .has_value());
    REQUIRE(graph.finishTaskGraph());
    Atlas::StaticPrioritySchedulingPolicy policy;
    Atlas::SynchronousCpuExecutor executor;
    Atlas::KahnScheduler scheduler{ graph, executor, policy };

    const Atlas::SchedulerResult result{ scheduler.execute() };

    REQUIRE(result.status == Atlas::SchedulerStatus::Success);
    REQUIRE(order == std::vector<int>{ 2, 3, 1 });
}

TEST_CASE("KahnScheduler rejects an invalid policy index", "[UNIT]")
{
    Atlas::TaskGraph graph;
    const std::optional<Atlas::TaskHandle> task{ graph.addCpuTask([] {}) };
    REQUIRE(task.has_value());
    REQUIRE(graph.finishTaskGraph());
    InvalidIndexPolicy policy;
    Atlas::SynchronousCpuExecutor executor;
    Atlas::KahnScheduler scheduler{ graph, executor, policy };

    const Atlas::SchedulerResult result{ scheduler.execute() };

    REQUIRE(result.status == Atlas::SchedulerStatus::PolicyError);
    REQUIRE(result.exception != nullptr);
    REQUIRE(graph.findTask(task.value()).value()->executionInfo.state == Atlas::TaskState::Ready);
    REQUIRE_THROWS_AS(std::rethrow_exception(result.exception), std::out_of_range);
}

TEST_CASE("KahnScheduler rejects a null policy clone before execution", "[UNIT]")
{
    Atlas::TaskGraph graph;
    REQUIRE(graph.addCpuTask([] {}).has_value());
    REQUIRE(graph.finishTaskGraph());
    NullClonePolicy policy;
    Atlas::SynchronousCpuExecutor executor;

    REQUIRE_THROWS_AS(Atlas::KahnScheduler(graph, executor, policy), std::invalid_argument);
}

TEST_CASE("KahnScheduler clones independent policy state for CPU and GPU", "[UNIT]")
{
    Atlas::TaskGraph graph;
    REQUIRE(graph.addCpuTask([] {}).has_value());
    REQUIRE(graph.finishTaskGraph());
    CloneCountingPolicy policy;
    Atlas::SynchronousCpuExecutor cpuExecutor;
    UnusedGpuExecutor gpuExecutor;

    Atlas::KahnScheduler scheduler{ graph, cpuExecutor, gpuExecutor, policy };

    REQUIRE(policy.cloneCount() == 2U);
}

TEST_CASE("KahnScheduler drains accepted work after a later policy failure", "[UNIT]")
{
    Atlas::TaskGraph graph;
    const std::optional<Atlas::TaskHandle> accepted{ graph.addCpuTask([] {}) };
    const std::optional<Atlas::TaskHandle> rejected{ graph.addCpuTask([] {}) };
    const std::optional<Atlas::TaskHandle> dependent{ graph.addCpuTask([] {}) };
    REQUIRE(accepted.has_value());
    REQUIRE(rejected.has_value());
    REQUIRE(dependent.has_value());
    REQUIRE(graph.addDependency(dependent.value(), accepted.value()));
    REQUIRE(graph.finishTaskGraph());
    const std::exception_ptr policyFailure{ std::make_exception_ptr(std::runtime_error{ "second selection failed" }) };
    FailAfterFirstSelectionPolicy policy{ policyFailure };
    ImmediateCpuExecutor executor;
    Atlas::KahnScheduler scheduler{ graph, executor, policy };

    const Atlas::SchedulerResult result{ scheduler.execute() };

    REQUIRE(result.status == Atlas::SchedulerStatus::PolicyError);
    REQUIRE(result.executedTaskCount == 1U);
    REQUIRE(result.exception == policyFailure);
    REQUIRE(graph.findTask(accepted.value()).value()->executionInfo.state == Atlas::TaskState::Success);
    REQUIRE(graph.findTask(rejected.value()).value()->executionInfo.state == Atlas::TaskState::Ready);
    REQUIRE(graph.findTask(dependent.value()).value()->executionInfo.state == Atlas::TaskState::Blocked);
}

TEST_CASE("KahnScheduler preserves a task exception while reporting a policy error", "[UNIT]")
{
    Atlas::TaskGraph graph;
    const std::exception_ptr taskFailure{ std::make_exception_ptr(std::runtime_error{ "task failed" }) };
    REQUIRE(graph.addCpuTask([taskFailure] { std::rethrow_exception(taskFailure); }).has_value());
    REQUIRE(graph.addCpuTask([] {}).has_value());
    REQUIRE(graph.finishTaskGraph());
    const std::exception_ptr policyFailure{ std::make_exception_ptr(std::runtime_error{ "second selection failed" }) };
    FailAfterFirstSelectionPolicy policy{ policyFailure };
    ImmediateCpuExecutor executor;
    Atlas::KahnScheduler scheduler{ graph, executor, policy };

    const Atlas::SchedulerResult result{ scheduler.execute() };

    REQUIRE(result.status == Atlas::SchedulerStatus::PolicyError);
    REQUIRE(result.executedTaskCount == 0U);
    REQUIRE(result.exception == taskFailure);
}

TEST_CASE("KahnScheduler gives executor failure precedence over policy error", "[UNIT]")
{
    Atlas::TaskGraph graph;
    REQUIRE(graph.addCpuTask([] {}).has_value());
    REQUIRE(graph.addCpuTask([] {}).has_value());
    REQUIRE(graph.finishTaskGraph());
    const std::exception_ptr policyFailure{ std::make_exception_ptr(std::runtime_error{ "second selection failed" }) };
    FailAfterFirstSelectionPolicy policy{ policyFailure };
    MissingCompletionCpuExecutor executor;
    Atlas::KahnScheduler scheduler{ graph, executor, policy };

    const Atlas::SchedulerResult result{ scheduler.execute() };

    REQUIRE(result.status == Atlas::SchedulerStatus::ExecutorUnavailable);
    REQUIRE(result.executedTaskCount == 0U);
    REQUIRE(result.exception == policyFailure);
}
