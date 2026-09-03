#include "TaskPackInternal.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>

namespace Atlas::Detail
{
    namespace
    {
        atlas_task_pack_status appendBytes(void* userData, const atlas_task_pack_byte_view bytes) noexcept
        {
            if (userData == nullptr)
            {
                return ATLAS_TASK_PACK_STATUS_ERROR;
            }
            auto& output{ *static_cast<WriterBuffer*>(userData) };
            if ((bytes.data == nullptr && bytes.size != 0U) || bytes.size > std::numeric_limits<std::size_t>::max())
            {
                output.exceeded = true;
                return ATLAS_TASK_PACK_STATUS_ERROR;
            }
            const std::size_t count{ static_cast<std::size_t>(bytes.size) };
            if (count == 0U)
            {
                return ATLAS_TASK_PACK_STATUS_OK;
            }
            if (count > output.limit - std::min(output.limit, output.bytes.size()))
            {
                output.exceeded = true;
                return ATLAS_TASK_PACK_STATUS_ERROR;
            }
            if (output.aggregateBytes != nullptr &&
                count > output.aggregateLimit - std::min(output.aggregateLimit, *output.aggregateBytes))
            {
                output.exceeded = true;
                return ATLAS_TASK_PACK_STATUS_ERROR;
            }
            try
            {
                output.bytes.append(reinterpret_cast<const char*>(bytes.data), count);
                if (output.aggregateBytes != nullptr)
                {
                    *output.aggregateBytes += count;
                }
                return ATLAS_TASK_PACK_STATUS_OK;
            }
            catch (...)
            {
                output.exceeded = true;
                return ATLAS_TASK_PACK_STATUS_ERROR;
            }
        }
    } // namespace

    NativeModule::NativeModule(const std::filesystem::path& path)
    {
#if defined(_WIN32)
        handle = static_cast<void*>(LoadLibraryW(path.c_str()));
        if (handle == nullptr)
        {
            throw std::runtime_error{ "Unable to load trusted task-pack library: " + path.string() };
        }
#else
        handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (handle == nullptr)
        {
            const char* const message{ dlerror() };
            throw std::runtime_error{ "Unable to load trusted task-pack library: " +
                                      (message == nullptr ? path.string() : std::string{ message }) };
        }
#endif
    }

    NativeModule::~NativeModule()
    {
        if (handle == nullptr)
        {
            return;
        }
#if defined(_WIN32)
        FreeLibrary(static_cast<HMODULE>(handle));
#else
        dlclose(handle);
#endif
    }

    atlas_task_pack_get_api_fn NativeModule::entryPoint() const
    {
#if defined(_WIN32)
        const FARPROC address{ GetProcAddress(static_cast<HMODULE>(handle), ATLAS_TASK_PACK_ENTRY_POINT) };
        if (address == nullptr)
        {
            throw std::runtime_error{ "Task-pack library does not export " ATLAS_TASK_PACK_ENTRY_POINT };
        }
        atlas_task_pack_get_api_fn result{ nullptr };
        static_assert(sizeof(result) == sizeof(address));
        std::memcpy(&result, &address, sizeof(result));
#else
        dlerror();
        void* const address{ dlsym(handle, ATLAS_TASK_PACK_ENTRY_POINT) };
        const char* const error{ dlerror() };
        if (error != nullptr || address == nullptr)
        {
            throw std::runtime_error{ "Task-pack library does not export " ATLAS_TASK_PACK_ENTRY_POINT };
        }
        atlas_task_pack_get_api_fn result{ nullptr };
        static_assert(sizeof(result) == sizeof(address));
        std::memcpy(&result, &address, sizeof(result));
#endif
        return result;
    }

    atlas_task_pack_writer makeWriter(WriterBuffer& buffer) noexcept
    {
        return atlas_task_pack_writer{ sizeof(atlas_task_pack_writer), &buffer, &appendBytes };
    }

    atlas_task_pack_string_view stringView(const std::string_view value) noexcept
    {
        return atlas_task_pack_string_view{ value.data(), static_cast<std::uint64_t>(value.size()) };
    }

    void requireValidStatus(const atlas_task_pack_status status, const std::string_view operation)
    {
        if (status != ATLAS_TASK_PACK_STATUS_OK && status != ATLAS_TASK_PACK_STATUS_ERROR)
        {
            throw std::runtime_error{ "Task-pack callback returned an invalid status while " + std::string{ operation } };
        }
    }

    std::runtime_error callbackError(const std::string_view operation, const WriterBuffer& error)
    {
        if (error.exceeded)
        {
            return std::runtime_error{ "Task-pack error output exceeded its bound while " + std::string{ operation } };
        }
        return std::runtime_error{ "Task-pack callback failed while " + std::string{ operation } +
                                   (error.bytes.empty() ? std::string{} : ": " + error.bytes) };
    }

    std::string hostPlatform()
    {
#if defined(_WIN32)
        return "windows";
#elif defined(__linux__)
        return "linux";
#else
        return "unsupported";
#endif
    }

    std::string hostArchitecture()
    {
#if defined(_M_X64) || defined(__x86_64__)
        return "x86_64";
#elif defined(_M_ARM64) || defined(__aarch64__)
        return "aarch64";
#else
        return "unsupported";
#endif
    }
} // namespace Atlas::Detail
