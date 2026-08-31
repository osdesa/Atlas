#ifndef ATLAS_VULKAN_DISPATCH_EXECUTOR
#define ATLAS_VULKAN_DISPATCH_EXECUTOR

#include "CompletionChannel.h"
#include "atlas/Tasking/TaskHandle.h"
#include "atlas/Vulkan/VulkanCompute.h"

#include <cstdint>

/** @file VulkanDispatchExecutor.h @brief Declares GPU dispatch submission and completion. */

namespace Atlas
{
    /**
     * @ingroup executor
     * @brief Testable contract for capacity-bounded Vulkan dispatch submission.
     * @plantumlfile vulkan_dispatch_executor.puml
     */
    class VulkanDispatchExecutor
    {
      public:
        /// @brief Submits one dispatch whose outcome is published to @p completionChannel.
        virtual bool submit(TaskHandle taskHandle, VulkanDispatch dispatch, CompletionChannel& completionChannel) = 0;

        /// @brief Returns the maximum number of accepted dispatches that may execute concurrently.
        std::uint32_t maxConcurrency() const noexcept
        {
            return maximumConcurrency;
        }

        /// @brief Stops acceptance and drains work already accepted by the executor.
        virtual void shutdown() noexcept = 0;
        /// @brief Destroys an executor through the Vulkan dispatch interface.
        virtual ~VulkanDispatchExecutor() = default;

        /// @brief Prevents copying executor state.
        VulkanDispatchExecutor(const VulkanDispatchExecutor&) = delete;
        /// @brief Prevents copy assignment of executor state.
        VulkanDispatchExecutor& operator=(const VulkanDispatchExecutor&) = delete;
        /// @brief Prevents moving executor state.
        VulkanDispatchExecutor(VulkanDispatchExecutor&&) = delete;
        /// @brief Prevents move assignment of executor state.
        VulkanDispatchExecutor& operator=(VulkanDispatchExecutor&&) = delete;

      protected:
        /**
         * @brief Constructs an executor reporting the fixed capacity @p maxJobs.
         * @param maxJobs Maximum number of dispatches that may be in flight.
         */
        explicit VulkanDispatchExecutor(const std::uint32_t maxJobs) : maximumConcurrency{ maxJobs } {}

      private:
        /// @brief Maximum number of dispatches this executor can run concurrently.
        const std::uint32_t maximumConcurrency;
    };
} // namespace Atlas

#endif // !ATLAS_VULKAN_DISPATCH_EXECUTOR
