#ifndef ATLAS_TEST_TASK_PACK
#define ATLAS_TEST_TASK_PACK

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace Atlas::Testing
{
    class TaskPackTestPack final
    {
      public:
        explicit TaskPackTestPack(std::string packId = "test.pack")
        {
            static std::atomic_uint64_t next{ 0U };
            const auto timestamp{ std::chrono::steady_clock::now().time_since_epoch().count() };
            bool created{ false };
            for (std::uint64_t attempt{ 0U }; attempt < 100U; ++attempt)
            {
                directory = std::filesystem::temp_directory_path() / ("atlas-task-pack-test-" + std::to_string(timestamp) + "-" +
                                                                      std::to_string(next.fetch_add(1U, std::memory_order_relaxed)));
                if (std::filesystem::create_directory(directory))
                {
                    created = true;
                    break;
                }
            }
            if (!created)
            {
                throw std::runtime_error{ "Unable to create a unique test task-pack directory" };
            }
            std::filesystem::create_directories(directory / "bin");
            std::filesystem::create_directories(directory / "shaders");
            const std::filesystem::path librarySource{ ATLAS_TASK_PACK_MOCK_PATH };
            libraryName = librarySource.filename().string();
            std::filesystem::copy_file(librarySource, directory / "bin" / libraryName);
            std::filesystem::copy_file(ATLAS_TASK_PACK_TEST_SPIRV_PATH, directory / "shaders" / "vector_add.spv");

#if defined(_WIN32)
            constexpr const char* platform{ "windows" };
#else
            constexpr const char* platform{ "linux" };
#endif
#if defined(_M_ARM64) || defined(__aarch64__)
            constexpr const char* architecture{ "aarch64" };
#else
            constexpr const char* architecture{ "x86_64" };
#endif
            manifest =
                R"({"schema_version":1,"abi_version":1,"pack_id":")" + packId +
                R"(","version":"1.0.0","name":"Test pack","description":"Test callbacks","platforms":[{"platform":")" + platform +
                R"(","architecture":")" + architecture + R"(","library":"bin/)" + libraryName +
                R"("}],"shaders":[{"shader_id":"vector_add","path":"shaders/vector_add.spv","entry_point":"main","storage_buffers":[{"binding":0,"access":"read_only"},{"binding":1,"access":"read_only"},{"binding":2,"access":"write_only"}]}],"tasks":[{"task_id":"cpu_success","name":"CPU success","description":"Succeeds","resource":"cpu","parameters":[{"id":"amount","type":"unsigned_integer","minimum":1,"maximum":10,"default":3,"required":false}],"summaries":[{"id":"value","type":"unsigned_integer","minimum":0,"maximum":100}]},{"task_id":"cpu_error","name":"CPU error","description":"Fails","resource":"cpu","parameters":[],"summaries":[]},{"task_id":"gpu_vector","name":"GPU vector","description":"Adds vectors","resource":"gpu","parameters":[],"summaries":[{"id":"ok","type":"boolean"}],"shader_id":"vector_add","result_bindings":[2],"supports_slicing":true}]})";
            writeManifest(manifest);
        }

        ~TaskPackTestPack()
        {
            std::error_code ignored;
            std::filesystem::remove_all(directory, ignored);
        }

        TaskPackTestPack(const TaskPackTestPack&) = delete;
        TaskPackTestPack& operator=(const TaskPackTestPack&) = delete;

        void writeManifest(const std::string& value)
        {
            std::ofstream output{ directory / "manifest.json", std::ios::binary | std::ios::trunc };
            output << value;
            if (!output)
            {
                throw std::runtime_error{ "Unable to write test task-pack manifest" };
            }
        }

        std::filesystem::path directory;
        std::string libraryName;
        std::string manifest;
    };
} // namespace Atlas::Testing

#endif // !ATLAS_TEST_TASK_PACK
