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
        std::optional<std::filesystem::path> outputDirectory;
        bool validateOnly{ false };
        bool overwrite{ false };
    };

    void printUsage(std::ostream& stream)
    {
        stream << "Usage: atlas_bench --manifest <file> [--output-dir <directory>] [--validate-only] [--overwrite]\n";
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
            if (argument == "--manifest" || argument == "--output-dir")
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
        if (result.manifest.empty())
        {
            throw std::runtime_error{ "--manifest is required" };
        }
        if (!result.validateOnly && !result.outputDirectory.has_value())
        {
            throw std::runtime_error{ "--output-dir is required unless --validate-only is used" };
        }
        if (result.validateOnly && result.outputDirectory.has_value())
        {
            throw std::runtime_error{ "--output-dir is not used with --validate-only" };
        }
        return result;
    }
} // namespace

int main(const int argumentCount, char** arguments)
{
    try
    {
        const CommandLine commandLine{ parseCommandLine(argumentCount, arguments) };
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
    catch (const std::exception& error)
    {
        std::cerr << "atlas_bench: " << error.what() << '\n';
        printUsage(std::cerr);
        return EXIT_FAILURE;
    }
}
