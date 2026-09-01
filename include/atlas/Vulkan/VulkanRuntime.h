#ifndef ATLAS_VULKAN_RUNTIME
#define ATLAS_VULKAN_RUNTIME

#include "VulkanCompute.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <vulkan/vulkan_core.h>

/** @file VulkanRuntime.h @brief Declares compute-only Vulkan runtime ownership. */

namespace Atlas
{
    namespace Detail
    {
        struct VulkanTestingAccess;
    }

    /**
     * @ingroup vulkan
     * @brief Device properties exposed to deterministic selection callbacks.
     */
    struct VulkanDeviceInfo
    {
        /// @brief Stable position in the runtime's physical-device enumeration.
        std::uint32_t enumerationIndex{ 0U };
        /// @brief Driver-reported physical-device name.
        std::string name;
        /// @brief Vulkan physical-device classification.
        VkPhysicalDeviceType type{ VK_PHYSICAL_DEVICE_TYPE_OTHER };
        /// @brief Vulkan API version supported by the device.
        std::uint32_t apiVersion{ 0U };
        /// @brief Queue families capable of compute submission.
        std::vector<std::uint32_t> computeQueueFamilies;
    };

    /**
     * @ingroup vulkan
     * @brief Selects one index from the supplied compute-capable devices.
     */
    using VulkanDeviceSelector = std::function<std::optional<std::size_t>(std::span<const VulkanDeviceInfo>)>;
    /**
     * @ingroup vulkan
     * @brief Receives error-severity validation-layer messages.
     */
    using VulkanValidationCallback = std::function<void(std::string_view)>;

    /**
     * @ingroup vulkan
     * @brief Controls validation and physical-device selection during runtime creation.
     */
    struct VulkanRuntimeOptions
    {
        /// @brief Requests the Khronos validation layer and debug messenger.
        bool enableValidation{ false };
        /// @brief Optional application selector; the default is deterministic by device type.
        VulkanDeviceSelector deviceSelector;
        /// @brief Optional receiver for validation error messages.
        VulkanValidationCallback validationCallback;
    };

    /**
     * @ingroup vulkan
     * @brief Timestamp-query properties of the selected compute queue.
     */
    struct VulkanTimestampCapabilities
    {
        /// @brief Whether the queue exposes usable device timestamps.
        bool supported{ false };
        /// @brief Number of valid low-order timestamp bits for the compute queue.
        std::uint32_t validBits{ 0U };
        /// @brief Nanoseconds represented by one device timestamp tick.
        float periodNanoseconds{ 0.0F };
    };

    /**
     * @ingroup vulkan
     * @brief Owns one compute-capable Vulkan device and queue.
     *
     * Device loss is permanent for the shared context retained by this runtime,
     * its resources, and executors. Later device operations throw VulkanError
     * with VK_ERROR_DEVICE_LOST; Atlas does not recreate the device or fall back.
     * @plantumlfile vulkan_runtime.puml
     */
    class VulkanRuntime final
    {
      public:
        /**
         * @brief Creates a compute-only Vulkan instance, device, queue, and command pool.
         * @param options Validation and physical-device selection options.
         */
        explicit VulkanRuntime(VulkanRuntimeOptions options = {});
        /// @brief Destroys the owned device and all remaining Vulkan resources.
        ~VulkanRuntime();

        /// @brief Transfers exclusive runtime ownership.
        VulkanRuntime(VulkanRuntime&&) noexcept;
        /// @brief Replaces this runtime with transferred ownership.
        VulkanRuntime& operator=(VulkanRuntime&&) noexcept;
        /// @brief Prevents copying Vulkan device ownership.
        VulkanRuntime(const VulkanRuntime&) = delete;
        /// @brief Prevents copy assignment of Vulkan device ownership.
        VulkanRuntime& operator=(const VulkanRuntime&) = delete;

        /// @brief Returns the selected physical-device description.
        const VulkanDeviceInfo& deviceInfo() const noexcept;

        /// @brief Returns device-clock timestamp support for the selected compute queue.
        VulkanTimestampCapabilities timestampCapabilities() const noexcept;

        /// @brief Allocates one persistent device-local storage buffer.
        /// @throws VulkanError When the device is lost or allocation fails.
        VulkanBuffer createBuffer(std::size_t sizeInBytes) const;
        /// @brief Creates a reusable pipeline from validated SPIR-V and bindings.
        /// @throws VulkanError When the device is lost or pipeline creation fails.
        VulkanComputePipeline createComputePipeline(const ComputeShader& shader) const;

        /// @brief Copies @p data into @p buffer through an internal staging allocation.
        /// @throws VulkanError When the device is lost or transfer operations fail.
        void upload(const VulkanBuffer& buffer, std::span<const std::byte> data, std::size_t offset = 0U) const;
        /// @brief Copies bytes from @p buffer into @p destination through staging.
        /// @throws VulkanError When the device is lost or transfer operations fail.
        void download(const VulkanBuffer& buffer, std::span<std::byte> destination, std::size_t offset = 0U) const;

        /// @brief Selects discrete, integrated, virtual, then CPU devices with stable ties.
        static std::optional<std::size_t> selectDefaultDevice(std::span<const VulkanDeviceInfo> devices) noexcept;

      private:
        /// @brief Private implementation that owns the shared Vulkan context.
        struct Impl;
        /// @brief Owned runtime state; resources keep the context alive.
        std::unique_ptr<Impl> implementation;

        friend class VulkanExecutor;
        friend struct Detail::VulkanTestingAccess;
    };
} // namespace Atlas

#endif // !ATLAS_VULKAN_RUNTIME
