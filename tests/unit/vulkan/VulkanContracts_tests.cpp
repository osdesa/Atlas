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
