# Milestone 15 robustness validation

## Contract

Milestone 15 validates Atlas's existing fixed-policy behavior rather than
adding a scheduling mechanism. Generated tests use deterministic seeds and
report the master seed, round, and derived seed on failure. The bounded default
is master seed `684453` with 128 rounds; manual soak validation uses 10,000
rounds. Each generated graph mixes CPU, ordinary Vulkan, and sliced Vulkan
tasks, sparse acyclic dependencies, priorities, and all built-in policies.

The fixed large-graph cases contain 10,000 independent tasks and a 4,096-task
sparse dependency chain. `TaskGraph` retains insertion order while using an
indexed lookup and a single topological finalisation pass so these checks do not
depend on repeated linear handle searches.

## Fault and lifecycle coverage

The deterministic suite covers submission rejection and exceptions, producer
failure, stream closure, missing, unknown, duplicate, extra, resource-mismatched,
and work-unit-mismatched completions, policy failure, task failure, publication
overflow, cancellation boundaries, and executor shutdown/draining.

Private Vulkan fault injection exercises dispatch setup, queue submission,
fence completion, and timestamp-readback boundaries without exposing raw
Vulkan handles or a public testing API. `VK_ERROR_DEVICE_LOST` is permanently
latched by the shared runtime context. Accepted work receives attributed
failure completions, later work is rejected, and schedulers report
`ExecutorUnavailable`; other dispatch exceptions remain task failures.

## Reproduction

Ordinary validation includes the bounded stress cases:

```bash
cmake --preset ci-linux
cmake --build --preset ci-linux --parallel
ctest --preset ci-linux
./build/ci-linux/apps/atlas/atlas
./build/ci-linux/apps/atlas_bench/atlas_bench \
  --suite benchmarks/manifests/smoke-v1.json \
  --output-dir build/ci-linux/robustness-smoke
```

The manually dispatched `Manual robustness` workflow selects Lavapipe and runs
the 10,000-round stress suite, the complete ASan/UBSan suite, and the TSan
`CONCURRENCY` set twenty times. It uploads JUnit, CTest, and benchmark outputs.
Equivalent local commands are documented in the
[Development Guide](development.md), including external `VK_DRIVER_FILES`
selection for an optional physical GPU.

## Performance decision

No absolute duration or benchmark threshold gates this milestone. Milestone 13
established useful environment-specific comparisons but did not establish a
portable absolute threshold for shared runners or unrelated physical hosts.
CTest timeouts therefore detect hangs only; benchmark measurements remain
diagnostic inputs for Milestone 16 final evaluation.
