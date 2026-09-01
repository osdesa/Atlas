#ifndef ATLAS_VULKAN_EXECUTOR
#define ATLAS_VULKAN_EXECUTOR

#include "VulkanDispatchExecutor.h"
#include "atlas/Vulkan/VulkanRuntime.h"

#include <memory>

/** @file VulkanExecutor.h @brief Declares asynchronous capacity-one Vulkan compute execution. */

namespace Atlas
{
    /**
     * @ingroup executor
     * @brief Asynchronously executes one Vulkan compute dispatch at a time.
     *
     * VK_ERROR_DEVICE_LOST permanently fails the retained runtime context. Work
     * accepted before loss receives one attributed failure completion; later
     * submissions throw VulkanError rather than attempting device recovery.
     * @plantumlfile vulkan_executor.puml
     */
    class VulkanExecutor final : public VulkanDispatchExecutor
    {
      public:
        /// @brief Retains @p runtime's shared context and starts one dispatch worker.
        explicit VulkanExecutor(VulkanRuntime& runtime);
        /// @brief Drains accepted dispatches and joins the worker.
        ~VulkanExecutor() override;

        /**
         * @copydoc VulkanDispatchExecutor::submit(TaskHandle,VulkanDispatch,CompletionChannel&)
         * @throws VulkanError With VK_ERROR_DEVICE_LOST after the retained context is lost.
         */
        bool submit(TaskHandle taskHandle, VulkanDispatch dispatch, CompletionChannel& completionChannel) override;
        /// @copydoc VulkanDispatchExecutor::shutdown()
        void shutdown() noexcept override;

      private:
        /// @brief Private implementation hiding Vulkan handles and worker state.
        struct Impl;
        /// @brief Owned asynchronous executor implementation.
        std::unique_ptr<Impl> implementation;
    };
} // namespace Atlas

#endif // !ATLAS_VULKAN_EXECUTOR
