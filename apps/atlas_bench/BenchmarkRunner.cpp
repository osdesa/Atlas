#include "BenchmarkRunner.h"

#include "BenchmarkWork.h"
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
#include <memory>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

/**
 * @file BenchmarkRunner.cpp
 * @brief Implements fresh-graph repeated benchmark execution.
 */

namespace Atlas::Benchmark
{
    namespace
    {
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

    } // namespace

    struct BenchmarkRunner::Impl final
    {
        explicit Impl(ExperimentManifest experimentConfig)
            : manifest{ std::move(experimentConfig) }, cpuExecutor{ manifest.workerCount }, runtime{}, gpuExecutor{ runtime }
        {
            if (manifest.gpu.taskCount != 0U)
            {
                gpuResources = std::make_unique<Detail::GpuResources>(runtime, manifest.gpu);
            }
        }

        RunRecord executeOne(const std::uint64_t seed, const std::size_t repetition, const std::vector<TaskDescriptor>& descriptors,
                             TraceSession* const traceSession)
        {
            if (gpuResources != nullptr)
            {
                gpuResources->reset(seed);
            }
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
                            cpuResults->at(index) = Detail::runCpuKernel(initial, iterations);
                        },
                        options);
                }
                else
                {
                    if (manifest.gpu.sliced)
                    {
                        handle =
                            graph.addGpuTask(SlicedVulkanDispatch{ gpuResources->dispatch(), manifest.gpu.sliceWorkgroups }, options);
                    }
                    else
                    {
                        handle = graph.addGpuTask(gpuResources->dispatch(), options);
                    }
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
            KahnScheduler scheduler{ graph, cpuExecutor, gpuExecutor, *policy, traceSession };
            SchedulerResult result{ scheduler.execute() };

            std::vector<TaskMeasurement> tasks;
            tasks.reserve(descriptors.size());
            for (const TaskDescriptor& descriptor : descriptors)
            {
                const auto task{ graph.snapshotTask(handles.at(descriptor.index)) };
                if (!task.has_value())
                {
                    throw std::runtime_error{ "Generated task disappeared from its graph" };
                }
                const TaskExecutionInfo& info{ task.value().executionInfo };
                tasks.push_back(TaskMeasurement{ descriptor.index, descriptor.name, descriptor.resource, descriptor.priority,
                                                 descriptor.burstIndex, info.state, info.executionDuration,
                                                 info.deviceExecutionDuration, info.readyWaitDuration, info.responseDuration,
                                                 info.selectionBypassCount, info.completedWorkUnitCount, info.totalWorkUnitCount });
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
                    if (cpuResults->at(descriptor.index) != Detail::runCpuKernel(initial, manifest.cpu.iterations))
                    {
                        throw std::runtime_error{ "CPU benchmark output validation failed" };
                    }
                }
                if (gpuResources != nullptr)
                {
                    gpuResources->verify(manifest.gpu.taskCount);
                }
            }

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

        BenchmarkBatch run()
        {
            BenchmarkBatch batch;
            for (const std::uint64_t seed : manifest.seeds)
            {
                const std::vector<TaskDescriptor> descriptors{ generateWorkload(manifest, seed) };
                for (std::size_t warmup{ 0U }; warmup < manifest.warmupRuns; ++warmup)
                {
                    const RunRecord record{ executeOne(seed, warmup, descriptors, nullptr) };
                    if (record.schedulerResult.status != SchedulerStatus::Success)
                    {
                        throw std::runtime_error{ "A benchmark warmup run failed" };
                    }
                }
                for (std::size_t repetition{ 0U }; repetition < manifest.repetitions; ++repetition)
                {
                    batch.records.push_back(executeOne(seed, repetition, descriptors, nullptr));
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
        VulkanRuntime runtime;
        VulkanExecutor gpuExecutor;
        std::unique_ptr<Detail::GpuResources> gpuResources;
    };

    BenchmarkRunner::BenchmarkRunner(ExperimentManifest experiment) : implementation{ std::make_unique<Impl>(std::move(experiment)) }
    {
    }

    BenchmarkRunner::~BenchmarkRunner() = default;

    BenchmarkBatch BenchmarkRunner::run()
    {
        return implementation->run();
    }

    RunRecord BenchmarkRunner::runSingle(const std::uint64_t seed, const std::size_t repetition,
                                         const std::vector<TaskDescriptor>& descriptors, TraceSession* const traceSession)
    {
        return implementation->executeOne(seed, repetition, descriptors, traceSession);
    }
} // namespace Atlas::Benchmark
