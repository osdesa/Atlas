#ifndef ATLAS_BASELINE_RUNNER
#define ATLAS_BASELINE_RUNNER

#include "BaselineConfig.h"
#include "BenchmarkMetrics.h"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

/**
 * @file BaselineRunner.h
 * @brief Declares paired execution of one baseline comparison suite.
 */

namespace Atlas::Benchmark
{
    class BenchmarkProgressWriter;
    /// @brief One normalized scheduled or direct measured run.
    struct BaselineRunRecord
    {
        std::string suiteId;
        std::string caseId;
        std::string variantId;
        ExecutionMode executionMode{ ExecutionMode::Direct };
        std::size_t executionOrder{ 0U };
        RunRecord run;
    };

    /// @brief All measured suite records and whether every planned run succeeded.
    struct BaselineBatch
    {
        std::vector<BaselineRunRecord> records;
        bool succeeded{ true };
    };

    /**
     * @brief Owns per-variant reusable resources while executing paired trials.
     */
    class BaselineSuiteRunner final
    {
      public:
        /// @brief Stores a validated suite for later execution.
        explicit BaselineSuiteRunner(BaselineSuite suite, BenchmarkProgressWriter* progress = nullptr);
        /// @brief Destroys any retained suite resources after draining.
        ~BaselineSuiteRunner();

        /// @brief Runs every case, warmup, seed, repetition, and variant.
        BaselineBatch run();

        BaselineSuiteRunner(const BaselineSuiteRunner&) = delete;
        BaselineSuiteRunner& operator=(const BaselineSuiteRunner&) = delete;
        BaselineSuiteRunner(BaselineSuiteRunner&&) = delete;
        BaselineSuiteRunner& operator=(BaselineSuiteRunner&&) = delete;

      private:
        struct Impl;
        std::unique_ptr<Impl> implementation;
    };
} // namespace Atlas::Benchmark

#endif // !ATLAS_BASELINE_RUNNER
