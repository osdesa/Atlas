#ifndef ATLAS_GPU_EXECUTOR
#define ATLAS_GPU_EXECUTOR

#include "CompletionChannel.h"
#include "TaskCompletion.h"
#include "atlas/Tasking/TaskHandle.h"
#include "atlas/Vulkan/VulkanCompute.h"

#include <cstdint>
#include <optional>
#include <utility>

/** @file GpuExecutor.h @brief Declares GPU dispatch submission and completion. */

namespace Atlas
{
    /**
     * @ingroup executor
     * @brief Backend-neutral contract for capacity-bounded GPU work.
     * @plantumlfile gpu_executor.puml
     */
    class GpuExecutor
    {
      public:
        /// @brief Submits one dispatch for standalone completion retrieval.
        virtual bool submit(TaskHandle taskHandle, VulkanDispatch dispatch) = 0;
        /// @brief Submits one dispatch whose outcome is published to @p completionChannel.
        virtual bool submit(TaskHandle taskHandle, VulkanDispatch dispatch, CompletionChannel& completionChannel)
        {
            if (!submit(taskHandle, std::move(dispatch)))
            {
                return false;
            }

            std::optional<TaskCompletion> completion{ waitForCompletion() };
            if (!completion.has_value())
            {
                completionChannel.signalProducerFailure(ExecutionResource::GPU);
            }
            else
            {
                completion->resource = ExecutionResource::GPU;
                completionChannel.publish(std::move(completion.value()));
            }
            return true;
        }
        /// @brief Waits for the oldest outstanding standalone completion.
        virtual std::optional<TaskCompletion> waitForCompletion() = 0;

        /// @brief Returns the maximum number of accepted dispatches that may execute concurrently.
        std::uint32_t maxConcurrency() const noexcept
        {
            return maximumConcurrency;
        }

        /// @brief Stops acceptance and drains work already accepted by the executor.
        virtual void shutdown() noexcept = 0;
        /// @brief Destroys an executor through the common GPU interface.
        virtual ~GpuExecutor() = default;

        /// @brief Prevents copying executor state.
        GpuExecutor(const GpuExecutor&) = delete;
        /// @brief Prevents copy assignment of executor state.
        GpuExecutor& operator=(const GpuExecutor&) = delete;
        /// @brief Prevents moving executor state.
        GpuExecutor(GpuExecutor&&) = delete;
        /// @brief Prevents move assignment of executor state.
        GpuExecutor& operator=(GpuExecutor&&) = delete;

      protected:
        /**
         * @brief Constructs an executor reporting the fixed capacity @p maxJobs.
         * @param maxJobs Maximum number of dispatches that may be in flight.
         */
        explicit GpuExecutor(const std::uint32_t maxJobs) : maximumConcurrency{ maxJobs } {}

      private:
        /// @brief Maximum number of dispatches this executor can run concurrently.
        const std::uint32_t maximumConcurrency;
    };
} // namespace Atlas

#endif // !ATLAS_GPU_EXECUTOR
