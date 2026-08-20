#ifndef ATLAS_EXECUTION_RESOURCE
#define ATLAS_EXECUTION_RESOURCE

#include <cstdint>

/**
 * @file ExecutionResource.h
 * @brief Declares backend-neutral execution-resource classifications.
 */

namespace Atlas
{
    /**
     * @ingroup tasking
     * @brief Identifies the resource on which a task is intended to execute.
     *
     * This classification expresses task intent only. It does not contain
     * backend objects and does not currently cause KahnScheduler to select a CPU
     * or GPU backend. All task callables are sent to its injected CPU executor.
     */
    enum class ExecutionResource : std::uint8_t
    {
        CPU, ///< The task is intended for a CPU execution backend.
        GPU, ///< The task is intended for a GPU execution backend.
    };

    /**
     * @brief Reports whether a value identifies a supported execution resource.
     * @param resource The execution-resource value to validate.
     * @return True for CPU and GPU; false for out-of-range values.
     */
    constexpr bool isValidExecutionResource(ExecutionResource resource) noexcept
    {
        switch (resource)
        {
        case ExecutionResource::CPU:
        case ExecutionResource::GPU:
            return true;
        default:
            return false;
        }
    }
} // namespace Atlas

#endif // !ATLAS_EXECUTION_RESOURCE
