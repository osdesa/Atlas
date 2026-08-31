#include "atlas/Executor/VulkanExecutor.h"

/** @file VulkanExecutor.cpp @brief Implements asynchronous capacity-one Vulkan dispatch execution. */

#include "../Vulkan/VulkanInternal.h"
#include "atlas/Executor/CompletionChannel.h"
#include "atlas/Vulkan/VulkanError.h"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <exception>
#include <limits>
#include <list>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace Atlas
{
    /// @cond INTERNAL
    struct VulkanExecutor::Impl final
    {
        enum class Lifecycle : std::uint8_t
        {
            Running,      ///< The worker accepts new dispatches.
            ShuttingDown, ///< Accepted dispatches are draining.
            Stopped       ///< The worker has exited permanently.
        };

        struct WorkItem
        {
            /// @brief Validated declarative dispatch to execute.
            VulkanDispatch dispatch;
            /// @brief Task-attributed outcome populated by the worker.
            TaskCompletion completion;
            /// @brief Scheduler channel for the outcome.
            CompletionChannel* completionChannel{ nullptr };
        };

        /// @brief Starts the single worker for @p runtimeContext.
        explicit Impl(std::shared_ptr<Detail::VulkanContext> runtimeContext) : context{ std::move(runtimeContext) }
        {
            worker = std::jthread{ [this] { workerLoop(); } };
        }

        /// @brief Requests shutdown and joins the worker after draining accepted work.
        ~Impl()
        {
            shutdown();
        }

        /// @brief Queues one dispatch unless shutdown has begun.
        bool submit(TaskHandle taskHandle, VulkanDispatch&& dispatch, CompletionChannel& completionChannel)
        {
            if (!taskHandle.isValid())
            {
                throw std::invalid_argument{ "Invalid task handle provided to VulkanExecutor::submit" };
            }

            {
                std::lock_guard lock{ stateMutex };
                if (lifecycle != Lifecycle::Running)
                {
                    return false;
                }
                pending.emplace_back(WorkItem{ dispatch,
                                               TaskCompletion{ taskHandle, nullptr, std::chrono::microseconds{ 0 },
                                                               ExecutionResource::GPU, dispatch.workUnitIndex() },
                                               &completionChannel });
                ++unfinished;
            }
            workAvailable.notify_one();
            return true;
        }

        /// @brief Stops acceptance, drains pending work, and joins the worker.
        void shutdown() noexcept
        {
            {
                std::lock_guard lock{ stateMutex };
                if (lifecycle == Lifecycle::Stopped)
                {
                    return;
                }
                lifecycle = Lifecycle::ShuttingDown;
            }
            workAvailable.notify_all();
            if (worker.joinable())
            {
                worker.join();
            }
            {
                std::lock_guard lock{ stateMutex };
                lifecycle = Lifecycle::Stopped;
            }
        }

        /// @brief Executes queued dispatches and publishes their outcomes.
        void workerLoop() noexcept
        {
            while (true)
            {
                std::list<WorkItem> executing;
                {
                    std::unique_lock lock{ stateMutex };
                    workAvailable.wait(lock, [this] { return !pending.empty() || lifecycle != Lifecycle::Running; });
                    if (pending.empty())
                    {
                        return;
                    }
                    executing.splice(executing.end(), pending, pending.begin());
                }

                WorkItem& item{ executing.front() };
                const auto start{ std::chrono::steady_clock::now() };
                try
                {
                    execute(item.dispatch);
                }
                catch (...)
                {
                    item.completion.exception = std::current_exception();
                }
                item.completion.executionDuration =
                    std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start);

                item.completionChannel->publish(std::move(item.completion));
                std::lock_guard lock{ stateMutex };
                --unfinished;
            }
        }

        /// @brief Records and submits one dispatch, waiting for its fence.
        void execute(const VulkanDispatch& dispatch)
        {
            const auto& pipeline{ Detail::VulkanAccess::pipeline(dispatch.pipeline()) };
            if (pipeline == nullptr || pipeline->context != context)
            {
                throw std::invalid_argument{ "VulkanExecutor received a dispatch from a different runtime" };
            }

            std::lock_guard queueLock{ context->queueMutex };
            VkDescriptorPool descriptorPool{ VK_NULL_HANDLE };
            VkCommandBuffer commandBuffer{ VK_NULL_HANDLE };
            VkFence fence{ VK_NULL_HANDLE };

            try
            {
                const VkDescriptorPoolSize poolSize{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                                     static_cast<std::uint32_t>(dispatch.buffers().size()) };
                const VkDescriptorPoolCreateInfo poolInfo{ .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                                                           .pNext = nullptr,
                                                           .flags = 0U,
                                                           .maxSets = 1U,
                                                           .poolSizeCount = 1U,
                                                           .pPoolSizes = &poolSize };
                Detail::throwIfFailed(vkCreateDescriptorPool(context->device, &poolInfo, nullptr, &descriptorPool),
                                      "vkCreateDescriptorPool");

                VkDescriptorSet descriptorSet{ VK_NULL_HANDLE };
                const VkDescriptorSetAllocateInfo descriptorInfo{ .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                                                                  .pNext = nullptr,
                                                                  .descriptorPool = descriptorPool,
                                                                  .descriptorSetCount = 1U,
                                                                  .pSetLayouts = &pipeline->descriptorSetLayout };
                Detail::throwIfFailed(vkAllocateDescriptorSets(context->device, &descriptorInfo, &descriptorSet),
                                      "vkAllocateDescriptorSets");

                std::vector<VkDescriptorBufferInfo> bufferInfos;
                std::vector<VkWriteDescriptorSet> descriptorWrites;
                bufferInfos.reserve(dispatch.buffers().size());
                descriptorWrites.reserve(dispatch.buffers().size());
                for (const BufferBinding& binding : dispatch.buffers())
                {
                    bufferInfos.emplace_back(
                        VkDescriptorBufferInfo{ Detail::VulkanAccess::buffer(binding.buffer)->buffer, 0U, VK_WHOLE_SIZE });
                }
                for (std::size_t index{ 0U }; index < dispatch.buffers().size(); ++index)
                {
                    descriptorWrites.emplace_back(VkWriteDescriptorSet{ .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                                                        .pNext = nullptr,
                                                                        .dstSet = descriptorSet,
                                                                        .dstBinding = dispatch.buffers()[index].binding,
                                                                        .dstArrayElement = 0U,
                                                                        .descriptorCount = 1U,
                                                                        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                                                        .pImageInfo = nullptr,
                                                                        .pBufferInfo = &bufferInfos.at(index),
                                                                        .pTexelBufferView = nullptr });
                }
                vkUpdateDescriptorSets(context->device, static_cast<std::uint32_t>(descriptorWrites.size()), descriptorWrites.data(),
                                       0U, nullptr);

                const VkCommandBufferAllocateInfo commandInfo{ .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                                                               .pNext = nullptr,
                                                               .commandPool = context->commandPool,
                                                               .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                                                               .commandBufferCount = 1U };
                Detail::throwIfFailed(vkAllocateCommandBuffers(context->device, &commandInfo, &commandBuffer),
                                      "vkAllocateCommandBuffers");
                const VkFenceCreateInfo fenceInfo{ .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .pNext = nullptr, .flags = 0U };
                Detail::throwIfFailed(vkCreateFence(context->device, &fenceInfo, nullptr, &fence), "vkCreateFence");

                const VkCommandBufferBeginInfo beginInfo{ .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                                          .pNext = nullptr,
                                                          .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
                                                          .pInheritanceInfo = nullptr };
                Detail::throwIfFailed(vkBeginCommandBuffer(commandBuffer, &beginInfo), "vkBeginCommandBuffer");

                std::vector<VkBufferMemoryBarrier> beforeBarriers;
                beforeBarriers.reserve(dispatch.buffers().size());
                for (const BufferBinding& binding : dispatch.buffers())
                {
                    VkAccessFlags destinationAccess{ VK_ACCESS_SHADER_READ_BIT };
                    if (binding.access == BufferAccess::WriteOnly)
                    {
                        destinationAccess = VK_ACCESS_SHADER_WRITE_BIT;
                    }
                    else if (binding.access == BufferAccess::ReadWrite)
                    {
                        destinationAccess = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
                    }
                    beforeBarriers.emplace_back(
                        VkBufferMemoryBarrier{ .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
                                               .pNext = nullptr,
                                               .srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
                                               .dstAccessMask = destinationAccess,
                                               .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                               .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                               .buffer = Detail::VulkanAccess::buffer(binding.buffer)->buffer,
                                               .offset = 0U,
                                               .size = VK_WHOLE_SIZE });
                }
                vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0U, 0U,
                                     nullptr, static_cast<std::uint32_t>(beforeBarriers.size()), beforeBarriers.data(), 0U, nullptr);
                vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->pipeline);
                vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->pipelineLayout, 0U, 1U,
                                        &descriptorSet, 0U, nullptr);
                const DispatchDimensions dimensions{ dispatch.dimensions() };
                const DispatchOffset offset{ dispatch.baseWorkgroup() };
                vkCmdDispatchBase(commandBuffer, offset.x, offset.y, offset.z, dimensions.x, dimensions.y, dimensions.z);

                std::vector<VkBufferMemoryBarrier> afterBarriers;
                afterBarriers.reserve(dispatch.buffers().size());
                for (const BufferBinding& binding : dispatch.buffers())
                {
                    VkAccessFlags sourceAccess{ VK_ACCESS_SHADER_READ_BIT };
                    if (binding.access == BufferAccess::WriteOnly)
                    {
                        sourceAccess = VK_ACCESS_SHADER_WRITE_BIT;
                    }
                    else if (binding.access == BufferAccess::ReadWrite)
                    {
                        sourceAccess = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
                    }
                    afterBarriers.emplace_back(
                        VkBufferMemoryBarrier{ .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
                                               .pNext = nullptr,
                                               .srcAccessMask = sourceAccess,
                                               .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
                                               .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                               .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                               .buffer = Detail::VulkanAccess::buffer(binding.buffer)->buffer,
                                               .offset = 0U,
                                               .size = VK_WHOLE_SIZE });
                }
                vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0U, 0U,
                                     nullptr, static_cast<std::uint32_t>(afterBarriers.size()), afterBarriers.data(), 0U, nullptr);
                Detail::throwIfFailed(vkEndCommandBuffer(commandBuffer), "vkEndCommandBuffer");

                const VkSubmitInfo submitInfo{ .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                                               .pNext = nullptr,
                                               .waitSemaphoreCount = 0U,
                                               .pWaitSemaphores = nullptr,
                                               .pWaitDstStageMask = nullptr,
                                               .commandBufferCount = 1U,
                                               .pCommandBuffers = &commandBuffer,
                                               .signalSemaphoreCount = 0U,
                                               .pSignalSemaphores = nullptr };
                Detail::throwIfFailed(vkQueueSubmit(context->queue, 1U, &submitInfo, fence), "vkQueueSubmit");
                Detail::throwIfFailed(vkWaitForFences(context->device, 1U, &fence, VK_TRUE, std::numeric_limits<std::uint64_t>::max()),
                                      "vkWaitForFences");
            }
            catch (...)
            {
                if (fence != VK_NULL_HANDLE)
                {
                    vkDestroyFence(context->device, fence, nullptr);
                }
                if (commandBuffer != VK_NULL_HANDLE)
                {
                    vkFreeCommandBuffers(context->device, context->commandPool, 1U, &commandBuffer);
                }
                if (descriptorPool != VK_NULL_HANDLE)
                {
                    vkDestroyDescriptorPool(context->device, descriptorPool, nullptr);
                }
                throw;
            }

            vkDestroyFence(context->device, fence, nullptr);
            vkFreeCommandBuffers(context->device, context->commandPool, 1U, &commandBuffer);
            vkDestroyDescriptorPool(context->device, descriptorPool, nullptr);
        }

        /// @brief Runtime context borrowed by all submitted resources.
        std::shared_ptr<Detail::VulkanContext> context;
        /// @brief Guards lifecycle, queues, and unfinished-work accounting.
        std::mutex stateMutex;
        /// @brief Wakes the worker when dispatches arrive or shutdown begins.
        std::condition_variable workAvailable;
        /// @brief FIFO dispatches not yet claimed by the worker.
        std::list<WorkItem> pending;
        /// @brief Accepted dispatches not yet fully published or retained.
        std::size_t unfinished{ 0U };
        /// @brief Current executor lifecycle state.
        Lifecycle lifecycle{ Lifecycle::Running };
        /// @brief Single worker responsible for Vulkan queue execution.
        std::jthread worker;
    };
    /// @endcond

    VulkanExecutor::VulkanExecutor(VulkanRuntime& runtime) : VulkanDispatchExecutor{ 1U }
    {
        if (runtime.implementation == nullptr || runtime.implementation->context == nullptr)
        {
            throw std::invalid_argument{ "VulkanExecutor requires a valid VulkanRuntime" };
        }
        implementation = std::make_unique<Impl>(runtime.implementation->context);
    }

    VulkanExecutor::~VulkanExecutor() = default;

    bool VulkanExecutor::submit(TaskHandle taskHandle, VulkanDispatch dispatch, CompletionChannel& completionChannel)
    {
        return implementation->submit(taskHandle, std::move(dispatch), completionChannel);
    }

    void VulkanExecutor::shutdown() noexcept
    {
        implementation->shutdown();
    }
} // namespace Atlas
