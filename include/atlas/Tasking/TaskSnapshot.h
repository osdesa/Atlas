#ifndef ATLAS_TASK_SNAPSHOT
#define ATLAS_TASK_SNAPSHOT

#include "TaskExecutionInfo.h"
#include "TaskHandle.h"
#include "TaskOptions.h"

#include <vector>

/** @file TaskSnapshot.h @brief Declares the immutable public task-inspection value. */

namespace Atlas
{
    /**
     * @ingroup tasking
     * @brief A point-in-time copy of graph-owned task metadata and execution state.
     *
     * The graph retains payloads and mutable records. A snapshot never aliases
     * graph storage and is therefore safe to retain after its graph is destroyed.
     */
    struct TaskSnapshot final
    {
        /// @brief Graph-scoped identity of the observed task.
        TaskHandle handle;
        /// @brief Immutable task metadata.
        TaskOptions options;
        /// @brief State and measurements at snapshot creation time.
        TaskExecutionInfo executionInfo;
        /// @brief Prerequisites in graph insertion order.
        std::vector<TaskHandle> dependencies;
        /// @brief Dependants in graph insertion order.
        std::vector<TaskHandle> dependents;
    };
} // namespace Atlas

#endif // !ATLAS_TASK_SNAPSHOT
