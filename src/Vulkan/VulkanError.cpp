#include "atlas/Vulkan/VulkanError.h"

/** @file VulkanError.cpp @brief Implements Vulkan API error reporting. */

#include <string>

namespace Atlas
{
    namespace
    {
        std::string buildMessage(const VkResult result, const std::string_view operation)
        {
            return std::string{ operation } + " failed with VkResult " + std::to_string(static_cast<int>(result));
        }
    } // namespace

    VulkanError::VulkanError(const VkResult result, const std::string_view operation)
        : std::runtime_error{ buildMessage(result, operation) }, failureResult{ result }, failedOperation{ operation }
    {
    }
} // namespace Atlas
