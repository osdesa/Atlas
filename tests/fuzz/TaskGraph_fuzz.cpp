#include "atlas/Tasking/TaskGraph.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, const std::size_t size)
{
    if (size == 0U)
    {
        return 0;
    }

    const std::span<const std::uint8_t> input{ data, size };
    Atlas::TaskGraph graph;
    const std::size_t taskCount{ static_cast<std::size_t>(input.front() % 64U) + 1U };
    std::vector<Atlas::TaskHandle> handles;
    handles.reserve(taskCount);
    for (std::size_t index{ 0U }; index < taskCount; ++index)
    {
        const auto handle{ graph.addCpuTask([] {}) };
        if (!handle.has_value())
        {
            return 0;
        }
        handles.emplace_back(handle.value());
    }

    for (std::size_t offset{ 1U }; offset + 2U < input.size(); offset += 3U)
    {
        const std::size_t dependent{ input[offset] % taskCount };
        const std::size_t dependency{ input[offset + 1U] % taskCount };
        if ((input[offset + 2U] & 1U) == 0U)
        {
            graph.addDependency(handles[dependent], handles[dependency]);
        }
        else
        {
            graph.removeDependency(handles[dependent], handles[dependency]);
        }
    }

    if (graph.finishTaskGraph())
    {
        for (const Atlas::TaskHandle handle : handles)
        {
            const auto snapshot{ graph.snapshotTask(handle) };
            if (!snapshot.has_value() || snapshot->handle != handle)
            {
                __builtin_trap();
            }
        }
    }
    return 0;
}
