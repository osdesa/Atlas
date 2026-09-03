#include "TaskPackInternal.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace Atlas::Detail
{
    namespace
    {
        using Json = nlohmann::json;
        constexpr std::size_t maximumManifestBytes{ 1024U * 1024U };
        constexpr std::uintmax_t maximumPackBytes{ 256U * 1024U * 1024U };
        constexpr std::size_t maximumPackFiles{ 128U };
        constexpr std::uintmax_t maximumShaderBytes{ 16U * 1024U * 1024U };
        constexpr std::uintmax_t maximumLibraryBytes{ 128U * 1024U * 1024U };
        constexpr std::size_t maximumFields{ 128U };
        constexpr std::size_t maximumTasks{ 256U };
        constexpr std::size_t maximumStringBytes{ 4096U };

        class Sha256 final
        {
          public:
            void update(const void* data, std::size_t size) noexcept
            {
                const auto* bytes{ static_cast<const std::uint8_t*>(data) };
                totalBytes += size;
                while (size != 0U)
                {
                    const std::size_t copied{ std::min(size, block.size() - blockSize) };
                    std::memcpy(block.data() + blockSize, bytes, copied);
                    blockSize += copied;
                    bytes += copied;
                    size -= copied;
                    if (blockSize == block.size())
                    {
                        transform(block.data());
                        blockSize = 0U;
                    }
                }
            }

            std::string finish() noexcept
            {
                const std::uint64_t bitCount{ static_cast<std::uint64_t>(totalBytes) * 8U };
                block.at(blockSize++) = 0x80U;
                if (blockSize > 56U)
                {
                    std::fill(block.begin() + static_cast<std::ptrdiff_t>(blockSize), block.end(), 0U);
                    transform(block.data());
                    blockSize = 0U;
                }
                std::fill(block.begin() + static_cast<std::ptrdiff_t>(blockSize), block.begin() + 56, 0U);
                for (std::size_t index{ 0U }; index < sizeof(bitCount); ++index)
                {
                    block.at(63U - index) = static_cast<std::uint8_t>(bitCount >> (index * 8U));
                }
                transform(block.data());

                std::ostringstream result;
                result << std::hex << std::setfill('0');
                for (const std::uint32_t value : state)
                {
                    result << std::setw(8) << value;
                }
                return result.str();
            }

          private:
            static constexpr std::array<std::uint32_t, 64U> constants{
                0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
                0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
                0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
                0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
                0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
                0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
                0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
                0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U
            };

            void transform(const std::uint8_t* bytes) noexcept
            {
                std::array<std::uint32_t, 64U> words{};
                for (std::size_t index{ 0U }; index < 16U; ++index)
                {
                    words.at(index) = static_cast<std::uint32_t>(bytes[index * 4U]) << 24U |
                                      static_cast<std::uint32_t>(bytes[index * 4U + 1U]) << 16U |
                                      static_cast<std::uint32_t>(bytes[index * 4U + 2U]) << 8U |
                                      static_cast<std::uint32_t>(bytes[index * 4U + 3U]);
                }
                for (std::size_t index{ 16U }; index < words.size(); ++index)
                {
                    const std::uint32_t first{ std::rotr(words.at(index - 15U), 7) ^ std::rotr(words.at(index - 15U), 18) ^
                                               (words.at(index - 15U) >> 3U) };
                    const std::uint32_t second{ std::rotr(words.at(index - 2U), 17) ^ std::rotr(words.at(index - 2U), 19) ^
                                                (words.at(index - 2U) >> 10U) };
                    words.at(index) = words.at(index - 16U) + first + words.at(index - 7U) + second;
                }

                std::uint32_t a{ state.at(0U) }, b{ state.at(1U) }, c{ state.at(2U) }, d{ state.at(3U) };
                std::uint32_t e{ state.at(4U) }, f{ state.at(5U) }, g{ state.at(6U) }, h{ state.at(7U) };
                for (std::size_t index{ 0U }; index < words.size(); ++index)
                {
                    const std::uint32_t choose{ (e & f) ^ (~e & g) };
                    const std::uint32_t majority{ (a & b) ^ (a & c) ^ (b & c) };
                    const std::uint32_t sumOne{ std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25) };
                    const std::uint32_t sumZero{ std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22) };
                    const std::uint32_t first{ h + sumOne + choose + constants.at(index) + words.at(index) };
                    const std::uint32_t second{ sumZero + majority };
                    h = g;
                    g = f;
                    f = e;
                    e = d + first;
                    d = c;
                    c = b;
                    b = a;
                    a = first + second;
                }
                state.at(0U) += a;
                state.at(1U) += b;
                state.at(2U) += c;
                state.at(3U) += d;
                state.at(4U) += e;
                state.at(5U) += f;
                state.at(6U) += g;
                state.at(7U) += h;
            }

            std::array<std::uint32_t, 8U> state{ 0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
                                                 0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U };
            std::array<std::uint8_t, 64U> block{};
            std::size_t blockSize{ 0U };
            std::size_t totalBytes{ 0U };
        };

        void requireKeys(const Json& object, const std::set<std::string>& allowed, const std::string_view location)
        {
            if (!object.is_object())
            {
                throw std::invalid_argument{ std::string{ location } + " must be an object" };
            }
            for (const auto& [key, value] : object.items())
            {
                static_cast<void>(value);
                if (!allowed.contains(key))
                {
                    throw std::invalid_argument{ std::string{ location } + " contains unknown field '" + key + "'" };
                }
            }
        }

        std::string requiredString(const Json& object, const char* key, const std::string_view location,
                                   const std::size_t maximum = maximumStringBytes)
        {
            if (!object.contains(key) || !object.at(key).is_string())
            {
                throw std::invalid_argument{ std::string{ location } + "." + key + " must be a string" };
            }
            const std::string value{ object.at(key).get<std::string>() };
            if (value.empty() || value.size() > maximum || value.find('\0') != std::string::npos)
            {
                throw std::invalid_argument{ std::string{ location } + "." + key + " is empty or too long" };
            }
            return value;
        }

        std::string optionalString(const Json& object, const char* key, const std::string_view location)
        {
            if (!object.contains(key))
            {
                return {};
            }
            return requiredString(object, key, location);
        }

        void requireIdentifier(const std::string_view identifier, const std::string_view location)
        {
            if (identifier.size() > 128U ||
                !std::all_of(identifier.begin(), identifier.end(), [](const unsigned char value)
                             { return std::isalnum(value) != 0 || value == '.' || value == '_' || value == '-'; }))
            {
                throw std::invalid_argument{ std::string{ location } + " is not a portable identifier" };
            }
        }

        std::filesystem::path safeRelativePath(const std::string& text, const std::string_view location)
        {
            if (text.find('\\') != std::string::npos || text.find(':') != std::string::npos)
            {
                throw std::invalid_argument{ std::string{ location } + " is not a portable forward-slash path" };
            }
            const std::filesystem::path value{ text };
            if (value.empty() || value.is_absolute() || value.has_root_name())
            {
                throw std::invalid_argument{ std::string{ location } + " must be a relative path" };
            }
            for (const auto& component : value)
            {
                if (component == "." || component == "..")
                {
                    throw std::invalid_argument{ std::string{ location } + " contains traversal" };
                }
            }
            return value.lexically_normal();
        }

        std::vector<std::byte> readBoundedFile(const std::filesystem::path& path, const std::uintmax_t maximum)
        {
            std::error_code error;
            const auto status{ std::filesystem::symlink_status(path, error) };
            if (error || !std::filesystem::is_regular_file(status) || std::filesystem::is_symlink(status))
            {
                throw std::invalid_argument{ "Task-pack referenced file is missing, non-regular, or a symlink: " + path.string() };
            }
            const std::uintmax_t size{ std::filesystem::file_size(path, error) };
            if (error || size == 0U || size > maximum || size > std::numeric_limits<std::size_t>::max())
            {
                throw std::invalid_argument{ "Task-pack referenced file is empty or oversized: " + path.string() };
            }
            std::ifstream stream{ path, std::ios::binary };
            std::vector<std::byte> bytes(static_cast<std::size_t>(size));
            stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            if (!stream || stream.peek() != std::ifstream::traits_type::eof())
            {
                throw std::runtime_error{ "Unable to read task-pack file: " + path.string() };
            }
            return bytes;
        }

        BufferAccess parseAccess(const std::string& text, const std::string_view location)
        {
            if (text == "read_only")
            {
                return BufferAccess::ReadOnly;
            }
            if (text == "write_only")
            {
                return BufferAccess::WriteOnly;
            }
            if (text == "read_write")
            {
                return BufferAccess::ReadWrite;
            }
            throw std::invalid_argument{ std::string{ location } + " has an invalid buffer access" };
        }

        std::uint32_t unsigned32(const Json& value, const std::string_view location)
        {
            if (!value.is_number_unsigned())
            {
                throw std::invalid_argument{ std::string{ location } + " must be an unsigned 32-bit integer" };
            }
            const std::uint64_t result{ value.get<std::uint64_t>() };
            if (result > std::numeric_limits<std::uint32_t>::max())
            {
                throw std::invalid_argument{ std::string{ location } + " exceeds the unsigned 32-bit range" };
            }
            return static_cast<std::uint32_t>(result);
        }

        std::optional<std::int64_t> signed64(const Json& value)
        {
            if (!value.is_number_integer())
            {
                return std::nullopt;
            }
            if (value.is_number_unsigned())
            {
                const std::uint64_t result{ value.get<std::uint64_t>() };
                if (result > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
                {
                    return std::nullopt;
                }
                return static_cast<std::int64_t>(result);
            }
            return value.get<std::int64_t>();
        }

        TaskPackScalar scalarForField(const TaskPackFieldDescriptor& field, const Json& value, const std::string_view location)
        {
            switch (field.type)
            {
            case TaskPackFieldType::Boolean:
                if (!value.is_boolean())
                {
                    break;
                }
                return value.get<bool>();
            case TaskPackFieldType::SignedInteger:
                if (const std::optional<std::int64_t> result{ signed64(value) }; result.has_value())
                {
                    if ((!field.minimumSigned || *result >= *field.minimumSigned) &&
                        (!field.maximumSigned || *result <= *field.maximumSigned))
                    {
                        return *result;
                    }
                }
                break;
            case TaskPackFieldType::UnsignedInteger:
                if (value.is_number_unsigned())
                {
                    const std::uint64_t result{ value.get<std::uint64_t>() };
                    if ((!field.minimumUnsigned || result >= *field.minimumUnsigned) &&
                        (!field.maximumUnsigned || result <= *field.maximumUnsigned))
                    {
                        return result;
                    }
                }
                break;
            case TaskPackFieldType::Number:
                if (value.is_number())
                {
                    const double result{ value.get<double>() };
                    if (std::isfinite(result) && (!field.minimumNumber || result >= *field.minimumNumber) &&
                        (!field.maximumNumber || result <= *field.maximumNumber))
                    {
                        return result;
                    }
                }
                break;
            case TaskPackFieldType::String:
                if (value.is_string())
                {
                    std::string result{ value.get<std::string>() };
                    if (result.find('\0') == std::string::npos && (!field.maximumLength || result.size() <= *field.maximumLength))
                    {
                        return result;
                    }
                }
                break;
            case TaskPackFieldType::Enumeration:
                if (value.is_string())
                {
                    std::string result{ value.get<std::string>() };
                    if (std::find(field.enumValues.begin(), field.enumValues.end(), result) != field.enumValues.end())
                    {
                        return result;
                    }
                }
                break;
            }
            throw std::invalid_argument{ std::string{ location } + " has the wrong type or is outside its declared bounds" };
        }

        Json jsonForScalar(const TaskPackScalar& value)
        {
            return std::visit([](const auto& scalar) { return Json(scalar); }, value);
        }

        TaskPackFieldDescriptor parseField(const Json& value, const std::string_view location)
        {
            requireKeys(value,
                        { "id", "name", "description", "type", "required", "default", "minimum", "maximum", "max_length", "values" },
                        location);
            TaskPackFieldDescriptor field;
            field.id = requiredString(value, "id", location, 128U);
            requireIdentifier(field.id, std::string{ location } + ".id");
            field.displayName = value.contains("name") ? requiredString(value, "name", location) : field.id;
            field.description = optionalString(value, "description", location);
            if (value.contains("required") && !value.at("required").is_boolean())
            {
                throw std::invalid_argument{ std::string{ location } + ".required must be boolean" };
            }
            field.required = value.contains("required") ? value.at("required").get<bool>() : true;
            const std::string type{ requiredString(value, "type", location, 32U) };
            if (type == "boolean")
            {
                field.type = TaskPackFieldType::Boolean;
            }
            else if (type == "integer")
            {
                field.type = TaskPackFieldType::SignedInteger;
            }
            else if (type == "unsigned_integer")
            {
                field.type = TaskPackFieldType::UnsignedInteger;
            }
            else if (type == "number")
            {
                field.type = TaskPackFieldType::Number;
            }
            else if (type == "string")
            {
                field.type = TaskPackFieldType::String;
            }
            else if (type == "enum")
            {
                field.type = TaskPackFieldType::Enumeration;
            }
            else
            {
                throw std::invalid_argument{ std::string{ location } + ".type is unsupported" };
            }

            const bool hasMinimum{ value.contains("minimum") };
            const bool hasMaximum{ value.contains("maximum") };
            const bool hasMaximumLength{ value.contains("max_length") };
            const bool hasValues{ value.contains("values") };
            if ((field.type == TaskPackFieldType::Boolean && (hasMinimum || hasMaximum || hasMaximumLength || hasValues)) ||
                ((field.type == TaskPackFieldType::SignedInteger || field.type == TaskPackFieldType::UnsignedInteger ||
                  field.type == TaskPackFieldType::Number) &&
                 (hasMaximumLength || hasValues)) ||
                (field.type == TaskPackFieldType::String && (hasMinimum || hasMaximum || hasValues)) ||
                (field.type == TaskPackFieldType::Enumeration && (hasMinimum || hasMaximum || hasMaximumLength)))
            {
                throw std::invalid_argument{ std::string{ location } + " contains bounds that do not apply to its type" };
            }

            if (field.type == TaskPackFieldType::SignedInteger)
            {
                if (value.contains("minimum"))
                {
                    field.minimumSigned = signed64(value.at("minimum"));
                    if (!field.minimumSigned.has_value())
                    {
                        throw std::invalid_argument{ std::string{ location } + ".minimum must be a signed 64-bit integer" };
                    }
                }
                if (value.contains("maximum"))
                {
                    field.maximumSigned = signed64(value.at("maximum"));
                    if (!field.maximumSigned.has_value())
                    {
                        throw std::invalid_argument{ std::string{ location } + ".maximum must be a signed 64-bit integer" };
                    }
                }
                if (field.minimumSigned && field.maximumSigned && *field.minimumSigned > *field.maximumSigned)
                {
                    throw std::invalid_argument{ std::string{ location } + " has reversed bounds" };
                }
            }
            else if (field.type == TaskPackFieldType::UnsignedInteger)
            {
                if (value.contains("minimum"))
                {
                    if (!value.at("minimum").is_number_unsigned())
                    {
                        throw std::invalid_argument{ std::string{ location } + ".minimum must be an unsigned integer" };
                    }
                    field.minimumUnsigned = value.at("minimum").get<std::uint64_t>();
                }
                if (value.contains("maximum"))
                {
                    if (!value.at("maximum").is_number_unsigned())
                    {
                        throw std::invalid_argument{ std::string{ location } + ".maximum must be an unsigned integer" };
                    }
                    field.maximumUnsigned = value.at("maximum").get<std::uint64_t>();
                }
                if (field.minimumUnsigned && field.maximumUnsigned && *field.minimumUnsigned > *field.maximumUnsigned)
                {
                    throw std::invalid_argument{ std::string{ location } + " has reversed bounds" };
                }
            }
            else if (field.type == TaskPackFieldType::Number)
            {
                if (value.contains("minimum"))
                {
                    if (!value.at("minimum").is_number())
                    {
                        throw std::invalid_argument{ std::string{ location } + ".minimum must be a number" };
                    }
                    field.minimumNumber = value.at("minimum").get<double>();
                }
                if (value.contains("maximum"))
                {
                    if (!value.at("maximum").is_number())
                    {
                        throw std::invalid_argument{ std::string{ location } + ".maximum must be a number" };
                    }
                    field.maximumNumber = value.at("maximum").get<double>();
                }
                if ((field.minimumNumber && !std::isfinite(*field.minimumNumber)) ||
                    (field.maximumNumber && !std::isfinite(*field.maximumNumber)) ||
                    (field.minimumNumber && field.maximumNumber && *field.minimumNumber > *field.maximumNumber))
                {
                    throw std::invalid_argument{ std::string{ location } + " has invalid number bounds" };
                }
            }
            else if (field.type == TaskPackFieldType::String)
            {
                if (!value.contains("max_length") || !value.at("max_length").is_number_unsigned())
                {
                    throw std::invalid_argument{ std::string{ location } + ".max_length is required for strings" };
                }
                const std::uint64_t maximum{ value.at("max_length").get<std::uint64_t>() };
                if (maximum == 0U || maximum > maximumStringBytes)
                {
                    throw std::invalid_argument{ std::string{ location } + ".max_length is invalid" };
                }
                field.maximumLength = static_cast<std::size_t>(maximum);
            }
            else if (field.type == TaskPackFieldType::Enumeration)
            {
                if (!value.contains("values") || !value.at("values").is_array() || value.at("values").empty() ||
                    value.at("values").size() > maximumFields)
                {
                    throw std::invalid_argument{ std::string{ location } + ".values must be a bounded non-empty array" };
                }
                for (const Json& entry : value.at("values"))
                {
                    if (!entry.is_string())
                    {
                        throw std::invalid_argument{ std::string{ location } + ".values entries must be strings" };
                    }
                    const std::string item{ entry.get<std::string>() };
                    if (item.empty() || item.size() > maximumStringBytes ||
                        std::find(field.enumValues.begin(), field.enumValues.end(), item) != field.enumValues.end())
                    {
                        throw std::invalid_argument{ std::string{ location } + ".values contains an invalid duplicate" };
                    }
                    field.enumValues.emplace_back(item);
                }
            }

            if (value.contains("default"))
            {
                field.defaultValue = scalarForField(field, value.at("default"), std::string{ location } + ".default");
            }
            if (!field.required && !field.defaultValue.has_value())
            {
                throw std::invalid_argument{ std::string{ location } + " must be required or provide a default" };
            }
            return field;
        }

        std::vector<TaskPackFieldDescriptor> parseFields(const Json& parent, const char* key, const std::string_view location)
        {
            if (!parent.contains(key) || !parent.at(key).is_array() || parent.at(key).size() > maximumFields)
            {
                throw std::invalid_argument{ std::string{ location } + "." + key + " must be a bounded array" };
            }
            std::vector<TaskPackFieldDescriptor> fields;
            std::set<std::string> ids;
            for (std::size_t index{ 0U }; index < parent.at(key).size(); ++index)
            {
                TaskPackFieldDescriptor field{ parseField(parent.at(key).at(index), std::string{ location } + "." + key + "[]") };
                if (!ids.emplace(field.id).second)
                {
                    throw std::invalid_argument{ std::string{ location } + "." + key + " contains a duplicate ID" };
                }
                fields.emplace_back(std::move(field));
            }
            return fields;
        }

        std::vector<std::pair<std::string, std::vector<std::byte>>> referencedFiles(const TaskPackManifest& manifest,
                                                                                    const std::vector<std::byte>& manifestBytes)
        {
            std::vector<std::pair<std::string, std::vector<std::byte>>> files;
            files.emplace_back("manifest.json", manifestBytes);
            std::set<std::filesystem::path> paths{ "manifest.json" };
            for (const TaskPackPlatformBinary& binary : manifest.platformBinaries)
            {
                if (!paths.emplace(binary.libraryPath).second)
                {
                    throw std::invalid_argument{ "Task-pack references the same file more than once" };
                }
                files.emplace_back(binary.libraryPath.generic_string(),
                                   readBoundedFile(manifest.sourceDirectory / binary.libraryPath, maximumLibraryBytes));
            }
            for (const TaskPackShaderDescriptor& shader : manifest.shaders)
            {
                if (!paths.emplace(shader.assetPath).second)
                {
                    throw std::invalid_argument{ "Task-pack references the same file more than once" };
                }
                files.emplace_back(shader.assetPath.generic_string(),
                                   readBoundedFile(manifest.sourceDirectory / shader.assetPath, maximumShaderBytes));
            }
            std::sort(files.begin(), files.end(), [](const auto& left, const auto& right) { return left.first < right.first; });
            return files;
        }

        std::string contentDigest(const std::vector<std::pair<std::string, std::vector<std::byte>>>& files)
        {
            Sha256 hash;
            constexpr std::string_view domain{ "atlas-task-pack-v1\0", 19U };
            hash.update(domain.data(), domain.size());
            const auto updateSize = [&hash](const std::uint64_t value)
            {
                std::array<std::uint8_t, 8U> bytes{};
                for (std::size_t index{ 0U }; index < bytes.size(); ++index)
                {
                    bytes.at(bytes.size() - index - 1U) = static_cast<std::uint8_t>(value >> (index * 8U));
                }
                hash.update(bytes.data(), bytes.size());
            };
            for (const auto& [name, bytes] : files)
            {
                const std::uint64_t nameSize{ name.size() };
                const std::uint64_t byteSize{ bytes.size() };
                updateSize(nameSize);
                hash.update(name.data(), name.size());
                updateSize(byteSize);
                hash.update(bytes.data(), bytes.size());
            }
            return hash.finish();
        }
    } // namespace

    TaskPackManifest inspectTaskPackDirectory(const std::filesystem::path& directory)
    {
        std::error_code error;
        const auto requestedStatus{ std::filesystem::symlink_status(directory, error) };
        if (error || std::filesystem::is_symlink(requestedStatus))
        {
            throw std::invalid_argument{ "Task-pack root must be a real directory, not a symlink" };
        }
        const std::filesystem::path canonicalDirectory{ std::filesystem::canonical(directory, error) };
        if (error || !std::filesystem::is_directory(canonicalDirectory))
        {
            throw std::invalid_argument{ "Task-pack path is not a readable directory" };
        }

        std::size_t fileCount{ 0U };
        std::uintmax_t totalBytes{ 0U };
        for (std::filesystem::recursive_directory_iterator iterator{ canonicalDirectory }, end; iterator != end; ++iterator)
        {
            const auto status{ iterator->symlink_status(error) };
            if (error || std::filesystem::is_symlink(status) ||
                (!std::filesystem::is_regular_file(status) && !std::filesystem::is_directory(status)))
            {
                throw std::invalid_argument{ "Task-pack directories may contain regular files and directories only" };
            }
            if (std::filesystem::is_regular_file(status))
            {
                ++fileCount;
                const std::uintmax_t fileBytes{ iterator->file_size(error) };
                if (error || fileBytes > maximumPackBytes - std::min(maximumPackBytes, totalBytes))
                {
                    throw std::invalid_argument{ "Task-pack directory exceeds file-count or total-size limits" };
                }
                totalBytes += fileBytes;
                if (fileCount > maximumPackFiles || totalBytes > maximumPackBytes)
                {
                    throw std::invalid_argument{ "Task-pack directory exceeds file-count or total-size limits" };
                }
            }
        }

        const std::vector<std::byte> manifestBytes{ readBoundedFile(canonicalDirectory / "manifest.json", maximumManifestBytes) };
        Json root;
        try
        {
            root = Json::parse(reinterpret_cast<const char*>(manifestBytes.data()),
                               reinterpret_cast<const char*>(manifestBytes.data() + manifestBytes.size()));
        }
        catch (const Json::exception& exception)
        {
            throw std::invalid_argument{ "Invalid task-pack manifest JSON: " + std::string{ exception.what() } };
        }
        requireKeys(root,
                    { "schema_version", "abi_version", "pack_id", "version", "name", "description", "platforms", "shaders", "tasks" },
                    "manifest");

        TaskPackManifest manifest;
        manifest.sourceDirectory = canonicalDirectory;
        if (!root.contains("schema_version") || unsigned32(root.at("schema_version"), "manifest.schema_version") != 1U)
        {
            throw std::invalid_argument{ "manifest.schema_version must be 1" };
        }
        if (!root.contains("abi_version") || unsigned32(root.at("abi_version"), "manifest.abi_version") != ATLAS_TASK_PACK_ABI_VERSION)
        {
            throw std::invalid_argument{ "manifest.abi_version is unsupported" };
        }
        manifest.packId = requiredString(root, "pack_id", "manifest", 128U);
        requireIdentifier(manifest.packId, "manifest.pack_id");
        manifest.version = requiredString(root, "version", "manifest", 128U);
        manifest.displayName = root.contains("name") ? requiredString(root, "name", "manifest") : manifest.packId;
        manifest.description = optionalString(root, "description", "manifest");

        if (!root.contains("platforms") || !root.at("platforms").is_array() || root.at("platforms").empty() ||
            root.at("platforms").size() > 16U)
        {
            throw std::invalid_argument{ "manifest.platforms must be a bounded non-empty array" };
        }
        std::set<std::pair<std::string, std::string>> triples;
        for (const Json& value : root.at("platforms"))
        {
            requireKeys(value, { "platform", "architecture", "library" }, "manifest.platforms[]");
            TaskPackPlatformBinary binary{ requiredString(value, "platform", "manifest.platforms[]", 32U),
                                           requiredString(value, "architecture", "manifest.platforms[]", 32U),
                                           safeRelativePath(requiredString(value, "library", "manifest.platforms[]"),
                                                            "manifest.platforms[].library") };
            if ((binary.platform != "linux" && binary.platform != "windows") ||
                (binary.architecture != "x86_64" && binary.architecture != "aarch64") ||
                !triples.emplace(binary.platform, binary.architecture).second)
            {
                throw std::invalid_argument{ "manifest.platforms contains an unsupported or duplicate host triple" };
            }
            if ((binary.platform == "linux" && binary.libraryPath.extension() != ".so") ||
                (binary.platform == "windows" && binary.libraryPath.extension() != ".dll"))
            {
                throw std::invalid_argument{ "manifest platform library has the wrong file extension" };
            }
            manifest.platformBinaries.emplace_back(std::move(binary));
        }

        if (!root.contains("shaders") || !root.at("shaders").is_array() || root.at("shaders").size() > maximumTasks)
        {
            throw std::invalid_argument{ "manifest.shaders must be a bounded array" };
        }
        std::set<std::string> shaderIds;
        for (const Json& value : root.at("shaders"))
        {
            requireKeys(value, { "shader_id", "path", "entry_point", "storage_buffers" }, "manifest.shaders[]");
            TaskPackShaderDescriptor shader;
            shader.shaderId = requiredString(value, "shader_id", "manifest.shaders[]", 128U);
            requireIdentifier(shader.shaderId, "manifest.shaders[].shader_id");
            if (!shaderIds.emplace(shader.shaderId).second)
            {
                throw std::invalid_argument{ "manifest.shaders contains a duplicate shader ID" };
            }
            shader.assetPath = safeRelativePath(requiredString(value, "path", "manifest.shaders[]"), "manifest.shaders[].path");
            if (shader.assetPath.extension() != ".spv")
            {
                throw std::invalid_argument{ "manifest shader assets must use the .spv extension" };
            }
            shader.entryPoint = requiredString(value, "entry_point", "manifest.shaders[]", 128U);
            if (!value.contains("storage_buffers") || !value.at("storage_buffers").is_array() || value.at("storage_buffers").empty() ||
                value.at("storage_buffers").size() > maximumTaskPackBuffers)
            {
                throw std::invalid_argument{ "manifest shader storage_buffers must be a bounded non-empty array" };
            }
            std::set<std::uint32_t> bindings;
            for (const Json& binding : value.at("storage_buffers"))
            {
                requireKeys(binding, { "binding", "access" }, "manifest.shaders[].storage_buffers[]");
                if (!binding.contains("binding"))
                {
                    throw std::invalid_argument{ "shader buffer binding must be unsigned" };
                }
                const std::uint32_t number{ unsigned32(binding.at("binding"), "shader buffer binding") };
                if (!bindings.emplace(number).second)
                {
                    throw std::invalid_argument{ "shader buffer bindings must be unique" };
                }
                shader.storageBuffers.emplace_back(ShaderBufferBinding{
                    number, parseAccess(requiredString(binding, "access", "shader buffer", 32U), "shader buffer") });
            }
            std::sort(shader.storageBuffers.begin(), shader.storageBuffers.end(),
                      [](const ShaderBufferBinding& left, const ShaderBufferBinding& right) { return left.binding < right.binding; });
            manifest.shaders.emplace_back(std::move(shader));
        }

        if (!root.contains("tasks") || !root.at("tasks").is_array() || root.at("tasks").empty() ||
            root.at("tasks").size() > maximumTasks)
        {
            throw std::invalid_argument{ "manifest.tasks must be a bounded non-empty array" };
        }
        std::set<std::string> taskIds;
        for (const Json& value : root.at("tasks"))
        {
            requireKeys(value,
                        { "task_id", "name", "description", "resource", "parameters", "summaries", "shader_id", "result_bindings",
                          "supports_slicing" },
                        "manifest.tasks[]");
            CustomTaskDescriptor task;
            task.packId = manifest.packId;
            task.packVersion = manifest.version;
            task.taskId = requiredString(value, "task_id", "manifest.tasks[]", 128U);
            requireIdentifier(task.taskId, "manifest.tasks[].task_id");
            if (!taskIds.emplace(task.taskId).second)
            {
                throw std::invalid_argument{ "manifest.tasks contains a duplicate task ID" };
            }
            task.displayName = value.contains("name") ? requiredString(value, "name", "manifest.tasks[]") : task.taskId;
            task.description = optionalString(value, "description", "manifest.tasks[]");
            const std::string resource{ requiredString(value, "resource", "manifest.tasks[]", 16U) };
            if (resource == "cpu")
            {
                task.resource = ExecutionResource::CPU;
            }
            else if (resource == "gpu")
            {
                task.resource = ExecutionResource::GPU;
            }
            else
            {
                throw std::invalid_argument{ "manifest task resource must be cpu or gpu" };
            }
            task.parameters = parseFields(value, "parameters", "manifest.tasks[]");
            task.summaries = parseFields(value, "summaries", "manifest.tasks[]");
            if (value.contains("supports_slicing") && !value.at("supports_slicing").is_boolean())
            {
                throw std::invalid_argument{ "manifest task supports_slicing must be boolean" };
            }
            task.supportsSlicing = value.contains("supports_slicing") && value.at("supports_slicing").get<bool>();

            if (task.resource == ExecutionResource::CPU)
            {
                if (value.contains("shader_id") || value.contains("result_bindings") || value.contains("supports_slicing"))
                {
                    throw std::invalid_argument{ "CPU manifest tasks cannot declare GPU fields" };
                }
            }
            else
            {
                task.shaderId = requiredString(value, "shader_id", "manifest.tasks[]", 128U);
                if (!shaderIds.contains(*task.shaderId))
                {
                    throw std::invalid_argument{ "GPU manifest task references an unknown shader" };
                }
                if (!value.contains("result_bindings") || !value.at("result_bindings").is_array() ||
                    value.at("result_bindings").size() > maximumTaskPackBuffers)
                {
                    throw std::invalid_argument{ "GPU result_bindings must be a bounded array" };
                }
                std::set<std::uint32_t> results;
                for (const Json& binding : value.at("result_bindings"))
                {
                    const std::uint32_t number{ unsigned32(binding, "GPU result binding") };
                    if (!results.emplace(number).second)
                    {
                        throw std::invalid_argument{ "GPU result bindings must be unique" };
                    }
                    task.resultBindings.emplace_back(number);
                }
                const auto shader{ std::find_if(manifest.shaders.begin(), manifest.shaders.end(),
                                                [&task](const TaskPackShaderDescriptor& candidate)
                                                { return candidate.shaderId == *task.shaderId; }) };
                for (const std::uint32_t resultBinding : task.resultBindings)
                {
                    const auto binding{ std::find_if(shader->storageBuffers.begin(), shader->storageBuffers.end(),
                                                     [resultBinding](const ShaderBufferBinding& candidate)
                                                     { return candidate.binding == resultBinding; }) };
                    if (binding == shader->storageBuffers.end() || binding->access == BufferAccess::ReadOnly)
                    {
                        throw std::invalid_argument{ "GPU result bindings must name writable shader storage buffers" };
                    }
                }
            }
            manifest.tasks.emplace_back(std::move(task));
        }

        const auto files{ referencedFiles(manifest, manifestBytes) };
        manifest.digest = contentDigest(files);
        for (CustomTaskDescriptor& task : manifest.tasks)
        {
            task.packDigest = manifest.digest;
        }
        return manifest;
    }

    std::string canonicalParameters(const CustomTaskDescriptor& descriptor, const std::string_view parameterJson)
    {
        if (parameterJson.size() > maximumTaskPackParameterBytes)
        {
            throw std::invalid_argument{ "Custom-task parameter JSON exceeds its bound" };
        }
        Json supplied;
        try
        {
            supplied = Json::parse(parameterJson);
        }
        catch (const Json::exception& exception)
        {
            throw std::invalid_argument{ "Invalid custom-task parameter JSON: " + std::string{ exception.what() } };
        }
        if (!supplied.is_object())
        {
            throw std::invalid_argument{ "Custom-task parameters must be one flat JSON object" };
        }
        Json canonical = Json::object();
        std::set<std::string> expected;
        for (const TaskPackFieldDescriptor& field : descriptor.parameters)
        {
            expected.emplace(field.id);
            if (supplied.contains(field.id))
            {
                canonical[field.id] = jsonForScalar(scalarForField(field, supplied.at(field.id), "parameter '" + field.id + "'"));
            }
            else if (field.defaultValue)
            {
                canonical[field.id] = jsonForScalar(*field.defaultValue);
            }
            else
            {
                throw std::invalid_argument{ "Missing required custom-task parameter '" + field.id + "'" };
            }
        }
        for (const auto& [key, value] : supplied.items())
        {
            static_cast<void>(value);
            if (!expected.contains(key))
            {
                throw std::invalid_argument{ "Unknown custom-task parameter '" + key + "'" };
            }
        }
        return canonical.dump();
    }

    CustomTaskSummary validateSummary(const CustomTaskDescriptor& descriptor, const std::string_view summaryJson)
    {
        Json supplied;
        try
        {
            supplied = summaryJson.empty() ? Json::object() : Json::parse(summaryJson);
        }
        catch (const Json::exception& exception)
        {
            throw std::runtime_error{ "Invalid custom-task summary JSON: " + std::string{ exception.what() } };
        }
        if (!supplied.is_object())
        {
            throw std::runtime_error{ "Custom-task summary must be one flat JSON object" };
        }
        CustomTaskSummary summary;
        Json canonical = Json::object();
        std::set<std::string> expected;
        try
        {
            for (const TaskPackFieldDescriptor& field : descriptor.summaries)
            {
                expected.emplace(field.id);
                if (supplied.contains(field.id))
                {
                    TaskPackScalar scalar{ scalarForField(field, supplied.at(field.id), "summary '" + field.id + "'") };
                    canonical[field.id] = jsonForScalar(scalar);
                    summary.fields.emplace_back(field.id, std::move(scalar));
                }
                else if (field.defaultValue)
                {
                    canonical[field.id] = jsonForScalar(*field.defaultValue);
                    summary.fields.emplace_back(field.id, *field.defaultValue);
                }
                else
                {
                    throw std::invalid_argument{ "Missing required custom-task summary '" + field.id + "'" };
                }
            }
            for (const auto& [key, value] : supplied.items())
            {
                static_cast<void>(value);
                if (!expected.contains(key))
                {
                    throw std::invalid_argument{ "Unknown custom-task summary '" + key + "'" };
                }
            }
        }
        catch (const std::invalid_argument& exception)
        {
            throw std::runtime_error{ exception.what() };
        }
        summary.canonicalJson = canonical.dump();
        if (summary.canonicalJson.size() > maximumTaskPackSummaryBytes)
        {
            throw std::runtime_error{ "Canonical custom-task summary exceeds its bound" };
        }
        return summary;
    }

    std::vector<std::uint32_t> readSpirvAsset(const std::filesystem::path& path)
    {
        const std::vector<std::byte> bytes{ readBoundedFile(path, maximumShaderBytes) };
        if (bytes.size() < 5U * sizeof(std::uint32_t) || bytes.size() % sizeof(std::uint32_t) != 0U)
        {
            throw std::invalid_argument{ "SPIR-V asset is too small or not word-aligned" };
        }
        std::vector<std::uint32_t> words(bytes.size() / sizeof(std::uint32_t));
        std::memcpy(words.data(), bytes.data(), bytes.size());
        return words;
    }
} // namespace Atlas::Detail

namespace Atlas
{
    std::string CustomTaskDescriptor::qualifiedId() const
    {
        return packId + "/" + taskId;
    }
} // namespace Atlas
