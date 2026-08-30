#include "atlas/Vulkan/VulkanCompute.h"

/** @file VulkanCompute.cpp @brief Implements Vulkan compute description validation and opaque queries. */

#include "VulkanInternal.h"

#include <algorithm>
#include <limits>
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

    const VulkanComputePipeline& VulkanDispatch::pipeline() const noexcept
    {
        return implementation->computePipeline;
    }

    std::span<const BufferBinding> VulkanDispatch::buffers() const noexcept
    {
        return implementation->bufferBindings;
    }

    VulkanDispatch::VulkanDispatch(VulkanComputePipeline pipeline, std::vector<BufferBinding> buffers,
                                   const DispatchDimensions dimensions)
        : workgroupDimensions{ dimensions }
    {
        const std::shared_ptr<VulkanComputePipeline::Impl> pipelineResource{ pipeline.implementation };
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

        if (buffers.size() != pipelineResource->bindingNumbers.size())
        {
            throw std::invalid_argument{ "VulkanDispatch must bind every pipeline storage buffer exactly once" };
        }

        std::vector<std::uint32_t> suppliedBindings;
        suppliedBindings.reserve(buffers.size());
        for (const BufferBinding& binding : buffers)
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

        implementation = std::make_shared<Impl>(Impl{ std::move(pipeline), std::move(buffers) });
    }

    VulkanDispatch::VulkanDispatch(std::shared_ptr<Impl> description, const DispatchOffset offset, const DispatchDimensions dimensions,
                                   const std::size_t workUnitIndex) noexcept
        : implementation{ std::move(description) }, workgroupOffset{ offset }, workgroupDimensions{ dimensions },
          unitIndex{ workUnitIndex }
    {
    }

    namespace
    {
        /// @brief Returns the ceiling quotient of one non-zero logical and slice extent.
        std::size_t axisSliceCount(const std::uint32_t logicalExtent, const std::uint32_t sliceExtent) noexcept
        {
            return 1U + static_cast<std::size_t>((logicalExtent - 1U) / sliceExtent);
        }

        /// @brief Multiplies slice counts while rejecting address-space overflow.
        std::size_t checkedProduct(const std::size_t first, const std::size_t second)
        {
            if (first > std::numeric_limits<std::size_t>::max() / second)
            {
                throw std::invalid_argument{ "SlicedVulkanDispatch slice count exceeds addressable size" };
            }
            return first * second;
        }
    } // namespace

    SlicedVulkanDispatch::SlicedVulkanDispatch(VulkanDispatch logicalDispatch, const DispatchDimensions sliceDimensions)
        : dispatch{ std::move(logicalDispatch) }, maximumSliceDimensions{ sliceDimensions }
    {
        if (!maximumSliceDimensions.isValid())
        {
            throw std::invalid_argument{ "SlicedVulkanDispatch dimensions must be non-zero" };
        }

        const DispatchDimensions logicalDimensions{ dispatch.dimensions() };
        const std::size_t xSlices{ axisSliceCount(logicalDimensions.x, maximumSliceDimensions.x) };
        const std::size_t ySlices{ axisSliceCount(logicalDimensions.y, maximumSliceDimensions.y) };
        const std::size_t zSlices{ axisSliceCount(logicalDimensions.z, maximumSliceDimensions.z) };
        workUnitCount = checkedProduct(checkedProduct(xSlices, ySlices), zSlices);
    }

    VulkanDispatch SlicedVulkanDispatch::slice(const std::size_t index) const
    {
        if (index >= workUnitCount)
        {
            throw std::out_of_range{ "SlicedVulkanDispatch work-unit index is out of range" };
        }

        const DispatchDimensions logicalDimensions{ dispatch.dimensions() };
        const std::size_t xSlices{ axisSliceCount(logicalDimensions.x, maximumSliceDimensions.x) };
        const std::size_t ySlices{ axisSliceCount(logicalDimensions.y, maximumSliceDimensions.y) };
        const std::size_t xIndex{ index % xSlices };
        const std::size_t yIndex{ (index / xSlices) % ySlices };
        const std::size_t zIndex{ index / (xSlices * ySlices) };

        const auto axisOffset = [](const std::size_t tile, const std::uint32_t extent)
        { return static_cast<std::uint32_t>(tile * static_cast<std::size_t>(extent)); };
        const DispatchOffset offset{ axisOffset(xIndex, maximumSliceDimensions.x), axisOffset(yIndex, maximumSliceDimensions.y),
                                     axisOffset(zIndex, maximumSliceDimensions.z) };
        const DispatchDimensions dimensions{ std::min(maximumSliceDimensions.x, logicalDimensions.x - offset.x),
                                             std::min(maximumSliceDimensions.y, logicalDimensions.y - offset.y),
                                             std::min(maximumSliceDimensions.z, logicalDimensions.z - offset.z) };
        return VulkanDispatch{ dispatch.implementation, offset, dimensions, index };
    }
} // namespace Atlas
