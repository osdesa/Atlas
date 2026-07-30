#ifndef ATLAS_SCHEDULER_STATUS
#define ATLAS_SCHEDULER_STATUS

#include <cstdint>
#include <string_view>

/**
 * @file SchedulerStatus.h
 * @brief Declares scheduler execution status values.
 */

namespace Atlas
{
    /**
     * @ingroup scheduling
     * @brief Identifies the outcome of a scheduler execution.
     */
    enum class SchedulerStatus : std::uint8_t
    {
        Success,           ///< Every task completed successfully.
        InvalidGraph,      ///< The supplied graph is structurally invalid.
        GraphNotFinalised, ///< The supplied graph has not been finalised.
        TaskFailed,        ///< A task threw an exception or otherwise failed.
        Unknown,           ///< An unknown scheduler error occurred.
    };

    /**
     * @ingroup scheduling
     * @brief Retrieves the display name of a scheduler status.
     * @param status The status to describe.
     * @return A stable, human-readable status name.
     */
    std::string_view toString(SchedulerStatus status) noexcept;
} // namespace Atlas

#endif // !ATLAS_SCHEDULER_STATUS
