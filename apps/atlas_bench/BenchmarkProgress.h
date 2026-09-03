#ifndef ATLAS_BENCHMARK_PROGRESS
#define ATLAS_BENCHMARK_PROGRESS

#include "BaselineConfig.h"
#include "BenchmarkMetrics.h"
#include "WorkloadGenerator.h"
#include "atlas/Profiling/Trace.h"

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <memory>
#include <string>
#include <string_view>

/** @file BenchmarkProgress.h @brief Declares the Studio-only live benchmark JSONL stream. */

namespace Atlas::Benchmark
{
    /// @brief Identity and position of one warmup or measured suite execution.
    struct BenchmarkRunContext
    {
        std::size_t runId{ 0U };
        std::size_t runNumber{ 0U };
        std::size_t totalRunCount{ 0U };
        std::string caseId;
        std::string variantId;
        ExecutionMode executionMode{ ExecutionMode::Direct };
        std::uint64_t seed{ 0U };
        std::size_t repetition{ 0U };
        bool warmup{ false };
        std::size_t executionOrder{ 0U };
    };

    /**
     * @brief Writes one bounded, versioned benchmark progress stream.
     *
     * The writer is single-run-at-a-time. Its trace session is borrowed only
     * between beginRun() and finishRun(). Event publication remains bounded and
     * non-blocking; JSON output is performed by a consumer thread.
     */
    class BenchmarkProgressWriter final
    {
      public:
        explicit BenchmarkProgressWriter(std::ostream& output, std::size_t traceCapacity = 65'536U);
        ~BenchmarkProgressWriter();

        void beginSuite(const BaselineSuite& suite, std::size_t totalRunCount, std::size_t measuredRunCount);
        void beginRun(const BenchmarkRunContext& context, const std::vector<TaskDescriptor>& tasks);
        TraceSession* traceSession() noexcept;
        void finishRun(const BenchmarkRunContext& context, const RunRecord& record);
        void finishSuite(std::string_view status);
        void fail(std::string_view message);

        BenchmarkProgressWriter(const BenchmarkProgressWriter&) = delete;
        BenchmarkProgressWriter& operator=(const BenchmarkProgressWriter&) = delete;

      private:
        struct Impl;
        std::unique_ptr<Impl> implementation;
    };
} // namespace Atlas::Benchmark

#endif // !ATLAS_BENCHMARK_PROGRESS
