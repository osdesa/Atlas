#ifndef ATLAS_EXTENSION_TASK_PACK
#define ATLAS_EXTENSION_TASK_PACK

#include "atlas/Tasking/ExecutionResource.h"
#include "atlas/Tasking/TaskGraph.h"
#include "atlas/Vulkan/VulkanCompute.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

/** @file TaskPack.h @brief Declares trusted native task-pack discovery and prepared task ownership. */

/**
 * @defgroup extension Extension
 * @brief Trusted native CPU and declarative storage-buffer GPU task packs.
 */

namespace Atlas
{
    class VulkanRuntime;

    /**
     * @ingroup extension
     * @brief Scalar field types supported by version-one task packs.
     */
    enum class TaskPackFieldType : std::uint8_t
    {
        Boolean,
        SignedInteger,
        UnsignedInteger,
        Number,
        String,
        Enumeration
    };

    /**
     * @ingroup extension
     * @brief One validated scalar parameter or summary value.
     */
    using TaskPackScalar = std::variant<bool, std::int64_t, std::uint64_t, double, std::string>;

    /**
     * @ingroup extension
     * @brief Describes one flat, typed parameter or summary field.
     */
    struct TaskPackFieldDescriptor
    {
        /// @brief Stable field identifier used as the JSON object key.
        std::string id;
        /// @brief Human-readable field name.
        std::string displayName;
        /// @brief Human-readable field description.
        std::string description;
        /// @brief Required scalar JSON type.
        TaskPackFieldType type{ TaskPackFieldType::String };
        /// @brief Whether callers must supply this field when no default applies.
        bool required{ true };
        /// @brief Optional validated default inserted during canonicalization.
        std::optional<TaskPackScalar> defaultValue;
        /// @brief Inclusive signed-integer lower bound.
        std::optional<std::int64_t> minimumSigned;
        /// @brief Inclusive signed-integer upper bound.
        std::optional<std::int64_t> maximumSigned;
        /// @brief Inclusive unsigned-integer lower bound.
        std::optional<std::uint64_t> minimumUnsigned;
        /// @brief Inclusive unsigned-integer upper bound.
        std::optional<std::uint64_t> maximumUnsigned;
        /// @brief Inclusive finite-number lower bound.
        std::optional<double> minimumNumber;
        /// @brief Inclusive finite-number upper bound.
        std::optional<double> maximumNumber;
        /// @brief Required positive maximum UTF-8 byte length for a string field.
        std::optional<std::size_t> maximumLength;
        /// @brief Complete allowed values for an enumeration.
        std::vector<std::string> enumValues;
    };

    /**
     * @ingroup extension
     * @brief One platform/architecture native binary in a pack.
     */
    struct TaskPackPlatformBinary
    {
        /// @brief Operating-system identifier (`linux` or `windows`).
        std::string platform;
        /// @brief Architecture identifier (`x86_64` or `aarch64`).
        std::string architecture;
        /// @brief Safe pack-relative native-library path.
        std::filesystem::path libraryPath;
    };

    /**
     * @ingroup extension
     * @brief One manifest-listed SPIR-V compute asset.
     */
    struct TaskPackShaderDescriptor
    {
        /// @brief Pack-unique shader identifier.
        std::string shaderId;
        /// @brief Safe pack-relative `.spv` asset path.
        std::filesystem::path assetPath;
        /// @brief Selected compute entry point.
        std::string entryPoint{ "main" };
        /// @brief Exact set-zero storage-buffer interface and access.
        std::vector<ShaderBufferBinding> storageBuffers;
    };

    /**
     * @ingroup extension
     * @brief Public task metadata copied from a validated pack manifest.
     */
    struct CustomTaskDescriptor
    {
        /// @brief Owning pack identifier.
        std::string packId;
        /// @brief Display version from the manifest; not executable identity.
        std::string packVersion;
        /// @brief Canonical SHA-256 executable identity.
        std::string packDigest;
        /// @brief Pack-local task identifier.
        std::string taskId;
        /// @brief Human-readable task name.
        std::string displayName;
        /// @brief Human-readable task description.
        std::string description;
        /// @brief Required Atlas execution resource.
        ExecutionResource resource{ ExecutionResource::CPU };
        /// @brief Flat scalar parameter contract.
        std::vector<TaskPackFieldDescriptor> parameters;
        /// @brief Flat scalar summary contract.
        std::vector<TaskPackFieldDescriptor> summaries;
        /// @brief Required manifest shader for a GPU task; empty for CPU.
        std::optional<std::string> shaderId;
        /// @brief Exact GPU bindings downloaded before summary construction.
        std::vector<std::uint32_t> resultBindings;
        /// @brief Whether a caller may request cooperative dispatch slicing.
        bool supportsSlicing{ false };

        /// @brief Returns `<pack-id>/<task-id>` for diagnostics and display.
        std::string qualifiedId() const;
    };

    /**
     * @ingroup extension
     * @brief Fully inspected manifest plus its canonical content digest.
     */
    struct TaskPackManifest
    {
        /// @brief Manifest format version.
        std::uint32_t schemaVersion{ 1U };
        /// @brief Required native ABI version.
        std::uint32_t abiVersion{ 1U };
        /// @brief Stable pack identifier.
        std::string packId;
        /// @brief Publisher display version.
        std::string version;
        /// @brief Human-readable pack name.
        std::string displayName;
        /// @brief Human-readable pack description.
        std::string description;
        /// @brief Canonical SHA-256 over the manifest and referenced files.
        std::string digest;
        /// @brief Canonical inspected source directory.
        std::filesystem::path sourceDirectory;
        /// @brief Supported native platform/architecture binaries.
        std::vector<TaskPackPlatformBinary> platformBinaries;
        /// @brief Manifest-listed precompiled SPIR-V assets.
        std::vector<TaskPackShaderDescriptor> shaders;
        /// @brief Manifest task descriptors carrying this pack's identity.
        std::vector<CustomTaskDescriptor> tasks;
    };

    /**
     * @ingroup extension
     * @brief Inputs used to prepare one independent custom-task node.
     */
    struct CustomTaskCreateInfo
    {
        /// @brief Flat JSON parameter object validated and canonicalized before the callback.
        std::string parameterJson{ "{}" };
        /// @brief Deterministic graph seed supplied to native preparation.
        std::uint64_t graphSeed{ 0U };
        /// @brief Stable node position supplied to native preparation.
        std::uint64_t stableNodeIndex{ 0U };
        /// @brief Required borrowed runtime for GPU tasks; it must outlive the instance.
        VulkanRuntime* vulkanRuntime{ nullptr };
        /// @brief Optional cooperative slice extent, accepted only when declared supported.
        std::optional<DispatchDimensions> sliceDimensions;
    };

    /**
     * @ingroup extension
     * @brief Bounded, schema-validated task summary.
     */
    struct CustomTaskSummary
    {
        /// @brief Deterministically ordered validated JSON object.
        std::string canonicalJson;
        /// @brief Validated scalar values in descriptor order.
        std::vector<std::pair<std::string, TaskPackScalar>> fields;
    };

    class TaskPackRegistry;

    /**
     * @ingroup extension
     * @brief Move-only prepared custom task and all native/resource lifetime state.
     *
     * The originating graph must outlive this instance after addToGraph(). Native
     * CPU code executes without sandboxing and may access the process, filesystem,
     * and network, create threads, hang, or terminate the process. Instance methods
     * are not internally synchronized and must not overlap.
     */
    class CustomTaskInstance final
    {
      public:
        ~CustomTaskInstance();
        /// @brief Transfers prepared task and native/resource lifetime ownership.
        CustomTaskInstance(CustomTaskInstance&&) noexcept;
        /// @brief Replaces this instance by transferring prepared task ownership.
        CustomTaskInstance& operator=(CustomTaskInstance&&) noexcept;
        CustomTaskInstance(const CustomTaskInstance&) = delete;
        CustomTaskInstance& operator=(const CustomTaskInstance&) = delete;

        /// @brief Returns the immutable descriptor used to prepare this instance.
        const CustomTaskDescriptor& descriptor() const noexcept;

        /**
         * @brief Adds the prepared ordinary CPU, GPU, or sliced GPU payload exactly once.
         * @return The graph-owned handle, or empty if already added or rejected by the graph.
         */
        std::optional<TaskHandle> addToGraph(TaskGraph& graph, TaskOptions options = TaskOptions{});

        /**
         * @brief Collects a bounded scalar summary after the added task reaches a terminal state.
         * @throws std::logic_error Before terminal execution or on repeated collection.
         * @throws std::runtime_error For missing, oversized, callback-failed, or schema-invalid output.
         */
        CustomTaskSummary collectSummary();

      private:
        struct Impl;
        explicit CustomTaskInstance(std::unique_ptr<Impl> state) noexcept;
        std::unique_ptr<Impl> implementation;

        friend class TaskPackRegistry;
    };

    /**
     * @ingroup extension
     * @brief Inspects and explicitly loads trusted native task-pack directories.
     *
     * Inspection performs bounded filesystem, manifest, and SHA-256 validation
     * without loading native code. loadDirectory() is an explicit trust boundary:
     * the selected library runs in-process with the user's full privileges. Registry
     * access is not internally synchronized.
     * @plantumlfile task_pack.puml
     */
    class TaskPackRegistry final
    {
      public:
        TaskPackRegistry();
        ~TaskPackRegistry();
        /// @brief Transfers all loaded pack registrations and module ownership.
        TaskPackRegistry(TaskPackRegistry&&) noexcept;
        /// @brief Replaces this registry by transferring loaded pack ownership.
        TaskPackRegistry& operator=(TaskPackRegistry&&) noexcept;
        TaskPackRegistry(const TaskPackRegistry&) = delete;
        TaskPackRegistry& operator=(const TaskPackRegistry&) = delete;

        /// @brief Validates and hashes a pack directory without loading its native module.
        TaskPackManifest inspectDirectory(const std::filesystem::path& directory) const;

        /// @brief Inspects then loads the native binary for the current host.
        const TaskPackManifest& loadDirectory(const std::filesystem::path& directory);

        /// @brief Resolves exactly one loaded pack ID, content digest, and task ID.
        const CustomTaskDescriptor* findTask(std::string_view packId, std::string_view packDigest,
                                             std::string_view taskId) const noexcept;

        /// @brief Prepares one independent CPU or GPU task from validated scalar JSON parameters.
        CustomTaskInstance createTask(std::string_view packId, std::string_view packDigest, std::string_view taskId,
                                      const CustomTaskCreateInfo& createInfo) const;

      private:
        struct Impl;
        std::unique_ptr<Impl> implementation;
    };
} // namespace Atlas

#endif // !ATLAS_EXTENSION_TASK_PACK
