#include "WorkloadGenerator.h"

#include <algorithm>
#include <random>
#include <stdexcept>
#include <utility>

/**
 * @file WorkloadGenerator.cpp
 * @brief Implements deterministic benchmark DAG generation.
 */

namespace Atlas::Benchmark
{
    namespace
    {
        void appendUniqueDependency(TaskDescriptor& task, const std::size_t dependency)
        {
            if (std::find(task.dependencies.begin(), task.dependencies.end(), dependency) == task.dependencies.end())
            {
                task.dependencies.push_back(dependency);
            }
        }

        std::pair<std::size_t, std::size_t> burstRange(const std::size_t burst, const std::size_t burstCount,
                                                       const std::size_t taskCount)
        {
            return { (burst * taskCount) / burstCount, ((burst + 1U) * taskCount) / burstCount };
        }

        void applyChain(std::vector<TaskDescriptor>& tasks, const std::size_t begin, const std::size_t end)
        {
            for (std::size_t index{ begin + 1U }; index < end; ++index)
            {
                appendUniqueDependency(tasks.at(index), index - 1U);
            }
        }

        void applyLayers(std::vector<TaskDescriptor>& tasks, const std::size_t begin, const std::size_t end,
                         const std::size_t configuredLayers)
        {
            const std::size_t count{ end - begin };
            const std::size_t layers{ std::min(configuredLayers, count) };
            for (std::size_t layer{ 1U }; layer < layers; ++layer)
            {
                const std::size_t previousBegin{ begin + ((layer - 1U) * count) / layers };
                const std::size_t previousEnd{ begin + (layer * count) / layers };
                const std::size_t currentBegin{ previousEnd };
                const std::size_t currentEnd{ begin + ((layer + 1U) * count) / layers };
                for (std::size_t current{ currentBegin }; current < currentEnd; ++current)
                {
                    for (std::size_t previous{ previousBegin }; previous < previousEnd; ++previous)
                    {
                        appendUniqueDependency(tasks.at(current), previous);
                    }
                }
            }
        }

        void applyRandom(std::vector<TaskDescriptor>& tasks, const std::size_t begin, const std::size_t end, const double probability,
                         std::mt19937_64& generator)
        {
            std::bernoulli_distribution includeEdge{ probability };
            for (std::size_t dependent{ begin + 1U }; dependent < end; ++dependent)
            {
                for (std::size_t dependency{ begin }; dependency < dependent; ++dependency)
                {
                    if (includeEdge(generator))
                    {
                        appendUniqueDependency(tasks.at(dependent), dependency);
                    }
                }
            }
        }
    } // namespace

    std::vector<TaskDescriptor> generateWorkload(const ExperimentManifest& manifest, const std::uint64_t seed)
    {
        const std::size_t totalTasks{ manifest.cpu.taskCount + manifest.gpu.taskCount };
        std::vector<ExecutionResource> resources;
        resources.reserve(totalTasks);
        resources.insert(resources.end(), manifest.cpu.taskCount, ExecutionResource::CPU);
        resources.insert(resources.end(), manifest.gpu.taskCount, ExecutionResource::GPU);

        std::mt19937_64 generator{ seed };
        std::shuffle(resources.begin(), resources.end(), generator);
        std::uniform_int_distribution<std::size_t> priorityIndex{ 0U, manifest.priorities.values.size() - 1U };

        std::vector<TaskDescriptor> tasks;
        tasks.reserve(totalTasks);
        std::size_t cpuIndex{ 0U };
        std::size_t gpuIndex{ 0U };
        for (std::size_t index{ 0U }; index < totalTasks; ++index)
        {
            const ExecutionResource resource{ resources.at(index) };
            const std::size_t resourceIndex{ resource == ExecutionResource::CPU ? cpuIndex++ : gpuIndex++ };
            const std::size_t selectedPriority{ manifest.priorities.assignment == PriorityAssignment::Cycle
                                                    ? index % manifest.priorities.values.size()
                                                    : priorityIndex(generator) };
            tasks.push_back(TaskDescriptor{ index,
                                            (resource == ExecutionResource::CPU ? "cpu-" : "gpu-") + std::to_string(resourceIndex),
                                            resource,
                                            manifest.priorities.values.at(selectedPriority),
                                            (index * manifest.bursts.count) / totalTasks,
                                            {} });
        }

        for (std::size_t burst{ 0U }; burst < manifest.bursts.count; ++burst)
        {
            const auto [begin, end]{ burstRange(burst, manifest.bursts.count, totalTasks) };
            switch (manifest.dependencies.shape)
            {
            case DependencyShape::Independent:
                break;
            case DependencyShape::Chain:
                applyChain(tasks, begin, end);
                break;
            case DependencyShape::Layered:
                applyLayers(tasks, begin, end, manifest.dependencies.layers);
                break;
            case DependencyShape::Random:
                applyRandom(tasks, begin, end, manifest.dependencies.edgeProbability, generator);
                break;
            }

            if (burst == 0U)
            {
                continue;
            }
            const auto [previousBegin, previousEnd]{ burstRange(burst - 1U, manifest.bursts.count, totalTasks) };
            static_cast<void>(previousEnd);
            for (std::size_t index{ begin }; index < end; ++index)
            {
                const bool hasSameBurstDependency{ std::any_of(
                    tasks.at(index).dependencies.begin(), tasks.at(index).dependencies.end(),
                    [begin](const std::size_t dependency) { return dependency >= begin; }) };
                if (!hasSameBurstDependency)
                {
                    appendUniqueDependency(tasks.at(index), previousBegin);
                }
            }
        }
        return tasks;
    }
} // namespace Atlas::Benchmark
