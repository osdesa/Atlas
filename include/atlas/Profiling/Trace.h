#ifndef ATLAS_PROFILING_TRACE
#define ATLAS_PROFILING_TRACE

#include "atlas/Tasking/ExecutionResource.h"
#include "atlas/Tasking/TaskState.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <optional>
#include <vector>

/** @file Trace.h @brief Declares bounded, non-blocking execution tracing. */

/** @defgroup profiling Profiling
 * @brief Best-effort execution events and device timing measurements.
 */

namespace Atlas
{
    /// @brief Compile-time indication that profiling instrumentation is active.
    inline constexpr bool profilingEnabled{ ATLAS_PROFILING_ENABLED != 0 };
    /// @brief Sentinel for an event field whose index is not applicable.
    inline constexpr std::size_t noTraceIndex{ std::numeric_limits<std::size_t>::max() };

    /**
     * @ingroup profiling
     * @brief Stable version-one execution event categories.
     */
    enum class TraceEventKind : std::uint8_t
    {
        SchedulerStarted,
        SchedulerFinished,
        TaskReady,
        PolicyDecision,
        TaskSelected,
        TaskResumed,
        SubmissionRequested,
        SubmissionAccepted,
        SubmissionRejected,
        BackendStarted,
        BackendFinished,
        CompletionObserved,
        TaskPaused,
        CancellationRequested,
        CancellationApplied,
        TaskSucceeded,
        TaskFailed,
        PolicyFailed,
        InfrastructureFailed
    };

    /**
     * @ingroup profiling
     * @brief Component that observed an event.
     */
    enum class TraceEventSource : std::uint8_t
    {
        Scheduler,
        CpuExecutor,
        VulkanExecutor
    };

    /**
     * @ingroup profiling
     * @brief Fixed-size event safe for bounded publication.
     */
    struct TraceEvent
    {
        /// @brief Session-unique producer sequence.
        std::uint64_t sequence{ 0U };
        /// @brief Nanoseconds since the session's steady-clock origin.
        std::uint64_t timestampNanoseconds{ 0U };
        /// @brief Observed event category.
        TraceEventKind kind{ TraceEventKind::SchedulerStarted };
        /// @brief Component that observed the event.
        TraceEventSource source{ TraceEventSource::Scheduler };
        /// @brief Associated backend when @ref hasResource is true.
        ExecutionResource resource{ ExecutionResource::CPU };
        /// @brief Whether graph and task identity fields are applicable.
        bool hasTask{ false };
        /// @brief Whether @ref resource is applicable.
        bool hasResource{ false };
        /// @brief Opaque graph identity when @ref hasTask is true.
        std::uint64_t graphId{ 0U };
        /// @brief Graph-local task identity when @ref hasTask is true.
        std::uint32_t taskId{ 0U };
        /// @brief Work-unit index or @ref noTraceIndex.
        std::size_t workUnitIndex{ noTraceIndex };
        /// @brief Executor worker index or @ref noTraceIndex.
        std::size_t workerIndex{ noTraceIndex };
        /// @brief Policy candidate count or @ref noTraceIndex.
        std::size_t readyCount{ noTraceIndex };
        /// @brief Policy-selected candidate index or @ref noTraceIndex.
        std::size_t selectedIndex{ noTraceIndex };
        /// @brief Task priority when task metadata is available.
        std::uint32_t priority{ 0U };
        /// @brief Lifecycle state before the event, when applicable.
        TaskState previousState{ TaskState::Unknown };
        /// @brief Lifecycle state after or at the event, when applicable.
        TaskState state{ TaskState::Unknown };
        /// @brief Host-observed duration carried by completion events.
        std::uint64_t hostDurationNanoseconds{ 0U };
        /// @brief Vulkan device duration when @ref hasDeviceDuration is true.
        std::uint64_t deviceDurationNanoseconds{ 0U };
        /// @brief Whether @ref deviceDurationNanoseconds is available.
        bool hasDeviceDuration{ false };
    };

    /**
     * @ingroup profiling
     * @brief Non-owning destination for best-effort trace events.
     */
    class TraceSink
    {
      public:
        virtual ~TraceSink() = default;
        /// @brief Attempts publication without blocking, allocating, or throwing.
        virtual bool tryPublish(TraceEvent event) noexcept = 0;
    };

    /**
     * @ingroup profiling
     * @brief Adds one monotonic origin and sequence to events.
     * @plantumlfile trace.puml
     */
    class TraceSession final
    {
      public:
        /// @brief Borrows a sink that must outlive this session and all producers.
        explicit TraceSession(TraceSink& destination) noexcept;
        /// @brief Timestamps and attempts to publish one event.
        bool emit(TraceEvent event) noexcept;
        /// @brief Returns nanoseconds since construction on the steady clock.
        std::uint64_t elapsedNanoseconds() const noexcept;

        TraceSession(const TraceSession&) = delete;
        TraceSession& operator=(const TraceSession&) = delete;

      private:
        using Clock = std::chrono::steady_clock;
        TraceSink& sink;
        Clock::time_point origin;
        std::atomic<std::uint64_t> nextSequence{ 0U };
    };

    /**
     * @ingroup profiling
     * @brief Preallocated multi-producer, single-consumer trace buffer.
     */
    class BoundedTraceBuffer final : public TraceSink
    {
      public:
        /// @brief Preallocates @p capacity event slots; rejects zero.
        explicit BoundedTraceBuffer(std::size_t capacity);
        /// @brief Attempts bounded publication and counts a rejected event as dropped.
        bool tryPublish(TraceEvent event) noexcept override;
        /// @brief Waits for one event or closure after all accepted events drain.
        std::optional<TraceEvent> waitPop() noexcept;
        /// @brief Retrieves one available event without waiting.
        std::optional<TraceEvent> tryPop() noexcept;
        /// @brief Rejects future publication and wakes consumers.
        void close() noexcept;
        /// @brief Returns the fixed slot count.
        std::size_t capacity() const noexcept;
        /// @brief Returns successful publication count.
        std::uint64_t acceptedEventCount() const noexcept;
        /// @brief Returns rejected publication count.
        std::uint64_t droppedEventCount() const noexcept;

      private:
        std::optional<TraceEvent> popLocked() noexcept;

        mutable std::mutex stateMutex;
        std::condition_variable eventAvailable;
        std::vector<TraceEvent> storage;
        std::size_t readIndex{ 0U };
        std::size_t writeIndex{ 0U };
        std::size_t eventCount{ 0U };
        bool closed{ false };
        std::atomic<std::uint64_t> accepted{ 0U };
        std::atomic<std::uint64_t> dropped{ 0U };
    };
} // namespace Atlas

#endif
