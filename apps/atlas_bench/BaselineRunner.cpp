#include "BaselineRunner.h"

#include "BenchmarkProgress.h"
#include "BenchmarkRunner.h"
#include "DirectBenchmarkRunner.h"
#include "WorkloadGenerator.h"
#include "atlas/Scheduler/SchedulerStatus.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

/**
 * @file BaselineRunner.cpp
 * @brief Implements deterministic paired suite execution and variant rotation.
 */

namespace Atlas::Benchmark
{
    namespace
    {
        struct VariantRunner final
        {
            VariantRunner(const BaselineSuite& suite, const BaselineCase& comparisonCase, const BaselineVariant& selectedVariant)
                : variant{ selectedVariant }
            {
                ExperimentManifest manifest{ makeExperimentManifest(suite, comparisonCase, variant) };
                manifest.warmupRuns = 0U;
                manifest.repetitions = 1U;
                if (variant.executionMode == ExecutionMode::Direct)
                {
                    direct = std::make_unique<DirectBenchmarkRunner>(std::move(manifest));
                }
                else
                {
                    scheduled = std::make_unique<BenchmarkRunner>(std::move(manifest));
                }
            }

            RunRecord run(const std::uint64_t seed, const std::size_t repetition, const std::vector<TaskDescriptor>& descriptors,
                          TraceSession* const traceSession)
            {
                return direct != nullptr ? direct->runSingle(seed, repetition, descriptors, traceSession)
                                         : scheduled->runSingle(seed, repetition, descriptors, traceSession);
            }

            BaselineVariant variant;
            std::unique_ptr<DirectBenchmarkRunner> direct;
            std::unique_ptr<BenchmarkRunner> scheduled;
        };
    } // namespace

    struct BaselineSuiteRunner::Impl final
    {
        explicit Impl(BaselineSuite suiteConfig, BenchmarkProgressWriter* const progressWriter)
            : suite{ std::move(suiteConfig) }, progress{ progressWriter }
        {
        }

        BaselineBatch run()
        {
            BaselineBatch batch;
            const std::size_t variantsPerSeed{ [&]
                                               {
                                                   std::size_t count{ 0U };
                                                   for (const BaselineCase& comparisonCase : suite.cases)
                                                       count += comparisonCase.variants.size();
                                                   return count;
                                               }() };
            const std::size_t totalRuns{ variantsPerSeed * suite.seeds.size() * (suite.warmupRuns + suite.repetitions) };
            const std::size_t measuredRuns{ variantsPerSeed * suite.seeds.size() * suite.repetitions };
            std::size_t nextRunId{ 0U };
            if (progress != nullptr)
                progress->beginSuite(suite, totalRuns, measuredRuns);
            try
            {
                for (const BaselineCase& comparisonCase : suite.cases)
                {
                    std::vector<std::unique_ptr<VariantRunner>> runners;
                    runners.reserve(comparisonCase.variants.size());
                    for (const BaselineVariant& variant : comparisonCase.variants)
                    {
                        runners.push_back(std::make_unique<VariantRunner>(suite, comparisonCase, variant));
                    }

                    for (std::size_t seedIndex{ 0U }; seedIndex < suite.seeds.size(); ++seedIndex)
                    {
                        const std::uint64_t seed{ suite.seeds.at(seedIndex) };
                        const ExperimentManifest descriptorManifest{ makeExperimentManifest(suite, comparisonCase,
                                                                                            comparisonCase.variants.front()) };
                        const std::vector<TaskDescriptor> descriptors{ generateWorkload(descriptorManifest, seed) };
                        for (std::size_t warmup{ 0U }; warmup < suite.warmupRuns; ++warmup)
                        {
                            const std::size_t start{ (seedIndex + warmup) % runners.size() };
                            for (std::size_t order{ 0U }; order < runners.size(); ++order)
                            {
                                VariantRunner& runner{ *runners.at((start + order) % runners.size()) };
                                const BenchmarkRunContext context{ nextRunId,
                                                                   nextRunId + 1U,
                                                                   totalRuns,
                                                                   comparisonCase.caseId,
                                                                   runner.variant.variantId,
                                                                   runner.variant.executionMode,
                                                                   seed,
                                                                   warmup,
                                                                   true,
                                                                   order };
                                if (progress != nullptr)
                                    progress->beginRun(context, descriptors);
                                const RunRecord record{ runner.run(seed, warmup, descriptors,
                                                                   progress == nullptr ? nullptr : progress->traceSession()) };
                                if (progress != nullptr)
                                    progress->finishRun(context, record);
                                ++nextRunId;
                                if (record.schedulerResult.status != SchedulerStatus::Success)
                                {
                                    throw std::runtime_error{ "A baseline suite warmup run failed" };
                                }
                            }
                        }

                        for (std::size_t repetition{ 0U }; repetition < suite.repetitions; ++repetition)
                        {
                            const std::size_t start{ (seedIndex + repetition) % runners.size() };
                            for (std::size_t order{ 0U }; order < runners.size(); ++order)
                            {
                                VariantRunner& runner{ *runners.at((start + order) % runners.size()) };
                                const BenchmarkRunContext context{ nextRunId,
                                                                   nextRunId + 1U,
                                                                   totalRuns,
                                                                   comparisonCase.caseId,
                                                                   runner.variant.variantId,
                                                                   runner.variant.executionMode,
                                                                   seed,
                                                                   repetition,
                                                                   false,
                                                                   order };
                                if (progress != nullptr)
                                    progress->beginRun(context, descriptors);
                                RunRecord record{ runner.run(seed, repetition, descriptors,
                                                             progress == nullptr ? nullptr : progress->traceSession()) };
                                if (progress != nullptr)
                                    progress->finishRun(context, record);
                                ++nextRunId;
                                batch.records.push_back(BaselineRunRecord{ suite.suiteId, comparisonCase.caseId,
                                                                           runner.variant.variantId, runner.variant.executionMode,
                                                                           order, std::move(record) });
                                if (batch.records.back().run.schedulerResult.status != SchedulerStatus::Success)
                                {
                                    batch.succeeded = false;
                                    if (progress != nullptr)
                                        progress->finishSuite("failed");
                                    return batch;
                                }
                            }
                        }
                    }
                }
            }
            catch (const std::exception& error)
            {
                if (progress != nullptr)
                    progress->fail(error.what());
                throw;
            }
            catch (...)
            {
                if (progress != nullptr)
                    progress->fail("unknown benchmark failure");
                throw;
            }
            return batch;
        }

        BaselineSuite suite;
        BenchmarkProgressWriter* progress;
    };

    BaselineSuiteRunner::BaselineSuiteRunner(BaselineSuite suite, BenchmarkProgressWriter* const progress)
        : implementation{ std::make_unique<Impl>(std::move(suite), progress) }
    {
    }

    BaselineSuiteRunner::~BaselineSuiteRunner() = default;

    BaselineBatch BaselineSuiteRunner::run()
    {
        return implementation->run();
    }
} // namespace Atlas::Benchmark
