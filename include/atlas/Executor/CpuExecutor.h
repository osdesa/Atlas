#ifndef ATLAS_CPU_EXECUTOR
#define ATLAS_CPU_EXECUTOR

#include "atlas/Executor/TaskFunction.h"
#include "atlas/Executor/TaskCompletion.h"

/**
 * @file CpuExecutor.h
 * @brief Declares the CPU executor for executing task graphs.
 */

/**
 * @defgroup executor Executor
 * @brief Types used to execute finalised task graphs.
 *
 * Executor provides a common interface and result model for current and future
 * task-graph execution strategies.
 */

namespace Atlas
{
    /**
     * @ingroup executor
     * @brief Defines the common interface implemented by Atlas executors.
     * @hideinheritancegraph
     * @plantumlfile cpu_executor.puml
     */
    class CpuExecutor
    {
      public:
        virtual bool submit(TaskHandle taskHandle, TaskFunction taskFunction) = 0;

        virtual std::optional<TaskCompletion> waitForCompletion() = 0;

        virtual void shutdown() noexcept = 0;

        /// @brief Destroys the scheduler through its common interface.
        virtual ~CpuExecutor() = default;

        /// @brief Prevents copying a scheduler with a borrowed graph reference.
        CpuExecutor(const CpuExecutor&) = delete;

        /// @brief Prevents copy-assigning a scheduler with a borrowed graph reference.
        CpuExecutor& operator=(const CpuExecutor&) = delete;

        /// @brief Prevents moving a scheduler with a borrowed graph reference.
        CpuExecutor(CpuExecutor&&) = delete;

        /// @brief Prevents move-assigning a scheduler with a borrowed graph reference.
        CpuExecutor& operator=(CpuExecutor&&) = delete;
    };
} // namespace Atlas

#endif // !ATLAS_CPU_EXECUTOR
