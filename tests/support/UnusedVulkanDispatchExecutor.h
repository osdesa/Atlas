#ifndef ATLAS_TEST_UNUSED_VULKAN_DISPATCH_EXECUTOR
#define ATLAS_TEST_UNUSED_VULKAN_DISPATCH_EXECUTOR

#include "atlas/Executor/VulkanDispatchExecutor.h"

/**
 * @file UnusedVulkanDispatchExecutor.h
 * @brief Provides a Vulkan executor sentinel for CPU-only scheduler test graphs.
 */

namespace Atlas::Test
{
    /// @brief Rejects accidental Vulkan submission from a graph declared CPU-only by its tasks.
    class UnusedVulkanDispatchExecutor final : public VulkanDispatchExecutor
    {
      public:
        UnusedVulkanDispatchExecutor() : VulkanDispatchExecutor{ 1U } {}
        bool submit(TaskHandle, VulkanDispatch, CompletionChannel&) override
        {
            return false;
        }
        void shutdown() noexcept override {}
    };

    inline UnusedVulkanDispatchExecutor unusedVulkanDispatchExecutor;
} // namespace Atlas::Test

#endif // !ATLAS_TEST_UNUSED_VULKAN_DISPATCH_EXECUTOR
