#include "atlas/Vulkan/VulkanError.h"

/** @file VulkanError.cpp @brief Implements Vulkan API error reporting. */

#include <string>

namespace Atlas
{
    namespace
    {
        std::string_view resultName(const VkResult result) noexcept
        {
            switch (result)
            {
            case VK_SUCCESS:
                return "VK_SUCCESS";
            case VK_ERROR_OUT_OF_HOST_MEMORY:
                return "VK_ERROR_OUT_OF_HOST_MEMORY";
            case VK_ERROR_OUT_OF_DEVICE_MEMORY:
                return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
            case VK_ERROR_INITIALIZATION_FAILED:
                return "VK_ERROR_INITIALIZATION_FAILED";
            case VK_ERROR_DEVICE_LOST:
                return "VK_ERROR_DEVICE_LOST";
            case VK_ERROR_LAYER_NOT_PRESENT:
                return "VK_ERROR_LAYER_NOT_PRESENT";
            case VK_ERROR_EXTENSION_NOT_PRESENT:
                return "VK_ERROR_EXTENSION_NOT_PRESENT";
            case VK_ERROR_FEATURE_NOT_PRESENT:
                return "VK_ERROR_FEATURE_NOT_PRESENT";
            case VK_ERROR_INCOMPATIBLE_DRIVER:
                return "VK_ERROR_INCOMPATIBLE_DRIVER";
            default:
                return "VK_RESULT_UNKNOWN";
            }
        }

        std::string buildMessage(const VkResult result, const std::string_view operation)
        {
            return std::string{ operation } + " failed with " + std::string{ resultName(result) } + " (VkResult " +
                   std::to_string(static_cast<int>(result)) + ')';
        }
    } // namespace

    VulkanError::VulkanError(const VkResult result, const std::string_view operation)
        : std::runtime_error{ buildMessage(result, operation) }, failureResult{ result }, failedOperation{ operation }
    {
    }
} // namespace Atlas
