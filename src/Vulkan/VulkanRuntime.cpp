#include "atlas/Vulkan/VulkanRuntime.h"

/** @file VulkanRuntime.cpp @brief Implements compute-only Vulkan runtime and persistent resource ownership. */

#include "VulkanInternal.h"
#include "atlas/Vulkan/VulkanError.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace Atlas
{
    namespace
    {
        /// @brief Khronos validation layer requested by validation-enabled runtimes.
        constexpr const char* validationLayerName{ "VK_LAYER_KHRONOS_validation" };

        /// @brief Forwards validation-layer errors to the runtime callback.
        VKAPI_ATTR VkBool32 VKAPI_CALL validationCallback(VkDebugUtilsMessageSeverityFlagBitsEXT, VkDebugUtilsMessageTypeFlagsEXT,
                                                          const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
                                                          void* userData) noexcept
        {
            auto* const context{ static_cast<Detail::VulkanContext*>(userData) };
            if (context != nullptr && context->validationCallback && callbackData != nullptr && callbackData->pMessage != nullptr)
            {
                try
                {
                    context->validationCallback(callbackData->pMessage);
                }
                catch (...)
                {
                }
            }
            return VK_FALSE;
        }

        /// @brief Reports whether an instance layer is available.
        bool hasLayer(const char* const requestedLayer)
        {
            std::uint32_t count{ 0U };
            Detail::throwIfFailed(vkEnumerateInstanceLayerProperties(&count, nullptr), "vkEnumerateInstanceLayerProperties");
            std::vector<VkLayerProperties> layers(count);
            Detail::throwIfFailed(vkEnumerateInstanceLayerProperties(&count, layers.data()), "vkEnumerateInstanceLayerProperties");
            return std::any_of(layers.begin(), layers.end(), [requestedLayer](const VkLayerProperties& layer)
                               { return std::strcmp(layer.layerName, requestedLayer) == 0; });
        }

        /// @brief Reports whether an instance extension is available.
        bool hasInstanceExtension(const char* const requestedExtension)
        {
            std::uint32_t count{ 0U };
            Detail::throwIfFailed(vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr),
                                  "vkEnumerateInstanceExtensionProperties");
            std::vector<VkExtensionProperties> extensions(count);
            Detail::throwIfFailed(vkEnumerateInstanceExtensionProperties(nullptr, &count, extensions.data()),
                                  "vkEnumerateInstanceExtensionProperties");
            return std::any_of(extensions.begin(), extensions.end(), [requestedExtension](const VkExtensionProperties& extension)
                               { return std::strcmp(extension.extensionName, requestedExtension) == 0; });
        }

        /// @brief Lists queue families capable of compute work.
        std::vector<std::uint32_t> computeQueueFamilies(const VkPhysicalDevice physicalDevice)
        {
            std::uint32_t count{ 0U };
            vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &count, nullptr);
            std::vector<VkQueueFamilyProperties> properties(count);
            vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &count, properties.data());

            std::vector<std::uint32_t> families;
            for (std::uint32_t index{ 0U }; index < count; ++index)
            {
                if (properties.at(index).queueCount != 0U && (properties.at(index).queueFlags & VK_QUEUE_COMPUTE_BIT) != 0U)
                {
                    families.emplace_back(index);
                }
            }
            return families;
        }

        /// @brief Prefers a compute-only family, falling back to the first candidate.
        std::uint32_t selectQueueFamily(const VkPhysicalDevice physicalDevice, const std::vector<std::uint32_t>& candidates)
        {
            std::uint32_t count{ 0U };
            vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &count, nullptr);
            std::vector<VkQueueFamilyProperties> properties(count);
            vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &count, properties.data());

            const auto computeOnly{ std::find_if(candidates.begin(), candidates.end(), [&properties](const std::uint32_t index)
                                                 { return (properties.at(index).queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0U; }) };
            return computeOnly == candidates.end() ? candidates.front() : *computeOnly;
        }

        struct TemporaryBuffer final
        {
            /// @brief Borrows the runtime context for staging allocation cleanup.
            explicit TemporaryBuffer(std::shared_ptr<Detail::VulkanContext> owner) : context{ std::move(owner) } {}
            /// @brief Releases the staging buffer and memory allocation.
            ~TemporaryBuffer()
            {
                if (buffer != VK_NULL_HANDLE)
                {
                    vkDestroyBuffer(context->device, buffer, nullptr);
                }
                if (memory != VK_NULL_HANDLE)
                {
                    vkFreeMemory(context->device, memory, nullptr);
                }
            }

            /// @brief Prevents accidental copying of Vulkan staging handles.
            TemporaryBuffer(const TemporaryBuffer&) = delete;
            /// @brief Prevents copy assignment of Vulkan staging handles.
            TemporaryBuffer& operator=(const TemporaryBuffer&) = delete;
            /// @brief Transfers staging ownership without duplicating handles.
            TemporaryBuffer(TemporaryBuffer&& other) noexcept
                : context{ std::move(other.context) }, buffer{ std::exchange(other.buffer, VK_NULL_HANDLE) },
                  memory{ std::exchange(other.memory, VK_NULL_HANDLE) }, hostCoherent{ other.hostCoherent }
            {
            }
            TemporaryBuffer& operator=(TemporaryBuffer&&) = delete;

            /// @brief Runtime context owning the staging device.
            std::shared_ptr<Detail::VulkanContext> context;
            /// @brief Host-visible staging buffer handle.
            VkBuffer buffer{ VK_NULL_HANDLE };
            /// @brief Memory bound to @ref buffer.
            VkDeviceMemory memory{ VK_NULL_HANDLE };
            /// @brief Whether mapped writes need an explicit flush/invalidate.
            bool hostCoherent{ false };
        };

        /// @brief Allocates and binds a buffer with the requested memory properties.
        void allocateBuffer(const std::shared_ptr<Detail::VulkanContext>& context, const VkDeviceSize size,
                            const VkBufferUsageFlags usage, const VkMemoryPropertyFlags required,
                            const VkMemoryPropertyFlags preferred, VkBuffer& buffer, VkDeviceMemory& memory,
                            bool* const hostCoherent = nullptr)
        {
            const VkBufferCreateInfo bufferInfo{ .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                                                 .pNext = nullptr,
                                                 .flags = 0U,
                                                 .size = size,
                                                 .usage = usage,
                                                 .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                                                 .queueFamilyIndexCount = 0U,
                                                 .pQueueFamilyIndices = nullptr };
            Detail::throwIfFailed(vkCreateBuffer(context->device, &bufferInfo, nullptr, &buffer), "vkCreateBuffer");

            try
            {
                VkMemoryRequirements requirements{};
                vkGetBufferMemoryRequirements(context->device, buffer, &requirements);
                const std::uint32_t memoryType{ Detail::findMemoryType(*context, requirements.memoryTypeBits, required, preferred) };
                const VkMemoryAllocateInfo allocationInfo{ .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                                                           .pNext = nullptr,
                                                           .allocationSize = requirements.size,
                                                           .memoryTypeIndex = memoryType };
                Detail::throwIfFailed(vkAllocateMemory(context->device, &allocationInfo, nullptr, &memory), "vkAllocateMemory");
                Detail::throwIfFailed(vkBindBufferMemory(context->device, buffer, memory, 0U), "vkBindBufferMemory");

                if (hostCoherent != nullptr)
                {
                    *hostCoherent =
                        (context->memoryProperties.memoryTypes[memoryType].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0U;
                }
            }
            catch (...)
            {
                if (memory != VK_NULL_HANDLE)
                {
                    vkFreeMemory(context->device, memory, nullptr);
                    memory = VK_NULL_HANDLE;
                }
                vkDestroyBuffer(context->device, buffer, nullptr);
                buffer = VK_NULL_HANDLE;
                throw;
            }
        }

        /// @brief Creates a temporary host-visible transfer buffer.
        TemporaryBuffer createStagingBuffer(const std::shared_ptr<Detail::VulkanContext>& context, const VkDeviceSize size)
        {
            TemporaryBuffer staging{ context };
            allocateBuffer(context, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, staging.buffer, staging.memory,
                           &staging.hostCoherent);
            return staging;
        }

        class CommandResources final
        {
          public:
            /// @brief Allocates one command buffer and unsignalled fence.
            explicit CommandResources(std::shared_ptr<Detail::VulkanContext> owner) : context{ std::move(owner) }
            {
                const VkCommandBufferAllocateInfo allocationInfo{ .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                                                                  .pNext = nullptr,
                                                                  .commandPool = context->commandPool,
                                                                  .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                                                                  .commandBufferCount = 1U };
                Detail::throwIfFailed(vkAllocateCommandBuffers(context->device, &allocationInfo, &commandBuffer),
                                      "vkAllocateCommandBuffers");

                const VkFenceCreateInfo fenceInfo{ .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .pNext = nullptr, .flags = 0U };
                try
                {
                    Detail::throwIfFailed(vkCreateFence(context->device, &fenceInfo, nullptr, &fence), "vkCreateFence");
                }
                catch (...)
                {
                    vkFreeCommandBuffers(context->device, context->commandPool, 1U, &commandBuffer);
                    commandBuffer = VK_NULL_HANDLE;
                    throw;
                }
            }

            /// @brief Releases the fence and command buffer after queue completion.
            ~CommandResources()
            {
                if (fence != VK_NULL_HANDLE)
                {
                    vkDestroyFence(context->device, fence, nullptr);
                }
                if (commandBuffer != VK_NULL_HANDLE)
                {
                    vkFreeCommandBuffers(context->device, context->commandPool, 1U, &commandBuffer);
                }
            }

            /// @brief Prevents copying command-buffer ownership.
            CommandResources(const CommandResources&) = delete;
            /// @brief Prevents copy assignment of command-buffer ownership.
            CommandResources& operator=(const CommandResources&) = delete;

            /// @brief Runtime context owning the command pool.
            std::shared_ptr<Detail::VulkanContext> context;
            /// @brief One-time primary command buffer.
            VkCommandBuffer commandBuffer{ VK_NULL_HANDLE };
            /// @brief Fence used to wait for command completion.
            VkFence fence{ VK_NULL_HANDLE };
        };

        /// @brief Returns the deterministic priority rank for a physical-device type.
        int deviceRank(const VkPhysicalDeviceType type) noexcept
        {
            switch (type)
            {
            case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
                return 0;
            case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
                return 1;
            case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
                return 2;
            case VK_PHYSICAL_DEVICE_TYPE_CPU:
                return 3;
            default:
                return 4;
            }
        }
    } // namespace

    /// @cond INTERNAL
    namespace Detail
    {
        /// @brief Waits for the queue and releases instance-level Vulkan handles.
        VulkanContext::~VulkanContext()
        {
            if (device != VK_NULL_HANDLE)
            {
                vkDeviceWaitIdle(device);
                if (commandPool != VK_NULL_HANDLE)
                {
                    vkDestroyCommandPool(device, commandPool, nullptr);
                }
                vkDestroyDevice(device, nullptr);
            }

            if (debugMessenger != VK_NULL_HANDLE && instance != VK_NULL_HANDLE)
            {
                const auto destroyMessenger{ reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
                    vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT")) };
                if (destroyMessenger != nullptr)
                {
                    destroyMessenger(instance, debugMessenger, nullptr);
                }
            }

            if (instance != VK_NULL_HANDLE)
            {
                vkDestroyInstance(instance, nullptr);
            }
        }

        void throwIfFailed(const VkResult result, const char* const operation)
        {
            if (result != VK_SUCCESS)
            {
                throw VulkanError{ result, operation };
            }
        }

        std::uint32_t findMemoryType(const VulkanContext& context, const std::uint32_t allowedTypes,
                                     const VkMemoryPropertyFlags requiredProperties, const VkMemoryPropertyFlags preferredProperties)
        {
            std::optional<std::uint32_t> fallback;
            for (std::uint32_t index{ 0U }; index < context.memoryProperties.memoryTypeCount; ++index)
            {
                const VkMemoryPropertyFlags flags{ context.memoryProperties.memoryTypes[index].propertyFlags };
                if ((allowedTypes & (1U << index)) != 0U && (flags & requiredProperties) == requiredProperties)
                {
                    if ((flags & preferredProperties) == preferredProperties)
                    {
                        return index;
                    }
                    if (!fallback.has_value())
                    {
                        fallback = index;
                    }
                }
            }

            if (fallback.has_value())
            {
                return fallback.value();
            }
            throw std::runtime_error{ "No compatible Vulkan memory type is available" };
        }
    } // namespace Detail

    /// @brief Releases the buffer handle and its bound device memory.
    VulkanBuffer::Impl::~Impl()
    {
        if (buffer != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(context->device, buffer, nullptr);
        }
        if (memory != VK_NULL_HANDLE)
        {
            vkFreeMemory(context->device, memory, nullptr);
        }
    }

    /// @brief Releases pipeline, layout, and descriptor resources.
    VulkanComputePipeline::Impl::~Impl()
    {
        if (pipeline != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(context->device, pipeline, nullptr);
        }
        if (pipelineLayout != VK_NULL_HANDLE)
        {
            vkDestroyPipelineLayout(context->device, pipelineLayout, nullptr);
        }
        if (descriptorSetLayout != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorSetLayout(context->device, descriptorSetLayout, nullptr);
        }
    }
    /// @endcond

    VulkanRuntime::VulkanRuntime(VulkanRuntimeOptions options) : implementation{ std::make_unique<Impl>() }
    {
        const auto enumerateInstanceVersion{ reinterpret_cast<PFN_vkEnumerateInstanceVersion>(
            vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkEnumerateInstanceVersion")) };
        std::uint32_t loaderVersion{ VK_API_VERSION_1_0 };
        if (enumerateInstanceVersion == nullptr || enumerateInstanceVersion(&loaderVersion) != VK_SUCCESS ||
            loaderVersion < VK_API_VERSION_1_1)
        {
            throw VulkanError{ VK_ERROR_INCOMPATIBLE_DRIVER, "require a Vulkan 1.1 loader" };
        }

        auto context{ std::make_shared<Detail::VulkanContext>() };
        context->validationCallback = std::move(options.validationCallback);

        std::vector<const char*> layers;
        std::vector<const char*> extensions;
        VkDebugUtilsMessengerCreateInfoEXT debugInfo{ .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
                                                      .pNext = nullptr,
                                                      .flags = 0U,
                                                      .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
                                                      .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                                                     VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                                                     VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
                                                      .pfnUserCallback = validationCallback,
                                                      .pUserData = context.get() };

        if (options.enableValidation)
        {
            if (!hasLayer(validationLayerName))
            {
                throw VulkanError{ VK_ERROR_LAYER_NOT_PRESENT, "enable Vulkan validation layer" };
            }
            if (!hasInstanceExtension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME))
            {
                throw VulkanError{ VK_ERROR_EXTENSION_NOT_PRESENT, "enable VK_EXT_debug_utils" };
            }
            layers.emplace_back(validationLayerName);
            extensions.emplace_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }

        const VkApplicationInfo applicationInfo{ .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                                                 .pNext = nullptr,
                                                 .pApplicationName = "Atlas",
                                                 .applicationVersion = VK_MAKE_API_VERSION(0U, 0U, 3U, 0U),
                                                 .pEngineName = "Atlas",
                                                 .engineVersion = VK_MAKE_API_VERSION(0U, 0U, 3U, 0U),
                                                 .apiVersion = VK_API_VERSION_1_1 };
        const VkInstanceCreateInfo instanceInfo{ .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                                                 .pNext = options.enableValidation ? &debugInfo : nullptr,
                                                 .flags = 0U,
                                                 .pApplicationInfo = &applicationInfo,
                                                 .enabledLayerCount = static_cast<std::uint32_t>(layers.size()),
                                                 .ppEnabledLayerNames = layers.data(),
                                                 .enabledExtensionCount = static_cast<std::uint32_t>(extensions.size()),
                                                 .ppEnabledExtensionNames = extensions.data() };
        Detail::throwIfFailed(vkCreateInstance(&instanceInfo, nullptr, &context->instance), "vkCreateInstance");

        if (options.enableValidation)
        {
            const auto createMessenger{ reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(context->instance, "vkCreateDebugUtilsMessengerEXT")) };
            if (createMessenger == nullptr)
            {
                throw VulkanError{ VK_ERROR_EXTENSION_NOT_PRESENT, "load vkCreateDebugUtilsMessengerEXT" };
            }
            Detail::throwIfFailed(createMessenger(context->instance, &debugInfo, nullptr, &context->debugMessenger),
                                  "vkCreateDebugUtilsMessengerEXT");
        }

        std::uint32_t physicalDeviceCount{ 0U };
        Detail::throwIfFailed(vkEnumeratePhysicalDevices(context->instance, &physicalDeviceCount, nullptr),
                              "vkEnumeratePhysicalDevices");
        if (physicalDeviceCount == 0U)
        {
            throw VulkanError{ VK_ERROR_INITIALIZATION_FAILED, "find a Vulkan physical device" };
        }

        std::vector<VkPhysicalDevice> physicalDevices(physicalDeviceCount);
        Detail::throwIfFailed(vkEnumeratePhysicalDevices(context->instance, &physicalDeviceCount, physicalDevices.data()),
                              "vkEnumeratePhysicalDevices");
        std::vector<VkPhysicalDevice> compatibleDevices;
        std::vector<VulkanDeviceInfo> deviceDescriptions;
        for (std::uint32_t index{ 0U }; index < physicalDeviceCount; ++index)
        {
            VkPhysicalDeviceProperties properties{};
            vkGetPhysicalDeviceProperties(physicalDevices.at(index), &properties);
            std::vector<std::uint32_t> queues{ computeQueueFamilies(physicalDevices.at(index)) };
            if (properties.apiVersion >= VK_API_VERSION_1_1 && !queues.empty())
            {
                compatibleDevices.emplace_back(physicalDevices.at(index));
                deviceDescriptions.emplace_back(
                    VulkanDeviceInfo{ index, properties.deviceName, properties.deviceType, properties.apiVersion, std::move(queues) });
            }
        }

        if (compatibleDevices.empty())
        {
            throw VulkanError{ VK_ERROR_FEATURE_NOT_PRESENT, "find a Vulkan 1.1 compute-capable device" };
        }

        const std::optional<std::size_t> selection{ options.deviceSelector ? options.deviceSelector(deviceDescriptions)
                                                                           : selectDefaultDevice(deviceDescriptions) };
        if (!selection.has_value() || selection.value() >= compatibleDevices.size())
        {
            throw std::invalid_argument{ "Vulkan device selector returned no compatible device" };
        }

        context->physicalDevice = compatibleDevices.at(selection.value());
        context->deviceDescription = deviceDescriptions.at(selection.value());
        context->queueFamilyIndex = selectQueueFamily(context->physicalDevice, context->deviceDescription.computeQueueFamilies);
        vkGetPhysicalDeviceProperties(context->physicalDevice, &context->properties);
        vkGetPhysicalDeviceMemoryProperties(context->physicalDevice, &context->memoryProperties);

        constexpr float queuePriority{ 1.0F };
        const VkDeviceQueueCreateInfo queueInfo{ .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                                                 .pNext = nullptr,
                                                 .flags = 0U,
                                                 .queueFamilyIndex = context->queueFamilyIndex,
                                                 .queueCount = 1U,
                                                 .pQueuePriorities = &queuePriority };
        const VkDeviceCreateInfo deviceInfo{ .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                                             .pNext = nullptr,
                                             .flags = 0U,
                                             .queueCreateInfoCount = 1U,
                                             .pQueueCreateInfos = &queueInfo,
                                             .enabledLayerCount = 0U,
                                             .ppEnabledLayerNames = nullptr,
                                             .enabledExtensionCount = 0U,
                                             .ppEnabledExtensionNames = nullptr,
                                             .pEnabledFeatures = nullptr };
        Detail::throwIfFailed(vkCreateDevice(context->physicalDevice, &deviceInfo, nullptr, &context->device), "vkCreateDevice");
        vkGetDeviceQueue(context->device, context->queueFamilyIndex, 0U, &context->queue);

        const VkCommandPoolCreateInfo commandPoolInfo{ .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                                                       .pNext = nullptr,
                                                       .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
                                                       .queueFamilyIndex = context->queueFamilyIndex };
        Detail::throwIfFailed(vkCreateCommandPool(context->device, &commandPoolInfo, nullptr, &context->commandPool),
                              "vkCreateCommandPool");
        implementation->context = std::move(context);
    }

    VulkanRuntime::~VulkanRuntime() = default;
    VulkanRuntime::VulkanRuntime(VulkanRuntime&&) noexcept = default;
    VulkanRuntime& VulkanRuntime::operator=(VulkanRuntime&&) noexcept = default;

    const VulkanDeviceInfo& VulkanRuntime::deviceInfo() const noexcept
    {
        return implementation->context->deviceDescription;
    }

    VulkanBuffer VulkanRuntime::createBuffer(const std::size_t sizeInBytes) const
    {
        if (sizeInBytes == 0U || sizeInBytes > implementation->context->properties.limits.maxStorageBufferRange)
        {
            throw std::invalid_argument{ "Vulkan buffer size is zero or exceeds maxStorageBufferRange" };
        }

        auto resource{ std::make_shared<VulkanBuffer::Impl>() };
        resource->context = implementation->context;
        resource->byteSize = sizeInBytes;
        allocateBuffer(resource->context, static_cast<VkDeviceSize>(sizeInBytes),
                       VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                       VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0U, resource->buffer, resource->memory);
        return VulkanBuffer{ std::move(resource) };
    }

    VulkanComputePipeline VulkanRuntime::createComputePipeline(const ComputeShader& shader) const
    {
        if (!shader.isValid())
        {
            throw std::invalid_argument{ "ComputeShader is empty, malformed, or has invalid storage-buffer bindings" };
        }
        if (shader.storageBufferBindings.size() > implementation->context->properties.limits.maxPerStageDescriptorStorageBuffers ||
            shader.storageBufferBindings.size() > implementation->context->properties.limits.maxDescriptorSetStorageBuffers)
        {
            throw std::invalid_argument{ "ComputeShader storage-buffer count exceeds device limits" };
        }

        auto resource{ std::make_shared<VulkanComputePipeline::Impl>() };
        resource->context = implementation->context;
        resource->bindingNumbers = shader.storageBufferBindings;
        std::sort(resource->bindingNumbers.begin(), resource->bindingNumbers.end());

        std::vector<VkDescriptorSetLayoutBinding> layoutBindings;
        layoutBindings.reserve(resource->bindingNumbers.size());
        for (const std::uint32_t binding : resource->bindingNumbers)
        {
            layoutBindings.emplace_back(
                VkDescriptorSetLayoutBinding{ binding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1U, VK_SHADER_STAGE_COMPUTE_BIT, nullptr });
        }

        VkShaderModule shaderModule{ VK_NULL_HANDLE };
        try
        {
            const VkDescriptorSetLayoutCreateInfo layoutInfo{ .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                                                              .pNext = nullptr,
                                                              .flags = 0U,
                                                              .bindingCount = static_cast<std::uint32_t>(layoutBindings.size()),
                                                              .pBindings = layoutBindings.data() };
            Detail::throwIfFailed(
                vkCreateDescriptorSetLayout(resource->context->device, &layoutInfo, nullptr, &resource->descriptorSetLayout),
                "vkCreateDescriptorSetLayout");

            const VkPipelineLayoutCreateInfo pipelineLayoutInfo{ .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                                                                 .pNext = nullptr,
                                                                 .flags = 0U,
                                                                 .setLayoutCount = 1U,
                                                                 .pSetLayouts = &resource->descriptorSetLayout,
                                                                 .pushConstantRangeCount = 0U,
                                                                 .pPushConstantRanges = nullptr };
            Detail::throwIfFailed(
                vkCreatePipelineLayout(resource->context->device, &pipelineLayoutInfo, nullptr, &resource->pipelineLayout),
                "vkCreatePipelineLayout");

            const VkShaderModuleCreateInfo shaderInfo{ .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                                                       .pNext = nullptr,
                                                       .flags = 0U,
                                                       .codeSize = shader.spirv.size() * sizeof(std::uint32_t),
                                                       .pCode = shader.spirv.data() };
            Detail::throwIfFailed(vkCreateShaderModule(resource->context->device, &shaderInfo, nullptr, &shaderModule),
                                  "vkCreateShaderModule");

            const VkPipelineShaderStageCreateInfo stageInfo{ .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                                                             .pNext = nullptr,
                                                             .flags = 0U,
                                                             .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                                                             .module = shaderModule,
                                                             .pName = shader.entryPoint.c_str(),
                                                             .pSpecializationInfo = nullptr };
            const VkComputePipelineCreateInfo pipelineInfo{ .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
                                                            .pNext = nullptr,
                                                            .flags = VK_PIPELINE_CREATE_DISPATCH_BASE_BIT,
                                                            .stage = stageInfo,
                                                            .layout = resource->pipelineLayout,
                                                            .basePipelineHandle = VK_NULL_HANDLE,
                                                            .basePipelineIndex = -1 };
            Detail::throwIfFailed(
                vkCreateComputePipelines(resource->context->device, VK_NULL_HANDLE, 1U, &pipelineInfo, nullptr, &resource->pipeline),
                "vkCreateComputePipelines");
        }
        catch (...)
        {
            if (shaderModule != VK_NULL_HANDLE)
            {
                vkDestroyShaderModule(resource->context->device, shaderModule, nullptr);
            }
            throw;
        }
        vkDestroyShaderModule(resource->context->device, shaderModule, nullptr);
        return VulkanComputePipeline{ std::move(resource) };
    }

    void VulkanRuntime::upload(const VulkanBuffer& buffer, const std::span<const std::byte> data, const std::size_t offset) const
    {
        if (!buffer.isValid() || buffer.implementation->context != implementation->context || offset > buffer.size() ||
            data.size() > buffer.size() - offset)
        {
            throw std::invalid_argument{ "VulkanRuntime::upload received an invalid buffer range" };
        }
        if (data.empty())
        {
            return;
        }

        TemporaryBuffer staging{ createStagingBuffer(implementation->context, static_cast<VkDeviceSize>(data.size())) };
        void* mapped{ nullptr };
        Detail::throwIfFailed(vkMapMemory(implementation->context->device, staging.memory, 0U, VK_WHOLE_SIZE, 0U, &mapped),
                              "vkMapMemory");
        std::memcpy(mapped, data.data(), data.size());
        VkResult flushResult{ VK_SUCCESS };
        if (!staging.hostCoherent)
        {
            const VkMappedMemoryRange range{ .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
                                             .pNext = nullptr,
                                             .memory = staging.memory,
                                             .offset = 0U,
                                             .size = VK_WHOLE_SIZE };
            flushResult = vkFlushMappedMemoryRanges(implementation->context->device, 1U, &range);
        }
        vkUnmapMemory(implementation->context->device, staging.memory);
        Detail::throwIfFailed(flushResult, "vkFlushMappedMemoryRanges");

        std::lock_guard lock{ implementation->context->queueMutex };
        CommandResources commands{ implementation->context };
        const VkCommandBufferBeginInfo beginInfo{ .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                                  .pNext = nullptr,
                                                  .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
                                                  .pInheritanceInfo = nullptr };
        Detail::throwIfFailed(vkBeginCommandBuffer(commands.commandBuffer, &beginInfo), "vkBeginCommandBuffer");
        const VkBufferCopy region{ .srcOffset = 0U, .dstOffset = static_cast<VkDeviceSize>(offset), .size = data.size() };
        vkCmdCopyBuffer(commands.commandBuffer, staging.buffer, buffer.implementation->buffer, 1U, &region);
        const VkBufferMemoryBarrier barrier{ .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
                                             .pNext = nullptr,
                                             .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                                             .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                                             .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                             .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                             .buffer = buffer.implementation->buffer,
                                             .offset = static_cast<VkDeviceSize>(offset),
                                             .size = data.size() };
        vkCmdPipelineBarrier(commands.commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0U, 0U,
                             nullptr, 1U, &barrier, 0U, nullptr);
        Detail::throwIfFailed(vkEndCommandBuffer(commands.commandBuffer), "vkEndCommandBuffer");
        const VkSubmitInfo submitInfo{ .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                                       .pNext = nullptr,
                                       .waitSemaphoreCount = 0U,
                                       .pWaitSemaphores = nullptr,
                                       .pWaitDstStageMask = nullptr,
                                       .commandBufferCount = 1U,
                                       .pCommandBuffers = &commands.commandBuffer,
                                       .signalSemaphoreCount = 0U,
                                       .pSignalSemaphores = nullptr };
        Detail::throwIfFailed(vkQueueSubmit(implementation->context->queue, 1U, &submitInfo, commands.fence), "vkQueueSubmit");
        Detail::throwIfFailed(
            vkWaitForFences(implementation->context->device, 1U, &commands.fence, VK_TRUE, std::numeric_limits<std::uint64_t>::max()),
            "vkWaitForFences");
    }

    void VulkanRuntime::download(const VulkanBuffer& buffer, const std::span<std::byte> destination, const std::size_t offset) const
    {
        if (!buffer.isValid() || buffer.implementation->context != implementation->context || offset > buffer.size() ||
            destination.size() > buffer.size() - offset)
        {
            throw std::invalid_argument{ "VulkanRuntime::download received an invalid buffer range" };
        }
        if (destination.empty())
        {
            return;
        }

        TemporaryBuffer staging{ createStagingBuffer(implementation->context, static_cast<VkDeviceSize>(destination.size())) };
        {
            std::lock_guard lock{ implementation->context->queueMutex };
            CommandResources commands{ implementation->context };
            const VkCommandBufferBeginInfo beginInfo{ .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                                      .pNext = nullptr,
                                                      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
                                                      .pInheritanceInfo = nullptr };
            Detail::throwIfFailed(vkBeginCommandBuffer(commands.commandBuffer, &beginInfo), "vkBeginCommandBuffer");
            const VkBufferMemoryBarrier barrier{ .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
                                                 .pNext = nullptr,
                                                 .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                                                 .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
                                                 .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                                 .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                                 .buffer = buffer.implementation->buffer,
                                                 .offset = static_cast<VkDeviceSize>(offset),
                                                 .size = destination.size() };
            vkCmdPipelineBarrier(commands.commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0U, 0U,
                                 nullptr, 1U, &barrier, 0U, nullptr);
            const VkBufferCopy region{ .srcOffset = static_cast<VkDeviceSize>(offset), .dstOffset = 0U, .size = destination.size() };
            vkCmdCopyBuffer(commands.commandBuffer, buffer.implementation->buffer, staging.buffer, 1U, &region);
            Detail::throwIfFailed(vkEndCommandBuffer(commands.commandBuffer), "vkEndCommandBuffer");
            const VkSubmitInfo submitInfo{ .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                                           .pNext = nullptr,
                                           .waitSemaphoreCount = 0U,
                                           .pWaitSemaphores = nullptr,
                                           .pWaitDstStageMask = nullptr,
                                           .commandBufferCount = 1U,
                                           .pCommandBuffers = &commands.commandBuffer,
                                           .signalSemaphoreCount = 0U,
                                           .pSignalSemaphores = nullptr };
            Detail::throwIfFailed(vkQueueSubmit(implementation->context->queue, 1U, &submitInfo, commands.fence), "vkQueueSubmit");
            Detail::throwIfFailed(vkWaitForFences(implementation->context->device, 1U, &commands.fence, VK_TRUE,
                                                  std::numeric_limits<std::uint64_t>::max()),
                                  "vkWaitForFences");
        }

        void* mapped{ nullptr };
        Detail::throwIfFailed(vkMapMemory(implementation->context->device, staging.memory, 0U, VK_WHOLE_SIZE, 0U, &mapped),
                              "vkMapMemory");
        VkResult invalidateResult{ VK_SUCCESS };
        if (!staging.hostCoherent)
        {
            const VkMappedMemoryRange range{ .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
                                             .pNext = nullptr,
                                             .memory = staging.memory,
                                             .offset = 0U,
                                             .size = VK_WHOLE_SIZE };
            invalidateResult = vkInvalidateMappedMemoryRanges(implementation->context->device, 1U, &range);
        }
        if (invalidateResult == VK_SUCCESS)
        {
            std::memcpy(destination.data(), mapped, destination.size());
        }
        vkUnmapMemory(implementation->context->device, staging.memory);
        Detail::throwIfFailed(invalidateResult, "vkInvalidateMappedMemoryRanges");
    }

    std::optional<std::size_t> VulkanRuntime::selectDefaultDevice(const std::span<const VulkanDeviceInfo> devices) noexcept
    {
        if (devices.empty())
        {
            return std::nullopt;
        }

        std::size_t bestIndex{ 0U };
        int bestRank{ deviceRank(devices.front().type) };
        for (std::size_t index{ 1U }; index < devices.size(); ++index)
        {
            const int rank{ deviceRank(devices[index].type) };
            if (rank < bestRank || (rank == bestRank && devices[index].enumerationIndex < devices[bestIndex].enumerationIndex))
            {
                bestIndex = index;
                bestRank = rank;
            }
        }
        return bestIndex;
    }
} // namespace Atlas
