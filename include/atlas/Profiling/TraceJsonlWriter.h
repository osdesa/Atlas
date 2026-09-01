#ifndef ATLAS_PROFILING_TRACE_JSONL_WRITER
#define ATLAS_PROFILING_TRACE_JSONL_WRITER

#include "atlas/Profiling/Trace.h"

#include <atomic>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string_view>
#include <thread>

/** @file TraceJsonlWriter.h @brief Declares asynchronous version-one JSON Lines trace output. */

namespace Atlas
{
    /**
     * @ingroup profiling
     * @brief Owns a bounded trace session and drains it to a versioned JSON Lines file.
     *
     * Producers never perform file I/O and drop events when the bounded buffer cannot
     * accept them. The writer and its session must outlive every traced scheduler and
     * executor submission.
     */
    class TraceJsonlWriter final
    {
      public:
        /// @brief Creates a new file and starts a consumer for @p capacity events.
        explicit TraceJsonlWriter(const std::filesystem::path& path, std::size_t capacity = 65'536U);
        /// @brief Finishes an abandoned stream without throwing.
        ~TraceJsonlWriter();

        /// @brief Returns the session to borrow during scheduler execution.
        TraceSession& session() noexcept;
        /// @brief Drains events and writes one completion footer with @p status.
        void finish(std::string_view status);
        /// @brief Returns the count accepted by the bounded sink.
        std::uint64_t acceptedEventCount() const noexcept;
        /// @brief Returns the count rejected by the bounded sink.
        std::uint64_t droppedEventCount() const noexcept;

        TraceJsonlWriter(const TraceJsonlWriter&) = delete;
        TraceJsonlWriter& operator=(const TraceJsonlWriter&) = delete;

      private:
        void consume() noexcept;

        std::ofstream output;
        BoundedTraceBuffer buffer;
        TraceSession traceSession;
        std::jthread consumer;
        std::atomic<bool> writeFailed{ false };
        bool finished{ false };
    };
} // namespace Atlas

#endif
