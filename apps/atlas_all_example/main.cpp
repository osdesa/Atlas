#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

/**
 * @file main.cpp
 * @brief Runs the CPU, standalone Vulkan, and mixed Atlas examples in sequence.
 */

namespace
{
    /**
     * @brief Quotes a path for use as a single command-line argument.
     * @param path Executable path to quote.
     * @return A platform-neutral quoted path.
     */
    std::string quotePath(const std::filesystem::path& path)
    {
        std::string quoted{ "\"" + path.string() + "\"" };
#ifdef _WIN32
        for (std::size_t position{ 1U }; position < quoted.size(); ++position)
        {
            if (quoted.at(position) == '"' && quoted.at(position - 1U) != '\\')
            {
                quoted.insert(position, "\\");
                ++position;
            }
        }
#endif
        return quoted;
    }

    /**
     * @brief Runs one sibling example and reports its result.
     * @param executable Directory-relative executable path.
     * @param description Human-readable stage name.
     * @return True when the child process exits successfully.
     */
    bool runExample(const std::filesystem::path& executable, const char* const description)
    {
        std::cout << "\n=== " << description << " ===\n" << std::flush;
        const int result{ std::system(quotePath(executable).c_str()) };
        if (result != 0)
        {
            std::cerr << description << " failed with exit code " << result << '\n';
            return false;
        }
        return true;
    }
} // namespace

int main(int, char* argv[])
{
    const std::filesystem::path executableDirectory{ std::filesystem::absolute(argv[0]).parent_path() };
    const std::filesystem::path applicationsDirectory{ executableDirectory / ".." };
#ifdef _WIN32
    constexpr const char* executableSuffix{ ".exe" };
#else
    constexpr const char* executableSuffix{ "" };
#endif
    const std::filesystem::path cpuExample{ applicationsDirectory / "atlas_cli" / (std::string{ "atlas_cli" } + executableSuffix) };
    const std::filesystem::path vulkanExample{ applicationsDirectory / "atlas_vulkan_example" /
                                               (std::string{ "atlas_vulkan_example" } + executableSuffix) };
    const std::filesystem::path mixedExample{ applicationsDirectory / "atlas_mixed_example" /
                                              (std::string{ "atlas_mixed_example" } + executableSuffix) };

    if (!runExample(cpuExample, "CPU pipeline (synchronous and worker-pool executors)") ||
        !runExample(vulkanExample, "Standalone Vulkan compute") || !runExample(mixedExample, "Mixed CPU -> Vulkan -> CPU graph"))
    {
        return EXIT_FAILURE;
    }

    std::cout << "\nAll Atlas examples completed successfully.\n";
    return EXIT_SUCCESS;
}
