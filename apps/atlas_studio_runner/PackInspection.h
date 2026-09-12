#ifndef ATLAS_STUDIO_PACK_INSPECTION
#define ATLAS_STUDIO_PACK_INSPECTION

#include "atlas/Extension/TaskPack.h"

#include <nlohmann/json.hpp>

namespace Atlas::Studio
{
    /** @brief Serializes validated descriptors only; never invokes native pack callbacks. */
    inline nlohmann::json inspectJson(const TaskPackManifest& pack)
    {
        using Json = nlohmann::json;
        auto fields = [](const std::vector<TaskPackFieldDescriptor>& descriptors)
        {
            Json result = Json::array();
            for (const auto& field : descriptors)
            {
                const char* names[] = { "boolean", "integer", "unsigned_integer", "number", "string", "enum" };
                Json value = { { "id", field.id },
                               { "name", field.displayName },
                               { "description", field.description },
                               { "type", names[static_cast<unsigned>(field.type)] },
                               { "required", field.required } };
                if (field.defaultValue)
                    std::visit([&](const auto& scalar) { value["default"] = scalar; }, *field.defaultValue);
                if (field.minimumSigned)
                    value["minimum"] = *field.minimumSigned;
                if (field.maximumSigned)
                    value["maximum"] = *field.maximumSigned;
                if (field.minimumUnsigned)
                    value["minimum"] = *field.minimumUnsigned;
                if (field.maximumUnsigned)
                    value["maximum"] = *field.maximumUnsigned;
                if (field.minimumNumber)
                    value["minimum"] = *field.minimumNumber;
                if (field.maximumNumber)
                    value["maximum"] = *field.maximumNumber;
                if (field.maximumLength)
                    value["max_length"] = *field.maximumLength;
                if (!field.enumValues.empty())
                    value["values"] = field.enumValues;
                result.push_back(std::move(value));
            }
            return result;
        };
        Json result = {
            { "inspection_schema_version", 1 }, { "pack_id", pack.packId },     { "version", pack.version },
            { "digest", pack.digest },          { "name", pack.displayName },   { "description", pack.description },
            { "tasks", Json::array() },         { "platforms", Json::array() }, { "files", Json::array({ "manifest.json" }) }
        };
        for (const auto& binary : pack.platformBinaries)
        {
            result["platforms"].push_back({ { "platform", binary.platform }, { "architecture", binary.architecture } });
            result["files"].push_back(binary.libraryPath.generic_string());
        }
        for (const auto& shader : pack.shaders)
            result["files"].push_back(shader.assetPath.generic_string());
        for (const auto& task : pack.tasks)
            result["tasks"].push_back({ { "pack_id", pack.packId },
                                        { "task_id", task.taskId },
                                        { "name", task.displayName },
                                        { "description", task.description },
                                        { "resource", task.resource == ExecutionResource::CPU ? "cpu" : "gpu" },
                                        { "supports_slicing", task.supportsSlicing },
                                        { "parameters", fields(task.parameters) },
                                        { "summaries", fields(task.summaries) } });
        return result;
    }
} // namespace Atlas::Studio
#endif
