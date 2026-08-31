#include "atlas/Executor/CompletionChannel.h"
#include "atlas/Tasking/GraphId.h"
#include "atlas/Tasking/TaskId.h"

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <future>
#include <stdexcept>
#include <thread>

namespace
{
    Atlas::TaskHandle testHandle(const std::uint32_t id)
    {
        static const Atlas::GraphId graphId{ Atlas::GraphId::create() };
        return Atlas::TaskHandle{ Atlas::TaskId{ id }, graphId };
    }
} // namespace

TEST_CASE("CompletionChannel validates its fixed capacity", "[UNIT]")
{
    REQUIRE_THROWS_AS(Atlas::CompletionChannel{ 0U }, std::invalid_argument);
    Atlas::CompletionChannel channel{ 2U };
    REQUIRE(channel.capacity() == 2U);
}

TEST_CASE("CompletionChannel returns attributed outcomes from concurrent producers", "[UNIT][CONCURRENCY]")
{
    Atlas::CompletionChannel channel{ 2U };
    const Atlas::TaskHandle cpuHandle{ testHandle(1U) };
    const Atlas::TaskHandle gpuHandle{ testHandle(2U) };
    std::atomic<bool> cpuPublished{ false };
    std::atomic<bool> gpuPublished{ false };

    std::jthread cpuProducer{ [&channel, cpuHandle, &cpuPublished]
                              {
                                  cpuPublished = channel.publish(Atlas::TaskCompletion{
                                      cpuHandle, nullptr, std::chrono::microseconds{ 3 }, Atlas::ExecutionResource::CPU });
                              } };
    std::jthread gpuProducer{ [&channel, gpuHandle, &gpuPublished]
                              {
                                  gpuPublished = channel.publish(Atlas::TaskCompletion{
                                      gpuHandle, nullptr, std::chrono::microseconds{ 5 }, Atlas::ExecutionResource::GPU });
                              } };
    cpuProducer.join();
    gpuProducer.join();
    REQUIRE(cpuPublished);
    REQUIRE(gpuPublished);

    const Atlas::CompletionEvent first{ channel.wait() };
    const Atlas::CompletionEvent second{ channel.wait() };
    REQUIRE(first.kind == Atlas::CompletionEventKind::Completion);
    REQUIRE(second.kind == Atlas::CompletionEventKind::Completion);
    REQUIRE(first.completion.has_value());
    REQUIRE(second.completion.has_value());
    REQUIRE(first.completion->handle != second.completion->handle);
}

TEST_CASE("CompletionChannel wakes for producer failure and closure", "[UNIT][CONCURRENCY]")
{
    Atlas::CompletionChannel channel{ 1U };
    auto waitingEvent{ std::async(std::launch::async, [&channel] { return channel.wait(); }) };

    channel.signalProducerFailure(Atlas::ExecutionResource::GPU);
    REQUIRE(waitingEvent.get().kind == Atlas::CompletionEventKind::ProducerFailure);

    channel.close();
    REQUIRE(channel.wait().kind == Atlas::CompletionEventKind::Closed);
}

TEST_CASE("CompletionChannel preserves a completion work-unit index", "[UNIT]")
{
    Atlas::CompletionChannel channel{ 1U };
    const Atlas::TaskHandle handle{ testHandle(3U) };

    REQUIRE(
        channel.publish(Atlas::TaskCompletion{ handle, nullptr, std::chrono::microseconds{ 7 }, Atlas::ExecutionResource::GPU, 4U }));

    const Atlas::CompletionEvent event{ channel.wait() };
    REQUIRE(event.kind == Atlas::CompletionEventKind::Completion);
    REQUIRE(event.completion.has_value());
    REQUIRE(event.completion->handle == handle);
    REQUIRE(event.completion->workUnitIndex == 4U);
}
