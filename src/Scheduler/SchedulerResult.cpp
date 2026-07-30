#include "atlas/Scheduler/SchedulerResult.h"

#include <exception>
#include <ostream>

/**
 * @file SchedulerResult.cpp
 * @brief Defines human-readable scheduler-result output.
 */

namespace
{
    void printException(std::ostream& stream, const std::exception_ptr& exception)
    {
        if (exception == nullptr)
        {
            stream << "none";
            return;
        }

        try
        {
            std::rethrow_exception(exception);
        }
        catch (const std::exception& error)
        {
            stream << error.what();
        }
        catch (...)
        {
            stream << "unknown exception";
        }
    }
} // namespace

namespace Atlas
{
    std::ostream& operator<<(std::ostream& stream, const SchedulerResult& result)
    {
        stream << "SchedulerResult{status=" << toString(result.status) << ", executedTaskCount=" << result.executedTaskCount
               << ", executionTime=" << result.executionTime.count() << "micro(s), exception=";
        printException(stream, result.exception);
        stream << '}';
        return stream;
    }
} // namespace Atlas
