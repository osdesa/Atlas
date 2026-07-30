#ifndef ATLAS_TASK_OPTIONS
#define ATLAS_TASK_OPTIONS

#include <string>

/**
 * @file TaskOptions.h
 * @brief Declares configuration metadata for a task.
 */

namespace Atlas
{
    /**
     * @ingroup tasking
     * @brief Contains configuration metadata for a task.
     *
     * @par Class diagram
     * @plantumlfile task_options.puml
     */
    struct TaskOptions
    {
        /// @brief Human-readable name of the task.
        std::string name;
    };
} // namespace Atlas

#endif // !ATLAS_TASK_OPTIONS
