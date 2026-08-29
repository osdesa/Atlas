#include "atlas/Executor/CompletionChannel.h"

/** @file CompletionChannel.cpp @brief Implements the preallocated heterogeneous completion stream. */

#include <stdexcept>
#include <utility>

namespace Atlas
{
    CompletionChannel::CompletionChannel(const std::size_t capacity) : completionStorage(capacity)
    {
        if (capacity == 0U)
        {
            throw std::invalid_argument{ "CompletionChannel capacity must be greater than zero" };
        }
    }

    bool CompletionChannel::publish(TaskCompletion completion) noexcept
    {
        {
            std::lock_guard lock{ stateMutex };
            if (isClosed || completionCount == completionStorage.size())
            {
                if (isValidExecutionResource(completion.resource))
                {
                    producerFailures.at(resourceIndex(completion.resource)) = true;
                }
                eventAvailable.notify_one();
                return false;
            }

            completionStorage.at(writeIndex).emplace(std::move(completion));
            writeIndex = (writeIndex + 1U) % completionStorage.size();
            ++completionCount;
            eventAvailable.notify_one();
        }

        return true;
    }

    void CompletionChannel::signalProducerFailure(const ExecutionResource resource) noexcept
    {
        {
            std::lock_guard lock{ stateMutex };
            if (isValidExecutionResource(resource))
            {
                producerFailures.at(resourceIndex(resource)) = true;
            }
            eventAvailable.notify_one();
        }
    }

    void CompletionChannel::close() noexcept
    {
        {
            std::lock_guard lock{ stateMutex };
            isClosed = true;
            eventAvailable.notify_all();
        }
    }

    CompletionEvent CompletionChannel::wait() noexcept
    {
        std::unique_lock lock{ stateMutex };
        eventAvailable.wait(lock, [this]
                            { return completionCount != 0U || producerFailures.at(0U) || producerFailures.at(1U) || isClosed; });
        return takeEvent().value_or(CompletionEvent{});
    }

    std::optional<CompletionEvent> CompletionChannel::tryReceive() noexcept
    {
        std::lock_guard lock{ stateMutex };
        return takeEvent();
    }

    std::size_t CompletionChannel::resourceIndex(const ExecutionResource resource) noexcept
    {
        return resource == ExecutionResource::GPU ? 1U : 0U;
    }

    std::optional<CompletionEvent> CompletionChannel::takeEvent() noexcept
    {
        if (completionCount != 0U)
        {
            TaskCompletion completion{ std::move(completionStorage.at(readIndex).value()) };
            completionStorage.at(readIndex).reset();
            readIndex = (readIndex + 1U) % completionStorage.size();
            --completionCount;
            const ExecutionResource resource{ completion.resource };
            return CompletionEvent{ CompletionEventKind::Completion, std::move(completion), resource };
        }

        for (std::size_t index{ 0U }; index < producerFailures.size(); ++index)
        {
            if (producerFailures.at(index))
            {
                producerFailures.at(index) = false;
                const ExecutionResource resource{ index == 0U ? ExecutionResource::CPU : ExecutionResource::GPU };
                return CompletionEvent{ CompletionEventKind::ProducerFailure, std::nullopt, resource };
            }
        }

        if (isClosed)
        {
            return CompletionEvent{};
        }

        return std::nullopt;
    }
} // namespace Atlas
