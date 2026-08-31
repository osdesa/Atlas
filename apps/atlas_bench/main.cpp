#include "BaselineAnalysis.h"
#include "BaselineConfig.h"
#include "BaselineRunner.h"
#include "BaselineWriter.h"
#include "BenchmarkConfig.h"
#include "BenchmarkRunner.h"
#include "ResultWriter.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

/** @file main.cpp @brief Runs versioned, reproducible Atlas benchmark experiments. */

namespace
{
    struct CommandLine
    {
        std::filesystem::path manifest;
        std::filesystem::path suite;
        std::optional<std::filesystem::path> outputDirectory;
        std::optional<std::filesystem::path> environmentFile;
        bool validateOnly{ false };
        bool overwrite{ false };
    };

    void printUsage(std::ostream& stream)
    {
        stream << "Usage: atlas_bench (--manifest <file> | --suite <file>) [--output-dir <directory>] "
                  "[--environment-file <file>] [--validate-only] [--overwrite]\n";
    }

    CommandLine parseCommandLine(const int argumentCount, char** arguments)
    {
        CommandLine result;
        for (int index{ 1 }; index < argumentCount; ++index)
        {
            const std::string_view argument{ arguments[index] };
            if (argument == "--help")
            {
                printUsage(std::cout);
                std::exit(EXIT_SUCCESS);
            }
            if (argument == "--validate-only")
            {
                result.validateOnly = true;
                continue;
            }
            if (argument == "--overwrite")
            {
                result.overwrite = true;
                continue;
            }
            if (argument == "--manifest" || argument == "--suite" || argument == "--output-dir" || argument == "--environment-file")
            {
                if (index + 1 >= argumentCount)
                {
                    throw std::runtime_error{ std::string{ argument } + " requires a value" };
                }
                const std::filesystem::path value{ arguments[++index] };
                if (argument == "--manifest")
                {
                    if (!result.manifest.empty())
                    {
                        throw std::runtime_error{ "--manifest may be supplied only once" };
                    }
                    result.manifest = value;
                }
                else if (argument == "--suite")
                {
                    if (!result.suite.empty())
                    {
                        throw std::runtime_error{ "--suite may be supplied only once" };
                    }
                    result.suite = value;
                }
                else if (argument == "--environment-file")
                {
                    if (result.environmentFile.has_value())
                    {
                        throw std::runtime_error{ "--environment-file may be supplied only once" };
                    }
                    result.environmentFile = value;
                }
                else
                {
                    if (result.outputDirectory.has_value())
                    {
                        throw std::runtime_error{ "--output-dir may be supplied only once" };
                    }
                    result.outputDirectory = value;
                }
                continue;
            }
            throw std::runtime_error{ "Unknown argument: " + std::string{ argument } };
        }
        if (result.manifest.empty() == result.suite.empty())
        {
            throw std::runtime_error{ "exactly one of --manifest or --suite is required" };
        }
        if (!result.validateOnly && !result.outputDirectory.has_value())
        {
            throw std::runtime_error{ "--output-dir is required unless --validate-only is used" };
        }
        if (result.validateOnly && result.outputDirectory.has_value())
        {
            throw std::runtime_error{ "--output-dir is not used with --validate-only" };
        }
        if (!result.manifest.empty() && result.environmentFile.has_value())
        {
            throw std::runtime_error{ "--environment-file is valid only with --suite" };
        }
        return result;
    }
} // namespace

int main(const int argumentCount, char** arguments)
{
    try
    {
        const CommandLine commandLine{ parseCommandLine(argumentCount, arguments) };
        if (!commandLine.manifest.empty())
        {
            const Atlas::Benchmark::ExperimentManifest manifest{ Atlas::Benchmark::loadManifest(commandLine.manifest) };
            if (commandLine.validateOnly)
            {
                std::cout << "Valid Atlas benchmark manifest: " << manifest.experimentId << '\n';
                return EXIT_SUCCESS;
            }

            Atlas::Benchmark::ResultWriter writer{ commandLine.outputDirectory.value(), manifest, commandLine.overwrite };
            Atlas::Benchmark::BenchmarkRunner runner{ manifest };
            const Atlas::Benchmark::BenchmarkBatch batch{ runner.run() };
            for (const Atlas::Benchmark::RunRecord& record : batch.records)
            {
                writer.append(record);
            }
            std::cout << "Wrote " << batch.records.size() << " measured run(s) to " << commandLine.outputDirectory->string() << '\n';
            return batch.succeeded ? EXIT_SUCCESS : EXIT_FAILURE;
        }

        const Atlas::Benchmark::BaselineSuite suite{ Atlas::Benchmark::loadBaselineSuite(commandLine.suite) };
        const std::optional<Atlas::Benchmark::EnvironmentMetadata> environment{
            commandLine.environmentFile.has_value()
                ? std::optional<Atlas::Benchmark::EnvironmentMetadata>{ Atlas::Benchmark::loadEnvironmentMetadata(
                      commandLine.environmentFile.value()) }
                : std::nullopt
        };
        if (commandLine.validateOnly)
        {
            std::cout << "Valid Atlas baseline suite: " << suite.suiteId << '\n';
            return EXIT_SUCCESS;
        }

        Atlas::Benchmark::BaselineSuiteRunner runner{ suite };
        const Atlas::Benchmark::BaselineBatch batch{ runner.run() };
        Atlas::Benchmark::BaselineWriter writer{ commandLine.outputDirectory.value(), suite, environment, batch.records,
                                                 commandLine.overwrite };
        for (const Atlas::Benchmark::BaselineRunRecord& record : batch.records)
        {
            writer.append(record);
        }
        if (batch.succeeded)
        {
            writer.writeSummary(Atlas::Benchmark::calculateBaselineSummary(suite, batch.records));
        }
        std::cout << "Wrote " << batch.records.size() << " baseline run(s) to " << commandLine.outputDirectory->string() << '\n';
        return batch.succeeded ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    catch (const std::exception& error)
    {
        std::cerr << "atlas_bench: " << error.what() << '\n';
        printUsage(std::cerr);
        return EXIT_FAILURE;
    }
}
