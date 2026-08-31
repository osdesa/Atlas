#ifndef ATLAS_TEST_STANDALONE_EXECUTOR_HARNESS
#define ATLAS_TEST_STANDALONE_EXECUTOR_HARNESS

#include "atlas/Executor/CompletionChannel.h"

#include <atomic>
#include <cstddef>
#include <optional>
#include <utility>

/**
 * @file StandaloneExecutorHarness.h
 * @brief Adapts channel-only executors for focused completion assertions in tests.
 */

namespace Atlas::Test
{
    /// @brief Owns the completion channel used by one executor unit test.
    template <typename Executor> class StandaloneExecutorHarness final
    {
      public:
        template <typename... Arguments>
        explicit StandaloneExecutorHarness(Arguments&&... arguments) : executor{ std::forward<Arguments>(arguments)... }
        {
        }

        template <typename Work> bool submit(TaskHandle handle, Work&& work)
        {
            const bool accepted{ executor.submit(handle, std::forward<Work>(work), channel) };
            if (accepted)
            {
                acceptedCount.fetch_add(1U, std::memory_order_release);
            }
            return accepted;
        }

        std::optional<TaskCompletion> waitForCompletion()
        {
            if (acceptedCount.load(std::memory_order_acquire) == 0U)
            {
                return std::nullopt;
            }
            CompletionEvent event{ channel.wait() };
            if (event.kind != CompletionEventKind::Completion || !event.completion.has_value())
            {
                return std::nullopt;
            }
            acceptedCount.fetch_sub(1U, std::memory_order_release);
            return std::move(event.completion);
        }

        std::uint32_t maxConcurrency() const noexcept
        {
            return executor.maxConcurrency();
        }
        void shutdown() noexcept
        {
            executor.shutdown();
        }

      private:
        CompletionChannel channel{ 1'024U };
        Executor executor;
        std::atomic_size_t acceptedCount{ 0U };
    };
} // namespace Atlas::Test

#endif // !ATLAS_TEST_STANDALONE_EXECUTOR_HARNESS
