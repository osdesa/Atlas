# Atlas

Atlas is an early C++20 task-graph prototype and the foundation for a future
user-space heterogeneous CPU/Vulkan GPU scheduler.

The current implementation can:

- create tasks identified by graph-scoped handles;
- build and finalise directed acyclic task graphs;
- reject missing, duplicate, self, cross-graph, and cyclic dependencies;
- execute a finalised graph sequentially with Kahn's topological algorithm; and
- report completion count, elapsed time, and exceptions thrown by task callables.

The example CLI exercises that path with a six-task graph. Unit and feature
tests cover the current graph and sequential-execution behaviour.

Atlas does **not** yet provide a CPU worker backend, runtime task submission,
interchangeable scheduling policies, Vulkan initialisation or compute execution,
GPU task slicing, mixed CPU/GPU scheduling, or a benchmarking framework. Vulkan
is currently limited to SDK discovery and link validation.

The intended GPU design is cooperative: a logical GPU task will be divided into
independently submitted execution slices, and scheduling control will return to
Atlas between slices. Atlas will not claim to interrupt a Vulkan dispatch that
is already executing. The accurate future description is **preemptive-style GPU
scheduling through cooperative execution slices**.

## Prerequisites

- CMake 3.24 or newer
- A C++20 compiler:
  - MSVC on Windows
  - GCC on Linux
  - Clang where supported by the platform toolchain
- Git, used by CMake while obtaining Catch2
- A Vulkan development environment:
  - Windows: install the [LunarG Vulkan SDK](https://vulkan.lunarg.com/sdk/home)
    and open a new terminal so `VULKAN_SDK` is available.
  - Linux: install the Vulkan loader development package, for example
    `libvulkan-dev` on Ubuntu.
- Ninja for the generic `dev` and Linux presets

The first test-enabled configure downloads Catch2 v3.8.1 through CMake
`FetchContent`.

## Build and test

### Windows

The Windows preset uses the newest Visual Studio generator known to the locally
installed CMake:

```powershell
cmake --preset dev-windows
cmake --build --preset dev-windows
ctest --preset dev-windows
```

The cross-platform `dev` preset is also available when Ninja and a configured
compiler environment are on `PATH`, such as from a Visual Studio Developer
PowerShell:

```powershell
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

### Linux

On Ubuntu, the prerequisites can be installed with:

```bash
sudo apt-get update
sudo apt-get install build-essential cmake ninja-build libvulkan-dev
```

Then configure, build, and test:

```bash
cmake --preset dev-linux
cmake --build --preset dev-linux
ctest --preset dev-linux
```

The generic `dev` preset uses the same Ninja-based development settings and is
also suitable on Linux.

## CMake targets

- `atlas` / `Atlas::Atlas`: compiled Atlas library and namespaced alias
- `atlas_cli`: example sequential task-graph executable linked to `Atlas::Atlas`
- `atlas_unit_tests`: Catch2 unit-test executable discovered by CTest
- `atlas_feature_tests`: Catch2 feature-test executable discovered by CTest

## CMake options

| Option | Raw CMake default | Purpose |
| --- | --- | --- |
| `ATLAS_BUILD_TESTS` | `ON` | Build and register the unit and feature tests |
| `ATLAS_WARNINGS_AS_ERRORS` | `OFF` | Promote warnings on Atlas-owned targets to errors |
| `ATLAS_ENABLE_SANITIZERS` | `OFF` | Enable AddressSanitizer and UndefinedBehaviorSanitizer on compatible non-Windows GCC/Clang builds |
| `ATLAS_ENABLE_CLANG_TIDY` | `OFF` | Run Clang-Tidy when it is available |

Options can be overridden on a preset invocation. For example, disable
sanitizers when the local execution environment cannot run them:

```bash
cmake --preset dev-linux -DATLAS_ENABLE_SANITIZERS=OFF
```

The checked-in presets inherit a shared quality configuration that enables
warnings as errors, sanitizers, and Clang-Tidy. Windows presets disable the
sanitizer option because the current sanitizer helper supports non-Windows GCC
and Clang only. Override a cache option explicitly when a local environment
requires a different configuration.

Warnings, sanitizers, and static analysis are applied only to Atlas targets;
third-party dependencies do not inherit those settings.

## Continuous integration

GitHub Actions configures, builds, and runs CTest on Ubuntu with GCC and Windows
with MSVC. Both jobs enable warnings as errors and request Clang-Tidy; Linux also
enables AddressSanitizer and UndefinedBehaviorSanitizer. CI installs the Vulkan
development environment needed for discovery and linking, but it does not run
Vulkan code or require a physical GPU.
