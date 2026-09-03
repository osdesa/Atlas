#include "BenchmarkWork.h"

#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <span>
#include <stdexcept>

namespace Atlas::Benchmark::Detail
{
    namespace
    {
        std::size_t checkedElementCount(const DispatchDimensions dimensions)
        {
            const std::size_t x{ dimensions.x };
            const std::size_t y{ dimensions.y };
            const std::size_t z{ dimensions.z };
            if (x > std::numeric_limits<std::size_t>::max() / y || x * y > std::numeric_limits<std::size_t>::max() / z)
            {
                throw std::invalid_argument{ "GPU benchmark workgroup product overflows size_t" };
            }
            const std::size_t count{ x * y * z };
            if (count > std::numeric_limits<std::size_t>::max() / sizeof(std::uint32_t))
            {
                throw std::invalid_argument{ "GPU benchmark buffer size overflows size_t" };
            }
            return count;
        }

        std::vector<std::uint32_t> readShader()
        {
            std::ifstream stream{ ATLAS_BENCHMARK_SPIRV_PATH, std::ios::binary };
            if (!stream)
            {
                throw std::runtime_error{ "Unable to open the compiled benchmark shader" };
            }
            const std::vector<char> bytes{ std::istreambuf_iterator<char>{ stream }, std::istreambuf_iterator<char>{} };
            if (stream.bad() || bytes.empty() || bytes.size() % sizeof(std::uint32_t) != 0U)
            {
                throw std::runtime_error{ "The compiled benchmark shader is malformed" };
            }
            std::vector<std::uint32_t> words(bytes.size() / sizeof(std::uint32_t));
            std::memcpy(words.data(), bytes.data(), bytes.size());
            return words;
        }
    } // namespace

    std::uint64_t runCpuKernel(std::uint64_t value, const std::uint64_t iterations) noexcept
    {
        for (std::uint64_t iteration{ 0U }; iteration < iterations; ++iteration)
        {
            value ^= value >> 12U;
            value ^= value << 25U;
            value ^= value >> 27U;
            value *= 2'685'821'657'736'338'717ULL;
        }
        return value;
    }

    GpuResources::GpuResources(VulkanRuntime& runtimeContext, const GpuWorkloadConfig& config)
        : elementCount{ checkedElementCount(config.workgroups) }, runtime{ runtimeContext },
          dimensionsBuffer{ runtime.createBuffer(4U * sizeof(std::uint32_t)) },
          outputBuffer{ runtime.createBuffer(elementCount * sizeof(std::uint32_t)) },
          pipeline{ runtime.createComputePipeline(
              ComputeShader{ readShader(), "main", { { 0U, BufferAccess::ReadOnly }, { 1U, BufferAccess::ReadWrite } } }) },
          computeDispatch{ pipeline,
                           { { 0U, dimensionsBuffer, BufferAccess::ReadOnly }, { 1U, outputBuffer, BufferAccess::ReadWrite } },
                           config.workgroups }
    {
        const std::vector<std::uint32_t> dimensions{ config.workgroups.x, config.workgroups.y, config.workgroups.z, 0U };
        runtime.upload(dimensionsBuffer, std::as_bytes(std::span{ dimensions }));
    }

    void GpuResources::reset(const std::uint64_t seed)
    {
        initialValues.resize(elementCount);
        for (std::size_t index{ 0U }; index < elementCount; ++index)
        {
            initialValues.at(index) = static_cast<std::uint32_t>(seed) ^ static_cast<std::uint32_t>(index + 1U);
        }
        runtime.upload(outputBuffer, std::as_bytes(std::span{ initialValues }));
    }

    void GpuResources::verify(const std::size_t gpuTaskCount) const
    {
        std::vector<std::uint32_t> actual(elementCount);
        runtime.download(outputBuffer, std::as_writable_bytes(std::span{ actual }));
        for (std::size_t index{ 0U }; index < elementCount; ++index)
        {
            std::uint32_t expected{ initialValues.at(index) };
            for (std::size_t task{ 0U }; task < gpuTaskCount; ++task)
            {
                expected = expected * 1'664'525U + 1'013'904'223U;
            }
            if (actual.at(index) != expected)
            {
                throw std::runtime_error{ "GPU benchmark output validation failed" };
            }
        }
    }

    const VulkanDispatch& GpuResources::dispatch() const noexcept
    {
        return computeDispatch;
    }
} // namespace Atlas::Benchmark::Detail
