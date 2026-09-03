#ifndef ATLAS_TASK_PACK_INTERNAL
#define ATLAS_TASK_PACK_INTERNAL

#include "atlas/Extension/TaskPack.h"
#include "atlas/Extension/TaskPackAbi.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace Atlas::Detail
{
    constexpr std::size_t maximumTaskPackErrorBytes{ 4096U };
    constexpr std::size_t maximumTaskPackParameterBytes{ 64U * 1024U };
    constexpr std::size_t maximumTaskPackSummaryBytes{ 64U * 1024U };
    constexpr std::size_t maximumTaskPackBuffers{ 32U };
    constexpr std::size_t maximumTaskPackGpuBytes{ 256U * 1024U * 1024U };

    /** @brief Move-disabled owner of one loaded native task-pack module. */
    struct NativeModule final
    {
        /** @brief Loads the explicitly trusted library at @p path. */
        explicit NativeModule(const std::filesystem::path& path);
        ~NativeModule();
        NativeModule(const NativeModule&) = delete;
        NativeModule& operator=(const NativeModule&) = delete;
        NativeModule(NativeModule&&) = delete;
        NativeModule& operator=(NativeModule&&) = delete;

        /** @brief Resolves the required version-negotiation entry point. */
        atlas_task_pack_get_api_fn entryPoint() const;

      private:
        void* handle{ nullptr };
    };

    /** @brief Shared module and copied callback table retained by prepared work. */
    struct ModuleState final
    {
        /// @brief Keeps native callback addresses mapped.
        std::shared_ptr<NativeModule> module;
        /// @brief Host-owned copy of the validated ABI table.
        atlas_task_pack_api_v1 api{};
    };

    /** @brief One inspected manifest paired with its validated loaded module. */
    struct LoadedTaskPack final
    {
        /// @brief Host-owned inspected metadata.
        TaskPackManifest manifest;
        /// @brief Shared module lifetime state.
        std::shared_ptr<ModuleState> module;
    };

    /** @brief Bounded host storage behind one ABI writer. */
    struct WriterBuffer final
    {
        /// @brief Bytes accepted so far.
        std::string bytes;
        /// @brief Per-writer byte limit.
        std::size_t limit{ 0U };
        /// @brief Records invalid input, allocation failure, or a limit breach.
        bool exceeded{ false };
        /// @brief Optional shared byte counter used by a writer group.
        std::size_t* aggregateBytes{ nullptr };
        /// @brief Shared limit when aggregateBytes is non-null.
        std::size_t aggregateLimit{ 0U };
    };

    atlas_task_pack_writer makeWriter(WriterBuffer& buffer) noexcept;
    atlas_task_pack_string_view stringView(std::string_view value) noexcept;
    void requireValidStatus(atlas_task_pack_status status, std::string_view operation);
    std::runtime_error callbackError(std::string_view operation, const WriterBuffer& error);

    TaskPackManifest inspectTaskPackDirectory(const std::filesystem::path& directory);
    std::string canonicalParameters(const CustomTaskDescriptor& descriptor, std::string_view parameterJson);
    CustomTaskSummary validateSummary(const CustomTaskDescriptor& descriptor, std::string_view summaryJson);
    std::vector<std::uint32_t> readSpirvAsset(const std::filesystem::path& path);
    std::string hostPlatform();
    std::string hostArchitecture();
} // namespace Atlas::Detail

#endif // !ATLAS_TASK_PACK_INTERNAL
