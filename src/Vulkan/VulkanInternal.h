#ifndef ATLAS_VULKAN_INTERNAL
#define ATLAS_VULKAN_INTERNAL

#include "atlas/Vulkan/VulkanCompute.h"
#include "atlas/Vulkan/VulkanRuntime.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>
#include <vulkan/vulkan.h>

/// @cond INTERNAL
namespace Atlas
{
    namespace Detail
    {
        /// @brief Private executor boundaries available for deterministic fault injection.
        enum class VulkanExecutorFaultPoint : std::uint8_t
        {
            BeforeExecution,
            BeforeQueueSubmit,
            AfterFenceWait,
            BeforeTimestampReadback
        };

        class VulkanAccess final
        {
          public:
            static const std::shared_ptr<VulkanBuffer::Impl>& buffer(const VulkanBuffer& value) noexcept
            {
                return value.implementation;
            }

            static const std::shared_ptr<VulkanComputePipeline::Impl>& pipeline(const VulkanComputePipeline& value) noexcept
            {
                return value.implementation;
            }

            /// @brief Retains non-Vulkan state for as long as a dispatch description survives.
            static void retainLifetime(VulkanDispatch& value, std::shared_ptr<void> lifetime) noexcept;
        };

        struct VulkanContext final
        {
            ~VulkanContext();

            /// @brief Throws immediately after the logical device has been reported lost.
            void requireDeviceAvailable(const char* operation) const;
            /// @brief Latches device loss and throws for any unsuccessful device operation.
            void checkDeviceResult(VkResult result, const char* operation);

            /// @brief Vulkan instance shared by all resources in this runtime.
            VkInstance instance{ VK_NULL_HANDLE };
            /// @brief Optional validation debug messenger.
            VkDebugUtilsMessengerEXT debugMessenger{ VK_NULL_HANDLE };
            /// @brief Selected physical device.
            VkPhysicalDevice physicalDevice{ VK_NULL_HANDLE };
            /// @brief Logical device used for compute operations.
            VkDevice device{ VK_NULL_HANDLE };
            /// @brief Compute queue selected during runtime creation.
            VkQueue queue{ VK_NULL_HANDLE };
            /// @brief Command pool for transfer and dispatch command buffers.
            VkCommandPool commandPool{ VK_NULL_HANDLE };
            /// @brief Queue family owning @ref queue.
            std::uint32_t queueFamilyIndex{ 0U };
            /// @brief Selected device properties and limits.
            VkPhysicalDeviceProperties properties{};
            /// @brief Timestamp-query properties for the selected queue family.
            VulkanTimestampCapabilities timestampCapabilities;
            /// @brief Memory types available on the selected device.
            VkPhysicalDeviceMemoryProperties memoryProperties{};
            /// @brief Public device-selection description.
            VulkanDeviceInfo deviceDescription;
            /// @brief Application callback for validation errors.
            VulkanValidationCallback validationCallback;
            /// @brief Serializes submissions and transfer operations on the queue.
            std::mutex queueMutex;
            /// @brief Permanently records VK_ERROR_DEVICE_LOST for every context owner.
            std::atomic_bool deviceLost{ false };
            /// @brief Private test hook; empty during ordinary execution.
            std::function<void(VulkanExecutorFaultPoint)> executorFaultInjector;
        };

        /// @brief Throws VulkanError when @p result is not VK_SUCCESS.
        void throwIfFailed(VkResult result, const char* operation);
        /// @brief Finds a memory type satisfying required and preferred flags.
        std::uint32_t findMemoryType(const VulkanContext& context, std::uint32_t allowedTypes,
                                     VkMemoryPropertyFlags requiredProperties, VkMemoryPropertyFlags preferredProperties = 0U);
    } // namespace Detail

    struct VulkanBuffer::Impl final
    {
        ~Impl();

        /// @brief Runtime context that owns the device used by this buffer.
        std::shared_ptr<Detail::VulkanContext> context;
        /// @brief Device-local storage buffer handle.
        VkBuffer buffer{ VK_NULL_HANDLE };
        /// @brief Memory allocation bound to @ref buffer.
        VkDeviceMemory memory{ VK_NULL_HANDLE };
        /// @brief Logical allocation size exposed by VulkanBuffer::size().
        std::size_t byteSize{ 0U };
    };

    struct VulkanComputePipeline::Impl final
    {
        ~Impl();

        /// @brief Runtime context that owns the device used by this pipeline.
        std::shared_ptr<Detail::VulkanContext> context;
        /// @brief Descriptor layout matching the shader's storage bindings.
        VkDescriptorSetLayout descriptorSetLayout{ VK_NULL_HANDLE };
        /// @brief Pipeline layout used for compute dispatch.
        VkPipelineLayout pipelineLayout{ VK_NULL_HANDLE };
        /// @brief Compute pipeline handle.
        VkPipeline pipeline{ VK_NULL_HANDLE };
        /// @brief Sorted reflected storage-buffer interface exposed to dispatch validation.
        std::vector<ShaderBufferBinding> storageBufferBindings;
    };

    struct VulkanDispatch::Impl final
    {
        /// @brief Pipeline selected for every work unit in the logical dispatch.
        VulkanComputePipeline computePipeline;
        /// @brief Exact storage-buffer bindings shared by every work unit.
        std::vector<BufferBinding> bufferBindings;
        /// @brief Optional extension-module state retained by prepared custom GPU work.
        std::shared_ptr<void> lifetimeAnchor;
    };

    struct VulkanRuntime::Impl final
    {
        /// @brief Shared context kept alive by runtime-owned resources.
        std::shared_ptr<Detail::VulkanContext> context;
    };

    inline void Detail::VulkanAccess::retainLifetime(VulkanDispatch& value, std::shared_ptr<void> lifetime) noexcept
    {
        value.implementation->lifetimeAnchor = std::move(lifetime);
    }

    namespace Detail
    {
        /// @brief Grants tests access to private Vulkan fault injection without public handles.
        struct VulkanTestingAccess final
        {
            static std::shared_ptr<VulkanContext> context(VulkanRuntime& runtime) noexcept
            {
                return runtime.implementation->context;
            }
        };
    } // namespace Detail
} // namespace Atlas
/// @endcond

#endif // !ATLAS_VULKAN_INTERNAL
