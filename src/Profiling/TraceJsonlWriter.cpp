#include "atlas/Profiling/TraceJsonlWriter.h"

#include <stdexcept>
#include <string>

namespace Atlas
{
    namespace
    {
        const char* kindName(const TraceEventKind kind) noexcept
        {
            switch (kind)
            {
            case TraceEventKind::SchedulerStarted:
                return "scheduler_started";
            case TraceEventKind::SchedulerFinished:
                return "scheduler_finished";
            case TraceEventKind::TaskReady:
                return "task_ready";
            case TraceEventKind::PolicyDecision:
                return "policy_decision";
            case TraceEventKind::TaskSelected:
                return "task_selected";
            case TraceEventKind::TaskResumed:
                return "task_resumed";
            case TraceEventKind::SubmissionRequested:
                return "submission_requested";
            case TraceEventKind::SubmissionAccepted:
                return "submission_accepted";
            case TraceEventKind::SubmissionRejected:
                return "submission_rejected";
            case TraceEventKind::BackendStarted:
                return "backend_started";
            case TraceEventKind::BackendFinished:
                return "backend_finished";
            case TraceEventKind::CompletionObserved:
                return "completion_observed";
            case TraceEventKind::TaskPaused:
                return "task_paused";
            case TraceEventKind::CancellationRequested:
                return "cancellation_requested";
            case TraceEventKind::CancellationApplied:
                return "cancellation_applied";
            case TraceEventKind::TaskSucceeded:
                return "task_succeeded";
            case TraceEventKind::TaskFailed:
                return "task_failed";
            case TraceEventKind::PolicyFailed:
                return "policy_failed";
            case TraceEventKind::InfrastructureFailed:
                return "infrastructure_failed";
            }
            return "unknown";
        }

        const char* sourceName(const TraceEventSource source) noexcept
        {
            switch (source)
            {
            case TraceEventSource::Scheduler:
                return "scheduler";
            case TraceEventSource::CpuExecutor:
                return "cpu_executor";
            case TraceEventSource::VulkanExecutor:
                return "vulkan_executor";
            }
            return "unknown";
        }

        const char* resourceName(const ExecutionResource resource) noexcept
        {
            return resource == ExecutionResource::CPU ? "cpu" : "gpu";
        }

        const char* stateName(const TaskState state) noexcept
        {
            switch (state)
            {
            case TaskState::Unknown:
                return "unknown";
            case TaskState::Blocked:
                return "blocked";
            case TaskState::Ready:
                return "ready";
            case TaskState::Running:
                return "running";
            case TaskState::Paused:
                return "paused";
            case TaskState::Success:
                return "success";
            case TaskState::Failure:
                return "failure";
            case TaskState::Cancelled:
                return "cancelled";
            }
            return "unknown";
        }

        void writeOptionalIndex(std::ofstream& output, const char* name, const std::size_t value)
        {
            if (value != noTraceIndex)
            {
                output << ",\"" << name << "\":" << value;
            }
        }

        void writeJsonString(std::ofstream& output, const std::string_view value)
        {
            constexpr char hexadecimal[]{ "0123456789abcdef" };
            output << '"';
            for (const char rawCharacter : value)
            {
                const auto character{ static_cast<unsigned char>(rawCharacter) };
                switch (character)
                {
                case '"':
                    output << "\\\"";
                    break;
                case '\\':
                    output << "\\\\";
                    break;
                case '\b':
                    output << "\\b";
                    break;
                case '\f':
                    output << "\\f";
                    break;
                case '\n':
                    output << "\\n";
                    break;
                case '\r':
                    output << "\\r";
                    break;
                case '\t':
                    output << "\\t";
                    break;
                default:
                    if (character < 0x20U)
                    {
                        output << "\\u00" << hexadecimal[character >> 4U] << hexadecimal[character & 0x0FU];
                    }
                    else
                    {
                        output << static_cast<char>(character);
                    }
                    break;
                }
            }
            output << '"';
        }
    } // namespace

    TraceJsonlWriter::TraceJsonlWriter(const std::filesystem::path& path, const std::size_t capacity)
        : buffer{ capacity }, traceSession{ buffer }
    {
        if (!profilingEnabled)
        {
            throw std::runtime_error{ "Trace output is unavailable because Atlas was built with ATLAS_ENABLE_PROFILING=OFF" };
        }
        if (std::filesystem::exists(path))
        {
            throw std::runtime_error{ "Trace output already exists: " + path.string() };
        }
        output.open(path);
        if (!output)
        {
            throw std::runtime_error{ "Unable to open trace output: " + path.string() };
        }
        output << "{\"record_type\":\"header\",\"trace_schema_version\":1,\"clock\":\"steady_nanoseconds\"}\n";
        consumer = std::jthread{ [this] { consume(); } };
    }

    TraceJsonlWriter::~TraceJsonlWriter()
    {
        try
        {
            finish("abandoned");
        }
        catch (...)
        {
        }
    }

    TraceSession& TraceJsonlWriter::session() noexcept
    {
        return traceSession;
    }

    void TraceJsonlWriter::finish(const std::string_view status)
    {
        if (finished)
        {
            return;
        }
        buffer.close();
        if (consumer.joinable())
        {
            consumer.join();
        }
        output << "{\"record_type\":\"footer\",\"status\":";
        writeJsonString(output, status);
        output << ",\"accepted_events\":" << buffer.acceptedEventCount() << ",\"dropped_events\":" << buffer.droppedEventCount()
               << ",\"complete\":true}\n";
        output.flush();
        finished = true;
        if (writeFailed.load(std::memory_order_relaxed) || !output)
        {
            throw std::runtime_error{ "Unable to write trace output" };
        }
    }

    std::uint64_t TraceJsonlWriter::acceptedEventCount() const noexcept
    {
        return buffer.acceptedEventCount();
    }

    std::uint64_t TraceJsonlWriter::droppedEventCount() const noexcept
    {
        return buffer.droppedEventCount();
    }

    void TraceJsonlWriter::consume() noexcept
    {
        while (const std::optional<TraceEvent> event = buffer.waitPop())
        {
            output << "{\"record_type\":\"event\",\"sequence\":" << event->sequence
                   << ",\"timestamp_ns\":" << event->timestampNanoseconds << ",\"kind\":\"" << kindName(event->kind)
                   << "\",\"source\":\"" << sourceName(event->source) << "\"";
            if (event->hasTask)
            {
                output << ",\"graph_id\":" << event->graphId << ",\"task_id\":" << event->taskId;
            }
            if (event->hasResource)
            {
                output << ",\"resource\":\"" << resourceName(event->resource) << "\"";
            }
            writeOptionalIndex(output, "work_unit_index", event->workUnitIndex);
            writeOptionalIndex(output, "worker_index", event->workerIndex);
            writeOptionalIndex(output, "ready_count", event->readyCount);
            writeOptionalIndex(output, "selected_index", event->selectedIndex);
            output << ",\"priority\":" << event->priority << ",\"previous_state\":\"" << stateName(event->previousState)
                   << "\",\"state\":\"" << stateName(event->state) << "\",\"host_duration_ns\":" << event->hostDurationNanoseconds;
            if (event->hasDeviceDuration)
            {
                output << ",\"device_duration_ns\":" << event->deviceDurationNanoseconds;
            }
            output << "}\n";
            if (!output)
            {
                writeFailed.store(true, std::memory_order_relaxed);
            }
        }
    }
} // namespace Atlas
