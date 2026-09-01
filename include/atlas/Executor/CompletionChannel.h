#ifndef ATLAS_COMPLETION_CHANNEL
#define ATLAS_COMPLETION_CHANNEL

#include "TaskCompletion.h"
#include "atlas/Profiling/Trace.h"
#include "atlas/Tasking/ExecutionResource.h"

#include <array>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <vector>

/**
 * @file CompletionChannel.h
 * @brief Declares the shared completion stream used by heterogeneous scheduling.
 */

namespace Atlas
{
    /// @brief Classifies one event retrieved from a CompletionChannel.
    enum class CompletionEventKind : std::uint8_t
    {
        Completion,      ///< A task outcome is available in the event.
        ProducerFailure, ///< A backend could not publish a required outcome.
        Closed           ///< No further events can be published.
    };

    /// @brief Carries either a task completion or a backend stream event.
    struct CompletionEvent
    {
        /// @brief The event variant carried by this value.
        CompletionEventKind kind{ CompletionEventKind::Closed };
        /// @brief The task outcome when @ref kind is Completion.
        std::optional<TaskCompletion> completion;
        /// @brief The producer associated with a failure event or completion.
        ExecutionResource resource{ ExecutionResource::CPU };
    };

    /**
     * @ingroup executor
     * @brief Fixed-capacity, allocation-free publication channel for task outcomes.
     *
     * Storage is allocated by the constructor. Concurrent producers may publish
     * outcomes while one scheduler thread waits for them. Publication never
     * allocates or throws. Closing the channel and signalling a producer failure
     * wake a waiting consumer.
     * @plantumlfile completion_channel.puml
     */
    class CompletionChannel final
    {
      public:
        /**
         * @brief Allocates storage for at most @p capacity unpublished completions.
         * @param capacity Maximum number of queued completion outcomes.
         * @param traceSession Optional borrowed session that must outlive producers.
         */
        explicit CompletionChannel(std::size_t capacity, TraceSession* traceSession = nullptr);

        /// @brief Publishes one task outcome without allocating or throwing.
        bool publish(TaskCompletion completion) noexcept;
        /// @brief Wakes the consumer with an infrastructure failure for @p resource.
        void signalProducerFailure(ExecutionResource resource) noexcept;
        /// @brief Prevents further publication and wakes a waiting consumer.
        void close() noexcept;

        /// @brief Blocks until a completion, producer failure, or closure is available.
        CompletionEvent wait() noexcept;
        /// @brief Retrieves an available event without blocking.
        std::optional<CompletionEvent> tryReceive() noexcept;

        /// @brief Returns the fixed number of preallocated completion slots.
        std::size_t capacity() const noexcept
        {
            return completionStorage.size();
        }

        /// @brief Returns the optional trace session shared by accepted work.
        TraceSession* traceSession() const noexcept
        {
            return tracing;
        }

        /// @brief Prevents copying synchronization and channel storage.
        CompletionChannel(const CompletionChannel&) = delete;
        /// @brief Prevents copy assignment of channel state.
        CompletionChannel& operator=(const CompletionChannel&) = delete;
        /// @brief Prevents moving a channel while producers may reference it.
        CompletionChannel(CompletionChannel&&) = delete;
        /// @brief Prevents move assignment of channel state.
        CompletionChannel& operator=(CompletionChannel&&) = delete;

      private:
        /// @brief Maps CPU and GPU resources to the two failure slots.
        static std::size_t resourceIndex(ExecutionResource resource) noexcept;
        /// @brief Removes the oldest queued event while the state mutex is held.
        std::optional<CompletionEvent> takeEvent() noexcept;

        /// @brief Guards all queue indices, counters, flags, and storage access.
        std::mutex stateMutex;
        /// @brief Wakes a consumer after publication, failure, or closure.
        std::condition_variable eventAvailable;
        /// @brief Preallocated ring storage for task completions.
        std::vector<std::optional<TaskCompletion>> completionStorage;
        /// @brief Ring position of the next completion to consume.
        std::size_t readIndex{ 0U };
        /// @brief Ring position of the next completion to publish.
        std::size_t writeIndex{ 0U };
        /// @brief Number of occupied completion slots.
        std::size_t completionCount{ 0U };
        /// @brief Latched producer failures indexed by execution resource.
        std::array<bool, 2U> producerFailures{};
        /// @brief Whether publication has been closed permanently.
        bool isClosed{ false };
        /// @brief Borrowed session that outlives all producers using this channel.
        TraceSession* tracing{ nullptr };
    };
} // namespace Atlas

#endif // !ATLAS_COMPLETION_CHANNEL
