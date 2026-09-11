#include "atlas/Extension/TaskPackAbi.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string_view>
#include <vector>

namespace
{
    /** @brief Test-only fault selection inherited by an isolated runner process. */
    bool fault(const std::string_view name) noexcept
    {
        const char* selected{ std::getenv("ATLAS_TEST_PACK_FAULT") };
        return selected != nullptr && name == selected;
    }

    atlas_task_pack_status write(atlas_task_pack_writer* writer, const std::string_view value)
    {
        if (writer == nullptr || writer->struct_size != sizeof(atlas_task_pack_writer) || writer->write == nullptr)
        {
            return ATLAS_TASK_PACK_STATUS_ERROR;
        }
        return writer->write(writer->user_data,
                             atlas_task_pack_byte_view{ reinterpret_cast<const std::uint8_t*>(value.data()), value.size() });
    }

    atlas_task_pack_status writeBytes(atlas_task_pack_writer* writer, const void* data, const std::size_t size)
    {
        if (writer == nullptr || writer->struct_size != sizeof(atlas_task_pack_writer) || writer->write == nullptr)
        {
            return ATLAS_TASK_PACK_STATUS_ERROR;
        }
        return writer->write(writer->user_data, atlas_task_pack_byte_view{ static_cast<const std::uint8_t*>(data), size });
    }

    struct NodeContext final
    {
        bool fail{ false };
        std::atomic_bool executed{ false };
        std::array<float, 4U> left{ 2.0F, 2.0F, 2.0F, 2.0F };
        std::array<float, 4U> right{ 3.0F, 3.0F, 3.0F, 3.0F };
    };

    atlas_task_pack_status describeTask(const std::uint64_t index, atlas_task_pack_task_metadata* task, atlas_task_pack_writer* taskId,
                                        atlas_task_pack_writer* error) noexcept
    {
        try
        {
            if (task == nullptr || task->struct_size != sizeof(atlas_task_pack_task_metadata))
            {
                return write(error, "invalid metadata output");
            }
            static constexpr std::array<std::string_view, 3U> ids{ "cpu_success", "cpu_error", "gpu_vector" };
            if (index >= ids.size())
            {
                return write(error, "invalid metadata index");
            }
            task->resource = index == 2U ? ATLAS_TASK_PACK_RESOURCE_GPU : ATLAS_TASK_PACK_RESOURCE_CPU;
            if (fault("metadata_status"))
                return 99U;
            if (fault("metadata_size"))
                task->struct_size = 0U;
            if (fault("metadata_resource"))
                task->resource = 99U;
            if (fault("metadata_duplicate"))
                return write(taskId, "cpu_success");
            if (fault("metadata_oversize"))
            {
                const std::array<char, 129U> bytes{};
                writeBytes(taskId, bytes.data(), bytes.size());
                return ATLAS_TASK_PACK_STATUS_OK;
            }
            return write(taskId, ids.at(static_cast<std::size_t>(index)));
        }
        catch (...)
        {
            return ATLAS_TASK_PACK_STATUS_ERROR;
        }
    }

    atlas_task_pack_status prepareCpu(const atlas_task_pack_string_view taskId, const atlas_task_pack_node_input* input,
                                      void** context, atlas_task_pack_writer* error) noexcept
    {
        try
        {
            if (input == nullptr || input->struct_size != sizeof(atlas_task_pack_node_input) || context == nullptr ||
                input->parameter_json.data == nullptr)
            {
                return write(error, "invalid CPU preparation input");
            }
            const std::string_view id{ taskId.data, static_cast<std::size_t>(taskId.size) };
            if (id != "cpu_success" && id != "cpu_error")
            {
                return write(error, "unknown CPU task");
            }
            if (fault("cpu_null_context"))
                return ATLAS_TASK_PACK_STATUS_OK;
            *context = new NodeContext{ .fail = id == "cpu_error" };
            if (fault("cpu_prepare_status"))
                return 99U;
            return ATLAS_TASK_PACK_STATUS_OK;
        }
        catch (...)
        {
            return write(error, "CPU preparation exception");
        }
    }

    atlas_task_pack_status executeCpu(void* context, atlas_task_pack_writer* summary, atlas_task_pack_writer* error) noexcept
    {
        try
        {
            auto* node{ static_cast<NodeContext*>(context) };
            if (node == nullptr)
            {
                return write(error, "missing CPU context");
            }
            if (node->executed.exchange(true))
            {
                write(error, "CPU context was reused");
                return ATLAS_TASK_PACK_STATUS_ERROR;
            }
            if (node->fail)
            {
                write(error, "requested CPU failure");
                return ATLAS_TASK_PACK_STATUS_ERROR;
            }
            if (fault("native_exit"))
                std::_Exit(73);
            if (fault("cpu_execute_status"))
                return 99U;
            if (fault("cpu_summary_invalid"))
                return write(summary, R"({"value":true})");
            if (fault("cpu_summary_oversize") || fault("cpu_error_oversize"))
            {
                const std::array<char, 65537U> bytes{};
                writeBytes(fault("cpu_error_oversize") ? error : summary, bytes.data(), bytes.size());
                return ATLAS_TASK_PACK_STATUS_OK;
            }
            return write(summary, R"({"value":42})");
        }
        catch (...)
        {
            return write(error, "CPU execution exception");
        }
    }

    atlas_task_pack_status prepareGpu(const atlas_task_pack_string_view taskId, const atlas_task_pack_node_input* input,
                                      void** context, atlas_task_pack_gpu_preparation_v1* preparation,
                                      atlas_task_pack_writer* error) noexcept
    {
        try
        {
            const std::string_view id{ taskId.data, static_cast<std::size_t>(taskId.size) };
            if (id != "gpu_vector" || input == nullptr || input->struct_size != sizeof(atlas_task_pack_node_input) ||
                context == nullptr || preparation == nullptr || preparation->struct_size != sizeof(atlas_task_pack_gpu_preparation_v1))
            {
                return write(error, "invalid GPU preparation input");
            }
            if (preparation->buffers == nullptr || preparation->buffer_count != 3U || preparation->buffers[0U].binding != 0U ||
                preparation->buffers[0U].access != ATLAS_TASK_PACK_BUFFER_READ_ONLY || preparation->buffers[1U].binding != 1U ||
                preparation->buffers[1U].access != ATLAS_TASK_PACK_BUFFER_READ_ONLY || preparation->buffers[2U].binding != 2U ||
                preparation->buffers[2U].access != ATLAS_TASK_PACK_BUFFER_WRITE_ONLY)
            {
                return write(error, "unexpected GPU storage-buffer interface");
            }
            const NodeContext initial{};
            preparation->buffers[0U].size = sizeof(initial.left);
            preparation->buffers[1U].size = sizeof(initial.right);
            preparation->buffers[2U].size = sizeof(initial.left);
            preparation->buffers[2U].readback = 1U;
            if (writeBytes(&preparation->buffers[0U].initial_bytes, initial.left.data(), sizeof(initial.left)) !=
                    ATLAS_TASK_PACK_STATUS_OK ||
                writeBytes(&preparation->buffers[1U].initial_bytes, initial.right.data(), sizeof(initial.right)) !=
                    ATLAS_TASK_PACK_STATUS_OK)
            {
                return write(error, "GPU initialization output exceeded its bound");
            }
            preparation->workgroups_x = 4U;
            preparation->workgroups_y = 1U;
            preparation->workgroups_z = 1U;
            *context = new NodeContext{};
            if (fault("gpu_prepare_status"))
                return 99U;
            if (fault("gpu_preparation_size"))
                preparation->struct_size = 0U;
            if (fault("gpu_buffer_count"))
                preparation->buffer_count = 0U;
            if (fault("gpu_buffers_pointer"))
                preparation->buffers = nullptr;
            else
            {
                auto& buffer{ preparation->buffers[2U] };
                if (fault("gpu_buffer_size"))
                    buffer.struct_size = 0U;
                if (fault("gpu_zero_allocation"))
                    buffer.size = 0U;
                if (fault("gpu_excess_allocation"))
                    buffer.size = 256U * 1024U * 1024U + 1U;
                if (fault("gpu_binding"))
                    buffer.binding = 99U;
                if (fault("gpu_access"))
                    buffer.access = ATLAS_TASK_PACK_BUFFER_READ_ONLY;
                if (fault("gpu_readback_flag"))
                    buffer.readback = 2U;
                if (fault("gpu_missing_readback"))
                    buffer.readback = 0U;
                if (fault("gpu_readonly_readback"))
                    preparation->buffers[0U].readback = 1U;
                if (fault("gpu_reserved"))
                    buffer.reserved[0U] = 1U;
                if (fault("gpu_writer"))
                    buffer.initial_bytes.write = nullptr;
                if (fault("gpu_partial_initialization"))
                    preparation->buffers[0U].size += sizeof(float);
                if (fault("gpu_zero_workgroups"))
                    preparation->workgroups_x = 0U;
                if (fault("gpu_device_workgroups"))
                    preparation->workgroups_x = UINT32_MAX;
            }
            return ATLAS_TASK_PACK_STATUS_OK;
        }
        catch (...)
        {
            return write(error, "GPU preparation exception");
        }
    }

    atlas_task_pack_status summarizeGpu(void*, const atlas_task_pack_gpu_readback_v1* readbacks, const std::uint64_t readbackCount,
                                        atlas_task_pack_writer* summary, atlas_task_pack_writer* error) noexcept
    {
        try
        {
            if (fault("gpu_summary_status"))
                return 99U;
            if (fault("gpu_summary_error"))
            {
                write(error, "requested GPU readback failure");
                return ATLAS_TASK_PACK_STATUS_ERROR;
            }
            if (fault("gpu_summary_invalid"))
                return write(summary, R"({"ok":1})");
            if (fault("gpu_summary_oversize"))
            {
                const std::array<char, 65537U> bytes{};
                writeBytes(summary, bytes.data(), bytes.size());
                return ATLAS_TASK_PACK_STATUS_OK;
            }
            if (readbackCount != 1U || readbacks == nullptr || readbacks[0U].struct_size != sizeof(atlas_task_pack_gpu_readback_v1) ||
                readbacks[0U].binding != 2U || readbacks[0U].bytes.size != 4U * sizeof(float))
            {
                return write(error, "invalid GPU readback");
            }
            const auto* values{ reinterpret_cast<const float*>(readbacks[0U].bytes.data) };
            if (values[0U] != 5.0F || values[3U] != 5.0F)
            {
                return write(error, "unexpected GPU result");
            }
            return write(summary, R"({"ok":true})");
        }
        catch (...)
        {
            return write(error, "GPU summary exception");
        }
    }

    void destroy(void* context) noexcept
    {
        delete static_cast<NodeContext*>(context);
    }

    const atlas_task_pack_api_v1 api{ sizeof(atlas_task_pack_api_v1),
                                      ATLAS_TASK_PACK_ABI_VERSION,
                                      { sizeof(atlas_task_pack_common_callbacks_v1), 3U, &describeTask },
                                      { sizeof(atlas_task_pack_cpu_callbacks_v1), &prepareCpu, &executeCpu, &destroy },
                                      { sizeof(atlas_task_pack_gpu_callbacks_v1), &prepareGpu, &summarizeGpu, &destroy } };
} // namespace

extern "C" ATLAS_TASK_PACK_EXPORT const atlas_task_pack_api_v1* atlas_task_pack_get_api(const std::uint32_t requested) noexcept
{
    // Each fault test gets a fresh process, so the returned table stays immutable.
    static const atlas_task_pack_api_v1 selected = []
    {
        auto result{ api };
        if (fault("abi_version"))
            result.abi_version = 99U;
        if (fault("api_size"))
            result.struct_size = 0U;
        if (fault("common_size"))
            result.common.struct_size = 0U;
        if (fault("task_count"))
            result.common.task_count = 0U;
        if (fault("describe_null"))
            result.common.describe_task = nullptr;
        if (fault("cpu_size"))
            result.cpu.struct_size = 0U;
        if (fault("cpu_prepare_null"))
            result.cpu.prepare = nullptr;
        if (fault("cpu_execute_null"))
            result.cpu.execute = nullptr;
        if (fault("cpu_destroy_null"))
            result.cpu.destroy = nullptr;
        if (fault("gpu_size"))
            result.gpu.struct_size = 0U;
        if (fault("gpu_prepare_null"))
            result.gpu.prepare = nullptr;
        if (fault("gpu_summary_null"))
            result.gpu.summarize = nullptr;
        if (fault("gpu_destroy_null"))
            result.gpu.destroy = nullptr;
        return result;
    }();
    return requested == ATLAS_TASK_PACK_ABI_VERSION && !fault("api_null") ? &selected : nullptr;
}
