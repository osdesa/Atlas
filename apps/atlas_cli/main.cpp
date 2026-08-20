#include "atlas/Executor/CpuExecutor.h"
#include "atlas/Executor/SynchronousCpuExecutor.h"
#include "atlas/Executor/WorkerpoolExecutor.h"
#include "atlas/Scheduler/KahnScheduler.h"
#include "atlas/Tasking/TaskGraph.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

/**
 * @file main.cpp
 * @brief Demonstrates a complex task graph on both Atlas CPU executors.
 */

namespace
{
    using namespace std::chrono_literals;

    constexpr std::size_t samplesPerSensor{ 24'000U };
    constexpr std::size_t histogramBucketCount{ 32U };

    /** @brief Intermediate data produced by the sensor-processing pipeline. */
    struct PipelineState
    {
        std::array<std::vector<double>, 3U> sensorStreams;
        std::vector<double> mergedSamples;
        std::vector<double> calibratedSamples;
        std::vector<double> filteredSamples;
        std::array<std::size_t, histogramBucketCount> histogram{};
        double sampleMean{ 0.0 };
        double sampleDeviation{ 0.0 };
        double spectralEnergy{ 0.0 };
        double qualityScore{ 0.0 };
        std::size_t anomalyCount{ 0U };
        std::size_t compressedBytes{ 0U };
        std::uint64_t rawSignature{ 0U };
        std::uint64_t compressedChecksum{ 0U };
        std::uint64_t packageChecksum{ 0U };
        bool configured{ false };
        bool merged{ false };
        bool calibrated{ false };
        bool filtered{ false };
        bool statisticsCalculated{ false };
        bool histogramBuilt{ false };
        bool spectrumAnalysed{ false };
        bool anomaliesDetected{ false };
        bool compressed{ false };
        bool qualityCalculated{ false };
        bool signatureCalculated{ false };
        bool packageAssembled{ false };
        bool packageValidated{ false };
        bool published{ false };
    };

    /** @brief Stable output used to compare executor runs. */
    struct PipelineSummary
    {
        std::size_t sampleCount{ 0U };
        std::size_t anomalyCount{ 0U };
        std::size_t compressedBytes{ 0U };
        std::uint64_t packageChecksum{ 0U };

        bool operator==(const PipelineSummary&) const = default;
    };

    void simulateStageLatency(const std::chrono::milliseconds duration)
    {
        std::this_thread::sleep_for(duration);
    }

    void requireStage(const bool condition, const char* const message)
    {
        if (!condition)
        {
            throw std::logic_error{ message };
        }
    }

    /**
     * @brief Populates and finalises a branched sensor-processing task graph.
     * @param graph The empty graph to populate.
     * @param state Storage shared by dependent pipeline stages.
     * @return True when every task and dependency was added and the graph was finalised.
     */
    bool createPipelineGraph(Atlas::TaskGraph& graph, PipelineState& state)
    {
        const auto configure{ graph.addTask(
            [&state]
            {
                state = PipelineState{};
                simulateStageLatency(120ms);
                state.configured = true;
            },
            Atlas::TaskOptions{ "Configure processing run" }) };

        std::array<std::optional<Atlas::TaskHandle>, 3U> ingestTasks;
        for (std::size_t sensorIndex{ 0U }; sensorIndex < ingestTasks.size(); ++sensorIndex)
        {
            ingestTasks.at(sensorIndex) = graph.addTask(
                [&state, sensorIndex]
                {
                    requireStage(state.configured, "The pipeline must be configured before sensor ingestion");

                    constexpr double pi{ 3.14159265358979323846 };
                    auto& stream{ state.sensorStreams.at(sensorIndex) };
                    stream.resize(samplesPerSensor);
                    for (std::size_t sampleIndex{ 0U }; sampleIndex < stream.size(); ++sampleIndex)
                    {
                        const double phase{ static_cast<double>(sampleIndex) *
                                            (0.0035 + (static_cast<double>(sensorIndex) * 0.0008)) };
                        const double slowDrift{ static_cast<double>(sampleIndex % 1'000U) / 5'000.0 };
                        const double sensorScale{ 1.0 + (0.15 * static_cast<double>(sensorIndex)) };
                        const bool injectAnomaly{ sampleIndex != 0U && (sampleIndex % 4'093U) == 0U };
                        const double anomalyPulse{ injectAnomaly ? (sensorIndex == 1U ? -14.0 : 14.0) : 0.0 };
                        stream.at(sampleIndex) =
                            (std::sin(phase * pi) * sensorScale) + (std::cos(phase * 0.37 * pi) * 0.25) + slowDrift + anomalyPulse;
                    }
                    simulateStageLatency(300ms);
                },
                Atlas::TaskOptions{ "Ingest sensor stream " + std::to_string(sensorIndex + 1U) });
        }

        const auto mergeStreams{ graph.addTask(
            [&state]
            {
                for (const auto& stream : state.sensorStreams)
                {
                    requireStage(stream.size() == samplesPerSensor, "Every sensor stream must be ingested before merging");
                }

                state.mergedSamples.reserve(samplesPerSensor * state.sensorStreams.size());
                for (const auto& stream : state.sensorStreams)
                {
                    state.mergedSamples.insert(state.mergedSamples.end(), stream.begin(), stream.end());
                }
                simulateStageLatency(180ms);
                state.merged = true;
            },
            Atlas::TaskOptions{ "Merge sensor streams" }) };

        const auto calibrate{ graph.addTask(
            [&state]
            {
                requireStage(state.merged, "Sensor streams must be merged before calibration");
                state.calibratedSamples.resize(state.mergedSamples.size());
                std::transform(state.mergedSamples.begin(), state.mergedSamples.end(), state.calibratedSamples.begin(),
                               [](const double sample) { return (sample - 0.075) * 1.04; });
                simulateStageLatency(280ms);
                state.calibrated = true;
            },
            Atlas::TaskOptions{ "Calibrate samples" }) };

        const auto calculateSignature{ graph.addTask(
            [&state]
            {
                requireStage(state.merged, "Merged samples are required to calculate the input signature");
                constexpr std::uint64_t fnvOffset{ 14'695'981'039'346'656'037ULL };
                constexpr std::uint64_t fnvPrime{ 1'099'511'628'211ULL };
                state.rawSignature = fnvOffset;
                for (const double sample : state.mergedSamples)
                {
                    const auto quantized{ static_cast<std::int64_t>((sample + 4.0) * 1'000'000.0) };
                    state.rawSignature ^= static_cast<std::uint64_t>(quantized);
                    state.rawSignature *= fnvPrime;
                }
                simulateStageLatency(240ms);
                state.signatureCalculated = true;
            },
            Atlas::TaskOptions{ "Calculate input signature" }) };

        const auto filterNoise{ graph.addTask(
            [&state]
            {
                requireStage(state.calibrated, "Calibrated samples are required for noise filtering");
                state.filteredSamples.resize(state.calibratedSamples.size());
                for (std::size_t index{ 0U }; index < state.calibratedSamples.size(); ++index)
                {
                    const std::size_t first{ index > 2U ? index - 2U : 0U };
                    const std::size_t last{ std::min(index + 2U, state.calibratedSamples.size() - 1U) };
                    const double sum{ std::accumulate(state.calibratedSamples.begin() + static_cast<std::ptrdiff_t>(first),
                                                      state.calibratedSamples.begin() + static_cast<std::ptrdiff_t>(last + 1U), 0.0) };
                    state.filteredSamples.at(index) = sum / static_cast<double>((last - first) + 1U);
                }
                simulateStageLatency(380ms);
                state.filtered = true;
            },
            Atlas::TaskOptions{ "Filter sensor noise" }) };

        const auto calculateStatistics{ graph.addTask(
            [&state]
            {
                requireStage(state.calibrated, "Calibrated samples are required for statistics");
                const double sampleCount{ static_cast<double>(state.calibratedSamples.size()) };
                state.sampleMean = std::accumulate(state.calibratedSamples.begin(), state.calibratedSamples.end(), 0.0) / sampleCount;
                const double squaredDifferenceTotal{ std::accumulate(state.calibratedSamples.begin(), state.calibratedSamples.end(),
                                                                     0.0,
                                                                     [&state](const double total, const double sample)
                                                                     {
                                                                         const double difference{ sample - state.sampleMean };
                                                                         return total + (difference * difference);
                                                                     }) };
                state.sampleDeviation = std::sqrt(squaredDifferenceTotal / sampleCount);
                simulateStageLatency(260ms);
                state.statisticsCalculated = true;
            },
            Atlas::TaskOptions{ "Calculate sample statistics" }) };

        const auto buildHistogram{ graph.addTask(
            [&state]
            {
                requireStage(state.calibrated, "Calibrated samples are required for histogram generation");
                for (const double sample : state.calibratedSamples)
                {
                    const auto bucket{ static_cast<std::size_t>(std::clamp(static_cast<int>((sample + 2.0) * 8.0), 0, 31)) };
                    ++state.histogram.at(bucket);
                }
                simulateStageLatency(220ms);
                state.histogramBuilt = true;
            },
            Atlas::TaskOptions{ "Build amplitude histogram" }) };

        const auto analyseSpectrum{ graph.addTask(
            [&state]
            {
                requireStage(state.filtered, "Filtered samples are required for spectral analysis");
                constexpr std::array<double, 8U> frequencies{ 0.007, 0.011, 0.017, 0.023, 0.031, 0.043, 0.059, 0.071 };
                double energy{ 0.0 };
                for (const double frequency : frequencies)
                {
                    double real{ 0.0 };
                    double imaginary{ 0.0 };
                    for (std::size_t index{ 0U }; index < state.filteredSamples.size(); index += 4U)
                    {
                        const double angle{ frequency * static_cast<double>(index) };
                        real += state.filteredSamples.at(index) * std::cos(angle);
                        imaginary += state.filteredSamples.at(index) * std::sin(angle);
                    }
                    energy += (real * real) + (imaginary * imaginary);
                }
                state.spectralEnergy = energy / static_cast<double>(state.filteredSamples.size());
                simulateStageLatency(420ms);
                state.spectrumAnalysed = true;
            },
            Atlas::TaskOptions{ "Analyse frequency spectrum" }) };

        const auto detectAnomalies{ graph.addTask(
            [&state]
            {
                requireStage(state.filtered && state.statisticsCalculated,
                             "Filtered samples and statistics are required for anomaly detection");
                const double threshold{ state.sampleDeviation * 2.35 };
                state.anomalyCount = static_cast<std::size_t>(
                    std::count_if(state.filteredSamples.begin(), state.filteredSamples.end(), [&state, threshold](const double sample)
                                  { return std::abs(sample - state.sampleMean) > threshold; }));
                simulateStageLatency(300ms);
                state.anomaliesDetected = true;
            },
            Atlas::TaskOptions{ "Detect anomalous samples" }) };

        const auto compressSamples{ graph.addTask(
            [&state]
            {
                requireStage(state.filtered, "Filtered samples are required for compression");
                constexpr std::uint64_t fnvOffset{ 14'695'981'039'346'656'037ULL };
                constexpr std::uint64_t fnvPrime{ 1'099'511'628'211ULL };
                std::int64_t previousSample{ 0 };
                state.compressedChecksum = fnvOffset;
                for (const double sample : state.filteredSamples)
                {
                    const auto quantized{ static_cast<std::int64_t>((sample + 4.0) * 10'000.0) };
                    const std::int64_t delta{ quantized - previousSample };
                    const std::int64_t magnitude{ std::abs(delta) };
                    state.compressedBytes += magnitude <= 127 ? 1U : (magnitude <= 32'767 ? 2U : 4U);
                    state.compressedChecksum ^= static_cast<std::uint64_t>(delta);
                    state.compressedChecksum *= fnvPrime;
                    previousSample = quantized;
                }
                simulateStageLatency(340ms);
                state.compressed = true;
            },
            Atlas::TaskOptions{ "Delta-compress samples" }) };

        const auto calculateQuality{ graph.addTask(
            [&state]
            {
                requireStage(state.statisticsCalculated && state.histogramBuilt && state.spectrumAnalysed && state.anomaliesDetected,
                             "All analysis branches must complete before quality scoring");
                const double anomalyRatio{ static_cast<double>(state.anomalyCount) /
                                           static_cast<double>(state.filteredSamples.size()) };
                const auto populatedBuckets{ std::count_if(state.histogram.begin(), state.histogram.end(),
                                                           [](const std::size_t count) { return count != 0U; }) };
                state.qualityScore =
                    (100.0 * (1.0 - anomalyRatio)) + static_cast<double>(populatedBuckets) - std::log1p(state.spectralEnergy);
                simulateStageLatency(180ms);
                state.qualityCalculated = true;
            },
            Atlas::TaskOptions{ "Calculate data quality" }) };

        const auto assemblePackage{ graph.addTask(
            [&state]
            {
                requireStage(state.signatureCalculated && state.compressed && state.qualityCalculated,
                             "Signature, compression, and quality results are required for packaging");
                const auto quantizedQuality{ static_cast<std::uint64_t>((state.qualityScore + 100.0) * 1'000'000.0) };
                state.packageChecksum = state.rawSignature ^ (state.compressedChecksum << 1U) ^
                                        (static_cast<std::uint64_t>(state.compressedBytes) << 32U) ^ quantizedQuality;
                simulateStageLatency(160ms);
                state.packageAssembled = true;
            },
            Atlas::TaskOptions{ "Assemble telemetry package" }) };

        const auto validatePackage{ graph.addTask(
            [&state]
            {
                requireStage(state.packageAssembled && state.histogramBuilt, "The package and histogram are required for validation");
                const std::size_t histogramSamples{ std::accumulate(state.histogram.begin(), state.histogram.end(), 0U) };
                requireStage(histogramSamples == state.calibratedSamples.size(), "Histogram validation failed");
                requireStage(state.compressedBytes > 0U && state.packageChecksum != 0U, "Package integrity validation failed");
                simulateStageLatency(120ms);
                state.packageValidated = true;
            },
            Atlas::TaskOptions{ "Validate telemetry package" }) };

        const auto publishPackage{ graph.addTask(
            [&state]
            {
                requireStage(state.packageValidated, "The telemetry package must be validated before publication");
                simulateStageLatency(80ms);
                state.published = true;
            },
            Atlas::TaskOptions{ "Publish telemetry package" }) };

        if (!configure || !ingestTasks.at(0U) || !ingestTasks.at(1U) || !ingestTasks.at(2U) || !mergeStreams || !calibrate ||
            !calculateSignature || !filterNoise || !calculateStatistics || !buildHistogram || !analyseSpectrum || !detectAnomalies ||
            !compressSamples || !calculateQuality || !assemblePackage || !validatePackage || !publishPackage)
        {
            return false;
        }

        const bool dependenciesAdded{ graph.addDependency(ingestTasks.at(0U).value(), configure.value()) &&
                                      graph.addDependency(ingestTasks.at(1U).value(), configure.value()) &&
                                      graph.addDependency(ingestTasks.at(2U).value(), configure.value()) &&
                                      graph.addDependency(mergeStreams.value(), ingestTasks.at(0U).value()) &&
                                      graph.addDependency(mergeStreams.value(), ingestTasks.at(1U).value()) &&
                                      graph.addDependency(mergeStreams.value(), ingestTasks.at(2U).value()) &&
                                      graph.addDependency(calibrate.value(), mergeStreams.value()) &&
                                      graph.addDependency(calculateSignature.value(), mergeStreams.value()) &&
                                      graph.addDependency(filterNoise.value(), calibrate.value()) &&
                                      graph.addDependency(calculateStatistics.value(), calibrate.value()) &&
                                      graph.addDependency(buildHistogram.value(), calibrate.value()) &&
                                      graph.addDependency(analyseSpectrum.value(), filterNoise.value()) &&
                                      graph.addDependency(detectAnomalies.value(), filterNoise.value()) &&
                                      graph.addDependency(detectAnomalies.value(), calculateStatistics.value()) &&
                                      graph.addDependency(compressSamples.value(), filterNoise.value()) &&
                                      graph.addDependency(calculateQuality.value(), calculateStatistics.value()) &&
                                      graph.addDependency(calculateQuality.value(), buildHistogram.value()) &&
                                      graph.addDependency(calculateQuality.value(), analyseSpectrum.value()) &&
                                      graph.addDependency(calculateQuality.value(), detectAnomalies.value()) &&
                                      graph.addDependency(assemblePackage.value(), calculateSignature.value()) &&
                                      graph.addDependency(assemblePackage.value(), compressSamples.value()) &&
                                      graph.addDependency(assemblePackage.value(), calculateQuality.value()) &&
                                      graph.addDependency(validatePackage.value(), assemblePackage.value()) &&
                                      graph.addDependency(validatePackage.value(), buildHistogram.value()) &&
                                      graph.addDependency(publishPackage.value(), validatePackage.value()) };

        return dependenciesAdded && graph.finishTaskGraph();
    }

    std::optional<PipelineSummary> runPipeline(Atlas::CpuExecutor& executor, const std::string_view executorName)
    {
        Atlas::TaskGraph graph;
        PipelineState state;
        if (!createPipelineGraph(graph, state))
        {
            std::cerr << "Failed to create the pipeline graph for " << executorName << '\n';
            return std::nullopt;
        }

        std::cout << "Running " << graph.getTaskCount() << " pipeline tasks with " << executorName << "...\n";
        Atlas::KahnScheduler scheduler{ graph, executor };
        const Atlas::SchedulerResult result{ scheduler.execute() };
        std::cout << executorName << ": " << result << '\n';

        if (result.status != Atlas::SchedulerStatus::Success || !state.published)
        {
            std::cerr << "Pipeline execution failed with " << executorName << '\n';
            return std::nullopt;
        }

        std::cout << "  Published " << state.filteredSamples.size() << " samples, found " << state.anomalyCount
                  << " anomalies, compressed to " << state.compressedBytes << " bytes, checksum " << state.packageChecksum << '\n';

        std::cout << '\n';

        return PipelineSummary{ state.filteredSamples.size(), state.anomalyCount, state.compressedBytes, state.packageChecksum };
    }
} // namespace

int main()
{
    Atlas::SynchronousCpuExecutor synchronousExecutor;
    const auto synchronousSummary{ runPipeline(synchronousExecutor, "SynchronousCpuExecutor") };

    Atlas::WorkerpoolExecutor workerpoolExecutor{ 4U };
    const auto workerpoolSummary{ runPipeline(workerpoolExecutor, "WorkerpoolExecutor (4 threads)") };

    if (!synchronousSummary || !workerpoolSummary)
    {
        return EXIT_FAILURE;
    }

    if (synchronousSummary.value() != workerpoolSummary.value())
    {
        std::cerr << "Executor results did not match\n";
        return EXIT_FAILURE;
    }

    std::cout << "Both CPU executors produced identical pipeline results.\n";
    return EXIT_SUCCESS;
}
