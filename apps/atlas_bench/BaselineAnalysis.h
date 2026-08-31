#ifndef ATLAS_BASELINE_ANALYSIS
#define ATLAS_BASELINE_ANALYSIS

#include "BaselineConfig.h"
#include "BaselineRunner.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

/**
 * @file BaselineAnalysis.h
 * @brief Declares deterministic uncertainty analysis for paired suite runs.
 */

namespace Atlas::Benchmark
{
    /// @brief Interpretation attached to one comparison metric.
    enum class MetricDirection : std::uint8_t
    {
        Lower,
        Higher,
        Descriptive
    };

    /// @brief Optional lower and upper 95% confidence bounds.
    struct ConfidenceInterval
    {
        std::optional<double> lower;
        std::optional<double> upper;
    };

    /// @brief Absolute aggregate for one variant and metric.
    struct VariantMetricSummary
    {
        std::string metric;
        MetricDirection direction{ MetricDirection::Descriptive };
        std::size_t sampleCount{ 0U };
        std::optional<double> mean;
        ConfidenceInterval confidenceInterval;
    };

    /// @brief Paired effect for one variant relative to the case reference.
    struct ComparisonMetricSummary
    {
        std::string metric;
        MetricDirection direction{ MetricDirection::Descriptive };
        std::size_t pairedSampleCount{ 0U };
        std::optional<double> meanDifference;
        ConfidenceInterval differenceConfidenceInterval;
        std::size_t percentSampleCount{ 0U };
        std::optional<double> meanPercentChange;
        ConfidenceInterval percentConfidenceInterval;
    };

    /// @brief Absolute metrics for one suite variant.
    struct VariantSummary
    {
        std::string variantId;
        ExecutionMode executionMode{ ExecutionMode::Direct };
        std::vector<VariantMetricSummary> metrics;
    };

    /// @brief One non-reference variant's paired comparison.
    struct VariantComparison
    {
        std::string variantId;
        std::string referenceVariantId;
        std::vector<ComparisonMetricSummary> metrics;
    };

    /// @brief Absolute and paired summaries for one logical workload case.
    struct CaseSummary
    {
        std::string caseId;
        std::string referenceVariantId;
        std::vector<VariantSummary> variants;
        std::vector<VariantComparison> comparisons;
    };

    /// @brief Complete suite-level deterministic comparison summary.
    struct BaselineSummary
    {
        std::string suiteId;
        std::vector<CaseSummary> cases;
    };

    /**
     * @brief Calculates absolute means and paired deterministic bootstrap intervals.
     * @param suite Resolved suite defining case and reference order.
     * @param records Complete successful measured records.
     * @return Summary in stable suite, variant, and metric order.
     * @throws std::runtime_error If a planned run is missing, duplicated, or failed.
     */
    BaselineSummary calculateBaselineSummary(const BaselineSuite& suite, const std::vector<BaselineRunRecord>& records);

    /// @brief Returns the stable result spelling for a metric direction.
    std::string toString(MetricDirection direction);
} // namespace Atlas::Benchmark

#endif // !ATLAS_BASELINE_ANALYSIS
