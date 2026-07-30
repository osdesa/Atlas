#include "atlas/Tasking/TaskGraph.h"
#include "atlas/atlas.h"

#include <cstdlib>
#include <iostream>

namespace
{
    bool createFrameGraph(Atlas::TaskGraph& frameGraph)
    {
        const auto prepareFrame{ frameGraph.addTask([] { std::cout << "Preparing frame\n"; },
                                                    { .name = "Prepare frame" }) };
        const auto updateSimulation{ frameGraph.addTask([] { std::cout << "Updating simulation\n"; },
                                                        { .name = "Update simulation" }) };
        const auto uploadResources{ frameGraph.addTask([] { std::cout << "Uploading resources\n"; },
                                                       { .name = "Upload resources" }) };
        const auto renderFrame{ frameGraph.addTask([] { std::cout << "Rendering frame\n"; },
                                                   { .name = "Render frame" }) };
        const auto presentFrame{ frameGraph.addTask([] { std::cout << "Presenting frame\n"; },
                                                    { .name = "Present frame" }) };

        if (!prepareFrame || !updateSimulation || !uploadResources || !renderFrame || !presentFrame)
        {
            return false;
        }

        const bool dependenciesAdded{ frameGraph.addDependency(updateSimulation.value(), prepareFrame.value()) &&
                                      frameGraph.addDependency(uploadResources.value(), prepareFrame.value()) &&
                                      frameGraph.addDependency(renderFrame.value(), updateSimulation.value()) &&
                                      frameGraph.addDependency(renderFrame.value(), uploadResources.value()) &&
                                      frameGraph.addDependency(presentFrame.value(), renderFrame.value()) };

        return dependenciesAdded && frameGraph.finishTaskGraph();
    }
} // namespace

int main()
{
    std::cout << "Atlas " << Atlas::version() << '\n';

    Atlas::TaskGraph frameGraph{ 1U };
    if (!createFrameGraph(frameGraph))
    {
        std::cerr << "Failed to create the frame task graph\n";
        return EXIT_FAILURE;
    }

    std::cout << "Created a valid task graph with " << frameGraph.getTaskCount() << " tasks\n";
    return EXIT_SUCCESS;
}
