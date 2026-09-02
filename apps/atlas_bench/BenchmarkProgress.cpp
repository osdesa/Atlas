#include "BenchmarkProgress.h"

#include "atlas/Scheduler/SchedulerStatus.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <thread>
#include <utility>

/** @file BenchmarkProgress.cpp @brief Implements Studio benchmark progress JSONL output. */

namespace Atlas::Benchmark
{
    namespace
    {
        using Json = nlohmann::json;

        const char* stateName(const TaskState state) noexcept
        {
            static constexpr const char* names[]{
                "unknown", "ready", "running", "success", "failure", "blocked", "paused", "cancelled"
            };
            return names[static_cast<std::size_t>(state)];
        }

        const char* kindName(const TraceEventKind kind) noexcept
        {
            static constexpr const char* names[]{
                "scheduler_started", "scheduler_finished",   "task_ready",          "policy_decision",        "task_selected",
                "task_resumed",      "submission_requested", "submission_accepted", "submission_rejected",    "backend_started",
                "backend_finished",  "completion_observed",  "task_paused",         "cancellation_requested", "cancellation_applied",
                "task_succeeded",    "task_failed",          "policy_failed",       "infrastructure_failed"
            };
            return names[static_cast<std::size_t>(kind)];
        }

        const char* resourceName(const ExecutionResource resource) noexcept
        {
            return resource == ExecutionResource::CPU ? "cpu" : "gpu";
        }

        Json contextJson(const BenchmarkRunContext& context)
        {
            return Json{ { "run_id", context.runId },
                         { "run_number", context.runNumber },
                         { "total_run_count", context.totalRunCount },
                         { "case_id", context.caseId },
                         { "variant_id", context.variantId },
                         { "execution", toString(context.executionMode) },
                         { "seed", context.seed },
                         { "repetition", context.repetition },
                         { "warmup", context.warmup },
                         { "execution_order", context.executionOrder } };
        }
    } // namespace

    struct BenchmarkProgressWriter::Impl final
    {
        struct RunTrace final
        {
            RunTrace(Impl& ownerValue, const std::size_t capacity, const std::size_t id, const ExecutionMode mode)
                : owner{ ownerValue }, runId{ id }, executionMode{ mode }, buffer{ capacity }, session{ buffer },
                  consumer{ [this] { consume(); } }
            {
            }

            void close()
            {
                buffer.close();
                if (consumer.joinable())
                {
                    consumer.join();
                }
            }

            void consume() noexcept
            {
                while (const std::optional<TraceEvent> event = buffer.waitPop())
                {
                    Json record{ { "record_type", "event" },
                                 { "run_id", runId },
                                 { "sequence", event->sequence },
                                 { "timestamp_ns", event->timestampNanoseconds },
                                 { "kind", kindName(event->kind) },
                                 { "source", event->source == TraceEventSource::Scheduler
                                                 ? (executionMode == ExecutionMode::Direct ? "direct_coordinator" : "scheduler")
                                             : event->source == TraceEventSource::CpuExecutor ? "cpu_executor"
                                                                                              : "vulkan_executor" },
                                 { "priority", event->priority },
                                 { "previous_state", stateName(event->previousState) },
                                 { "state", stateName(event->state) },
                                 { "host_duration_ns", event->hostDurationNanoseconds } };
                    if (event->hasTask)
                    {
                        record["task_id"] = event->taskId;
                    }
                    if (event->hasResource)
                    {
                        record["resource"] = resourceName(event->resource);
                    }
                    if (event->workUnitIndex != noTraceIndex)
                        record["work_unit_index"] = event->workUnitIndex;
                    if (event->workerIndex != noTraceIndex)
                        record["worker_index"] = event->workerIndex;
                    if (event->readyCount != noTraceIndex)
                        record["ready_count"] = event->readyCount;
                    if (event->selectedIndex != noTraceIndex)
                        record["selected_index"] = event->selectedIndex;
                    if (event->hasDeviceDuration)
                        record["device_duration_ns"] = event->deviceDurationNanoseconds;
                    owner.write(record);
                }
            }

            Impl& owner;
            std::size_t runId;
            ExecutionMode executionMode;
            BoundedTraceBuffer buffer;
            TraceSession session;
            std::jthread consumer;
        };

        explicit Impl(std::ostream& outputValue, const std::size_t capacity) : output{ outputValue }, traceCapacity{ capacity }
        {
            if (!profilingEnabled)
            {
                throw std::runtime_error{ "Studio benchmark progress is unavailable because Atlas profiling is disabled" };
            }
        }

        ~Impl()
        {
            if (activeTrace != nullptr)
            {
                activeTrace->close();
            }
            if (started && !finished)
            {
                write(Json{ { "record_type", "footer" },
                            { "status", "abandoned" },
                            { "completed_run_count", completedRuns },
                            { "total_run_count", totalRuns },
                            { "complete", true } });
            }
        }

        void write(const Json& value)
        {
            std::lock_guard lock{ outputMutex };
            output << value.dump() << std::endl;
        }

        std::ostream& output;
        std::size_t traceCapacity;
        std::size_t totalRuns{ 0U };
        std::size_t completedRuns{ 0U };
        bool started{ false };
        bool finished{ false };
        std::mutex outputMutex;
        std::unique_ptr<RunTrace> activeTrace;
    };

    BenchmarkProgressWriter::BenchmarkProgressWriter(std::ostream& output, const std::size_t traceCapacity)
        : implementation{ std::make_unique<Impl>(output, traceCapacity) }
    {
    }

    BenchmarkProgressWriter::~BenchmarkProgressWriter() = default;

    void BenchmarkProgressWriter::beginSuite(const BaselineSuite& suite, const std::size_t totalRunCount,
                                             const std::size_t measuredRunCount)
    {
        implementation->totalRuns = totalRunCount;
        implementation->started = true;
        implementation->write(Json{ { "record_type", "header" },
                                    { "benchmark_stream_version", 1U },
                                    { "suite_id", suite.suiteId },
                                    { "total_run_count", totalRunCount },
                                    { "measured_run_count", measuredRunCount },
                                    { "trace_capacity", implementation->traceCapacity } });
    }

    void BenchmarkProgressWriter::beginRun(const BenchmarkRunContext& context, const std::vector<TaskDescriptor>& tasks)
    {
        Json record = contextJson(context);
        record["record_type"] = "run_started";
        implementation->write(record);
        for (const TaskDescriptor& task : tasks)
        {
            implementation->write(Json{ { "record_type", "task" },
                                        { "run_id", context.runId },
                                        { "task_id", task.index + 1U },
                                        { "name", task.name },
                                        { "resource", resourceName(task.resource) },
                                        { "priority", task.priority },
                                        { "burst", task.burstIndex },
                                        { "state", task.dependencies.empty() ? "ready" : "blocked" } });
        }
        implementation->activeTrace =
            std::make_unique<Impl::RunTrace>(*implementation, implementation->traceCapacity, context.runId, context.executionMode);
    }

    TraceSession* BenchmarkProgressWriter::traceSession() noexcept
    {
        return implementation->activeTrace == nullptr ? nullptr : &implementation->activeTrace->session;
    }

    void BenchmarkProgressWriter::finishRun(const BenchmarkRunContext& context, const RunRecord& record)
    {
        implementation->activeTrace->close();
        Json tasks = Json::array();
        for (const TaskMeasurement& task : record.tasks)
        {
            Json item{ { "node_id", task.name },
                       { "task_id", task.index + 1U },
                       { "state", stateName(task.state) },
                       { "execution_duration_ns", task.executionDuration.count() },
                       { "completed_work_units", task.completedWorkUnitCount },
                       { "total_work_units", task.totalWorkUnitCount },
                       { "ready_wait_ns", task.readyWaitDuration.count() },
                       { "selection_bypass_count", task.selectionBypassCount } };
            if (task.responseDuration.has_value())
                item["response_duration_ns"] = task.responseDuration->count();
            if (task.deviceExecutionDuration.has_value())
                item["device_execution_duration_ns"] = task.deviceExecutionDuration->count();
            tasks.push_back(std::move(item));
        }
        Json result = contextJson(context);
        result["record_type"] = "run_finished";
        result["status"] = std::string{ toString(record.schedulerResult.status) };
        result["executed_task_count"] = record.schedulerResult.executedTaskCount;
        result["execution_time_ns"] = record.schedulerResult.executionTime.count();
        result["control_active_ns"] = record.schedulerResult.schedulerActiveDuration.count();
        result["throughput_tasks_per_second"] = record.metrics.throughputTasksPerSecond;
        result["device"] = record.gpuDeviceName.has_value() ? Json(record.gpuDeviceName.value()) : Json(nullptr);
        result["timestamp_supported"] = record.gpuTimestampSupported;
        result["accepted_events"] = implementation->activeTrace->buffer.acceptedEventCount();
        result["dropped_events"] = implementation->activeTrace->buffer.droppedEventCount();
        result["tasks"] = std::move(tasks);
        implementation->write(result);
        implementation->activeTrace.reset();
        ++implementation->completedRuns;
    }

    void BenchmarkProgressWriter::finishSuite(const std::string_view status)
    {
        implementation->write(Json{ { "record_type", "footer" },
                                    { "status", status },
                                    { "completed_run_count", implementation->completedRuns },
                                    { "total_run_count", implementation->totalRuns },
                                    { "complete", true } });
        implementation->finished = true;
    }

    void BenchmarkProgressWriter::fail(const std::string_view message)
    {
        if (implementation->activeTrace != nullptr)
        {
            implementation->activeTrace->close();
            implementation->activeTrace.reset();
        }
        implementation->write(Json{ { "record_type", "error" }, { "message", message } });
        finishSuite("failed");
    }
} // namespace Atlas::Benchmark
