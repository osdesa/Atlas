#include "SpirvReflection.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <spirv-tools/libspirv.hpp>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Atlas::Detail
{
    namespace
    {
        constexpr std::uint16_t opEntryPoint{ 15U };
        constexpr std::uint16_t opTypeStruct{ 30U };
        constexpr std::uint16_t opTypePointer{ 32U };
        constexpr std::uint16_t opVariable{ 59U };
        constexpr std::uint16_t opDecorate{ 71U };
        constexpr std::uint16_t opMemberDecorate{ 72U };
        constexpr std::uint16_t opDecorationGroup{ 73U };
        constexpr std::uint16_t opGroupDecorate{ 74U };
        constexpr std::uint16_t opGroupMemberDecorate{ 75U };
        constexpr std::uint16_t firstSpecConstantOpcode{ 48U };
        constexpr std::uint16_t lastSpecConstantOpcode{ 52U };

        constexpr std::uint32_t executionModelCompute{ 5U };
        constexpr std::uint32_t storageClassUniformConstant{ 0U };
        constexpr std::uint32_t storageClassUniform{ 2U };
        constexpr std::uint32_t storageClassPushConstant{ 9U };
        constexpr std::uint32_t storageClassStorageBuffer{ 12U };
        constexpr std::uint32_t decorationBlock{ 2U };
        constexpr std::uint32_t decorationBufferBlock{ 3U };
        constexpr std::uint32_t decorationNonWritable{ 24U };
        constexpr std::uint32_t decorationNonReadable{ 25U };
        constexpr std::uint32_t decorationBinding{ 33U };
        constexpr std::uint32_t decorationDescriptorSet{ 34U };

        struct Decorations final
        {
            std::optional<std::uint32_t> binding;
            std::optional<std::uint32_t> descriptorSet;
            bool block{ false };
            bool bufferBlock{ false };
            bool nonWritable{ false };
            bool nonReadable{ false };
        };

        struct PointerType final
        {
            std::uint32_t storageClass{ 0U };
            std::uint32_t pointee{ 0U };
        };

        struct Variable final
        {
            std::uint32_t type{ 0U };
            std::uint32_t storageClass{ 0U };
        };

        std::string instructionString(const std::uint32_t* words, const std::size_t wordCount)
        {
            const char* const bytes{ reinterpret_cast<const char*>(words) };
            const std::size_t byteCount{ wordCount * sizeof(std::uint32_t) };
            const void* const terminator{ std::memchr(bytes, '\0', byteCount) };
            if (terminator == nullptr)
            {
                throw std::invalid_argument{ "SPIR-V entry point has an unterminated name" };
            }
            return std::string{ bytes, static_cast<const char*>(terminator) };
        }

        BufferAccess reflectedAccess(const Decorations& decorations)
        {
            if (decorations.nonWritable && decorations.nonReadable)
            {
                throw std::invalid_argument{ "SPIR-V storage buffer is both NonWritable and NonReadable" };
            }
            if (decorations.nonWritable)
            {
                return BufferAccess::ReadOnly;
            }
            if (decorations.nonReadable)
            {
                return BufferAccess::WriteOnly;
            }
            return BufferAccess::ReadWrite;
        }

        void applyDecoration(Decorations& target, const std::uint32_t decoration, const std::uint32_t* operands,
                             const std::size_t operandCount)
        {
            switch (decoration)
            {
            case decorationBinding:
                if (operandCount != 1U || target.binding.has_value())
                {
                    throw std::invalid_argument{ "SPIR-V contains an invalid or duplicate Binding decoration" };
                }
                target.binding = operands[0U];
                break;
            case decorationDescriptorSet:
                if (operandCount != 1U || target.descriptorSet.has_value())
                {
                    throw std::invalid_argument{ "SPIR-V contains an invalid or duplicate DescriptorSet decoration" };
                }
                target.descriptorSet = operands[0U];
                break;
            case decorationBlock:
                target.block = true;
                break;
            case decorationBufferBlock:
                target.bufferBlock = true;
                break;
            case decorationNonWritable:
                target.nonWritable = true;
                break;
            case decorationNonReadable:
                target.nonReadable = true;
                break;
            default:
                break;
            }
        }
    } // namespace

    std::vector<ShaderBufferBinding> validateAndReflectComputeShader(const ComputeShader& shader)
    {
        if (!shader.isValid())
        {
            throw std::invalid_argument{ "ComputeShader is empty, malformed, or has an invalid declared interface" };
        }

        std::string validationMessage;
        spvtools::SpirvTools tools{ SPV_ENV_VULKAN_1_1 };
        tools.SetMessageConsumer(
            [&validationMessage](spv_message_level_t, const char*, const spv_position_t& position, const char* message)
            {
                validationMessage = "SPIR-V validation failed at word " + std::to_string(position.index) + ": " +
                                    (message == nullptr ? std::string{} : std::string{ message });
            });
        if (!tools.Validate(shader.spirv.data(), shader.spirv.size()))
        {
            throw std::invalid_argument{ validationMessage.empty() ? "SPIR-V validation failed for Vulkan 1.1" : validationMessage };
        }

        std::unordered_map<std::uint32_t, Decorations> decorations;
        std::unordered_map<std::uint32_t, std::unordered_map<std::uint32_t, Decorations>> memberDecorations;
        std::unordered_map<std::uint32_t, PointerType> pointerTypes;
        std::unordered_map<std::uint32_t, std::size_t> structMemberCounts;
        std::unordered_map<std::uint32_t, Variable> variables;
        std::size_t selectedEntryPoints{ 0U };
        bool unsupportedEntryPoint{ false };

        for (std::size_t offset{ 5U }; offset < shader.spirv.size();)
        {
            const std::uint32_t instruction{ shader.spirv.at(offset) };
            const std::size_t wordCount{ instruction >> 16U };
            const std::uint16_t opcode{ static_cast<std::uint16_t>(instruction & 0xFFFFU) };
            const std::uint32_t* const words{ shader.spirv.data() + offset };

            if (opcode >= firstSpecConstantOpcode && opcode <= lastSpecConstantOpcode)
            {
                throw std::invalid_argument{ "SPIR-V specialization constants are not supported" };
            }
            if (opcode == opDecorationGroup || opcode == opGroupDecorate || opcode == opGroupMemberDecorate)
            {
                throw std::invalid_argument{ "SPIR-V decoration groups are not supported by the strict interface reflector" };
            }

            if (opcode == opEntryPoint)
            {
                const std::string name{ instructionString(words + 3U, wordCount - 3U) };
                if (name == shader.entryPoint)
                {
                    if (words[1U] != executionModelCompute)
                    {
                        unsupportedEntryPoint = true;
                    }
                    ++selectedEntryPoints;
                }
            }
            else if (opcode == opDecorate)
            {
                applyDecoration(decorations[words[1U]], words[2U], words + 3U, wordCount - 3U);
            }
            else if (opcode == opMemberDecorate)
            {
                applyDecoration(memberDecorations[words[1U]][words[2U]], words[3U], words + 4U, wordCount - 4U);
            }
            else if (opcode == opTypeStruct)
            {
                structMemberCounts[words[1U]] = wordCount - 2U;
            }
            else if (opcode == opTypePointer)
            {
                pointerTypes[words[1U]] = PointerType{ words[2U], words[3U] };
            }
            else if (opcode == opVariable)
            {
                variables[words[2U]] = Variable{ words[1U], words[3U] };
            }
            offset += wordCount;
        }

        if (selectedEntryPoints != 1U || unsupportedEntryPoint)
        {
            throw std::invalid_argument{ "SPIR-V must contain exactly one selected GLCompute entry point" };
        }

        std::vector<ShaderBufferBinding> reflected;
        for (const auto& [variableId, variable] : variables)
        {
            const Decorations& variableDecorations{ decorations[variableId] };
            const bool descriptor{ variableDecorations.binding.has_value() || variableDecorations.descriptorSet.has_value() };
            if (variable.storageClass == storageClassPushConstant)
            {
                throw std::invalid_argument{ "SPIR-V push constants are not supported" };
            }
            if (variable.storageClass == storageClassUniformConstant)
            {
                throw std::invalid_argument{ "SPIR-V images and samplers are not supported" };
            }
            if (variable.storageClass != storageClassStorageBuffer && variable.storageClass != storageClassUniform)
            {
                if (descriptor)
                {
                    throw std::invalid_argument{ "SPIR-V descriptor interface contains an unsupported storage class" };
                }
                continue;
            }
            if (!variableDecorations.binding.has_value() || !variableDecorations.descriptorSet.has_value())
            {
                throw std::invalid_argument{ "SPIR-V storage-buffer variables require Binding and DescriptorSet decorations" };
            }
            if (variableDecorations.descriptorSet.value() != 0U)
            {
                throw std::invalid_argument{ "SPIR-V compute interfaces may use descriptor set zero only" };
            }

            const auto pointer{ pointerTypes.find(variable.type) };
            if (pointer == pointerTypes.end() || pointer->second.storageClass != variable.storageClass)
            {
                throw std::invalid_argument{ "SPIR-V storage-buffer variable has an invalid pointer type" };
            }
            const auto structEntry{ structMemberCounts.find(pointer->second.pointee) };
            if (structEntry == structMemberCounts.end())
            {
                throw std::invalid_argument{ "SPIR-V descriptors must be one non-arrayed storage-buffer struct" };
            }
            const Decorations& typeDecorations{ decorations[pointer->second.pointee] };
            if ((variable.storageClass == storageClassStorageBuffer && !typeDecorations.block) ||
                (variable.storageClass == storageClassUniform && !typeDecorations.bufferBlock))
            {
                throw std::invalid_argument{ "SPIR-V descriptor is not a storage buffer" };
            }

            Decorations accessDecorations{ variableDecorations };
            bool allMembersNonWritable{ structEntry->second != 0U };
            bool allMembersNonReadable{ structEntry->second != 0U };
            for (std::size_t member{ 0U }; member < structEntry->second; ++member)
            {
                const Decorations& memberValues{ memberDecorations[pointer->second.pointee][static_cast<std::uint32_t>(member)] };
                allMembersNonWritable = allMembersNonWritable && memberValues.nonWritable;
                allMembersNonReadable = allMembersNonReadable && memberValues.nonReadable;
            }
            accessDecorations.nonWritable = accessDecorations.nonWritable || allMembersNonWritable;
            accessDecorations.nonReadable = accessDecorations.nonReadable || allMembersNonReadable;
            reflected.emplace_back(ShaderBufferBinding{ variableDecorations.binding.value(), reflectedAccess(accessDecorations) });
        }

        const auto byBinding = [](const ShaderBufferBinding& left, const ShaderBufferBinding& right)
        { return left.binding < right.binding; };
        std::sort(reflected.begin(), reflected.end(), byBinding);
        if (reflected.empty() || std::adjacent_find(reflected.begin(), reflected.end(),
                                                    [](const ShaderBufferBinding& left, const ShaderBufferBinding& right)
                                                    { return left.binding == right.binding; }) != reflected.end())
        {
            throw std::invalid_argument{ "SPIR-V storage-buffer bindings are empty or duplicated" };
        }

        std::vector<ShaderBufferBinding> declared{ shader.storageBufferBindings };
        std::sort(declared.begin(), declared.end(), byBinding);
        if (declared != reflected)
        {
            throw std::invalid_argument{ "ComputeShader declared bindings or access do not match the reflected SPIR-V interface" };
        }
        return reflected;
    }
} // namespace Atlas::Detail
