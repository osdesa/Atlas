#ifndef ATLAS_BASELINE_CONFIG
#define ATLAS_BASELINE_CONFIG

#include "BenchmarkTypes.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

/**
 * @file BaselineConfig.h
 * @brief Declares the current comparison-suite configuration.
 */

namespace Atlas::Benchmark
{
    /// @brief Selects direct executor coordination or normal Atlas scheduling.
    enum class ExecutionMode : std::uint8_t
    {
        Direct,
        Scheduled
    };

    /// @brief Logical generated workload shared by every variant in one case.
    struct BaselineWorkloadConfig
    {
        CpuWorkloadConfig cpu;
        GpuWorkloadConfig gpu;
        DependencyConfig dependencies;
        PriorityConfig priorities;
        BurstConfig bursts;
    };

    /// @brief One direct or scheduled execution variant in a comparison case.
    struct BaselineVariant
    {
        std::string variantId;
        ExecutionMode executionMode{ ExecutionMode::Direct };
        PolicyConfig policy;
        bool sliced{ false };
        DispatchDimensions sliceWorkgroups;
    };

    /// @brief One logical workload and the variants compared on that workload.
    struct BaselineCase
    {
        std::string caseId;
        BaselineWorkloadConfig workload;
        std::string referenceVariant;
        std::vector<BaselineVariant> variants;
    };

    /// @brief Complete resolved version-one baseline comparison suite.
    struct BaselineSuite
    {
        std::uint32_t schemaVersion{ 1U };
        std::string suiteId;
        std::vector<std::uint64_t> seeds;
        std::size_t warmupRuns{ 0U };
        std::size_t repetitions{ 1U };
        std::uint32_t workerCount{ 1U };
        std::vector<BaselineCase> cases;
    };

    /// @brief Optional user-supplied non-portable environment description.
    struct EnvironmentMetadata
    {
        std::uint32_t schemaVersion{ 1U };
        std::string environmentId;
        std::optional<std::string> cpuModel;
        std::optional<std::uint64_t> physicalMemoryBytes;
        std::optional<std::string> osVersion;
        std::optional<std::string> gpuDriver;
        std::optional<std::string> powerProfile;
        std::optional<std::string> notes;
    };

    /**
     * @brief Loads and strictly validates one version-one baseline suite.
     * @param path Suite file to read.
     * @return Fully resolved comparison-suite configuration.
     * @throws std::runtime_error When the file or suite contract is invalid.
     */
    BaselineSuite loadBaselineSuite(const std::filesystem::path& path);

    /**
     * @brief Loads and strictly validates optional host metadata.
     * @param path Environment metadata file to read.
     * @return Validated user-supplied environment fields.
     * @throws std::runtime_error When the file or metadata contract is invalid.
     */
    EnvironmentMetadata loadEnvironmentMetadata(const std::filesystem::path& path);

    /// @brief Returns the stable schema spelling for an execution mode.
    std::string toString(ExecutionMode mode);

    /**
     * @brief Materializes one existing experiment configuration from a suite variant.
     * @param suite Owning suite with repetitions, seeds, and worker capacity.
     * @param comparisonCase Logical workload to execute.
     * @param variant Scheduled or direct variant configuration.
     * @return An experiment manifest sharing the case's logical work.
     */
    ExperimentManifest makeExperimentManifest(const BaselineSuite& suite, const BaselineCase& comparisonCase,
                                              const BaselineVariant& variant);
} // namespace Atlas::Benchmark

#endif // !ATLAS_BASELINE_CONFIG
