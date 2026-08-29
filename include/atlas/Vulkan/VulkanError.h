#ifndef ATLAS_VULKAN_ERROR
#define ATLAS_VULKAN_ERROR

#include <stdexcept>
#include <string>
#include <string_view>
#include <vulkan/vulkan_core.h>

/** @file VulkanError.h @brief Declares Vulkan API failure reporting. */

namespace Atlas
{
    /**
     * @ingroup vulkan
     * @brief Exception raised when a Vulkan API operation fails.
     */
    class VulkanError final : public std::runtime_error
    {
      public:
        /**
         * @brief Creates an exception for @p operation returning @p result.
         * @param result Vulkan result returned by the failed operation.
         * @param operation Human-readable operation description.
         */
        VulkanError(VkResult result, std::string_view operation);

        /// @brief Returns the Vulkan result reported by the failed operation.
        VkResult result() const noexcept
        {
            return failureResult;
        }

        /// @brief Returns the human-readable operation description.
        const std::string& operation() const noexcept
        {
            return failedOperation;
        }

      private:
        VkResult failureResult;
        std::string failedOperation;
    };
} // namespace Atlas

#endif // !ATLAS_VULKAN_ERROR
