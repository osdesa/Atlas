#ifndef ATLAS_SPIRV_REFLECTION
#define ATLAS_SPIRV_REFLECTION

#include "atlas/Vulkan/VulkanCompute.h"

#include <vector>

namespace Atlas::Detail
{
    /** @brief Validates one module for Vulkan 1.1 and reflects its strict storage-buffer interface. */
    std::vector<ShaderBufferBinding> validateAndReflectComputeShader(const ComputeShader& shader);
} // namespace Atlas::Detail

#endif // !ATLAS_SPIRV_REFLECTION
