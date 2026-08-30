# Atlas

Atlas is a C++20 heterogeneous CPU/Vulkan task-graph prototype.

The current API builds directed acyclic graphs of graph-scoped task handles,
validates dependencies, and executes finalised graphs with a capacity-aware
Kahn scheduler. Tasks carry optional names, static priorities, and
backend-neutral CPU/GPU resource intent. Vulkan work may be one ordinary
dispatch or a logical dispatch divided into deterministic work units. Execution
records lifecycle state, exceptions, accumulated payload duration, work-unit
progress, logical completed-task count, and total elapsed time.

The scheduler retains FIFO ready-task selection and a single control thread.
Explicit CPU and Vulkan tasks are dispatched through independently capacity-
bounded executors, and one shared completion channel returns whichever backend
finishes first. The scheduler alone applies task state, exceptions, durations,
progress, cancellation, and dependency release. Cancellation can become
effective before submission or at a sliced GPU boundary; accepted work is
always drained. Repeated graph execution remains unsupported.

Atlas provides Vulkan 1.1 compute initialization, persistent storage buffers and
dispatch-base pipelines, staging transfers, ordinary and sliced declarative
dispatch, standalone execution, and mixed CPU/GPU graphs. Sliced tasks return
to the GPU FIFO tail after each incomplete work unit, enabling cooperative
interleaving without claiming to interrupt an active Vulkan dispatch.

Graphics, multiple queues, public pause/resume, priority policies, runtime graph
submission, repeated execution, and true Vulkan dispatch preemption remain
deferred.

The Vulkan backend and mixed CPU/GPU scheduling design is recorded in the
[Milestones 4 and 5 Vulkan roadmap](milestone-4-5-vulkan-roadmap.md).
Cooperative work units and fail-stop cancellation are specified in the
[Milestone 6 design](milestone-6-cooperative-gpu-slicing.md).

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
