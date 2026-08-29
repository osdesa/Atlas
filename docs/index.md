# Atlas

Atlas is a C++20 heterogeneous CPU/Vulkan task-graph prototype.

The current API builds directed acyclic graphs of graph-scoped task handles,
validates dependencies, and executes finalised graphs with a capacity-aware
Kahn scheduler. Tasks carry optional names, static priorities, and
backend-neutral CPU/GPU resource intent. Graph finalisation assigns initial
`Ready` or `Blocked` states, and execution records `Running`, `Success`, or
`Failure` in each task's execution information. Task execution duration,
exceptions, completed-task count, and total elapsed time are also reported.

The scheduler retains FIFO ready-task selection and a single control thread.
Explicit CPU and Vulkan tasks are dispatched through independently capacity-
bounded executors, and one shared completion channel returns whichever backend
finishes first. The scheduler alone applies task state, exceptions, durations,
and dependency release. Repeated graph execution and cancellation remain
unsupported.

Atlas provides compute-only Vulkan initialization, persistent storage buffers
and pipelines, staging transfers, declarative dispatch, standalone execution,
and mixed CPU/GPU graphs. Graphics, multiple queues, cancellation, GPU slices,
and multiple scheduling policies remain deferred.

Future GPU scheduling will be cooperative. A logical GPU task will consist of
independently submitted slices, allowing Atlas to reconsider scheduling between
slices without claiming to interrupt a Vulkan dispatch already in flight. The
intended description is **preemptive-style GPU scheduling through cooperative
execution slices**.

The Vulkan backend and mixed CPU/GPU scheduling design is recorded in the
[Milestones 4 and 5 Vulkan roadmap](milestone-4-5-vulkan-roadmap.md).

## API documentation

Generate the HTML API documentation with:

```powershell
cmake --preset docs-windows
cmake --build --preset docs-windows
```

On Linux, use the equivalent presets:

```bash
cmake --preset docs-linux
cmake --build --preset docs-linux
```

Select the **Windows documentation** configure preset in VS Code's CMake Tools
panel. The **docs** target then appears under the **Documentation** folder.

The generated entry point is
`build/<docs-preset>/docs/html/index.html`.

The site uses the [Doxygen Awesome](https://github.com/jothepro/doxygen-awesome-css)
theme, pinned to version 2.4.2.

Tasking, executor, Vulkan resource, and scheduling class pages use PlantUML
class diagrams, pinned to version 1.2026.3.
`atlas_docs` first runs `clang-uml` against a generated compilation database,
so the diagrams follow the current C++ declarations without any diagram or
styling code in public headers. This requires `clang-uml` and Ninja in addition
to Doxygen, Graphviz, and Java.
