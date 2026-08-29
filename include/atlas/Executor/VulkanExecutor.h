#ifndef ATLAS_VULKAN_EXECUTOR
#define ATLAS_VULKAN_EXECUTOR

#include "GpuExecutor.h"
#include "atlas/Vulkan/VulkanRuntime.h"

#include <memory>

/** @file VulkanExecutor.h @brief Declares asynchronous capacity-one Vulkan compute execution. */

namespace Atlas
{
    /**
     * @ingroup executor
     * @brief Asynchronously executes one Vulkan compute dispatch at a time.
     * @plantumlfile vulkan_executor.puml
     */
    class VulkanExecutor final : public GpuExecutor
    {
      public:
        /// @brief Retains @p runtime's shared context and starts one dispatch worker.
        explicit VulkanExecutor(VulkanRuntime& runtime);
        /// @brief Drains accepted dispatches and joins the worker.
        ~VulkanExecutor() override;

        /// @copydoc GpuExecutor::submit(TaskHandle,VulkanDispatch)
        bool submit(TaskHandle taskHandle, VulkanDispatch dispatch) override;
        /// @copydoc GpuExecutor::submit(TaskHandle,VulkanDispatch,CompletionChannel&)
        bool submit(TaskHandle taskHandle, VulkanDispatch dispatch, CompletionChannel& completionChannel) override;
        /// @copydoc GpuExecutor::waitForCompletion()
        std::optional<TaskCompletion> waitForCompletion() override;
        /// @copydoc GpuExecutor::shutdown()
        void shutdown() noexcept override;

      private:
        /// @brief Private implementation hiding Vulkan handles and worker state.
        struct Impl;
        /// @brief Owned asynchronous executor implementation.
        std::unique_ptr<Impl> implementation;
    };
} // namespace Atlas

#endif // !ATLAS_VULKAN_EXECUTOR
