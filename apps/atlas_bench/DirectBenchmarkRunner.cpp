#include "DirectBenchmarkRunner.h"

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
#include <cstring>
#include <deque>
#include <fstream>
#include <iterator>
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

#ifndef ATLAS_BENCHMARK_VULKAN_ENABLED
#define ATLAS_BENCHMARK_VULKAN_ENABLED 0
#endif

namespace Atlas::Benchmark
{
    namespace
    {
        using Clock = std::chrono::steady_clock;

        std::uint64_t cpuKernel(std::uint64_t value, const std::uint64_t iterations) noexcept
        {
            for (std::uint64_t iteration{ 0U }; iteration < iterations; ++iteration)
            {
                value ^= value >> 12U;
                value ^= value << 25U;
                value ^= value >> 27U;
                value *= 2'685'821'657'736'338'717ULL;
            }
            return value;
        }

#if ATLAS_BENCHMARK_VULKAN_ENABLED
        std::size_t checkedElementCount(const DispatchDimensions dimensions)
        {
            const std::size_t x{ dimensions.x };
            const std::size_t y{ dimensions.y };
            const std::size_t z{ dimensions.z };
            if (x > std::numeric_limits<std::size_t>::max() / y || x * y > std::numeric_limits<std::size_t>::max() / z)
            {
                throw std::runtime_error{ "GPU benchmark workgroup product overflows size_t" };
            }
            return x * y * z;
        }

        std::vector<std::uint32_t> readShader()
        {
            std::ifstream stream{ ATLAS_BENCHMARK_SPIRV_PATH, std::ios::binary };
            if (!stream)
            {
                throw std::runtime_error{ "Unable to open the compiled benchmark shader" };
            }
            const std::vector<char> bytes{ std::istreambuf_iterator<char>{ stream }, std::istreambuf_iterator<char>{} };
            if (stream.bad() || bytes.empty() || bytes.size() % sizeof(std::uint32_t) != 0U)
            {
                throw std::runtime_error{ "The compiled benchmark shader is malformed" };
            }
            std::vector<std::uint32_t> words(bytes.size() / sizeof(std::uint32_t));
            std::memcpy(words.data(), bytes.data(), bytes.size());
            return words;
        }

        struct GpuResources final
        {
            explicit GpuResources(const GpuWorkloadConfig& config)
                : elementCount{ checkedElementCount(config.workgroups) }, runtime{},
                  dimensionsBuffer{ runtime.createBuffer(4U * sizeof(std::uint32_t)) },
                  outputBuffer{ runtime.createBuffer(elementCount * sizeof(std::uint32_t)) },
                  pipeline{ runtime.createComputePipeline(ComputeShader{ readShader(), "main", { 0U, 1U } }) },
                  dispatch{ pipeline,
                            { { 0U, dimensionsBuffer, BufferAccess::ReadOnly }, { 1U, outputBuffer, BufferAccess::ReadWrite } },
                            config.workgroups },
                  executor{ runtime }
            {
                const std::vector<std::uint32_t> dimensions{ config.workgroups.x, config.workgroups.y, config.workgroups.z, 0U };
                runtime.upload(dimensionsBuffer, std::as_bytes(std::span{ dimensions }));
            }

            void reset(const std::uint64_t seed)
            {
                initialValues.resize(elementCount);
                for (std::size_t index{ 0U }; index < elementCount; ++index)
                {
                    initialValues.at(index) = static_cast<std::uint32_t>(seed) ^ static_cast<std::uint32_t>(index + 1U);
                }
                runtime.upload(outputBuffer, std::as_bytes(std::span{ initialValues }));
            }

            void verify(const std::size_t gpuTaskCount) const
            {
                std::vector<std::uint32_t> actual(elementCount);
                runtime.download(outputBuffer, std::as_writable_bytes(std::span{ actual }));
                for (std::size_t index{ 0U }; index < elementCount; ++index)
                {
                    std::uint32_t expected{ initialValues.at(index) };
                    for (std::size_t task{ 0U }; task < gpuTaskCount; ++task)
                    {
                        expected = expected * 1'664'525U + 1'013'904'223U;
                    }
                    if (actual.at(index) != expected)
                    {
                        throw std::runtime_error{ "Direct GPU benchmark output validation failed" };
                    }
                }
            }

            std::size_t elementCount{ 0U };
            VulkanRuntime runtime;
            VulkanBuffer dimensionsBuffer;
            VulkanBuffer outputBuffer;
            VulkanComputePipeline pipeline;
            VulkanDispatch dispatch;
            VulkanExecutor executor;
            std::vector<std::uint32_t> initialValues;
        };
#endif

        struct DirectTaskState final
        {
            TaskState state{ TaskState::Blocked };
            std::size_t remainingDependencies{ 0U };
            std::vector<std::size_t> dependants;
            std::chrono::microseconds executionDuration{ 0 };
            std::chrono::microseconds readyWaitDuration{ 0 };
            std::optional<std::chrono::microseconds> responseDuration;
            Clock::time_point readyEntered;
            Clock::time_point firstReady;
            bool observedReady{ false };
        };

        std::chrono::microseconds elapsed(const Clock::time_point start, const Clock::time_point end)
        {
            return std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        }
    } // namespace

    struct DirectBenchmarkRunner::Impl final
    {
        explicit Impl(ExperimentManifest experimentConfig)
            : manifest{ std::move(experimentConfig) }, cpuExecutor{ manifest.workerCount }
        {
            if (manifest.gpu.taskCount != 0U)
            {
#if ATLAS_BENCHMARK_VULKAN_ENABLED
                gpuResources = std::make_unique<GpuResources>(manifest.gpu);
#else
                throw std::runtime_error{ "This atlas_bench build does not include Vulkan benchmark support" };
#endif
            }
        }

        RunRecord runSingle(const std::uint64_t seed, const std::size_t repetition)
        {
            const std::vector<TaskDescriptor> descriptors{ generateWorkload(manifest, seed) };
#if ATLAS_BENCHMARK_VULKAN_ENABLED
            if (gpuResources != nullptr)
            {
                gpuResources->reset(seed);
            }
#endif
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

            CompletionChannel channel{ descriptors.size() };
            std::deque<std::size_t> cpuReady;
            std::deque<std::size_t> gpuReady;
            std::size_t cpuInFlight{ 0U };
            std::size_t gpuInFlight{ 0U };
            std::size_t totalInFlight{ 0U };
            std::size_t completedTasks{ 0U };
            SchedulerStatus status{ SchedulerStatus::Success };
            std::exception_ptr firstException;
            std::chrono::microseconds controlActive{ 0 };

            const Clock::time_point runStart{ Clock::now() };
            Clock::time_point activeStart{ runStart };
            const auto enqueueReady = [&](const std::size_t index)
            {
                DirectTaskState& state{ states.at(index) };
                state.state = TaskState::Ready;
                state.readyEntered = Clock::now();
                if (!state.observedReady)
                {
                    state.firstReady = state.readyEntered;
                    state.observedReady = true;
                }
                (descriptors.at(index).resource == ExecutionResource::CPU ? cpuReady : gpuReady).push_back(index);
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
                                cpuResults->at(index) = cpuKernel(initial, iterations);
                            },
                            channel);
                    }
                    else
                    {
#if ATLAS_BENCHMARK_VULKAN_ENABLED
                        accepted = gpuResources->executor.submit(handles.at(index), gpuResources->dispatch, channel);
#endif
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
                    markInfrastructureFailure();
                    return false;
                }
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
#if ATLAS_BENCHMARK_VULKAN_ENABLED
                    while (gpuResources != nullptr && gpuInFlight < gpuResources->executor.maxConcurrency() && !gpuReady.empty() &&
                           status == SchedulerStatus::Success)
                    {
                        submitOne(gpuReady, ExecutionResource::GPU);
                    }
#endif
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
                const Clock::time_point completed{ Clock::now() };
                if (completion.succeeded())
                {
                    state.state = TaskState::Success;
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
            const std::chrono::microseconds executionTime{ elapsed(runStart, Clock::now()) };

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
                    if (cpuResults->at(descriptor.index) != cpuKernel(initial, manifest.cpu.iterations))
                    {
                        throw std::runtime_error{ "Direct CPU benchmark output validation failed" };
                    }
                }
#if ATLAS_BENCHMARK_VULKAN_ENABLED
                if (gpuResources != nullptr)
                {
                    gpuResources->verify(manifest.gpu.taskCount);
                }
#endif
            }

            std::vector<TaskMeasurement> tasks;
            tasks.reserve(descriptors.size());
            for (const TaskDescriptor& descriptor : descriptors)
            {
                const DirectTaskState& state{ states.at(descriptor.index) };
                tasks.push_back(TaskMeasurement{ descriptor.index, descriptor.name, descriptor.resource, descriptor.priority,
                                                 descriptor.burstIndex, state.state, state.executionDuration, state.readyWaitDuration,
                                                 state.responseDuration, 0U, state.state == TaskState::Success ? 1U : 0U, 1U });
            }

            SchedulerResult result{ status, completedTasks, firstException, executionTime, controlActive, {}, 0U };
            RunRecord record{ manifest.experimentId, seed,         repetition,  result, std::move(tasks), {},
                              std::nullopt,          std::nullopt, std::nullopt };
#if ATLAS_BENCHMARK_VULKAN_ENABLED
            if (gpuResources != nullptr)
            {
                record.gpuDeviceName = gpuResources->runtime.deviceInfo().name;
                record.gpuApiVersion = gpuResources->runtime.deviceInfo().apiVersion;
                record.gpuDeviceType = static_cast<std::uint32_t>(gpuResources->runtime.deviceInfo().type);
            }
#endif
            record.metrics = calculateMetrics(record.schedulerResult, record.tasks, manifest.workerCount);
            return record;
        }

        ExperimentManifest manifest;
        WorkerpoolExecutor cpuExecutor;
#if ATLAS_BENCHMARK_VULKAN_ENABLED
        std::unique_ptr<GpuResources> gpuResources;
#endif
    };

    DirectBenchmarkRunner::DirectBenchmarkRunner(ExperimentManifest experiment)
        : implementation{ std::make_unique<Impl>(std::move(experiment)) }
    {
    }

    DirectBenchmarkRunner::~DirectBenchmarkRunner() = default;

    RunRecord DirectBenchmarkRunner::runSingle(const std::uint64_t seed, const std::size_t repetition)
    {
        return implementation->runSingle(seed, repetition);
    }
} // namespace Atlas::Benchmark
