#ifndef ATLAS_TEST_VULKAN_FACTORY
#define ATLAS_TEST_VULKAN_FACTORY

#include "../../src/Vulkan/VulkanInternal.h"

#include <memory>
#include <utility>
#include <vector>

namespace Atlas::Testing
{
    class VulkanTestFactory final
    {
      public:
        struct Resources
        {
            VulkanComputePipeline pipeline;
            std::vector<VulkanBuffer> buffers;
        };

        static Resources resources(std::vector<ShaderBufferBinding> bindings = { { 0U, BufferAccess::ReadWrite } })
        {
            std::shared_ptr<Detail::VulkanContext> context{ createContext() };
            std::vector<VulkanBuffer> buffers;
            buffers.reserve(bindings.size());
            for (std::size_t index{ 0U }; index < bindings.size(); ++index)
            {
                buffers.emplace_back(createBuffer(context));
            }

            auto pipelineImplementation{ std::make_shared<VulkanComputePipeline::Impl>() };
            pipelineImplementation->context = std::move(context);
            pipelineImplementation->storageBufferBindings = std::move(bindings);
            return Resources{ VulkanComputePipeline{ std::move(pipelineImplementation) }, std::move(buffers) };
        }

        static VulkanDispatch dispatch()
        {
            Resources values{ resources() };
            return VulkanDispatch{ values.pipeline, { { 0U, values.buffers.front(), BufferAccess::ReadWrite } }, { 1U, 1U, 1U } };
        }

      private:
        static std::shared_ptr<Detail::VulkanContext> createContext()
        {
            auto context{ std::make_shared<Detail::VulkanContext>() };
            context->properties.limits.maxComputeWorkGroupCount[0] = 65'535U;
            context->properties.limits.maxComputeWorkGroupCount[1] = 65'535U;
            context->properties.limits.maxComputeWorkGroupCount[2] = 65'535U;
            return context;
        }

        static VulkanBuffer createBuffer(const std::shared_ptr<Detail::VulkanContext>& context)
        {
            auto implementation{ std::make_shared<VulkanBuffer::Impl>() };
            implementation->context = context;
            implementation->byteSize = sizeof(std::uint32_t);
            return VulkanBuffer{ std::move(implementation) };
        }
    };
} // namespace Atlas::Testing

#endif // !ATLAS_TEST_VULKAN_FACTORY
