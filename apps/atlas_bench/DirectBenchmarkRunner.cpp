#include "DirectBenchmarkRunner.h"

#include "BenchmarkWork.h"
#include "WorkloadGenerator.h"
#include "atlas/Executor/CompletionChannel.h"
#include "atlas/Executor/VulkanExecutor.h"
#include "atlas/Executor/WorkerpoolExecutor.h"
#include "atlas/Scheduler/SchedulerStatus.h"
#include "atlas/Tasking/GraphId.h"
#include "atlas/Tasking/TaskId.h"
#include "atlas/Tasking/TaskState.h"
#include "atlas/Vulkan/VulkanRuntime.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

/**
 * @file DirectBenchmarkRunner.cpp
 * @brief Implements direct dependency coordination over Atlas executors.
 */

namespace Atlas::Benchmark
{
    namespace
    {
        using Clock = std::chrono::steady_clock;

        struct DirectTaskState final
        {
            TaskState state{ TaskState::Blocked };
            std::size_t remainingDependencies{ 0U };
            std::vector<std::size_t> dependants;
            std::chrono::nanoseconds executionDuration{ 0 };
            std::optional<std::chrono::nanoseconds> deviceExecutionDuration;
            std::chrono::nanoseconds readyWaitDuration{ 0 };
            std::optional<std::chrono::nanoseconds> responseDuration;
            Clock::time_point readyEntered;
            Clock::time_point firstReady;
            bool observedReady{ false };
        };

        std::chrono::nanoseconds elapsed(const Clock::time_point start, const Clock::time_point end)
        {
            return std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
        }
    } // namespace

    struct DirectBenchmarkRunner::Impl final
    {
        explicit Impl(ExperimentManifest experimentConfig)
            : manifest{ std::move(experimentConfig) }, cpuExecutor{ manifest.workerCount }, runtime{}, gpuExecutor{ runtime }
        {
            if (manifest.gpu.taskCount != 0U)
            {
                gpuResources = std::make_unique<Detail::GpuResources>(runtime, manifest.gpu);
            }
        }

        RunRecord runSingle(const std::uint64_t seed, const std::size_t repetition, const std::vector<TaskDescriptor>& descriptors,
                            TraceSession* const traceSession)
        {
            if (gpuResources != nullptr)
            {
                gpuResources->reset(seed);
            }
            auto cpuResults{ std::make_shared<std::vector<std::uint64_t>>(descriptors.size(), 0U) };
            const GraphId graphId{ GraphId::create() };
            if (!graphId.isValid() || descriptors.size() > std::numeric_limits<std::uint32_t>::max())
            {
                throw std::runtime_error{ "Unable to allocate direct benchmark task identities" };
            }

            std::vector<TaskHandle> handles;
            std::vector<DirectTaskState> states(descriptors.size());
            handles.reserve(descriptors.size());
            for (std::size_t index{ 0U }; index < descriptors.size(); ++index)
            {
                handles.emplace_back(TaskId{ static_cast<std::uint32_t>(index + 1U) }, graphId);
                states.at(index).remainingDependencies = descriptors.at(index).dependencies.size();
                for (const std::size_t dependency : descriptors.at(index).dependencies)
                {
                    states.at(dependency).dependants.push_back(index);
                }
            }

            CompletionChannel channel{ descriptors.size(), traceSession };
            std::deque<std::size_t> cpuReady;
            std::deque<std::size_t> gpuReady;
            std::size_t cpuInFlight{ 0U };
            std::size_t gpuInFlight{ 0U };
            std::size_t totalInFlight{ 0U };
            std::size_t completedTasks{ 0U };
            SchedulerStatus status{ SchedulerStatus::Success };
            std::exception_ptr firstException;
            std::chrono::nanoseconds controlActive{ 0 };

            const Clock::time_point runStart{ Clock::now() };
            Clock::time_point activeStart{ runStart };
            const auto emit = [&](const TraceEventKind kind, const std::optional<std::size_t> index = std::nullopt,
                                  const TaskState previous = TaskState::Unknown, const TaskState state = TaskState::Unknown)
            {
                if (traceSession == nullptr)
                {
                    return;
                }
                TraceEvent event{ .kind = kind, .source = TraceEventSource::Scheduler, .previousState = previous, .state = state };
                if (index.has_value())
                {
                    const TaskDescriptor& descriptor{ descriptors.at(index.value()) };
                    event.resource = descriptor.resource;
                    event.hasTask = true;
                    event.hasResource = true;
                    event.graphId = graphId.getValue();
                    event.taskId = handles.at(index.value()).getTaskID().getValue();
                    event.priority = descriptor.priority;
                }
                traceSession->emit(std::move(event));
            };
            emit(TraceEventKind::SchedulerStarted);
            const auto enqueueReady = [&](const std::size_t index)
            {
                DirectTaskState& state{ states.at(index) };
                const TaskState previous{ state.state };
                state.state = TaskState::Ready;
                state.readyEntered = Clock::now();
                if (!state.observedReady)
                {
                    state.firstReady = state.readyEntered;
                    state.observedReady = true;
                }
                (descriptors.at(index).resource == ExecutionResource::CPU ? cpuReady : gpuReady).push_back(index);
                emit(TraceEventKind::TaskReady, index, previous, TaskState::Ready);
            };
            for (std::size_t index{ 0U }; index < states.size(); ++index)
            {
                if (states.at(index).remainingDependencies == 0U)
                {
                    enqueueReady(index);
                }
            }

            const auto markInfrastructureFailure = [&] { status = SchedulerStatus::ExecutorUnavailable; };

            const auto submitOne = [&](std::deque<std::size_t>& ready, const ExecutionResource resource) -> bool
            {
                if (ready.empty())
                {
                    return false;
                }
                const std::size_t index{ ready.front() };
                ready.pop_front();
                DirectTaskState& state{ states.at(index) };
                const Clock::time_point selected{ Clock::now() };
                state.readyWaitDuration += elapsed(state.readyEntered, selected);
                state.state = TaskState::Running;
                emit(TraceEventKind::TaskSelected, index, TaskState::Ready, TaskState::Running);
                emit(TraceEventKind::SubmissionRequested, index, TaskState::Running, TaskState::Running);
                controlActive += elapsed(activeStart, selected);

                bool accepted{ false };
                try
                {
                    if (resource == ExecutionResource::CPU)
                    {
                        accepted = cpuExecutor.submit(
                            handles.at(index),
                            [cpuResults, index, seed, iterations = manifest.cpu.iterations]
                            {
                                const std::uint64_t initial{ seed ^ (static_cast<std::uint64_t>(index) + 0x9E3779B97F4A7C15ULL) };
                                cpuResults->at(index) = Detail::runCpuKernel(initial, iterations);
                            },
                            channel);
                    }
                    else
                    {
                        accepted = gpuResources != nullptr && gpuExecutor.submit(handles.at(index), gpuResources->dispatch(), channel);
                    }
                }
                catch (...)
                {
                    accepted = false;
                    firstException = std::current_exception();
                }
                activeStart = Clock::now();
                if (!accepted)
                {
                    state.state = TaskState::Failure;
                    emit(TraceEventKind::SubmissionRejected, index, TaskState::Running, TaskState::Failure);
                    markInfrastructureFailure();
                    return false;
                }
                emit(TraceEventKind::SubmissionAccepted, index, TaskState::Running, TaskState::Running);
                ++totalInFlight;
                if (resource == ExecutionResource::CPU)
                {
                    ++cpuInFlight;
                }
                else
                {
                    ++gpuInFlight;
                }
                return true;
            };

            while (completedTasks < descriptors.size() || totalInFlight != 0U)
            {
                if (status == SchedulerStatus::Success)
                {
                    while (cpuInFlight < cpuExecutor.maxConcurrency() && !cpuReady.empty() && status == SchedulerStatus::Success)
                    {
                        submitOne(cpuReady, ExecutionResource::CPU);
                    }
                    while (gpuResources != nullptr && gpuInFlight < gpuExecutor.maxConcurrency() && !gpuReady.empty() &&
                           status == SchedulerStatus::Success)
                    {
                        submitOne(gpuReady, ExecutionResource::GPU);
                    }
                }

                if (totalInFlight == 0U)
                {
                    if (completedTasks != descriptors.size() && status == SchedulerStatus::Success)
                    {
                        markInfrastructureFailure();
                    }
                    break;
                }

                controlActive += elapsed(activeStart, Clock::now());
                const CompletionEvent event{ channel.wait() };
                activeStart = Clock::now();
                if (event.kind != CompletionEventKind::Completion || !event.completion.has_value())
                {
                    markInfrastructureFailure();
                    if (event.kind == CompletionEventKind::ProducerFailure)
                    {
                        if (event.resource == ExecutionResource::CPU)
                        {
                            totalInFlight -= cpuInFlight;
                            cpuInFlight = 0U;
                        }
                        else
                        {
                            totalInFlight -= gpuInFlight;
                            gpuInFlight = 0U;
                        }
                    }
                    continue;
                }

                const TaskCompletion& completion{ event.completion.value() };
                const std::uint32_t taskValue{ completion.handle.getTaskID().getValue() };
                const bool validIdentity{ completion.handle.getGraphID() == graphId && taskValue != 0U &&
                                          static_cast<std::size_t>(taskValue) <= descriptors.size() };
                if (!validIdentity)
                {
                    markInfrastructureFailure();
                    if (completion.resource == ExecutionResource::CPU && cpuInFlight != 0U)
                    {
                        --cpuInFlight;
                        --totalInFlight;
                    }
                    else if (completion.resource == ExecutionResource::GPU && gpuInFlight != 0U)
                    {
                        --gpuInFlight;
                        --totalInFlight;
                    }
                    continue;
                }

                const std::size_t index{ static_cast<std::size_t>(taskValue - 1U) };
                emit(TraceEventKind::CompletionObserved, index, TaskState::Running, TaskState::Running);
                DirectTaskState& state{ states.at(index) };
                const ExecutionResource expectedResource{ descriptors.at(index).resource };
                if (state.state != TaskState::Running || completion.resource != expectedResource || completion.workUnitIndex != 0U)
                {
                    markInfrastructureFailure();
                }
                if (expectedResource == ExecutionResource::CPU)
                {
                    if (cpuInFlight == 0U)
                    {
                        markInfrastructureFailure();
                    }
                    else
                    {
                        --cpuInFlight;
                        --totalInFlight;
                    }
                }
                else if (gpuInFlight == 0U)
                {
                    markInfrastructureFailure();
                }
                else
                {
                    --gpuInFlight;
                    --totalInFlight;
                }

                state.executionDuration += completion.executionDuration;
                state.deviceExecutionDuration = completion.deviceExecutionDuration;
                const Clock::time_point completed{ Clock::now() };
                if (completion.succeeded())
                {
                    state.state = TaskState::Success;
                    emit(TraceEventKind::TaskSucceeded, index, TaskState::Running, TaskState::Success);
                    ++completedTasks;
                    if (state.observedReady)
                    {
                        state.responseDuration = elapsed(state.firstReady, completed);
                    }
                    if (status == SchedulerStatus::Success)
                    {
                        for (const std::size_t dependant : state.dependants)
                        {
                            DirectTaskState& dependantState{ states.at(dependant) };
                            if (dependantState.remainingDependencies == 0U)
                            {
                                markInfrastructureFailure();
                                break;
                            }
                            --dependantState.remainingDependencies;
                            if (dependantState.remainingDependencies == 0U)
                            {
                                enqueueReady(dependant);
                            }
                        }
                    }
                }
                else
                {
                    state.state = TaskState::Failure;
                    emit(TraceEventKind::TaskFailed, index, TaskState::Running, TaskState::Failure);
                    if (state.observedReady)
                    {
                        state.responseDuration = elapsed(state.firstReady, completed);
                    }
                    if (status != SchedulerStatus::ExecutorUnavailable)
                    {
                        status = SchedulerStatus::TaskFailed;
                    }
                    if (firstException == nullptr)
                    {
                        firstException = completion.exception;
                    }
                }
            }
            controlActive += elapsed(activeStart, Clock::now());
            const std::chrono::nanoseconds executionTime{ elapsed(runStart, Clock::now()) };
            emit(TraceEventKind::SchedulerFinished);

            if (status == SchedulerStatus::Success && completedTasks != descriptors.size())
            {
                status = SchedulerStatus::ExecutorUnavailable;
            }
            if (status == SchedulerStatus::Success)
            {
                for (const TaskDescriptor& descriptor : descriptors)
                {
                    if (descriptor.resource != ExecutionResource::CPU)
                    {
                        continue;
                    }
                    const std::uint64_t initial{ seed ^ (static_cast<std::uint64_t>(descriptor.index) + 0x9E3779B97F4A7C15ULL) };
                    if (cpuResults->at(descriptor.index) != Detail::runCpuKernel(initial, manifest.cpu.iterations))
                    {
                        throw std::runtime_error{ "Direct CPU benchmark output validation failed" };
                    }
                }
                if (gpuResources != nullptr)
                {
                    gpuResources->verify(manifest.gpu.taskCount);
                }
            }

            std::vector<TaskMeasurement> tasks;
            tasks.reserve(descriptors.size());
            for (const TaskDescriptor& descriptor : descriptors)
            {
                const DirectTaskState& state{ states.at(descriptor.index) };
                tasks.push_back(TaskMeasurement{ descriptor.index, descriptor.name, descriptor.resource, descriptor.priority,
                                                 descriptor.burstIndex, state.state, state.executionDuration,
                                                 state.deviceExecutionDuration, state.readyWaitDuration, state.responseDuration, 0U,
                                                 state.state == TaskState::Success ? 1U : 0U, 1U });
            }

            SchedulerResult result{ status, completedTasks, firstException, executionTime, controlActive, {}, 0U };
            RunRecord record{ manifest.experimentId, seed,         repetition,   result, std::move(tasks), {},
                              std::nullopt,          std::nullopt, std::nullopt, false };
            if (gpuResources != nullptr)
            {
                record.gpuDeviceName = runtime.deviceInfo().name;
                record.gpuApiVersion = runtime.deviceInfo().apiVersion;
                record.gpuDeviceType = static_cast<std::uint32_t>(runtime.deviceInfo().type);
                record.gpuTimestampSupported = runtime.timestampCapabilities().supported;
            }
            record.metrics = calculateMetrics(record.schedulerResult, record.tasks, manifest.workerCount);
            return record;
        }

        ExperimentManifest manifest;
        WorkerpoolExecutor cpuExecutor;
        VulkanRuntime runtime;
        VulkanExecutor gpuExecutor;
        std::unique_ptr<Detail::GpuResources> gpuResources;
    };

    DirectBenchmarkRunner::DirectBenchmarkRunner(ExperimentManifest experiment)
        : implementation{ std::make_unique<Impl>(std::move(experiment)) }
    {
    }

    DirectBenchmarkRunner::~DirectBenchmarkRunner() = default;

    RunRecord DirectBenchmarkRunner::runSingle(const std::uint64_t seed, const std::size_t repetition,
                                               const std::vector<TaskDescriptor>& descriptors, TraceSession* const traceSession)
    {
        return implementation->runSingle(seed, repetition, descriptors, traceSession);
    }
} // namespace Atlas::Benchmark
