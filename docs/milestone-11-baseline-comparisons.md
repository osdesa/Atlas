# Milestone 11: baseline comparisons

## Implementation status

Milestone 11 is implemented. `atlas_bench` can run a versioned comparison suite
whose variants share one deterministic logical workload, execute through either
a direct topological executor driver or the normal Atlas scheduler, and produce
paired uncertainty summaries. Existing Milestone 10 manifests and result
schema version one remain supported without reinterpretation.

The checked smoke suites validate behavior in CI. The larger canonical suite is
intended for deliberate local evaluation and is not a performance gate. Atlas
does not rank policies or claim one universal winner.

## Invocation

Build the Release benchmark configuration as before, then select a suite:

```bash
cmake --preset benchmark-linux
cmake --build --preset benchmark-linux --parallel
ctest --preset benchmark-linux

./build/benchmark-linux/apps/atlas_bench/atlas_bench \
  --suite benchmarks/manifests/baseline-vulkan-smoke-v1.json \
  --output-dir build/benchmark-linux/baseline-results
```

Use `baseline-cpu-smoke-v1.json` for a cross-platform CPU-only check and
`baseline-canonical-v1.json` for the full non-CI matrix. `--validate-only`
parses a suite without executing it. `--overwrite` replaces only the seven
known suite output files.

An optional strict environment file can be supplied with
`--environment-file`. It requires `schema_version: 1` and an
`environment_id`; CPU model, physical memory, OS version, GPU driver, power
profile, and notes are optional. User values supplement rather than override
the automatically recorded build and device fields.

## Direct comparison boundary

The direct variant is a benchmark-private topological executor driver, not a
second production scheduler. It consumes the same generated task descriptors,
dependency edges, priorities, CPU work, GPU dimensions, and seed-derived input
as scheduled variants. It deliberately bypasses:

- `TaskGraph` construction and lifecycle mutation;
- `KahnScheduler` and `SchedulingPolicy` objects; and
- cooperative slicing, because direct GPU tasks submit one complete logical
  dispatch.

The driver maintains stable insertion-ordered CPU and GPU ready queues, fills
the independent executor capacities, and consumes whichever attributed
completion arrives first through a preallocated shared channel. It validates
handle, resource, running state, and ordinary work-unit index. A task or
executor failure stops new submissions and drains accepted work.

This baseline therefore retains executor, completion-channel, dependency, and
host-coordination overhead. It isolates graph, lifecycle, policy, and slicing
cost; it is not raw function-call or raw Vulkan queue timing.

## Paired execution

Each case owns one logical workload and names a reference variant. Variants are
strictly either:

- `direct`, with no policy or slicing fields; or
- `scheduled`, with one built-in policy and an explicit nullable slice
  geometry.

For every case and seed, the same descriptor set is reconstructed for every
variant. Each warmup and measured trial has fresh task state and reset CPU/GPU
output. Variant order rotates by seed and repetition so one variant does not
always occupy the same thermal or cache position. The order position is stored
with each raw record.

CPU results, Vulkan output, task count, and scheduler/driver status are checked
after every successful run. Setup, resource creation, uploads/downloads,
correctness checks, analysis, and serialization remain outside measured graph
completion time.

## Measurement and uncertainty

Direct control-active timing uses the scheduler-active boundary: dependency
bookkeeping and completion application count, while executor submission calls
and blocking waits do not. Direct ready wait begins when a dependency count
reaches zero and closes on submission; response ends on the terminal outcome.
Selection bypass and slice-switch measurements are unavailable for direct
variants.

The summary includes completion time, throughput, response and ready-wait mean
and p95, selection-bypass mean and maximum, control-active fraction,
slice-switch mean, host-observed backend busy fractions, and per-resource Jain
fairness where available.

Absolute variant means and paired effects use a deterministic hierarchical
percentile bootstrap:

- 10,000 resamples and a fixed 95% interval;
- resample seeds, then repetitions within the selected seed;
- pair variants by case, seed, and repetition;
- report absolute difference and percent change from the case reference; and
- emit null bounds with fewer than two distinct seeds or unavailable values.

Metric direction is labelled as lower-is-better, higher-is-better, or
descriptive. The framework emits no aggregate score, ranking, or universal
winner. GPU busy duration remains host observed; Vulkan timestamp support stays
explicitly false/null until Milestone 12.

## Output contract

Successful suites create:

- `suite.resolved.json` and `environment.resolved.json` for provenance;
- `runs.jsonl` with one self-contained normalized record per measured run;
- `runs.csv` and `tasks.csv` for flat raw analysis; and
- `comparisons.json` and `comparisons.csv` for absolute and paired summaries.

Warmups are not exported. A measured failure retains completed raw records and
the failing record, returns failure, and omits both comparison summary files. A
warmup failure aborts before any result writer is created.

## Checked matrix

The CPU and Vulkan smoke suites use one seed and short workloads to prove
contracts without asserting useful uncertainty. The canonical suite uses ten
seeds, two warmups, ten measured repetitions, and four CPU workers. Its six
cases cover:

- short independent and long priority/bursty CPU workloads;
- short independent and long contention-heavy GPU workloads;
- balanced layered and priority/bursty mixed workloads; and
- unsliced FIFO/static priority, two slice geometries, round-robin quantums one,
  two, and four, plus sliced static priority where priorities vary.

The exact task counts, CPU iterations, workgroup dimensions, dependency shapes,
priorities, and variant IDs are versioned in the checked suite rather than
embedded in code.

## Deferred scope

Milestone 11 does not add event tracing, Vulkan timestamp queries, plots,
checked machine-specific results, adaptive scheduling, runtime graph mutation,
performance thresholds, or a final research conclusion. Profiling and
visualisation remain Milestone 12; the checked final evaluation package remains
Milestone 15.
