#include "../../support/VulkanTestFactory.h"
#include "atlas/Vulkan/VulkanCompute.h"
#include "atlas/Vulkan/VulkanError.h"
#include "atlas/Vulkan/VulkanRuntime.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <vector>

TEST_CASE("ComputeShader validates SPIR-V identity and unique storage bindings", "[UNIT]")
{
    Atlas::ComputeShader shader{ { 0x07230203U }, "main", { 0U, 1U } };
    REQUIRE(shader.isValid());

    shader.storageBufferBindings.emplace_back(1U);
    REQUIRE_FALSE(shader.isValid());
    shader.storageBufferBindings.pop_back();
    shader.spirv.front() = 0U;
    REQUIRE_FALSE(shader.isValid());
    shader.spirv.front() = 0x07230203U;
    shader.entryPoint.clear();
    REQUIRE_FALSE(shader.isValid());
    shader.entryPoint = "main";
    shader.storageBufferBindings.clear();
    REQUIRE_FALSE(shader.isValid());
}

TEST_CASE("VulkanDispatch rejects invalid descriptions without a Vulkan device", "[UNIT]")
{
    const Atlas::Testing::VulkanTestFactory::Resources first{ Atlas::Testing::VulkanTestFactory::resources({ 0U, 1U }) };
    const Atlas::Testing::VulkanTestFactory::Resources second{ Atlas::Testing::VulkanTestFactory::resources() };

    REQUIRE_THROWS_AS((Atlas::VulkanDispatch{ {}, {}, { 1U, 1U, 1U } }), std::invalid_argument);
    REQUIRE_THROWS_AS((Atlas::VulkanDispatch{ first.pipeline,
                                              { { 0U, first.buffers.at(0U), Atlas::BufferAccess::ReadOnly },
                                                { 0U, first.buffers.at(1U), Atlas::BufferAccess::WriteOnly } },
                                              { 1U, 1U, 1U } }),
                      std::invalid_argument);
    REQUIRE_THROWS_AS((Atlas::VulkanDispatch{ first.pipeline,
                                              { { 0U, first.buffers.at(0U), Atlas::BufferAccess::ReadOnly },
                                                { 1U, second.buffers.front(), Atlas::BufferAccess::WriteOnly } },
                                              { 1U, 1U, 1U } }),
                      std::invalid_argument);
    // This deliberately constructs a value that the public validation boundary must reject.
    // NOLINTBEGIN(clang-analyzer-optin.core.EnumCastOutOfRange)
    REQUIRE_THROWS_AS((Atlas::VulkanDispatch{ second.pipeline,
                                              { { 0U, second.buffers.front(), static_cast<Atlas::BufferAccess>(255U) } },
                                              { 1U, 1U, 1U } }),
                      std::invalid_argument);
    // NOLINTEND(clang-analyzer-optin.core.EnumCastOutOfRange)
    REQUIRE_THROWS_AS((Atlas::VulkanDispatch{
                          second.pipeline, { { 0U, second.buffers.front(), Atlas::BufferAccess::ReadWrite } }, { 65'536U, 1U, 1U } }),
                      std::invalid_argument);
}

TEST_CASE("DispatchDimensions require every dimension to be non-zero", "[UNIT]")
{
    REQUIRE(Atlas::DispatchDimensions{}.isValid());
    REQUIRE_FALSE(Atlas::DispatchDimensions{ 1U, 0U, 1U }.isValid());
}

TEST_CASE("SlicedVulkanDispatch tiles three dimensions in X-major order", "[UNIT]")
{
    Atlas::Testing::VulkanTestFactory::Resources resources{ Atlas::Testing::VulkanTestFactory::resources() };
    const Atlas::VulkanDispatch logicalDispatch{ resources.pipeline,
                                                 { { 0U, resources.buffers.front(), Atlas::BufferAccess::ReadWrite } },
                                                 { 5U, 3U, 2U } };
    const Atlas::SlicedVulkanDispatch slicedDispatch{ logicalDispatch, { 2U, 2U, 1U } };

    REQUIRE(slicedDispatch.sliceCount() == 12U);
    REQUIRE(slicedDispatch.logicalDispatch().dimensions().x == 5U);

    const Atlas::VulkanDispatch first{ slicedDispatch.slice(0U) };
    REQUIRE(first.baseWorkgroup().x == 0U);
    REQUIRE(first.baseWorkgroup().y == 0U);
    REQUIRE(first.baseWorkgroup().z == 0U);
    REQUIRE(first.dimensions().x == 2U);
    REQUIRE(first.dimensions().y == 2U);
    REQUIRE(first.dimensions().z == 1U);
    REQUIRE(first.workUnitIndex() == 0U);

    const Atlas::VulkanDispatch partialX{ slicedDispatch.slice(2U) };
    REQUIRE(partialX.baseWorkgroup().x == 4U);
    REQUIRE(partialX.baseWorkgroup().y == 0U);
    REQUIRE(partialX.dimensions().x == 1U);

    const Atlas::VulkanDispatch nextRow{ slicedDispatch.slice(3U) };
    REQUIRE(nextRow.baseWorkgroup().x == 0U);
    REQUIRE(nextRow.baseWorkgroup().y == 2U);
    REQUIRE(nextRow.dimensions().y == 1U);

    const Atlas::VulkanDispatch nextPlane{ slicedDispatch.slice(6U) };
    REQUIRE(nextPlane.baseWorkgroup().x == 0U);
    REQUIRE(nextPlane.baseWorkgroup().y == 0U);
    REQUIRE(nextPlane.baseWorkgroup().z == 1U);

    const Atlas::VulkanDispatch last{ slicedDispatch.slice(11U) };
    REQUIRE(last.baseWorkgroup().x == 4U);
    REQUIRE(last.baseWorkgroup().y == 2U);
    REQUIRE(last.baseWorkgroup().z == 1U);
    REQUIRE(last.dimensions().x == 1U);
    REQUIRE(last.dimensions().y == 1U);
    REQUIRE(last.workUnitIndex() == 11U);
    REQUIRE(&last.pipeline() == &first.pipeline());
    REQUIRE(last.buffers().data() == first.buffers().data());
}

TEST_CASE("SlicedVulkanDispatch validates geometry and indices", "[UNIT]")
{
    Atlas::Testing::VulkanTestFactory::Resources resources{ Atlas::Testing::VulkanTestFactory::resources() };
    const Atlas::VulkanDispatch logicalDispatch{ resources.pipeline,
                                                 { { 0U, resources.buffers.front(), Atlas::BufferAccess::ReadWrite } },
                                                 { 2U, 2U, 2U } };

    REQUIRE_THROWS_AS((Atlas::SlicedVulkanDispatch{ logicalDispatch, { 0U, 1U, 1U } }), std::invalid_argument);

    const Atlas::SlicedVulkanDispatch oneSlice{ logicalDispatch, { 8U, 8U, 8U } };
    REQUIRE(oneSlice.sliceCount() == 1U);
    REQUIRE(oneSlice.slice(0U).dimensions().x == 2U);
    REQUIRE_THROWS_AS(oneSlice.slice(1U), std::out_of_range);
}

TEST_CASE("VulkanRuntime default device selection is stable", "[UNIT]")
{
    const std::array devices{ Atlas::VulkanDeviceInfo{ 5U, "CPU", VK_PHYSICAL_DEVICE_TYPE_CPU, 0U, { 0U } },
                              Atlas::VulkanDeviceInfo{ 3U, "Integrated", VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU, 0U, { 0U } },
                              Atlas::VulkanDeviceInfo{ 2U, "Discrete later", VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU, 0U, { 0U } },
                              Atlas::VulkanDeviceInfo{ 1U, "Discrete first", VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU, 0U, { 0U } } };
    REQUIRE(Atlas::VulkanRuntime::selectDefaultDevice(devices) == 3U);
    REQUIRE_FALSE(Atlas::VulkanRuntime::selectDefaultDevice(std::span<const Atlas::VulkanDeviceInfo>{}).has_value());
}

TEST_CASE("VulkanError retains its result and operation", "[UNIT]")
{
    const Atlas::VulkanError error{ VK_ERROR_DEVICE_LOST, "execute test dispatch" };
    REQUIRE(error.result() == VK_ERROR_DEVICE_LOST);
    REQUIRE(error.operation() == "execute test dispatch");
    REQUIRE(std::string{ error.what() }.find("VkResult") != std::string::npos);
}
