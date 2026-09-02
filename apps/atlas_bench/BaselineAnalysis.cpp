#include "BaselineAnalysis.h"

#include "atlas/Scheduler/SchedulerStatus.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <numeric>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

/**
 * @file BaselineAnalysis.cpp
 * @brief Implements deterministic hierarchical percentile bootstrap analysis.
 */

namespace Atlas::Benchmark
{
    namespace
    {
        constexpr std::size_t bootstrapResamples{ 10'000U };

        enum class MetricId : std::uint8_t
        {
            CompletionTime,
            Throughput,
            ResponseMean,
            ResponseP95,
            ReadyWaitMean,
            ReadyWaitP95,
            SelectionBypassMean,
            SelectionBypassMaximum,
            ControlActiveFraction,
            ImmediateSliceSwitchMean,
            CpuBusyFraction,
            GpuHostBusyFraction,
            GpuTimestampBusyFraction,
            CpuJainFairness,
            GpuJainFairness
        };

        struct MetricSpec final
        {
            MetricId id;
            const char* name;
            MetricDirection direction;
        };

        constexpr std::array metricSpecs{
            MetricSpec{ MetricId::CompletionTime, "completion_time_us", MetricDirection::Lower },
            MetricSpec{ MetricId::Throughput, "throughput_tasks_per_second", MetricDirection::Higher },
            MetricSpec{ MetricId::ResponseMean, "response_mean_us", MetricDirection::Lower },
            MetricSpec{ MetricId::ResponseP95, "response_p95_us", MetricDirection::Lower },
            MetricSpec{ MetricId::ReadyWaitMean, "ready_wait_mean_us", MetricDirection::Lower },
            MetricSpec{ MetricId::ReadyWaitP95, "ready_wait_p95_us", MetricDirection::Lower },
            MetricSpec{ MetricId::SelectionBypassMean, "selection_bypass_mean", MetricDirection::Lower },
            MetricSpec{ MetricId::SelectionBypassMaximum, "selection_bypass_max", MetricDirection::Lower },
            MetricSpec{ MetricId::ControlActiveFraction, "control_active_fraction", MetricDirection::Lower },
            MetricSpec{ MetricId::ImmediateSliceSwitchMean, "immediate_slice_switch_mean_us", MetricDirection::Descriptive },
            MetricSpec{ MetricId::CpuBusyFraction, "cpu_busy_fraction", MetricDirection::Descriptive },
            MetricSpec{ MetricId::GpuHostBusyFraction, "gpu_host_busy_fraction", MetricDirection::Descriptive },
            MetricSpec{ MetricId::GpuTimestampBusyFraction, "gpu_timestamp_busy_fraction", MetricDirection::Descriptive },
            MetricSpec{ MetricId::CpuJainFairness, "cpu_jain_fairness", MetricDirection::Higher },
            MetricSpec{ MetricId::GpuJainFairness, "gpu_jain_fairness", MetricDirection::Higher }
        };

        struct Observation final
        {
            std::uint64_t seed{ 0U };
            std::size_t repetition{ 0U };
            double value{ 0.0 };
        };

        struct Aggregate final
        {
            std::size_t count{ 0U };
            std::optional<double> mean;
            ConfidenceInterval confidenceInterval;
        };

        std::optional<double> metricValue(const BaselineRunRecord& record, const MetricId metric)
        {
            const RunRecord& run{ record.run };
            switch (metric)
            {
            case MetricId::CompletionTime:
                return static_cast<double>(run.schedulerResult.executionTime.count()) / 1'000.0;
            case MetricId::Throughput:
                return run.metrics.throughputTasksPerSecond;
            case MetricId::ResponseMean:
                return run.metrics.responseLatency.mean;
            case MetricId::ResponseP95:
                return run.metrics.responseLatency.p95;
            case MetricId::ReadyWaitMean:
                return run.metrics.readyWait.mean;
            case MetricId::ReadyWaitP95:
                return run.metrics.readyWait.p95;
            case MetricId::SelectionBypassMean:
            case MetricId::SelectionBypassMaximum:
            {
                if (record.executionMode == ExecutionMode::Direct || run.tasks.empty())
                {
                    return std::nullopt;
                }
                const auto maximum =
                    std::max_element(run.tasks.begin(), run.tasks.end(), [](const TaskMeasurement& left, const TaskMeasurement& right)
                                     { return left.selectionBypassCount < right.selectionBypassCount; });
                if (metric == MetricId::SelectionBypassMaximum)
                {
                    return static_cast<double>(maximum->selectionBypassCount);
                }
                const std::size_t sum{ std::accumulate(run.tasks.begin(), run.tasks.end(), std::size_t{ 0U },
                                                       [](const std::size_t total, const TaskMeasurement& task)
                                                       { return total + task.selectionBypassCount; }) };
                return static_cast<double>(sum) / static_cast<double>(run.tasks.size());
            }
            case MetricId::ControlActiveFraction:
                return run.metrics.schedulerActiveFraction;
            case MetricId::ImmediateSliceSwitchMean:
                return run.metrics.immediateSliceSwitchMeanMicroseconds;
            case MetricId::CpuBusyFraction:
                return run.metrics.cpuBusyFraction;
            case MetricId::GpuHostBusyFraction:
                return run.metrics.gpuHostBusyFraction;
            case MetricId::GpuTimestampBusyFraction:
                return run.metrics.gpuTimestampBusyFraction;
            case MetricId::CpuJainFairness:
                return run.metrics.cpuJainFairness;
            case MetricId::GpuJainFairness:
                return run.metrics.gpuJainFairness;
            }
            throw std::logic_error{ "Unknown baseline metric" };
        }

        std::uint64_t stableSeed(const std::string& label)
        {
            std::uint64_t value{ 14'695'981'039'346'656'037ULL };
            for (const char character : label)
            {
                value ^= static_cast<unsigned char>(character);
                value *= 1'099'511'628'211ULL;
            }
            return value;
        }

        double arithmeticMean(const std::vector<double>& values)
        {
            return std::accumulate(values.begin(), values.end(), 0.0) / static_cast<double>(values.size());
        }

        ConfidenceInterval bootstrapInterval(const std::vector<Observation>& observations, const std::string& label)
        {
            std::map<std::uint64_t, std::vector<double>> groups;
            for (const Observation& observation : observations)
            {
                groups[observation.seed].push_back(observation.value);
            }
            if (groups.size() < 2U)
            {
                return {};
            }

            std::vector<const std::vector<double>*> groupValues;
            groupValues.reserve(groups.size());
            for (const auto& [seed, values] : groups)
            {
                static_cast<void>(seed);
                groupValues.push_back(&values);
            }

            std::mt19937_64 generator{ stableSeed(label) };
            std::uniform_int_distribution<std::size_t> groupChoice{ 0U, groupValues.size() - 1U };
            std::vector<double> bootstrapMeans;
            bootstrapMeans.reserve(bootstrapResamples);
            for (std::size_t sample{ 0U }; sample < bootstrapResamples; ++sample)
            {
                double total{ 0.0 };
                std::size_t count{ 0U };
                for (std::size_t selectedGroup{ 0U }; selectedGroup < groupValues.size(); ++selectedGroup)
                {
                    const std::vector<double>& values{ *groupValues.at(groupChoice(generator)) };
                    std::uniform_int_distribution<std::size_t> valueChoice{ 0U, values.size() - 1U };
                    for (std::size_t selectedValue{ 0U }; selectedValue < values.size(); ++selectedValue)
                    {
                        total += values.at(valueChoice(generator));
                        ++count;
                    }
                }
                bootstrapMeans.push_back(total / static_cast<double>(count));
            }
            std::sort(bootstrapMeans.begin(), bootstrapMeans.end());
            const std::size_t lowerIndex{ static_cast<std::size_t>(std::floor(0.025 * (bootstrapResamples - 1U))) };
            const std::size_t upperIndex{ static_cast<std::size_t>(std::ceil(0.975 * (bootstrapResamples - 1U))) };
            return ConfidenceInterval{ bootstrapMeans.at(lowerIndex), bootstrapMeans.at(upperIndex) };
        }

        Aggregate aggregate(const std::vector<Observation>& observations, const std::string& label)
        {
            if (observations.empty())
            {
                return {};
            }
            std::vector<double> values;
            values.reserve(observations.size());
            for (const Observation& observation : observations)
            {
                values.push_back(observation.value);
            }
            return Aggregate{ observations.size(), arithmeticMean(values), bootstrapInterval(observations, label) };
        }

        using RunKey = std::pair<std::uint64_t, std::size_t>;

        std::vector<const BaselineRunRecord*> selectRecords(const std::vector<BaselineRunRecord>& records, const std::string& caseId,
                                                            const std::string& variantId)
        {
            std::vector<const BaselineRunRecord*> selected;
            for (const BaselineRunRecord& record : records)
            {
                if (record.caseId == caseId && record.variantId == variantId)
                {
                    selected.push_back(&record);
                }
            }
            return selected;
        }

        std::vector<Observation> observations(const std::vector<const BaselineRunRecord*>& records, const MetricId metric)
        {
            std::vector<Observation> values;
            for (const BaselineRunRecord* record : records)
            {
                if (const std::optional<double> value{ metricValue(*record, metric) }; value.has_value())
                {
                    values.push_back(Observation{ record->run.seed, record->run.repetition, value.value() });
                }
            }
            return values;
        }
    } // namespace

    BaselineSummary calculateBaselineSummary(const BaselineSuite& suite, const std::vector<BaselineRunRecord>& records)
    {
        const std::size_t expectedCount{ std::accumulate(
            suite.cases.begin(), suite.cases.end(), std::size_t{ 0U }, [&suite](const std::size_t total, const BaselineCase& item)
            { return total + item.variants.size() * suite.seeds.size() * suite.repetitions; }) };
        if (records.size() != expectedCount)
        {
            throw std::runtime_error{ "Baseline summary requires every planned measured run" };
        }
        for (const BaselineRunRecord& record : records)
        {
            if (record.run.schedulerResult.status != SchedulerStatus::Success)
            {
                throw std::runtime_error{ "Baseline summary cannot include failed runs" };
            }
        }

        BaselineSummary summary{ suite.suiteId, {} };
        for (const BaselineCase& comparisonCase : suite.cases)
        {
            CaseSummary caseSummary{ comparisonCase.caseId, comparisonCase.referenceVariant, {}, {} };
            std::unordered_map<std::string, std::vector<const BaselineRunRecord*>> byVariant;
            for (const BaselineVariant& variant : comparisonCase.variants)
            {
                std::vector<const BaselineRunRecord*> selected{ selectRecords(records, comparisonCase.caseId, variant.variantId) };
                std::map<RunKey, const BaselineRunRecord*> unique;
                for (const BaselineRunRecord* record : selected)
                {
                    if (!unique.emplace(RunKey{ record->run.seed, record->run.repetition }, record).second)
                    {
                        throw std::runtime_error{ "Baseline summary found a duplicate paired run" };
                    }
                }
                if (unique.size() != suite.seeds.size() * suite.repetitions)
                {
                    throw std::runtime_error{ "Baseline summary found an incomplete variant" };
                }
                byVariant.emplace(variant.variantId, selected);

                VariantSummary variantSummary{ variant.variantId, variant.executionMode, {} };
                for (const MetricSpec& metric : metricSpecs)
                {
                    const std::vector<Observation> values{ observations(selected, metric.id) };
                    const Aggregate result{ aggregate(values, suite.suiteId + "/" + comparisonCase.caseId + "/" + variant.variantId +
                                                                  "/" + metric.name) };
                    variantSummary.metrics.push_back(
                        VariantMetricSummary{ metric.name, metric.direction, result.count, result.mean, result.confidenceInterval });
                }
                caseSummary.variants.push_back(std::move(variantSummary));
            }

            const std::vector<const BaselineRunRecord*>& reference{ byVariant.at(comparisonCase.referenceVariant) };
            for (const BaselineVariant& variant : comparisonCase.variants)
            {
                if (variant.variantId == comparisonCase.referenceVariant)
                {
                    continue;
                }
                const std::vector<const BaselineRunRecord*>& selected{ byVariant.at(variant.variantId) };
                VariantComparison comparison{ variant.variantId, comparisonCase.referenceVariant, {} };
                for (const MetricSpec& metric : metricSpecs)
                {
                    std::map<RunKey, double> referenceValues;
                    for (const BaselineRunRecord* record : reference)
                    {
                        if (const std::optional<double> value{ metricValue(*record, metric.id) }; value.has_value())
                        {
                            referenceValues.emplace(RunKey{ record->run.seed, record->run.repetition }, value.value());
                        }
                    }

                    std::vector<Observation> differences;
                    std::vector<Observation> percentages;
                    for (const BaselineRunRecord* record : selected)
                    {
                        const std::optional<double> value{ metricValue(*record, metric.id) };
                        const RunKey key{ record->run.seed, record->run.repetition };
                        const auto baseline{ referenceValues.find(key) };
                        if (!value.has_value() || baseline == referenceValues.end())
                        {
                            continue;
                        }
                        const double difference{ value.value() - baseline->second };
                        differences.push_back(Observation{ key.first, key.second, difference });
                        if (baseline->second != 0.0)
                        {
                            percentages.push_back(Observation{ key.first, key.second, 100.0 * difference / baseline->second });
                        }
                    }

                    const std::string label{ suite.suiteId + "/" + comparisonCase.caseId + "/" + variant.variantId + "/" +
                                             metric.name };
                    const Aggregate difference{ aggregate(differences, label + "/difference") };
                    const Aggregate percentage{ aggregate(percentages, label + "/percent") };
                    comparison.metrics.push_back(ComparisonMetricSummary{
                        metric.name, metric.direction, difference.count, difference.mean, difference.confidenceInterval,
                        percentage.count, percentage.mean, percentage.confidenceInterval });
                }
                caseSummary.comparisons.push_back(std::move(comparison));
            }
            summary.cases.push_back(std::move(caseSummary));
        }
        return summary;
    }

    std::string toString(const MetricDirection direction)
    {
        switch (direction)
        {
        case MetricDirection::Lower:
            return "lower_is_better";
        case MetricDirection::Higher:
            return "higher_is_better";
        case MetricDirection::Descriptive:
            return "descriptive";
        }
        throw std::logic_error{ "Unknown metric direction" };
    }
} // namespace Atlas::Benchmark
