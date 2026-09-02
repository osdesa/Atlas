#ifndef ATLAS_BENCHMARK_RUNNER
#define ATLAS_BENCHMARK_RUNNER

#include "BenchmarkMetrics.h"
#include "BenchmarkTypes.h"
#include "WorkloadGenerator.h"
#include "atlas/Profiling/Trace.h"

#include <memory>
#include <vector>

/**
 * @file BenchmarkRunner.h
 * @brief Declares repeated execution of one resolved benchmark experiment.
 */

namespace Atlas::Benchmark
{
    /// @brief Measured records and whether every measured run succeeded.
    struct BenchmarkBatch
    {
        std::vector<RunRecord> records;
        bool succeeded{ true };
    };

    /**
     * @brief Owns reusable executors/resources and rebuilds one graph per run.
     */
    class BenchmarkRunner final
    {
      public:
        /// @brief Prepares reusable resources for @p experiment.
        explicit BenchmarkRunner(ExperimentManifest experiment);
        /// @brief Drains and destroys reusable benchmark resources.
        ~BenchmarkRunner();

        /// @brief Runs every configured seed, warmup, and measured repetition.
        BenchmarkBatch run();

        /**
         * @brief Executes one measured run while retaining this runner's warmed resources.
         * @param seed Workload and input seed.
         * @param repetition Stable measured-repetition index.
         * @param descriptors Shared deterministic task descriptions for this case and seed.
         * @param traceSession Optional borrowed live-progress trace session.
         * @return One complete scheduled run record.
         */
        RunRecord runSingle(std::uint64_t seed, std::size_t repetition, const std::vector<TaskDescriptor>& descriptors,
                            TraceSession* traceSession = nullptr);

        BenchmarkRunner(const BenchmarkRunner&) = delete;
        BenchmarkRunner& operator=(const BenchmarkRunner&) = delete;
        BenchmarkRunner(BenchmarkRunner&&) = delete;
        BenchmarkRunner& operator=(BenchmarkRunner&&) = delete;

      private:
        struct Impl;
        std::unique_ptr<Impl> implementation;
    };
} // namespace Atlas::Benchmark

#endif // !ATLAS_BENCHMARK_RUNNER
