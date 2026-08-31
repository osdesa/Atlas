#include "BenchmarkTypes.h"

#include <stdexcept>

/**
 * @file BenchmarkTypes.cpp
 * @brief Implements stable names for resolved benchmark experiment types.
 */

namespace Atlas::Benchmark
{
    std::string toString(const PolicyKind kind)
    {
        switch (kind)
        {
        case PolicyKind::Fifo:
            return "fifo";
        case PolicyKind::RoundRobin:
            return "round_robin";
        case PolicyKind::StaticPriority:
            return "static_priority";
        }
        throw std::logic_error{ "Unknown benchmark policy kind" };
    }

    std::string toString(const DependencyShape shape)
    {
        switch (shape)
        {
        case DependencyShape::Independent:
            return "independent";
        case DependencyShape::Chain:
            return "chain";
        case DependencyShape::Layered:
            return "layered";
        case DependencyShape::Random:
            return "random";
        }
        throw std::logic_error{ "Unknown benchmark dependency shape" };
    }

    std::string toString(const PriorityAssignment assignment)
    {
        switch (assignment)
        {
        case PriorityAssignment::Cycle:
            return "cycle";
        case PriorityAssignment::Random:
            return "random";
        }
        throw std::logic_error{ "Unknown benchmark priority assignment" };
    }
} // namespace Atlas::Benchmark
