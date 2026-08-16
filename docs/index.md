# Atlas

Atlas is an early C++20 task-graph prototype and the foundation for a future
user-space heterogeneous CPU/Vulkan GPU scheduler.

The current API builds directed acyclic graphs of graph-scoped task handles,
validates dependencies, and executes finalised graphs sequentially with Kahn's
topological algorithm. Tasks carry optional names, static priorities, and
backend-neutral CPU/GPU resource intent. Graph finalisation assigns initial
`Ready` or `Blocked` states, and execution records `Running`, `Success`, or
`Failure` plus task-attributed terminal results. Task exceptions,
completed-task count, and total elapsed time are also reported.

The current executor remains FIFO, synchronous, and single-threaded. Priority
and resource intent do not yet affect selection or dispatch. Schedulers own
lifecycle changes; cancellation and repeated graph execution are not complete
runtime features. The detailed current contract is documented in
`docs/task-lifecycle.md`.

Atlas does not yet contain a CPU worker backend, Vulkan initialisation or compute
execution, GPU task slices, mixed CPU/GPU scheduling, or multiple scheduling
policies. Vulkan support is currently limited to SDK discovery and link
validation.

Future GPU scheduling will be cooperative. A logical GPU task will consist of
independently submitted slices, allowing Atlas to reconsider scheduling between
slices without claiming to interrupt a Vulkan dispatch already in flight. The
intended description is **preemptive-style GPU scheduling through cooperative
execution slices**.

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

Tasking and scheduling class pages use PlantUML class diagrams, pinned to version 1.2026.3.
`atlas_docs` first runs `clang-uml` against a generated compilation database,
so the diagrams follow the current C++ declarations without any diagram or
styling code in public headers. This requires `clang-uml` and Ninja in addition
to Doxygen, Graphviz, and Java.
