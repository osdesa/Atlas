#include "PackSnapshots.h"

#include <array>
#include <cerrno>
#include <fstream>
#include <random>
#include <set>
#include <stdexcept>
#if !defined(_WIN32)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#else
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Atlas::Studio
{
#if !defined(_WIN32)
    namespace
    {
        /** @brief Owns a no-follow source descriptor, including on exceptional copy exits. */
        struct SourceFile
        {
            explicit SourceFile(const int value) : fd{ value }
            {
                if (fd < 0)
                    throw std::runtime_error{ "Unable to open regular snapshot source" };
            }
            ~SourceFile()
            {
                ::close(fd);
            }
            SourceFile(const SourceFile&) = delete;
            SourceFile& operator=(const SourceFile&) = delete;
            int fd;
        };
    } // namespace
#endif
    PackSnapshots::PackSnapshots()
    {
        std::random_device random;
        for (unsigned attempt = 0; attempt < 100U; ++attempt)
        {
            const auto candidate =
                std::filesystem::temp_directory_path() / ("atlas-runner-" + std::to_string(random()) + "-" + std::to_string(random()));
#if defined(_WIN32)
            // The per-user Windows temporary directory supplies the inherited user ACL.
            if (!std::filesystem::create_directory(candidate))
                continue;
#else
            if (::mkdir(candidate.c_str(), 0700) != 0)
                continue;
#endif
            root = candidate;
            return;
        }
        throw std::runtime_error{ "Unable to create private task-pack snapshot directory" };
    }

    PackSnapshots::~PackSnapshots()
    {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
    }

    std::filesystem::path PackSnapshots::copy(const TaskPackManifest& inspected, const TaskPackRegistry& registry)
    {
        if (registry.inspectDirectory(inspected.sourceDirectory).digest != inspected.digest)
            throw std::runtime_error{ "Task pack changed after inspection" };
        const auto destination = root / inspected.digest;
        if (!std::filesystem::create_directory(destination))
            throw std::runtime_error{ "Duplicate task-pack snapshot" };
        std::set<std::filesystem::path> files{ "manifest.json" };
        for (const auto& binary : inspected.platformBinaries)
            files.insert(binary.libraryPath);
        for (const auto& shader : inspected.shaders)
            files.insert(shader.assetPath);
        std::size_t total = 0;
        for (const auto& relative : files)
        {
            auto source = inspected.sourceDirectory;
            for (const auto& component : relative)
            {
                source /= component;
                if (std::filesystem::is_symlink(std::filesystem::symlink_status(source)))
                    throw std::runtime_error{ "Task-pack snapshot source became a symlink" };
            }
            if (!std::filesystem::is_regular_file(std::filesystem::symlink_status(source)))
                throw std::runtime_error{ "Task-pack snapshot source is not a regular file" };
            const auto target = destination / relative;
            std::filesystem::create_directories(target.parent_path());
            std::ofstream output{ target, std::ios::binary };
            if (!output)
                throw std::runtime_error{ "Unable to open task-pack snapshot file" };
            std::array<char, 65536> bytes{};
#if !defined(_WIN32)
            SourceFile parent{ ::open(inspected.sourceDirectory.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC) };
            for (const auto& component : relative.parent_path())
            {
                const int next = ::openat(parent.fd, component.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
                if (next < 0)
                    throw std::runtime_error{ "Snapshot source directory changed" };
                ::close(parent.fd);
                parent.fd = next;
            }
            SourceFile input{ ::openat(parent.fd, relative.filename().c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC) };
            struct stat status{};
            if (::fstat(input.fd, &status) != 0 || !S_ISREG(status.st_mode))
                throw std::runtime_error{ "Snapshot source changed to a special file" };
            for (;;)
            {
                const auto count = ::read(input.fd, bytes.data(), bytes.size());
                if (count < 0 && errno == EINTR)
                    continue;
                if (count < 0)
                    throw std::runtime_error{ "Snapshot source read failed" };
                if (count == 0)
                    break;
                total += static_cast<std::size_t>(count);
                if (total > 256U * 1024U * 1024U)
                    throw std::runtime_error{ "Task-pack snapshot exceeds size bound" };
                output.write(bytes.data(), static_cast<std::streamsize>(count));
            }
#else
            // Deny replacement/writes while reading, and open reparse points themselves rather than following them.
            const HANDLE raw = CreateFileW(source.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                           FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
            if (raw == INVALID_HANDLE_VALUE)
                throw std::runtime_error{ "Unable to open task-pack snapshot file" };
            struct HandleOwner
            {
                HANDLE value;
                ~HandleOwner()
                {
                    CloseHandle(value);
                }
            } input{ raw };
            BY_HANDLE_FILE_INFORMATION information{};
            if (!GetFileInformationByHandle(input.value, &information) || GetFileType(input.value) != FILE_TYPE_DISK ||
                (information.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) != 0)
                throw std::runtime_error{ "Snapshot source changed to a special file" };
            for (;;)
            {
                DWORD count = 0;
                if (!ReadFile(input.value, bytes.data(), static_cast<DWORD>(bytes.size()), &count, nullptr))
                    throw std::runtime_error{ "Task-pack snapshot read failed" };
                if (count == 0)
                    break;
                total += count;
                if (total > 256U * 1024U * 1024U)
                    throw std::runtime_error{ "Task-pack snapshot exceeds size bound" };
                output.write(bytes.data(), static_cast<std::streamsize>(count));
            }
#endif
            output.close();
            if (!output)
                throw std::runtime_error{ "Task-pack snapshot copy failed" };
        }
        verify(inspected.digest, registry);
        return destination;
    }

    void PackSnapshots::verify(const std::string& digest, const TaskPackRegistry& registry) const
    {
        if (digest.size() != 64U || digest.find_first_not_of("0123456789abcdef") != std::string::npos ||
            registry.inspectDirectory(root / digest).digest != digest)
            throw std::runtime_error{ "Task-pack snapshot digest mismatch after copy" };
    }
} // namespace Atlas::Studio
