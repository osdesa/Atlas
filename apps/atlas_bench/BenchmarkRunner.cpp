#include "BenchmarkRunner.h"

#include "WorkloadGenerator.h"
#include "atlas/Executor/VulkanExecutor.h"
#include "atlas/Executor/WorkerpoolExecutor.h"
#include "atlas/Scheduler/FifoSchedulingPolicy.h"
#include "atlas/Scheduler/KahnScheduler.h"
#include "atlas/Scheduler/RoundRobinSchedulingPolicy.h"
#include "atlas/Scheduler/StaticPrioritySchedulingPolicy.h"
#include "atlas/Tasking/TaskGraph.h"
#include "atlas/Vulkan/VulkanRuntime.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

/**
 * @file BenchmarkRunner.cpp
 * @brief Implements fresh-graph repeated benchmark execution.
 */

#ifndef ATLAS_BENCHMARK_VULKAN_ENABLED
#define ATLAS_BENCHMARK_VULKAN_ENABLED 0
#endif

namespace Atlas::Benchmark
{
    namespace
    {
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

        std::unique_ptr<SchedulingPolicy> createPolicy(const PolicyConfig& config)
        {
            switch (config.kind)
            {
            case PolicyKind::Fifo:
                return std::make_unique<FifoSchedulingPolicy>();
            case PolicyKind::RoundRobin:
                return std::make_unique<RoundRobinSchedulingPolicy>(config.quantum);
            case PolicyKind::StaticPriority:
                return std::make_unique<StaticPrioritySchedulingPolicy>();
            }
            throw std::logic_error{ "Unknown benchmark policy kind" };
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
                        throw std::runtime_error{ "GPU benchmark output validation failed" };
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
    } // namespace

    struct BenchmarkRunner::Impl final
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

        RunRecord executeOne(const std::uint64_t seed, const std::size_t repetition, const std::vector<TaskDescriptor>& descriptors)
        {
#if ATLAS_BENCHMARK_VULKAN_ENABLED
            if (gpuResources != nullptr)
            {
                gpuResources->reset(seed);
            }
#endif
            auto cpuResults{ std::make_shared<std::vector<std::uint64_t>>(descriptors.size(), 0U) };
            TaskGraph graph;
            std::vector<TaskHandle> handles;
            handles.reserve(descriptors.size());
            for (const TaskDescriptor& descriptor : descriptors)
            {
                std::optional<TaskHandle> handle;
                const TaskOptions options{ descriptor.name, descriptor.resource, descriptor.priority };
                if (descriptor.resource == ExecutionResource::CPU)
                {
                    handle = graph.addCpuTask(
                        [cpuResults, index = descriptor.index, seed, iterations = manifest.cpu.iterations]
                        {
                            const std::uint64_t initial{ seed ^ (static_cast<std::uint64_t>(index) + 0x9E3779B97F4A7C15ULL) };
                            cpuResults->at(index) = cpuKernel(initial, iterations);
                        },
                        options);
                }
                else
                {
#if ATLAS_BENCHMARK_VULKAN_ENABLED
                    if (manifest.gpu.sliced)
                    {
                        handle =
                            graph.addGpuTask(SlicedVulkanDispatch{ gpuResources->dispatch, manifest.gpu.sliceWorkgroups }, options);
                    }
                    else
                    {
                        handle = graph.addGpuTask(gpuResources->dispatch, options);
                    }
#else
                    throw std::logic_error{ "GPU descriptor reached a CPU-only benchmark build" };
#endif
                }
                if (!handle.has_value())
                {
                    throw std::runtime_error{ "Unable to add a generated benchmark task" };
                }
                handles.push_back(handle.value());
            }

            for (const TaskDescriptor& descriptor : descriptors)
            {
                for (const std::size_t dependency : descriptor.dependencies)
                {
                    if (!graph.addDependency(handles.at(descriptor.index), handles.at(dependency)))
                    {
                        throw std::runtime_error{ "Unable to add a generated benchmark dependency" };
                    }
                }
            }
            if (!graph.finishTaskGraph())
            {
                throw std::runtime_error{ "Unable to finalise a generated benchmark graph" };
            }

            std::unique_ptr<SchedulingPolicy> policy{ createPolicy(manifest.policy) };
            SchedulerResult result;
            if (manifest.gpu.taskCount == 0U)
            {
                KahnScheduler scheduler{ graph, cpuExecutor, *policy };
                result = scheduler.execute();
            }
            else
            {
#if ATLAS_BENCHMARK_VULKAN_ENABLED
                KahnScheduler scheduler{ graph, cpuExecutor, gpuResources->executor, *policy };
                result = scheduler.execute();
#endif
            }

            std::vector<TaskMeasurement> tasks;
            tasks.reserve(descriptors.size());
            for (const TaskDescriptor& descriptor : descriptors)
            {
                const auto task{ graph.findTask(handles.at(descriptor.index)) };
                if (!task.has_value())
                {
                    throw std::runtime_error{ "Generated task disappeared from its graph" };
                }
                const TaskExecutionInfo& info{ task.value()->executionInfo };
                tasks.push_back(TaskMeasurement{ descriptor.index, descriptor.name, descriptor.resource, descriptor.priority,
                                                 descriptor.burstIndex, info.state, info.executionDuration, info.readyWaitDuration,
                                                 info.responseDuration, info.selectionBypassCount, info.completedWorkUnitCount,
                                                 info.totalWorkUnitCount });
            }

            if (result.status == SchedulerStatus::Success)
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
                        throw std::runtime_error{ "CPU benchmark output validation failed" };
                    }
                }
#if ATLAS_BENCHMARK_VULKAN_ENABLED
                if (gpuResources != nullptr)
                {
                    gpuResources->verify(manifest.gpu.taskCount);
                }
#endif
            }

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

        BenchmarkBatch run()
        {
            BenchmarkBatch batch;
            for (const std::uint64_t seed : manifest.seeds)
            {
                const std::vector<TaskDescriptor> descriptors{ generateWorkload(manifest, seed) };
                for (std::size_t warmup{ 0U }; warmup < manifest.warmupRuns; ++warmup)
                {
                    const RunRecord record{ executeOne(seed, warmup, descriptors) };
                    if (record.schedulerResult.status != SchedulerStatus::Success)
                    {
                        throw std::runtime_error{ "A benchmark warmup run failed" };
                    }
                }
                for (std::size_t repetition{ 0U }; repetition < manifest.repetitions; ++repetition)
                {
                    batch.records.push_back(executeOne(seed, repetition, descriptors));
                    if (batch.records.back().schedulerResult.status != SchedulerStatus::Success)
                    {
                        batch.succeeded = false;
                        return batch;
                    }
                }
            }
            return batch;
        }

        ExperimentManifest manifest;
        WorkerpoolExecutor cpuExecutor;
#if ATLAS_BENCHMARK_VULKAN_ENABLED
        std::unique_ptr<GpuResources> gpuResources;
#endif
    };

    BenchmarkRunner::BenchmarkRunner(ExperimentManifest experiment) : implementation{ std::make_unique<Impl>(std::move(experiment)) }
    {
    }

    BenchmarkRunner::~BenchmarkRunner() = default;

    BenchmarkBatch BenchmarkRunner::run()
    {
        return implementation->run();
    }

    RunRecord BenchmarkRunner::runSingle(const std::uint64_t seed, const std::size_t repetition)
    {
        const std::vector<TaskDescriptor> descriptors{ generateWorkload(implementation->manifest, seed) };
        return implementation->executeOne(seed, repetition, descriptors);
    }
} // namespace Atlas::Benchmark
