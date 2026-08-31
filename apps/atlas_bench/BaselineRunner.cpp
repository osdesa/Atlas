#include "BaselineRunner.h"

#include "BenchmarkRunner.h"
#include "DirectBenchmarkRunner.h"
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

            RunRecord run(const std::uint64_t seed, const std::size_t repetition)
            {
                return direct != nullptr ? direct->runSingle(seed, repetition) : scheduled->runSingle(seed, repetition);
            }

            BaselineVariant variant;
            std::unique_ptr<DirectBenchmarkRunner> direct;
            std::unique_ptr<BenchmarkRunner> scheduled;
        };
    } // namespace

    struct BaselineSuiteRunner::Impl final
    {
        explicit Impl(BaselineSuite suiteConfig) : suite{ std::move(suiteConfig) } {}

        BaselineBatch run()
        {
            BaselineBatch batch;
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
                    for (std::size_t warmup{ 0U }; warmup < suite.warmupRuns; ++warmup)
                    {
                        const std::size_t start{ (seedIndex + warmup) % runners.size() };
                        for (std::size_t order{ 0U }; order < runners.size(); ++order)
                        {
                            VariantRunner& runner{ *runners.at((start + order) % runners.size()) };
                            const RunRecord record{ runner.run(seed, warmup) };
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
                            RunRecord record{ runner.run(seed, repetition) };
                            batch.records.push_back(BaselineRunRecord{ suite.suiteId, comparisonCase.caseId, runner.variant.variantId,
                                                                       runner.variant.executionMode, order, std::move(record) });
                            if (batch.records.back().run.schedulerResult.status != SchedulerStatus::Success)
                            {
                                batch.succeeded = false;
                                return batch;
                            }
                        }
                    }
                }
            }
            return batch;
        }

        BaselineSuite suite;
    };

    BaselineSuiteRunner::BaselineSuiteRunner(BaselineSuite suite) : implementation{ std::make_unique<Impl>(std::move(suite)) } {}

    BaselineSuiteRunner::~BaselineSuiteRunner() = default;

    BaselineBatch BaselineSuiteRunner::run()
    {
        return implementation->run();
    }
} // namespace Atlas::Benchmark
