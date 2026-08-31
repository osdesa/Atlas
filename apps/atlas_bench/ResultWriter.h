#ifndef ATLAS_BENCHMARK_RESULT_WRITER
#define ATLAS_BENCHMARK_RESULT_WRITER

#include "BenchmarkConfig.h"
#include "BenchmarkMetrics.h"

#include <filesystem>
#include <fstream>

/**
 * @file ResultWriter.h
 * @brief Declares version-one JSON Lines and normalized CSV result output.
 */

namespace Atlas::Benchmark
{
    /**
     * @brief Owns one benchmark result directory and its machine-readable files.
     */
    class ResultWriter final
    {
      public:
        /**
         * @brief Creates result files and writes the resolved manifest.
         * @param outputDirectory Destination directory to create or reuse.
         * @param manifest Resolved configuration recorded with every run.
         * @param overwrite Whether known existing result files may be truncated.
         */
        ResultWriter(const std::filesystem::path& outputDirectory, const ExperimentManifest& manifest, bool overwrite);

        /// @brief Appends one measured repetition to JSONL and both CSV tables.
        void append(const RunRecord& record);

      private:
        ExperimentManifest resolvedManifest;
        std::ofstream jsonLines;
        std::ofstream runCsv;
        std::ofstream taskCsv;
    };
} // namespace Atlas::Benchmark

#endif // !ATLAS_BENCHMARK_RESULT_WRITER
