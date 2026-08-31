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
     * @brief Owns one compute-capable Vulkan device and queue.
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

        /// @brief Allocates one persistent device-local storage buffer.
        VulkanBuffer createBuffer(std::size_t sizeInBytes) const;
        /// @brief Creates a reusable pipeline from validated SPIR-V and bindings.
        VulkanComputePipeline createComputePipeline(const ComputeShader& shader) const;

        /// @brief Copies @p data into @p buffer through an internal staging allocation.
        void upload(const VulkanBuffer& buffer, std::span<const std::byte> data, std::size_t offset = 0U) const;
        /// @brief Copies bytes from @p buffer into @p destination through staging.
        void download(const VulkanBuffer& buffer, std::span<std::byte> destination, std::size_t offset = 0U) const;

        /// @brief Selects discrete, integrated, virtual, then CPU devices with stable ties.
        static std::optional<std::size_t> selectDefaultDevice(std::span<const VulkanDeviceInfo> devices) noexcept;

      private:
        /// @brief Private implementation that owns the shared Vulkan context.
        struct Impl;
        /// @brief Owned runtime state; resources keep the context alive.
        std::unique_ptr<Impl> implementation;

        friend class VulkanExecutor;
    };
} // namespace Atlas

#endif // !ATLAS_VULKAN_RUNTIME
