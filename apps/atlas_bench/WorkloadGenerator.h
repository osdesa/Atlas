#ifndef ATLAS_BENCHMARK_WORKLOAD_GENERATOR
#define ATLAS_BENCHMARK_WORKLOAD_GENERATOR

#include "BenchmarkConfig.h"
#include "atlas/Tasking/ExecutionResource.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

/**
 * @file WorkloadGenerator.h
 * @brief Declares deterministic benchmark task and dependency generation.
 */

namespace Atlas::Benchmark
{
    /// @brief Backend-neutral description of one generated benchmark task.
    struct TaskDescriptor
    {
        std::size_t index{ 0U };                              ///< Stable position in generated graph order.
        std::string name;                                     ///< Stable human-readable task name.
        ExecutionResource resource{ ExecutionResource::CPU }; ///< Backend assigned to the task.
        std::uint32_t priority{ 0U };                         ///< Immutable static scheduling priority.
        std::size_t burstIndex{ 0U };                         ///< Dependency-driven activation group.
        std::vector<std::size_t> dependencies;                ///< Earlier descriptor indices required by this task.
    };

    /**
     * @brief Generates one deterministic acyclic workload description.
     * @param manifest Resolved experiment configuration.
     * @param seed Seed controlling ordering, random priorities, and random edges.
     * @return Task descriptors in graph insertion and topological order.
     */
    std::vector<TaskDescriptor> generateWorkload(const ExperimentManifest& manifest, std::uint64_t seed);
} // namespace Atlas::Benchmark

#endif // !ATLAS_BENCHMARK_WORKLOAD_GENERATOR
