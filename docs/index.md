# Atlas

Atlas is a C++20 scheduler for modelling and executing dependent CPU and GPU work.

## API documentation

Generate the HTML API documentation with:

```powershell
cmake --preset docs-windows
cmake --build --preset docs-windows
```

Select the **Windows documentation** configure preset in VS Code's CMake Tools
panel. The **docs** target then appears under the **Documentation** folder.

The generated entry point is `build/docs-windows/docs/html/index.html`.

The site uses the [Doxygen Awesome](https://github.com/jothepro/doxygen-awesome-css)
theme, pinned to version 2.4.2.

Tasking and scheduling class pages use PlantUML class diagrams, pinned to version 1.2026.3.
`atlas_docs` first runs `clang-uml` against a generated compilation database,
so the diagrams follow the current C++ declarations without any diagram or
styling code in public headers. This requires `clang-uml` and Ninja in addition
to Doxygen, Graphviz, and Java.
