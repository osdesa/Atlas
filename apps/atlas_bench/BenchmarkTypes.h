#ifndef ATLAS_BENCHMARK_TYPES
#define ATLAS_BENCHMARK_TYPES

#include "atlas/Vulkan/VulkanCompute.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

/**
 * @file BenchmarkTypes.h
 * @brief Declares resolved benchmark experiment types used by baseline suites.
 */

namespace Atlas::Benchmark
{
    /// @brief Supported built-in scheduler policies.
    enum class PolicyKind : std::uint8_t
    {
        Fifo,
        RoundRobin,
        StaticPriority
    };

    /// @brief Supported generated dependency shapes.
    enum class DependencyShape : std::uint8_t
    {
        Independent,
        Chain,
        Layered,
        Random
    };

    /// @brief Supported priority assignment modes.
    enum class PriorityAssignment : std::uint8_t
    {
        Cycle,
        Random
    };

    /// @brief Scheduling-policy configuration.
    struct PolicyConfig
    {
        PolicyKind kind{ PolicyKind::Fifo };
        std::size_t quantum{ 1U };
    };

    /// @brief Deterministic CPU workload configuration.
    struct CpuWorkloadConfig
    {
        std::size_t taskCount{ 0U };
        std::uint64_t iterations{ 1U };
    };

    /// @brief Deterministic Vulkan workload configuration.
    struct GpuWorkloadConfig
    {
        std::size_t taskCount{ 0U };
        DispatchDimensions workgroups;
        bool sliced{ false };
        DispatchDimensions sliceWorkgroups;
    };

    /// @brief Generated DAG configuration.
    struct DependencyConfig
    {
        DependencyShape shape{ DependencyShape::Independent };
        std::size_t layers{ 1U };
        double edgeProbability{ 0.0 };
    };

    /// @brief Static-priority assignment configuration.
    struct PriorityConfig
    {
        PriorityAssignment assignment{ PriorityAssignment::Cycle };
        std::vector<std::uint32_t> values{ 0U };
    };

    /// @brief Dependency-driven activation-group configuration.
    struct BurstConfig
    {
        std::size_t count{ 1U };
    };

    /// @brief Complete resolved version-one experiment manifest.
    struct ExperimentManifest
    {
        std::uint32_t schemaVersion{ 1U };
        std::string experimentId;
        std::vector<std::uint64_t> seeds;
        std::size_t warmupRuns{ 0U };
        std::size_t repetitions{ 1U };
        std::uint32_t workerCount{ 1U };
        PolicyConfig policy;
        CpuWorkloadConfig cpu;
        GpuWorkloadConfig gpu;
        DependencyConfig dependencies;
        PriorityConfig priorities;
        BurstConfig bursts;
    };

    /// @brief Returns the stable manifest spelling for a policy kind.
    std::string toString(PolicyKind kind);
    /// @brief Returns the stable manifest spelling for a dependency shape.
    std::string toString(DependencyShape shape);
    /// @brief Returns the stable manifest spelling for a priority assignment.
    std::string toString(PriorityAssignment assignment);
} // namespace Atlas::Benchmark

#endif // !ATLAS_BENCHMARK_TYPES
