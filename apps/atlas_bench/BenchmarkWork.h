#ifndef ATLAS_BENCHMARK_WORK
#define ATLAS_BENCHMARK_WORK

#include "BenchmarkTypes.h"
#include "atlas/Vulkan/VulkanCompute.h"
#include "atlas/Vulkan/VulkanRuntime.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Atlas::Benchmark::Detail
{
    /// @brief Executes the deterministic CPU kernel shared by both benchmark arms.
    std::uint64_t runCpuKernel(std::uint64_t value, std::uint64_t iterations) noexcept;

    /** @brief Owns and verifies the identical persistent GPU workload used by both runners. */
    class GpuResources final
    {
      public:
        GpuResources(VulkanRuntime& runtime, const GpuWorkloadConfig& config);
        void reset(std::uint64_t seed);
        void verify(std::size_t gpuTaskCount) const;
        const VulkanDispatch& dispatch() const noexcept;

      private:
        std::size_t elementCount{ 0U };
        VulkanRuntime& runtime;
        VulkanBuffer dimensionsBuffer;
        VulkanBuffer outputBuffer;
        VulkanComputePipeline pipeline;
        VulkanDispatch computeDispatch;
        std::vector<std::uint32_t> initialValues;
    };
} // namespace Atlas::Benchmark::Detail

#endif // !ATLAS_BENCHMARK_WORK
