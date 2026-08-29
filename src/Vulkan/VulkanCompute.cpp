#include "atlas/Vulkan/VulkanCompute.h"

/** @file VulkanCompute.cpp @brief Implements Vulkan compute description validation and opaque queries. */

#include "VulkanInternal.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace Atlas
{
    bool ComputeShader::isValid() const noexcept
    {
        constexpr std::uint32_t spirvMagic{ 0x07230203U };
        if (spirv.empty() || spirv.front() != spirvMagic || entryPoint.empty() || storageBufferBindings.empty())
        {
            return false;
        }

        for (std::size_t index{ 0U }; index < storageBufferBindings.size(); ++index)
        {
            if (std::find(storageBufferBindings.begin() + static_cast<std::ptrdiff_t>(index + 1U), storageBufferBindings.end(),
                          storageBufferBindings.at(index)) != storageBufferBindings.end())
            {
                return false;
            }
        }
        return true;
    }

    std::size_t VulkanBuffer::size() const noexcept
    {
        return implementation == nullptr ? 0U : implementation->byteSize;
    }

    std::span<const std::uint32_t> VulkanComputePipeline::storageBufferBindings() const noexcept
    {
        if (implementation == nullptr)
        {
            return {};
        }
        return implementation->bindingNumbers;
    }

    VulkanDispatch::VulkanDispatch(VulkanComputePipeline pipeline, std::vector<BufferBinding> buffers,
                                   const DispatchDimensions dimensions)
        : computePipeline{ std::move(pipeline) }, bufferBindings{ std::move(buffers) }, workgroupDimensions{ dimensions }
    {
        const std::shared_ptr<VulkanComputePipeline::Impl> pipelineResource{ computePipeline.implementation };
        if (pipelineResource == nullptr)
        {
            throw std::invalid_argument{ "VulkanDispatch requires a valid compute pipeline" };
        }
        if (!workgroupDimensions.isValid())
        {
            throw std::invalid_argument{ "VulkanDispatch dimensions must be non-zero" };
        }

        const auto& context{ pipelineResource->context };
        const VkPhysicalDeviceLimits& limits{ context->properties.limits };
        if (dimensions.x > limits.maxComputeWorkGroupCount[0] || dimensions.y > limits.maxComputeWorkGroupCount[1] ||
            dimensions.z > limits.maxComputeWorkGroupCount[2])
        {
            throw std::invalid_argument{ "VulkanDispatch dimensions exceed device limits" };
        }

        if (bufferBindings.size() != pipelineResource->bindingNumbers.size())
        {
            throw std::invalid_argument{ "VulkanDispatch must bind every pipeline storage buffer exactly once" };
        }

        std::vector<std::uint32_t> suppliedBindings;
        suppliedBindings.reserve(bufferBindings.size());
        for (const BufferBinding& binding : bufferBindings)
        {
            const std::shared_ptr<VulkanBuffer::Impl> bufferResource{ binding.buffer.implementation };
            if (bufferResource == nullptr || !isValidBufferAccess(binding.access))
            {
                throw std::invalid_argument{ "VulkanDispatch contains an invalid buffer binding" };
            }
            if (bufferResource->context != context)
            {
                throw std::invalid_argument{ "VulkanDispatch resources belong to different Vulkan runtimes" };
            }
            suppliedBindings.emplace_back(binding.binding);
        }

        std::sort(suppliedBindings.begin(), suppliedBindings.end());
        if (std::adjacent_find(suppliedBindings.begin(), suppliedBindings.end()) != suppliedBindings.end() ||
            suppliedBindings != pipelineResource->bindingNumbers)
        {
            throw std::invalid_argument{ "VulkanDispatch bindings do not match the compute pipeline" };
        }
    }
} // namespace Atlas
