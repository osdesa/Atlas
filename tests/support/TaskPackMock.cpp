#include "atlas/Extension/TaskPackAbi.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <new>
#include <string_view>
#include <vector>

namespace
{
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
            *context = new NodeContext{ .fail = id == "cpu_error" };
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
            const auto* node{ static_cast<const NodeContext*>(context) };
            if (node == nullptr)
            {
                return write(error, "missing CPU context");
            }
            if (node->fail)
            {
                write(error, "requested CPU failure");
                return ATLAS_TASK_PACK_STATUS_ERROR;
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
            preparation->workgroups_x = 1U;
            preparation->workgroups_y = 1U;
            preparation->workgroups_z = 1U;
            *context = new NodeContext{};
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
    return requested == ATLAS_TASK_PACK_ABI_VERSION ? &api : nullptr;
}
