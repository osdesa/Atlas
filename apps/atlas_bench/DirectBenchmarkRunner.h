#ifndef ATLAS_DIRECT_BENCHMARK_RUNNER
#define ATLAS_DIRECT_BENCHMARK_RUNNER

#include "BenchmarkConfig.h"
#include "BenchmarkMetrics.h"

#include <cstddef>
#include <cstdint>
#include <memory>

/**
 * @file DirectBenchmarkRunner.h
 * @brief Declares benchmark-private topological executor coordination.
 */

namespace Atlas::Benchmark
{
    /**
     * @brief Executes generated DAGs without TaskGraph, KahnScheduler, or policies.
     */
    class DirectBenchmarkRunner final
    {
      public:
        /// @brief Prepares reusable direct-baseline executors and resources.
        explicit DirectBenchmarkRunner(ExperimentManifest experiment);
        /// @brief Drains and destroys reusable direct-baseline resources.
        ~DirectBenchmarkRunner();

        /**
         * @brief Executes one generated workload through direct executor submissions.
         * @param seed Workload and input seed.
         * @param repetition Stable measured-repetition index.
         * @return Complete direct-run measurements in the common run model.
         */
        RunRecord runSingle(std::uint64_t seed, std::size_t repetition);

        DirectBenchmarkRunner(const DirectBenchmarkRunner&) = delete;
        DirectBenchmarkRunner& operator=(const DirectBenchmarkRunner&) = delete;
        DirectBenchmarkRunner(DirectBenchmarkRunner&&) = delete;
        DirectBenchmarkRunner& operator=(DirectBenchmarkRunner&&) = delete;

      private:
        struct Impl;
        std::unique_ptr<Impl> implementation;
    };
} // namespace Atlas::Benchmark

#endif // !ATLAS_DIRECT_BENCHMARK_RUNNER
