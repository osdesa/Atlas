#ifndef ATLAS_EXTENSION_TASK_PACK_ABI
#define ATLAS_EXTENSION_TASK_PACK_ABI

/**
 * @file TaskPackAbi.h
 * @brief Pure-C ABI version 1 for trusted native Atlas task packs.
 *
 * Plugins must catch every exception before it crosses this boundary; the host
 * terminates if one escapes. Views are borrowed callback inputs and are valid
 * only for that call. Plugin outputs use host-owned structures and bounded
 * writers, so every returned byte is copied before the callback returns. No
 * allocator, C++ type, STL object, exception, or Vulkan handle crosses this ABI.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/** @brief ABI version requested by Atlas and reported by compatible packs. */
#define ATLAS_TASK_PACK_ABI_VERSION 1U
/** @brief Exact exported symbol name resolved by the host. */
#define ATLAS_TASK_PACK_ENTRY_POINT "atlas_task_pack_get_api"

#if defined(_WIN32)
/** @brief Native-library export annotation for the known entry point. */
#define ATLAS_TASK_PACK_EXPORT __declspec(dllexport)
#else
/** @brief Native-library export annotation for the known entry point. */
#define ATLAS_TASK_PACK_EXPORT __attribute__((visibility("default")))
#endif

    /** @brief Fixed-width status value returned by every task-pack callback. */
    typedef uint32_t atlas_task_pack_status;
/** @brief Callback completed successfully. */
#define ATLAS_TASK_PACK_STATUS_OK UINT32_C(0)
/** @brief Callback reported a recoverable error through its error writer. */
#define ATLAS_TASK_PACK_STATUS_ERROR UINT32_C(1)

    /** @brief Fixed-width resource value cross-checked against the pack manifest. */
    typedef uint32_t atlas_task_pack_resource;
/** @brief Task executes as a CPU callable. */
#define ATLAS_TASK_PACK_RESOURCE_CPU UINT32_C(1)
/** @brief Task executes as declarative Vulkan compute work. */
#define ATLAS_TASK_PACK_RESOURCE_GPU UINT32_C(2)

    /** @brief Fixed-width storage-buffer access value cross-checked against shader reflection. */
    typedef uint32_t atlas_task_pack_buffer_access;
/** @brief Shader may read but not write the storage buffer. */
#define ATLAS_TASK_PACK_BUFFER_READ_ONLY UINT32_C(1)
/** @brief Shader may write but not read prior storage-buffer contents. */
#define ATLAS_TASK_PACK_BUFFER_WRITE_ONLY UINT32_C(2)
/** @brief Shader may read and write the storage buffer. */
#define ATLAS_TASK_PACK_BUFFER_READ_WRITE UINT32_C(3)

    /** @brief Borrowed UTF-8 bytes; embedded null bytes are not permitted for identifiers. */
    typedef struct atlas_task_pack_string_view
    {
        /** @brief First borrowed byte, or null only when size is zero. */
        const char* data;
        /** @brief Byte count excluding any terminator. */
        uint64_t size;
    } atlas_task_pack_string_view;

    /** @brief Borrowed arbitrary bytes. */
    typedef struct atlas_task_pack_byte_view
    {
        /** @brief First borrowed byte, or null only when size is zero. */
        const uint8_t* data;
        /** @brief Byte count. */
        uint64_t size;
    } atlas_task_pack_byte_view;

    /**
     * @brief Host-owned bounded writer used only during its supplying callback.
     *
     * Plugins must not retain or concurrently invoke a writer or any of its
     * fields. The host copies accepted bytes before `write` returns.
     */
    typedef struct atlas_task_pack_writer
    {
        /** @brief Must equal `sizeof(atlas_task_pack_writer)`. */
        uint32_t struct_size;
        /** @brief Opaque host state passed unchanged to write. */
        void* user_data;
        /** @brief Appends bytes or returns ERROR after the output bound is reached. */
        atlas_task_pack_status (*write)(void* user_data, atlas_task_pack_byte_view bytes);
    } atlas_task_pack_writer;

    /** @brief Stable inputs supplied while preparing one independent graph node. */
    typedef struct atlas_task_pack_node_input
    {
        /** @brief Must equal `sizeof(atlas_task_pack_node_input)`. */
        uint32_t struct_size;
        /** @brief Validated canonical flat parameter JSON. */
        atlas_task_pack_string_view parameter_json;
        /** @brief Graph seed selected by the host. */
        uint64_t graph_seed;
        /** @brief Stable node position selected by the host. */
        uint64_t stable_node_index;
    } atlas_task_pack_node_input;

    /** @brief Native metadata used to cross-check one manifest task. */
    typedef struct atlas_task_pack_task_metadata
    {
        /** @brief Must remain `sizeof(atlas_task_pack_task_metadata)`. */
        uint32_t struct_size;
        /** @brief CPU or GPU resource cross-checked against the manifest. */
        atlas_task_pack_resource resource;
    } atlas_task_pack_task_metadata;

    /** @brief Common metadata callbacks shared by CPU and GPU tasks. */
    typedef struct atlas_task_pack_common_callbacks_v1
    {
        /** @brief Must equal `sizeof(atlas_task_pack_common_callbacks_v1)`. */
        uint32_t struct_size;
        /** @brief Number of unique native tasks, exactly matching the manifest. */
        uint64_t task_count;
        /** @brief Describes one zero-based task and writes its pack-local ID. */
        atlas_task_pack_status (*describe_task)(uint64_t index, atlas_task_pack_task_metadata* task, atlas_task_pack_writer* task_id,
                                                atlas_task_pack_writer* error);
    } atlas_task_pack_common_callbacks_v1;

    /** @brief CPU preparation and execution callbacks. */
    typedef struct atlas_task_pack_cpu_callbacks_v1
    {
        /** @brief Must equal `sizeof(atlas_task_pack_cpu_callbacks_v1)`. */
        uint32_t struct_size;
        /** @brief Creates an independent opaque context for one CPU node. */
        atlas_task_pack_status (*prepare)(atlas_task_pack_string_view task_id, const atlas_task_pack_node_input* input,
                                          void** node_context, atlas_task_pack_writer* error);
        /** @brief Executes one prepared context and writes summary JSON or an error. */
        atlas_task_pack_status (*execute)(void* node_context, atlas_task_pack_writer* summary, atlas_task_pack_writer* error);
        /** @brief Releases a non-null context; must not throw or fail. */
        void (*destroy)(void* node_context);
    } atlas_task_pack_cpu_callbacks_v1;

    /** @brief One host-owned storage-buffer output used during GPU preparation. */
    typedef struct atlas_task_pack_gpu_buffer_v1
    {
        /** @brief Must equal `sizeof(atlas_task_pack_gpu_buffer_v1)`. */
        uint32_t struct_size;
        /** @brief Host-initialized set-zero descriptor binding; do not modify. */
        uint32_t binding;
        /** @brief Host-initialized reflected access; do not modify. */
        atlas_task_pack_buffer_access access;
        /** @brief Plugin-supplied non-zero host allocation size. */
        uint64_t size;
        /** @brief Host-initialized writer for empty or complete initialization. */
        atlas_task_pack_writer initial_bytes;
        /** @brief One requests post-execution readback; zero disables it. */
        uint8_t readback;
        /** @brief Reserved bytes; initialize to zero. */
        uint8_t reserved[7];
    } atlas_task_pack_gpu_buffer_v1;

    /** @brief Host-owned declarative GPU work filled during preparation. */
    typedef struct atlas_task_pack_gpu_preparation_v1
    {
        /** @brief Must remain `sizeof(atlas_task_pack_gpu_preparation_v1)`. */
        uint32_t struct_size;
        /** @brief Non-zero logical dispatch X workgroups. */
        uint32_t workgroups_x;
        /** @brief Non-zero logical dispatch Y workgroups. */
        uint32_t workgroups_y;
        /** @brief Non-zero logical dispatch Z workgroups. */
        uint32_t workgroups_z;
        /** @brief Host-owned manifest interface outputs; do not replace. */
        atlas_task_pack_gpu_buffer_v1* buffers;
        /** @brief Host-initialized exact buffer count; do not modify. */
        uint64_t buffer_count;
    } atlas_task_pack_gpu_preparation_v1;

    /** @brief Post-execution bytes downloaded from one requested result binding. */
    typedef struct atlas_task_pack_gpu_readback_v1
    {
        /** @brief Must equal `sizeof(atlas_task_pack_gpu_readback_v1)`. */
        uint32_t struct_size;
        /** @brief Manifest-declared result binding. */
        uint32_t binding;
        /** @brief Borrowed downloaded bytes valid only during summarize. */
        atlas_task_pack_byte_view bytes;
    } atlas_task_pack_gpu_readback_v1;

    /** @brief GPU preparation, result construction, and destruction callbacks. */
    typedef struct atlas_task_pack_gpu_callbacks_v1
    {
        /** @brief Must equal `sizeof(atlas_task_pack_gpu_callbacks_v1)`. */
        uint32_t struct_size;
        /** @brief Creates one context and declarative GPU preparation output. */
        atlas_task_pack_status (*prepare)(atlas_task_pack_string_view task_id, const atlas_task_pack_node_input* input,
                                          void** node_context, atlas_task_pack_gpu_preparation_v1* preparation,
                                          atlas_task_pack_writer* error);
        /** @brief Converts host-provided readbacks into bounded summary JSON. */
        atlas_task_pack_status (*summarize)(void* node_context, const atlas_task_pack_gpu_readback_v1* readbacks,
                                            uint64_t readback_count, atlas_task_pack_writer* summary, atlas_task_pack_writer* error);
        /** @brief Releases a non-null context; must not throw or fail. */
        void (*destroy)(void* node_context);
    } atlas_task_pack_gpu_callbacks_v1;

    /** @brief Complete version-one callback table returned by the known entry point. */
    typedef struct atlas_task_pack_api_v1
    {
        /** @brief Must equal `sizeof(atlas_task_pack_api_v1)`. */
        uint32_t struct_size;
        /** @brief Must equal `ATLAS_TASK_PACK_ABI_VERSION`. */
        uint32_t abi_version;
        /** @brief Common metadata callback table. */
        atlas_task_pack_common_callbacks_v1 common;
        /** @brief CPU callback table, required when CPU tasks are declared. */
        atlas_task_pack_cpu_callbacks_v1 cpu;
        /** @brief GPU callback table, required when GPU tasks are declared. */
        atlas_task_pack_gpu_callbacks_v1 gpu;
    } atlas_task_pack_api_v1;

    /**
     * @brief Signature of the exported `atlas_task_pack_get_api` entry point.
     * @return An immutable module-static table valid until the library unloads,
     * or null when the requested ABI is unsupported.
     */
    typedef const atlas_task_pack_api_v1* (*atlas_task_pack_get_api_fn)(uint32_t requested_abi_version);

#ifdef __cplusplus
}
#endif

#endif // !ATLAS_EXTENSION_TASK_PACK_ABI
