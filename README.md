# Atlas

Atlas is intended to become a modern C++ user-space scheduler for heterogeneous
CPU/GPU work using Vulkan. The repository is currently at the scaffolding stage:
it contains only enough code to validate compilation, linking, Vulkan SDK
discovery, and the test infrastructure.

No scheduling logic, task model, Vulkan initialisation, command-line interface,
or GPU execution is implemented yet.

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
- `atlas_cli`: minimal executable linked to `Atlas::Atlas`
- `atlas_unit_tests`: Catch2 unit-test executable discovered by CTest
- `atlas_feature_tests`: Catch2 feature-test executable discovered by CTest

## CMake options

| Option | Default | Purpose |
| --- | --- | --- |
| `ATLAS_BUILD_TESTS` | `ON` | Build and register the unit and feature tests |
| `ATLAS_WARNINGS_AS_ERRORS` | `OFF` | Promote warnings on Atlas-owned targets to errors |
| `ATLAS_ENABLE_SANITIZERS` | `OFF` | Enable AddressSanitizer and UndefinedBehaviorSanitizer on compatible non-Windows GCC/Clang builds |
| `ATLAS_ENABLE_CLANG_TIDY` | `OFF` | Run Clang-Tidy when it is available |

Options can be added to a preset invocation, for example:

```bash
cmake --preset dev-linux -DATLAS_ENABLE_SANITIZERS=ON
```

Warnings, sanitizers, and static analysis are applied only to Atlas targets;
third-party dependencies do not inherit those settings.

## Continuous integration

GitHub Actions configures, builds, and runs CTest on Ubuntu with GCC and Windows
with MSVC. CI enables warnings as errors and installs only the Vulkan development
environment needed to compile and link; it does not require a physical GPU.
