# Development guide

## Repository layout

The repository is deliberately small during scaffolding:

- `include/atlas/`: public Atlas headers
- `src/`: compiled library implementation
- `apps/atlas_cli/`: minimal link-validation executable
- `tests/`: Catch2/CTest unit and feature tests
- `cmake/`: target-scoped warnings, sanitizers, and static-analysis helpers
- `.github/workflows/`: Windows and Linux continuous integration

Directories for future runtime components should be added only when their
implementations begin.

## Targets

- `atlas` is a compiled static library. `Atlas::Atlas` is its namespaced alias.
- `atlas_cli` links to `Atlas::Atlas` and executes an example frame task graph.
- `atlas_unit_tests` and `atlas_feature_tests` link to `Atlas::Atlas` and Catch2.
  `catch_discover_tests` registers their test cases with CTest.

The Vulkan SDK is discovered at configure time with `find_package(Vulkan
REQUIRED)`. `atlas` links to `Vulkan::Vulkan`, but no Vulkan API is called at
this stage.

## Presets

All checked-in configurations build below `build/<preset>`:

| Preset | Generator | Configuration | Intended use |
| --- | --- | --- | --- |
| `dev` | Ninja | Debug | Cross-platform local development |
| `dev-windows` | Locally available Visual Studio | Debug | Windows/MSVC development |
| `dev-linux` | Ninja | Debug | Linux development |
| `ci-windows` | Locally available Visual Studio | Release | Windows CI, warnings as errors |
| `ci-linux` | Ninja | Release | Linux CI, warnings as errors |

Each configure preset has a build and test preset with the same name:

```bash
cmake --preset dev-linux
cmake --build --preset dev-linux
ctest --preset dev-linux
```

Use `cmake --list-presets=all` to inspect presets available on the current
platform.

## Sanitizers

AddressSanitizer and UndefinedBehaviorSanitizer are available with compatible
non-Windows GCC and Clang toolchains:

```bash
cmake --preset dev-linux -DATLAS_ENABLE_SANITIZERS=ON
cmake --build --preset dev-linux
ctest --preset dev-linux
```

Sanitizer flags are attached only to Atlas-owned targets. Unsupported toolchains
produce a configure-time warning and otherwise remain buildable.

## Clang-Tidy

Clang-Tidy is optional and is located only when explicitly enabled:

```bash
cmake --preset dev -DATLAS_ENABLE_CLANG_TIDY=ON
cmake --build --preset dev
```

If `clang-tidy` is unavailable, configuration reports a warning and continues
without analysis. The checked-in `.clang-tidy` file selects focused correctness,
modernisation, performance, and readability checks.

## Pre-commit formatting

The versioned `.githooks/pre-commit` hook formats staged C and C++ files with
the repository's `.clang-format` configuration. Enable it after cloning with:

```bash
git config core.hooksPath .githooks
```

The hook formats and re-stages files that have no unstaged edits. It stops when
a staged file also has unstaged changes, preventing it from staging unrelated
work.

## Adding a future module

Keep build configuration target-based:

1. Add implementation and public headers only for the concrete module being
   developed.
2. Add files with `target_sources`; do not introduce global source lists.
3. Express includes, compile features, definitions, and dependencies with the
   relevant `target_*` command and the narrowest appropriate visibility.
4. Apply `atlas_set_project_warnings`, `atlas_enable_sanitizers`, and
   `atlas_enable_clang_tidy` only to new Atlas-owned targets.
5. Add focused tests to the appropriate unit or feature suite, or create a new
   test target only when its separate lifecycle justifies it.

Do not add global include directories or compiler flags. External dependencies
must remain isolated from Atlas warning and analysis settings.
