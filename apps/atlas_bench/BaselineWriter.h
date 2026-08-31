#ifndef ATLAS_BASELINE_WRITER
#define ATLAS_BASELINE_WRITER

#include "BaselineAnalysis.h"
#include "BaselineConfig.h"
#include "BaselineRunner.h"

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

/**
 * @file BaselineWriter.h
 * @brief Declares normalized suite-run and comparison-summary serialization.
 */

namespace Atlas::Benchmark
{
    /**
     * @brief Owns one baseline suite output directory and its known files.
     */
    class BaselineWriter final
    {
      public:
        /**
         * @brief Creates suite result files and records resolved provenance.
         * @param outputDirectory Destination directory.
         * @param suite Resolved suite configuration.
         * @param environment Optional validated user metadata.
         * @param records Available records used to resolve Vulkan metadata.
         * @param overwrite Whether known suite files may be replaced.
         */
        BaselineWriter(const std::filesystem::path& outputDirectory, BaselineSuite suite,
                       std::optional<EnvironmentMetadata> environment, const std::vector<BaselineRunRecord>& records, bool overwrite);

        /// @brief Appends one normalized measured run to JSONL and CSV outputs.
        void append(const BaselineRunRecord& record);

        /// @brief Writes the complete confidence-interval summary and flat table.
        void writeSummary(const BaselineSummary& summary);

      private:
        BaselineSuite resolvedSuite;
        std::optional<EnvironmentMetadata> userEnvironment;
        std::string resolvedEnvironmentJson;
        std::filesystem::path summaryPath;
        std::filesystem::path comparisonCsvPath;
        std::ofstream jsonLines;
        std::ofstream runCsv;
        std::ofstream taskCsv;
        std::ofstream comparisonCsv;
    };
} // namespace Atlas::Benchmark

#endif // !ATLAS_BASELINE_WRITER
