#include "atlas/Profiling/Trace.h"

#include <stdexcept>
#include <utility>

namespace Atlas
{
    TraceSession::TraceSession(TraceSink& destination) noexcept : sink{ destination }, origin{ Clock::now() } {}

    bool TraceSession::emit(TraceEvent event) noexcept
    {
        if constexpr (!profilingEnabled)
        {
            static_cast<void>(event);
            return false;
        }
        event.sequence = nextSequence.fetch_add(1U, std::memory_order_relaxed);
        event.timestampNanoseconds = elapsedNanoseconds();
        return sink.tryPublish(std::move(event));
    }

    std::uint64_t TraceSession::elapsedNanoseconds() const noexcept
    {
        const auto elapsed{ std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - origin).count() };
        return elapsed < 0 ? 0U : static_cast<std::uint64_t>(elapsed);
    }

    BoundedTraceBuffer::BoundedTraceBuffer(const std::size_t requestedCapacity) : storage(requestedCapacity)
    {
        if (requestedCapacity == 0U)
        {
            throw std::invalid_argument{ "BoundedTraceBuffer capacity must be greater than zero" };
        }
    }

    bool BoundedTraceBuffer::tryPublish(TraceEvent event) noexcept
    {
        std::unique_lock lock{ stateMutex, std::defer_lock };
        for (std::size_t attempt{ 0U }; attempt < 8U && !lock.try_lock(); ++attempt)
        {
        }
        if (!lock.owns_lock() || closed || eventCount == storage.size())
        {
            dropped.fetch_add(1U, std::memory_order_relaxed);
            return false;
        }
        storage.at(writeIndex) = std::move(event);
        writeIndex = (writeIndex + 1U) % storage.size();
        ++eventCount;
        accepted.fetch_add(1U, std::memory_order_relaxed);
        lock.unlock();
        eventAvailable.notify_one();
        return true;
    }

    std::optional<TraceEvent> BoundedTraceBuffer::waitPop() noexcept
    {
        std::unique_lock lock{ stateMutex };
        eventAvailable.wait(lock, [this] { return eventCount != 0U || closed; });
        return popLocked();
    }

    std::optional<TraceEvent> BoundedTraceBuffer::tryPop() noexcept
    {
        std::lock_guard lock{ stateMutex };
        return popLocked();
    }

    void BoundedTraceBuffer::close() noexcept
    {
        {
            std::lock_guard lock{ stateMutex };
            closed = true;
        }
        eventAvailable.notify_all();
    }

    std::size_t BoundedTraceBuffer::capacity() const noexcept
    {
        return storage.size();
    }

    std::uint64_t BoundedTraceBuffer::acceptedEventCount() const noexcept
    {
        return accepted.load(std::memory_order_relaxed);
    }

    std::uint64_t BoundedTraceBuffer::droppedEventCount() const noexcept
    {
        return dropped.load(std::memory_order_relaxed);
    }

    std::optional<TraceEvent> BoundedTraceBuffer::popLocked() noexcept
    {
        if (eventCount == 0U)
        {
            return std::nullopt;
        }
        TraceEvent event{ storage.at(readIndex) };
        readIndex = (readIndex + 1U) % storage.size();
        --eventCount;
        return event;
    }
} // namespace Atlas
