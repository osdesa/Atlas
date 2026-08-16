#include "atlas/Scheduler/KahnScheduler.h"
#include "atlas/Tasking/TaskGraph.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <vector>

/**
 * @file main.cpp
 * @brief Demonstrates construction and sequential execution of an Atlas task graph.
 */

namespace
{
    /**
     * @brief Data produced and consumed by the example frame tasks.
     */
    struct FrameState
    {
        std::array<float, 6U> objectPositions{ { -1.4F, -0.7F, -0.1F, 0.35F, 0.9F, 1.3F } };
        std::array<float, 6U> objectVelocities{ { 0.8F, 0.2F, -0.1F, 0.4F, -0.3F, -0.6F } };
        std::array<std::uint32_t, 4U> resourceSizes{ { 4'096U, 16'384U, 8'192U, 2'048U } };
        std::vector<float> visibleObjectPositions;
        std::uint64_t uploadedBytes{ 0U };
        std::uint64_t renderChecksum{ 0U };
        std::uint64_t presentedChecksum{ 0U };
        bool framePrepared{ false };
        bool resourcesUploaded{ false };
        bool simulationUpdated{ false };
        bool visibilityComputed{ false };
        bool renderCommandsBuilt{ false };
        bool framePresented{ false };
    };

    /**
     * @brief Populates and finalises the task graph used for one example frame.
     * @param frameGraph The empty graph to populate.
     * @param frameState The frame data that the tasks will process.
     * @return True when every task and dependency was added and the graph was finalised.
     */
    bool createFrameGraph(Atlas::TaskGraph& frameGraph, FrameState& frameState)
    {
        const auto prepareFrame{ frameGraph.addTask(
            [&frameState]
            {
                frameState.visibleObjectPositions.clear();
                frameState.uploadedBytes = 0U;
                frameState.renderChecksum = 0U;
                frameState.presentedChecksum = 0U;
                frameState.resourcesUploaded = false;
                frameState.simulationUpdated = false;
                frameState.visibilityComputed = false;
                frameState.renderCommandsBuilt = false;
                frameState.framePresented = false;
                frameState.framePrepared = true;
            },
            Atlas::TaskOptions{ "Prepare frame" }) };

        const auto updateSimulation{ frameGraph.addTask(
            [&frameState]
            {
                if (!frameState.framePrepared)
                {
                    throw std::logic_error{ "Frame must be prepared before updating the simulation" };
                }

                std::transform(frameState.objectPositions.begin(), frameState.objectPositions.end(),
                               frameState.objectVelocities.begin(), frameState.objectPositions.begin(),
                               [](float position, float velocity)
                               {
                                   constexpr float fixedDeltaSeconds{ 1.0F / 60.0F };
                                   return position + (velocity * fixedDeltaSeconds);
                               });
                frameState.simulationUpdated = true;
            },
            Atlas::TaskOptions{ "Update simulation" }) };

        const auto uploadResources{ frameGraph.addTask(
            [&frameState]
            {
                if (!frameState.framePrepared)
                {
                    throw std::logic_error{ "Frame must be prepared before uploading resources" };
                }

                for (const std::uint32_t resourceSize : frameState.resourceSizes)
                {
                    frameState.uploadedBytes += resourceSize;
                }
                frameState.resourcesUploaded = true;
            },
            Atlas::TaskOptions{ "Upload resources" }) };

        const auto cullObjects{ frameGraph.addTask(
            [&frameState]
            {
                if (!frameState.simulationUpdated)
                {
                    throw std::logic_error{ "Simulation must be updated before visibility culling" };
                }

                constexpr float horizontalViewLimit{ 1.0F };
                for (const float position : frameState.objectPositions)
                {
                    if (std::abs(position) <= horizontalViewLimit)
                    {
                        frameState.visibleObjectPositions.emplace_back(position);
                    }
                }
                frameState.visibilityComputed = true;
            },
            Atlas::TaskOptions{ "Cull visible objects" }) };

        const auto buildRenderCommands{ frameGraph.addTask(
            [&frameState]
            {
                if (!frameState.resourcesUploaded || !frameState.visibilityComputed)
                {
                    throw std::logic_error{ "Resources and visibility data are required to build render commands" };
                }

                constexpr std::uint64_t checksumMultiplier{ 1'099'511'628'211ULL };
                frameState.renderChecksum = frameState.uploadedBytes;
                for (const float position : frameState.visibleObjectPositions)
                {
                    const auto quantizedPosition{ static_cast<std::int64_t>((position + 2.0F) * 1'000.0F) };
                    frameState.renderChecksum =
                        (frameState.renderChecksum * checksumMultiplier) ^ static_cast<std::uint64_t>(quantizedPosition);
                }
                frameState.renderCommandsBuilt = true;
            },
            Atlas::TaskOptions{ "Build render commands" }) };

        const auto presentFrame{ frameGraph.addTask(
            [&frameState]
            {
                if (!frameState.renderCommandsBuilt)
                {
                    throw std::logic_error{ "Render commands must be built before presenting the frame" };
                }

                frameState.presentedChecksum =
                    frameState.renderChecksum ^ (static_cast<std::uint64_t>(frameState.visibleObjectPositions.size()) << 32U);
                frameState.framePresented = true;
            },
            Atlas::TaskOptions{ "Present frame" }) };

        if (!prepareFrame || !updateSimulation || !uploadResources || !cullObjects || !buildRenderCommands || !presentFrame)
        {
            return false;
        }

        const bool dependenciesAdded{ frameGraph.addDependency(updateSimulation.value(), prepareFrame.value()) &&
                                      frameGraph.addDependency(uploadResources.value(), prepareFrame.value()) &&
                                      frameGraph.addDependency(cullObjects.value(), updateSimulation.value()) &&
                                      frameGraph.addDependency(buildRenderCommands.value(), cullObjects.value()) &&
                                      frameGraph.addDependency(buildRenderCommands.value(), uploadResources.value()) &&
                                      frameGraph.addDependency(presentFrame.value(), buildRenderCommands.value()) };

        return dependenciesAdded && frameGraph.finishTaskGraph();
    }
} // namespace

int main()
{
    Atlas::TaskGraph frameGraph;
    FrameState frameState;
    if (!createFrameGraph(frameGraph, frameState))
    {
        std::cerr << "Failed to create the frame task graph\n";
        return EXIT_FAILURE;
    }

    Atlas::KahnScheduler scheduler{ frameGraph };
    const Atlas::SchedulerResult result{ scheduler.execute() };

    std::cout << result << '\n';
    if (result.status != Atlas::SchedulerStatus::Success || !frameState.framePresented)
    {
        std::cerr << "Failed to execute the frame task graph\n";
        return EXIT_FAILURE;
    }

    std::cout << "Presented " << frameState.visibleObjectPositions.size() << " visible objects after uploading "
              << frameState.uploadedBytes << " bytes; frame checksum: " << frameState.presentedChecksum << '\n';
    return EXIT_SUCCESS;
}
