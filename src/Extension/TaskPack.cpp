#include "../Vulkan/VulkanInternal.h"
#include "TaskPackInternal.h"
#include "atlas/Tasking/TaskState.h"
#include "atlas/Vulkan/VulkanRuntime.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace Atlas
{
    namespace
    {
        using PackKey = std::pair<std::string, std::string>;

        template <typename Callback, typename... Arguments>
        decltype(auto) invokePlugin(Callback callback, Arguments&&... arguments) noexcept
        {
            try
            {
                return callback(std::forward<Arguments>(arguments)...);
            }
            catch (...)
            {
                std::terminate();
            }
        }

        bool isTerminal(const TaskState state) noexcept
        {
            return state == TaskState::Success || state == TaskState::Failure || state == TaskState::Cancelled;
        }

        atlas_task_pack_resource abiResource(const ExecutionResource resource)
        {
            return resource == ExecutionResource::CPU ? ATLAS_TASK_PACK_RESOURCE_CPU : ATLAS_TASK_PACK_RESOURCE_GPU;
        }

        atlas_task_pack_buffer_access abiAccess(const BufferAccess access)
        {
            switch (access)
            {
            case BufferAccess::ReadOnly:
                return ATLAS_TASK_PACK_BUFFER_READ_ONLY;
            case BufferAccess::WriteOnly:
                return ATLAS_TASK_PACK_BUFFER_WRITE_ONLY;
            case BufferAccess::ReadWrite:
                return ATLAS_TASK_PACK_BUFFER_READ_WRITE;
            }
            throw std::runtime_error{ "Manifest shader contains an invalid buffer-access value" };
        }

        const TaskPackShaderDescriptor& shaderDescriptor(const TaskPackManifest& manifest, const std::string_view shaderId)
        {
            const auto result{ std::find_if(manifest.shaders.begin(), manifest.shaders.end(),
                                            [shaderId](const TaskPackShaderDescriptor& candidate)
                                            { return candidate.shaderId == shaderId; }) };
            if (result == manifest.shaders.end())
            {
                throw std::runtime_error{ "Custom GPU task references an unavailable manifest shader" };
            }
            return *result;
        }

        void validateApi(const TaskPackManifest& manifest, const std::shared_ptr<Detail::ModuleState>& module)
        {
            const atlas_task_pack_api_v1& api{ module->api };
            if (api.struct_size != sizeof(atlas_task_pack_api_v1) || api.abi_version != ATLAS_TASK_PACK_ABI_VERSION ||
                api.common.struct_size != sizeof(atlas_task_pack_common_callbacks_v1) || api.common.describe_task == nullptr ||
                api.common.task_count != manifest.tasks.size())
            {
                throw std::runtime_error{ "Task-pack callback table has an incompatible version, size, or common callback" };
            }

            const bool hasCpu{ std::any_of(manifest.tasks.begin(), manifest.tasks.end(),
                                           [](const CustomTaskDescriptor& task) { return task.resource == ExecutionResource::CPU; }) };
            const bool hasGpu{ std::any_of(manifest.tasks.begin(), manifest.tasks.end(),
                                           [](const CustomTaskDescriptor& task) { return task.resource == ExecutionResource::GPU; }) };
            if (api.cpu.struct_size != sizeof(atlas_task_pack_cpu_callbacks_v1) ||
                (hasCpu && (api.cpu.prepare == nullptr || api.cpu.execute == nullptr || api.cpu.destroy == nullptr)) ||
                api.gpu.struct_size != sizeof(atlas_task_pack_gpu_callbacks_v1) ||
                (hasGpu && (api.gpu.prepare == nullptr || api.gpu.summarize == nullptr || api.gpu.destroy == nullptr)))
            {
                throw std::runtime_error{ "Task-pack callback table has a missing or incompatible resource callback" };
            }

            std::map<std::string, atlas_task_pack_resource> nativeTasks;
            for (std::uint64_t index{ 0U }; index < api.common.task_count; ++index)
            {
                atlas_task_pack_task_metadata metadata{ sizeof(atlas_task_pack_task_metadata), ATLAS_TASK_PACK_RESOURCE_CPU };
                Detail::WriterBuffer taskIdOutput{ {}, 128U, false };
                Detail::WriterBuffer error{ {}, Detail::maximumTaskPackErrorBytes, false };
                atlas_task_pack_writer taskIdWriter{ Detail::makeWriter(taskIdOutput) };
                atlas_task_pack_writer writer{ Detail::makeWriter(error) };
                const atlas_task_pack_status status{ invokePlugin(api.common.describe_task, index, &metadata, &taskIdWriter,
                                                                  &writer) };
                Detail::requireValidStatus(status, "describing native task metadata");
                if (status != ATLAS_TASK_PACK_STATUS_OK)
                {
                    throw Detail::callbackError("describing native task metadata", error);
                }
                if (error.exceeded || taskIdOutput.exceeded || metadata.struct_size != sizeof(atlas_task_pack_task_metadata))
                {
                    throw std::runtime_error{ "Task-pack metadata callback violated its output contract" };
                }
                const std::string taskId{ std::move(taskIdOutput.bytes) };
                if (taskId.empty() || taskId.find('\0') != std::string::npos)
                {
                    throw std::runtime_error{ "Task-pack metadata callback returned an invalid task ID" };
                }
                if ((metadata.resource != ATLAS_TASK_PACK_RESOURCE_CPU && metadata.resource != ATLAS_TASK_PACK_RESOURCE_GPU) ||
                    !nativeTasks.emplace(taskId, metadata.resource).second)
                {
                    throw std::runtime_error{ "Task-pack metadata contains an invalid resource or duplicate task ID" };
                }
            }
            for (const CustomTaskDescriptor& task : manifest.tasks)
            {
                const auto native{ nativeTasks.find(task.taskId) };
                if (native == nativeTasks.end() || native->second != abiResource(task.resource))
                {
                    throw std::runtime_error{ "Task-pack native task IDs or resources do not match the manifest" };
                }
            }
        }

        struct PreparedState final
        {
            ~PreparedState() noexcept
            {
                if (context == nullptr)
                {
                    return;
                }
                if (descriptor.resource == ExecutionResource::CPU)
                {
                    invokePlugin(module->api.cpu.destroy, context);
                }
                else
                {
                    invokePlugin(module->api.gpu.destroy, context);
                }
            }

            std::shared_ptr<Detail::ModuleState> module;
            CustomTaskDescriptor descriptor;
            void* context{ nullptr };
            VulkanRuntime* runtime{ nullptr };
            std::vector<std::pair<std::uint32_t, VulkanBuffer>> readbackBuffers;
            std::string summaryBytes;
        };

        std::shared_ptr<PreparedState> prepareCpu(const Detail::LoadedTaskPack& pack, const CustomTaskDescriptor& descriptor,
                                                  const CustomTaskCreateInfo& createInfo, const std::string& parameters)
        {
            auto state{ std::make_shared<PreparedState>() };
            state->module = pack.module;
            state->descriptor = descriptor;
            Detail::WriterBuffer error{ {}, Detail::maximumTaskPackErrorBytes, false };
            atlas_task_pack_writer errorWriter{ Detail::makeWriter(error) };
            const atlas_task_pack_node_input input{ sizeof(atlas_task_pack_node_input), Detail::stringView(parameters),
                                                    createInfo.graphSeed, createInfo.stableNodeIndex };
            const atlas_task_pack_status status{ invokePlugin(pack.module->api.cpu.prepare, Detail::stringView(descriptor.taskId),
                                                              &input, &state->context, &errorWriter) };
            Detail::requireValidStatus(status, "preparing a CPU task");
            if (status != ATLAS_TASK_PACK_STATUS_OK)
            {
                throw Detail::callbackError("preparing a CPU task", error);
            }
            if (error.exceeded || state->context == nullptr)
            {
                throw std::runtime_error{ "Task-pack CPU preparation returned invalid context or oversized output" };
            }
            return state;
        }

        struct PreparedGpuBuffer final
        {
            std::uint32_t binding{ 0U };
            BufferAccess access{ BufferAccess::ReadOnly };
            std::size_t size{ 0U };
            std::vector<std::byte> initialBytes;
            bool readback{ false };
        };

        struct PreparedGpu final
        {
            std::shared_ptr<PreparedState> state;
            VulkanDispatch dispatch;
        };

        PreparedGpu prepareGpu(const Detail::LoadedTaskPack& pack, const CustomTaskDescriptor& descriptor,
                               const CustomTaskCreateInfo& createInfo, const std::string& parameters)
        {
            if (createInfo.vulkanRuntime == nullptr)
            {
                throw std::invalid_argument{ "Custom GPU task preparation requires a VulkanRuntime" };
            }
            if (createInfo.sliceDimensions.has_value() && !descriptor.supportsSlicing)
            {
                throw std::invalid_argument{ "Custom GPU task does not declare slicing support" };
            }
            if (createInfo.sliceDimensions.has_value() && !createInfo.sliceDimensions->isValid())
            {
                throw std::invalid_argument{ "Custom GPU task slice dimensions must be non-zero" };
            }

            auto state{ std::make_shared<PreparedState>() };
            state->module = pack.module;
            state->descriptor = descriptor;
            state->runtime = createInfo.vulkanRuntime;
            Detail::WriterBuffer error{ {}, Detail::maximumTaskPackErrorBytes, false };
            atlas_task_pack_writer errorWriter{ Detail::makeWriter(error) };
            const atlas_task_pack_node_input input{ sizeof(atlas_task_pack_node_input), Detail::stringView(parameters),
                                                    createInfo.graphSeed, createInfo.stableNodeIndex };
            if (!descriptor.shaderId.has_value())
            {
                throw std::runtime_error{ "Custom GPU task does not declare a shader" };
            }
            const TaskPackShaderDescriptor& shader{ shaderDescriptor(pack.manifest, *descriptor.shaderId) };
            std::size_t totalInitialBytes{ 0U };
            std::vector<Detail::WriterBuffer> initialOutputs;
            std::vector<atlas_task_pack_gpu_buffer_v1> nativeBuffers;
            initialOutputs.reserve(shader.storageBuffers.size());
            nativeBuffers.reserve(shader.storageBuffers.size());
            for (const ShaderBufferBinding& binding : shader.storageBuffers)
            {
                initialOutputs.emplace_back(Detail::WriterBuffer{
                    {}, Detail::maximumTaskPackGpuBytes, false, &totalInitialBytes, Detail::maximumTaskPackGpuBytes });
                nativeBuffers.emplace_back(atlas_task_pack_gpu_buffer_v1{ sizeof(atlas_task_pack_gpu_buffer_v1),
                                                                          binding.binding,
                                                                          abiAccess(binding.access),
                                                                          0U,
                                                                          Detail::makeWriter(initialOutputs.back()),
                                                                          0U,
                                                                          {} });
            }
            atlas_task_pack_gpu_preparation_v1 preparation{
                sizeof(atlas_task_pack_gpu_preparation_v1), 0U, 0U, 0U, nativeBuffers.data(), nativeBuffers.size()
            };
            const atlas_task_pack_gpu_buffer_v1* const expectedBuffers{ preparation.buffers };
            const std::uint64_t expectedBufferCount{ preparation.buffer_count };
            const atlas_task_pack_status status{ invokePlugin(pack.module->api.gpu.prepare, Detail::stringView(descriptor.taskId),
                                                              &input, &state->context, &preparation, &errorWriter) };
            Detail::requireValidStatus(status, "preparing a GPU task");
            if (status != ATLAS_TASK_PACK_STATUS_OK)
            {
                throw Detail::callbackError("preparing a GPU task", error);
            }
            if (error.exceeded || state->context == nullptr || preparation.struct_size != sizeof(atlas_task_pack_gpu_preparation_v1) ||
                preparation.buffers != expectedBuffers || preparation.buffer_count != expectedBufferCount)
            {
                throw std::runtime_error{ "Task-pack GPU preparation violated its bounded output contract" };
            }

            std::vector<PreparedGpuBuffer> requestedBuffers;
            requestedBuffers.reserve(static_cast<std::size_t>(preparation.buffer_count));
            std::size_t totalAllocationBytes{ 0U };
            for (std::uint64_t index{ 0U }; index < preparation.buffer_count; ++index)
            {
                const atlas_task_pack_gpu_buffer_v1& requested{ preparation.buffers[index] };
                const ShaderBufferBinding& declared{ shader.storageBuffers.at(static_cast<std::size_t>(index)) };
                const Detail::WriterBuffer& initial{ initialOutputs.at(static_cast<std::size_t>(index)) };
                const atlas_task_pack_writer expectedWriter{ Detail::makeWriter(initialOutputs.at(static_cast<std::size_t>(index))) };
                if (requested.struct_size != sizeof(atlas_task_pack_gpu_buffer_v1) || requested.size == 0U ||
                    requested.size > Detail::maximumTaskPackGpuBytes || requested.readback > 1U ||
                    requested.binding != declared.binding || requested.access != abiAccess(declared.access) ||
                    requested.initial_bytes.struct_size != expectedWriter.struct_size ||
                    requested.initial_bytes.user_data != expectedWriter.user_data ||
                    requested.initial_bytes.write != expectedWriter.write || initial.exceeded ||
                    initial.bytes.size() > requested.size ||
                    std::any_of(requested.reserved, requested.reserved + sizeof(requested.reserved),
                                [](const std::uint8_t value) { return value != 0U; }) ||
                    requested.size > Detail::maximumTaskPackGpuBytes - totalAllocationBytes)
                {
                    throw std::runtime_error{ "Task-pack GPU preparation returned an invalid buffer request" };
                }
                totalAllocationBytes += static_cast<std::size_t>(requested.size);
                PreparedGpuBuffer buffer{
                    requested.binding, declared.access, static_cast<std::size_t>(requested.size), {}, requested.readback != 0U
                };
                const std::size_t initialSize{ initial.bytes.size() };
                if (buffer.access != BufferAccess::WriteOnly && initialSize != buffer.size)
                {
                    throw std::runtime_error{ "Readable custom GPU buffers require complete initial bytes" };
                }
                if (initialSize != 0U && initialSize != buffer.size)
                {
                    throw std::runtime_error{ "Custom GPU buffer initialization must be empty or cover the complete buffer" };
                }
                buffer.initialBytes.resize(initialSize);
                if (initialSize != 0U)
                {
                    std::memcpy(buffer.initialBytes.data(), initial.bytes.data(), initialSize);
                }
                requestedBuffers.emplace_back(std::move(buffer));
            }
            std::vector<std::uint32_t> requestedReadbacks;
            for (const PreparedGpuBuffer& buffer : requestedBuffers)
            {
                if (buffer.readback)
                {
                    if (buffer.access == BufferAccess::ReadOnly)
                    {
                        throw std::runtime_error{ "Custom GPU result bindings must be writable by the shader" };
                    }
                    requestedReadbacks.emplace_back(buffer.binding);
                }
            }
            std::sort(requestedReadbacks.begin(), requestedReadbacks.end());
            std::vector<std::uint32_t> manifestReadbacks{ descriptor.resultBindings };
            std::sort(manifestReadbacks.begin(), manifestReadbacks.end());
            if (requestedReadbacks != manifestReadbacks)
            {
                throw std::runtime_error{ "Task-pack GPU readbacks do not match the manifest task" };
            }

            VulkanRuntime& runtime{ *createInfo.vulkanRuntime };
            const VulkanComputePipeline pipeline{ runtime.createComputePipeline(
                ComputeShader{ Detail::readSpirvAsset(pack.manifest.sourceDirectory / shader.assetPath), shader.entryPoint,
                               shader.storageBuffers }) };
            std::vector<BufferBinding> dispatchBuffers;
            dispatchBuffers.reserve(requestedBuffers.size());
            for (const PreparedGpuBuffer& requested : requestedBuffers)
            {
                VulkanBuffer buffer{ runtime.createBuffer(requested.size) };
                if (!requested.initialBytes.empty())
                {
                    runtime.upload(buffer, requested.initialBytes);
                }
                dispatchBuffers.emplace_back(BufferBinding{ requested.binding, buffer, requested.access });
                if (requested.readback)
                {
                    state->readbackBuffers.emplace_back(requested.binding, buffer);
                }
            }
            VulkanDispatch dispatch{ pipeline, std::move(dispatchBuffers),
                                     DispatchDimensions{ preparation.workgroups_x, preparation.workgroups_y,
                                                         preparation.workgroups_z } };
            Detail::VulkanAccess::retainLifetime(dispatch, pack.module);
            return PreparedGpu{ std::move(state), std::move(dispatch) };
        }
    } // namespace

    /** @brief Private exact-identity registry storage. */
    struct TaskPackRegistry::Impl final
    {
        /// @brief Loaded packs keyed by pack ID and content digest.
        std::map<PackKey, std::unique_ptr<Detail::LoadedTaskPack>> packs;
    };

    /** @brief Private prepared payload, ownership, and summary state. */
    struct CustomTaskInstance::Impl final
    {
        /// @brief Existing graph payload variants accepted by TaskGraph.
        using Payload = std::variant<TaskFunction, VulkanDispatch, SlicedVulkanDispatch>;

        /// @brief Retained native context and GPU result resources.
        std::shared_ptr<PreparedState> state;
        /// @brief Prepared existing payload added to a graph at most once.
        Payload payload;
        /// @brief Borrowed graph that must outlive this instance after insertion.
        TaskGraph* graph{ nullptr };
        /// @brief Graph handle assigned after successful insertion.
        std::optional<TaskHandle> handle;
        /// @brief Enforces one terminal result callback and collection.
        bool summaryCollected{ false };
    };

    TaskPackRegistry::TaskPackRegistry() : implementation{ std::make_unique<Impl>() } {}
    TaskPackRegistry::~TaskPackRegistry() = default;
    TaskPackRegistry::TaskPackRegistry(TaskPackRegistry&&) noexcept = default;
    TaskPackRegistry& TaskPackRegistry::operator=(TaskPackRegistry&&) noexcept = default;

    TaskPackManifest TaskPackRegistry::inspectDirectory(const std::filesystem::path& directory) const
    {
        return Detail::inspectTaskPackDirectory(directory);
    }

    const TaskPackManifest& TaskPackRegistry::loadDirectory(const std::filesystem::path& directory)
    {
        TaskPackManifest manifest{ inspectDirectory(directory) };
        const PackKey key{ manifest.packId, manifest.digest };
        if (const auto existing{ implementation->packs.find(key) }; existing != implementation->packs.end())
        {
            return existing->second->manifest;
        }

        const auto binary{ std::find_if(
            manifest.platformBinaries.begin(), manifest.platformBinaries.end(), [](const TaskPackPlatformBinary& candidate)
            { return candidate.platform == Detail::hostPlatform() && candidate.architecture == Detail::hostArchitecture(); }) };
        if (binary == manifest.platformBinaries.end())
        {
            throw std::runtime_error{ "Task pack has no native library for this platform and architecture" };
        }

        auto module{ std::make_shared<Detail::ModuleState>() };
        module->module = std::make_shared<Detail::NativeModule>(manifest.sourceDirectory / binary->libraryPath);
        const atlas_task_pack_get_api_fn getApi{ module->module->entryPoint() };
        const atlas_task_pack_api_v1* const api{ invokePlugin(getApi, ATLAS_TASK_PACK_ABI_VERSION) };
        if (api == nullptr)
        {
            throw std::runtime_error{ "Task-pack entry point rejected ABI version 1" };
        }
        module->api = *api;
        validateApi(manifest, module);

        auto loaded{ std::make_unique<Detail::LoadedTaskPack>(Detail::LoadedTaskPack{ std::move(manifest), std::move(module) }) };
        const TaskPackManifest& result{ loaded->manifest };
        implementation->packs.emplace(key, std::move(loaded));
        return result;
    }

    const CustomTaskDescriptor* TaskPackRegistry::findTask(const std::string_view packId, const std::string_view packDigest,
                                                           const std::string_view taskId) const noexcept
    {
        const auto pack{ implementation->packs.find(PackKey{ std::string{ packId }, std::string{ packDigest } }) };
        if (pack == implementation->packs.end())
        {
            return nullptr;
        }
        const auto task{ std::find_if(pack->second->manifest.tasks.begin(), pack->second->manifest.tasks.end(),
                                      [taskId](const CustomTaskDescriptor& candidate) { return candidate.taskId == taskId; }) };
        return task == pack->second->manifest.tasks.end() ? nullptr : &*task;
    }

    CustomTaskInstance TaskPackRegistry::createTask(const std::string_view packId, const std::string_view packDigest,
                                                    const std::string_view taskId, const CustomTaskCreateInfo& createInfo) const
    {
        const auto pack{ implementation->packs.find(PackKey{ std::string{ packId }, std::string{ packDigest } }) };
        if (pack == implementation->packs.end())
        {
            throw std::invalid_argument{ "Custom task requires an exactly loaded pack ID and digest" };
        }
        const CustomTaskDescriptor* const descriptor{ findTask(packId, packDigest, taskId) };
        if (descriptor == nullptr)
        {
            throw std::invalid_argument{ "Custom task ID is not present in the exactly loaded pack" };
        }
        const std::string parameters{ Detail::canonicalParameters(*descriptor, createInfo.parameterJson) };

        auto instance{ std::make_unique<CustomTaskInstance::Impl>() };
        if (descriptor->resource == ExecutionResource::CPU)
        {
            if (createInfo.sliceDimensions.has_value())
            {
                throw std::invalid_argument{ "Custom CPU tasks cannot request Vulkan slicing" };
            }
            instance->state = prepareCpu(*pack->second, *descriptor, createInfo, parameters);
            const std::shared_ptr<PreparedState> state{ instance->state };
            instance->payload = TaskFunction{ [state]
                                              {
                                                  Detail::WriterBuffer summary{ {}, Detail::maximumTaskPackSummaryBytes, false };
                                                  Detail::WriterBuffer error{ {}, Detail::maximumTaskPackErrorBytes, false };
                                                  atlas_task_pack_writer summaryWriter{ Detail::makeWriter(summary) };
                                                  atlas_task_pack_writer errorWriter{ Detail::makeWriter(error) };
                                                  const atlas_task_pack_status status{ invokePlugin(
                                                      state->module->api.cpu.execute, state->context, &summaryWriter, &errorWriter) };
                                                  Detail::requireValidStatus(status, "executing a CPU task");
                                                  if (status != ATLAS_TASK_PACK_STATUS_OK)
                                                  {
                                                      throw Detail::callbackError("executing a CPU task", error);
                                                  }
                                                  if (summary.exceeded || error.exceeded)
                                                  {
                                                      throw std::runtime_error{ "Task-pack CPU callback output exceeded its bound" };
                                                  }
                                                  state->summaryBytes = std::move(summary.bytes);
                                              } };
        }
        else
        {
            PreparedGpu prepared{ prepareGpu(*pack->second, *descriptor, createInfo, parameters) };
            instance->state = std::move(prepared.state);
            if (createInfo.sliceDimensions.has_value())
            {
                instance->payload = SlicedVulkanDispatch{ std::move(prepared.dispatch), *createInfo.sliceDimensions };
            }
            else
            {
                instance->payload = std::move(prepared.dispatch);
            }
        }
        return CustomTaskInstance{ std::move(instance) };
    }

    CustomTaskInstance::CustomTaskInstance(std::unique_ptr<Impl> state) noexcept : implementation{ std::move(state) } {}
    CustomTaskInstance::~CustomTaskInstance() = default;
    CustomTaskInstance::CustomTaskInstance(CustomTaskInstance&&) noexcept = default;
    CustomTaskInstance& CustomTaskInstance::operator=(CustomTaskInstance&&) noexcept = default;

    const CustomTaskDescriptor& CustomTaskInstance::descriptor() const noexcept
    {
        return implementation->state->descriptor;
    }

    std::optional<TaskHandle> CustomTaskInstance::addToGraph(TaskGraph& graph, TaskOptions options)
    {
        if (implementation->handle.has_value())
        {
            return std::nullopt;
        }
        options.executionResource = descriptor().resource;
        if (options.name.empty())
        {
            options.name = descriptor().displayName;
        }
        std::optional<TaskHandle> handle;
        if (const auto* function{ std::get_if<TaskFunction>(&implementation->payload) }; function != nullptr)
        {
            handle = graph.addCpuTask(*function, std::move(options));
        }
        else if (const auto* dispatch{ std::get_if<VulkanDispatch>(&implementation->payload) }; dispatch != nullptr)
        {
            handle = graph.addGpuTask(*dispatch, std::move(options));
        }
        else
        {
            handle = graph.addGpuTask(std::get<SlicedVulkanDispatch>(implementation->payload), std::move(options));
        }
        if (handle.has_value())
        {
            implementation->graph = &graph;
            implementation->handle = handle;
        }
        return handle;
    }

    CustomTaskSummary CustomTaskInstance::collectSummary()
    {
        if (!implementation->handle.has_value() || implementation->graph == nullptr)
        {
            throw std::logic_error{ "Custom task must be added to a graph before summary collection" };
        }
        if (implementation->summaryCollected)
        {
            throw std::logic_error{ "Custom task summary has already been collected" };
        }
        const std::optional<TaskSnapshot> snapshot{ implementation->graph->snapshotTask(*implementation->handle) };
        if (!snapshot.has_value() || !isTerminal(snapshot->executionInfo.state))
        {
            throw std::logic_error{ "Custom task summary is available only after terminal execution" };
        }

        if (descriptor().resource == ExecutionResource::GPU)
        {
            std::vector<std::vector<std::byte>> bytes;
            std::vector<atlas_task_pack_gpu_readback_v1> readbacks;
            bytes.reserve(implementation->state->readbackBuffers.size());
            readbacks.reserve(implementation->state->readbackBuffers.size());
            for (const auto& [binding, buffer] : implementation->state->readbackBuffers)
            {
                bytes.emplace_back(buffer.size());
                implementation->state->runtime->download(buffer, bytes.back());
                readbacks.emplace_back(atlas_task_pack_gpu_readback_v1{
                    sizeof(atlas_task_pack_gpu_readback_v1), binding,
                    atlas_task_pack_byte_view{ reinterpret_cast<const std::uint8_t*>(bytes.back().data()), bytes.back().size() } });
            }
            Detail::WriterBuffer summary{ {}, Detail::maximumTaskPackSummaryBytes, false };
            Detail::WriterBuffer error{ {}, Detail::maximumTaskPackErrorBytes, false };
            atlas_task_pack_writer summaryWriter{ Detail::makeWriter(summary) };
            atlas_task_pack_writer errorWriter{ Detail::makeWriter(error) };
            const atlas_task_pack_status status{ invokePlugin(implementation->state->module->api.gpu.summarize,
                                                              implementation->state->context, readbacks.data(), readbacks.size(),
                                                              &summaryWriter, &errorWriter) };
            Detail::requireValidStatus(status, "summarizing a GPU task");
            if (status != ATLAS_TASK_PACK_STATUS_OK)
            {
                throw Detail::callbackError("summarizing a GPU task", error);
            }
            if (summary.exceeded || error.exceeded)
            {
                throw std::runtime_error{ "Task-pack GPU summary output exceeded its bound" };
            }
            implementation->state->summaryBytes = std::move(summary.bytes);
        }

        CustomTaskSummary result{ Detail::validateSummary(descriptor(), implementation->state->summaryBytes) };
        implementation->summaryCollected = true;
        return result;
    }
} // namespace Atlas
