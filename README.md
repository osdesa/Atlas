# Atlas

Atlas is a C++20 heterogeneous CPU/Vulkan task-graph prototype. Vulkan is a
mandatory first-class dependency: Atlas has one supported build and does not
provide a CPU-only application mode or backend fallback.

The current implementation provides graph-scoped task identities, immutable
CPU and Vulkan work, synchronous and worker-pool CPU execution, persistent
Vulkan compute resources, cooperative dispatch slicing, resource-aware Kahn
scheduling, cancellation, FIFO/round-robin/static-priority policies, per-task
wait/response measurements, and reproducible comparison-suite benchmarking.
It also provides bounded execution tracing, JSONL validation/summary/timeline
tooling, and capability-checked Vulkan device-duration measurements.
Canonical physical Intel and Lavapipe baselines found no evidence-supported
adaptive scheduling target, so Atlas retains its fixed policies and proceeds to
robustness work. See the [Milestone 13 evaluation](docs/milestone-13-evaluation.md)
for the preregistered decision and uncertainty-supported results.
Milestone 15 adds replayable generated-DAG and large-graph validation,
systematic fault and cancellation stress, sanitizer/soak workflows, and
fail-stop Vulkan device-loss handling.

See the [User Guide](docs/user-guide.md) for requirements, exact build and run
commands, all CLI arguments, the benchmark JSON contract, output files, common
failures, and current limitations. Development contracts are documented in
[Development](docs/development.md), [Task lifecycle](docs/task-lifecycle.md),
and [AGENTS.md](AGENTS.md).

## Build and test

Requirements include CMake 3.24+, C++20, Threads, Vulkan 1.1 development files
and a usable compute device, `glslc`, and `spirv-val`.

```bash
cmake -S . -B build
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

The equivalent Linux development preset is:

```bash
cmake --preset dev-linux
cmake --build --preset dev-linux --parallel
ctest --preset dev-linux
```

## Run

```bash
./build/apps/atlas/atlas

./build/apps/atlas/atlas --trace build/atlas-trace.jsonl
python3 tools/atlas_trace.py summary build/atlas-trace.jsonl

./build/apps/atlas_bench/atlas_bench \
  --suite benchmarks/manifests/smoke-v1.json \
  --output-dir build/results
```

Both executables fail early and return non-zero when required Vulkan
initialization or execution is unavailable.
