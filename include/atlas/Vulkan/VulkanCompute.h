#ifndef ATLAS_VULKAN_COMPUTE
#define ATLAS_VULKAN_COMPUTE

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

/** @file VulkanCompute.h @brief Declares opaque Vulkan compute resources and dispatch descriptions. */

/**
 * @defgroup vulkan Vulkan
 * @brief Opaque compute resources, runtime ownership, and dispatch descriptions.
 */

namespace Atlas
{
    namespace Detail
    {
        class VulkanAccess;
    }
    namespace Testing
    {
        class VulkanTestFactory;
    }

    /**
     * @ingroup vulkan
     * @brief Declares how one dispatch accesses a bound storage buffer.
     */
    enum class BufferAccess : std::uint8_t
    {
        ReadOnly,  ///< The shader reads the buffer without writing it.
        WriteOnly, ///< The shader writes the buffer without consuming prior contents.
        ReadWrite  ///< The shader both reads and writes the buffer.
    };

    /**
     * @ingroup vulkan
     * @brief Reports whether @p access is a supported enumerator.
     */
    constexpr bool isValidBufferAccess(const BufferAccess access) noexcept
    {
        switch (access)
        {
        case BufferAccess::ReadOnly:
        case BufferAccess::WriteOnly:
        case BufferAccess::ReadWrite:
            return true;
        default:
            return false;
        }
    }

    /**
     * @ingroup vulkan
     * @brief Number of compute workgroups dispatched on each axis.
     */
    struct DispatchDimensions
    {
        /// @brief Workgroup count on the X axis.
        std::uint32_t x{ 1U };
        /// @brief Workgroup count on the Y axis.
        std::uint32_t y{ 1U };
        /// @brief Workgroup count on the Z axis.
        std::uint32_t z{ 1U };

        /// @brief Returns true when every workgroup count is non-zero.
        bool isValid() const noexcept
        {
            return x != 0U && y != 0U && z != 0U;
        }
    };

    /**
     * @ingroup vulkan
     * @brief Base workgroup coordinates for one region of a logical dispatch.
     */
    struct DispatchOffset
    {
        /// @brief Base workgroup coordinate on the X axis.
        std::uint32_t x{ 0U };
        /// @brief Base workgroup coordinate on the Y axis.
        std::uint32_t y{ 0U };
        /// @brief Base workgroup coordinate on the Z axis.
        std::uint32_t z{ 0U };
    };

    /**
     * @ingroup vulkan
     * @brief SPIR-V compute shader code and its expected storage-buffer interface.
     */
    struct ComputeShader
    {
        /// @brief SPIR-V module represented as native 32-bit words.
        std::vector<std::uint32_t> spirv;
        /// @brief Compute entry point created from the module.
        std::string entryPoint{ "main" };
        /// @brief Unique descriptor bindings expected in set zero.
        std::vector<std::uint32_t> storageBufferBindings;

        /// @brief Validates the module header, entry point, and binding uniqueness.
        bool isValid() const noexcept;
    };

    class VulkanRuntime;
    class VulkanExecutor;
    class VulkanDispatch;
    class SlicedVulkanDispatch;

    /**
     * @ingroup vulkan
     * @brief Opaque persistent device-local storage buffer.
     */
    class VulkanBuffer final
    {
      public:
        /// @brief Creates an empty, invalid buffer value.
        VulkanBuffer() = default;

        /// @brief Reports whether this value refers to an allocated buffer.
        bool isValid() const noexcept
        {
            return implementation != nullptr;
        }

        /// @brief Returns the allocation size in bytes, or zero for an empty value.
        std::size_t size() const noexcept;

      private:
        /// @brief Private implementation containing device-local Vulkan handles.
        struct Impl;
        /// @brief Constructs an opaque buffer around a shared implementation.
        explicit VulkanBuffer(std::shared_ptr<Impl> resource) : implementation{ std::move(resource) } {}

        /// @brief Shared opaque buffer resource and runtime ownership.
        std::shared_ptr<Impl> implementation;

        friend class VulkanRuntime;
        friend class VulkanExecutor;
        friend class VulkanDispatch;
        friend class Detail::VulkanAccess;
        friend class Testing::VulkanTestFactory;
    };

    /**
     * @ingroup vulkan
     * @brief Opaque reusable Vulkan compute pipeline.
     */
    class VulkanComputePipeline final
    {
      public:
        /// @brief Creates an empty, invalid pipeline value.
        VulkanComputePipeline() = default;

        /// @brief Reports whether this value refers to a created pipeline.
        bool isValid() const noexcept
        {
            return implementation != nullptr;
        }

        /// @brief Returns the sorted storage-buffer bindings required by this pipeline.
        std::span<const std::uint32_t> storageBufferBindings() const noexcept;

      private:
        /// @brief Private implementation containing pipeline and descriptor handles.
        struct Impl;
        /// @brief Constructs an opaque pipeline around a shared implementation.
        explicit VulkanComputePipeline(std::shared_ptr<Impl> resource) : implementation{ std::move(resource) } {}

        /// @brief Shared opaque pipeline resource and runtime ownership.
        std::shared_ptr<Impl> implementation;

        friend class VulkanRuntime;
        friend class VulkanExecutor;
        friend class VulkanDispatch;
        friend class Detail::VulkanAccess;
        friend class Testing::VulkanTestFactory;
    };

    /**
     * @ingroup vulkan
     * @brief Binds one persistent buffer to one storage descriptor.
     */
    struct BufferBinding
    {
        /// @brief Descriptor binding number in set zero.
        std::uint32_t binding{ 0U };
        /// @brief Persistent buffer visible to the dispatch.
        VulkanBuffer buffer;
        /// @brief Declared access used to select synchronization barriers.
        BufferAccess access{ BufferAccess::ReadOnly };
    };

    /**
     * @ingroup vulkan
     * @brief Validated, declarative compute work with no exposed Vulkan handles.
     * @plantumlfile vulkan_compute.puml
     */
    class VulkanDispatch final
    {
      public:
        /**
         * @brief Validates and creates a declarative compute dispatch.
         * @param pipeline Reusable compute pipeline to invoke.
         * @param buffers Exact storage-buffer bindings for the pipeline.
         * @param dimensions Non-zero workgroup counts for the dispatch.
         * @throws std::invalid_argument If resources, bindings, access, or dimensions are invalid.
         */
        VulkanDispatch(VulkanComputePipeline pipeline, std::vector<BufferBinding> buffers, DispatchDimensions dimensions);

        /// @brief Returns the reusable compute pipeline.
        const VulkanComputePipeline& pipeline() const noexcept;

        /// @brief Returns all validated storage-buffer bindings.
        std::span<const BufferBinding> buffers() const noexcept;

        /// @brief Returns the validated workgroup counts.
        DispatchDimensions dimensions() const noexcept
        {
            return workgroupDimensions;
        }

        /// @brief Returns the base workgroup coordinates for this work unit.
        DispatchOffset baseWorkgroup() const noexcept
        {
            return workgroupOffset;
        }

        /// @brief Returns this work unit's zero-based index in its logical dispatch.
        std::size_t workUnitIndex() const noexcept
        {
            return unitIndex;
        }

      private:
        /// @brief Immutable pipeline and bindings shared by every logical slice.
        struct Impl;

        /// @brief Creates a validated region sharing an existing dispatch description.
        VulkanDispatch(std::shared_ptr<Impl> description, DispatchOffset offset, DispatchDimensions dimensions,
                       std::size_t workUnitIndex) noexcept;

        /// @brief Shared immutable pipeline and buffer description.
        std::shared_ptr<Impl> implementation;
        /// @brief Base workgroup coordinates for this work unit.
        DispatchOffset workgroupOffset;
        /// @brief Non-zero workgroup dimensions for this dispatch.
        DispatchDimensions workgroupDimensions;
        /// @brief Zero-based work-unit index within the logical dispatch.
        std::size_t unitIndex{ 0U };

        friend class Detail::VulkanAccess;
        friend class SlicedVulkanDispatch;
    };

    /**
     * @ingroup vulkan
     * @brief A logical compute dispatch divided into deterministic cooperative work units.
     *
     * Slices cover the logical dimensions exactly once. Enumeration advances X
     * first, then Y, then Z. Every slice shares the logical dispatch's pipeline
     * and buffers and differs only in base workgroup coordinates and dimensions.
     */
    class SlicedVulkanDispatch final
    {
      public:
        /**
         * @brief Divides @p dispatch by maximum per-slice workgroup dimensions.
         * @param dispatch Validated logical dispatch to divide.
         * @param sliceDimensions Non-zero maximum slice extent on each axis.
         * @throws std::invalid_argument If a slice dimension is zero or the slice count overflows.
         */
        SlicedVulkanDispatch(VulkanDispatch dispatch, DispatchDimensions sliceDimensions);

        /// @brief Returns the complete validated logical dispatch.
        const VulkanDispatch& logicalDispatch() const noexcept
        {
            return dispatch;
        }

        /// @brief Returns the configured maximum workgroup extent per slice.
        DispatchDimensions sliceDimensions() const noexcept
        {
            return maximumSliceDimensions;
        }

        /// @brief Returns the number of work units covering the logical dispatch.
        std::size_t sliceCount() const noexcept
        {
            return workUnitCount;
        }

        /**
         * @brief Returns one validated work unit in X-major enumeration order.
         * @param index Zero-based work-unit index to materialize.
         * @return A dispatch sharing the logical pipeline and bindings with the
         * requested base coordinates and edge-clamped dimensions.
         * @throws std::out_of_range If @p index is not less than sliceCount().
         */
        VulkanDispatch slice(std::size_t index) const;

      private:
        /// @brief Complete logical dispatch whose resources are shared by slices.
        VulkanDispatch dispatch;
        /// @brief Maximum workgroup extent of one slice.
        DispatchDimensions maximumSliceDimensions;
        /// @brief Checked product of the per-axis tile counts.
        std::size_t workUnitCount{ 0U };
    };
} // namespace Atlas

#endif // !ATLAS_VULKAN_COMPUTE
