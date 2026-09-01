# Milestone 16 final evaluation

## Conclusion

Atlas's fixed FIFO and quantum-one round-robin policies remain defensible
defaults. Static priority does not: across both evaluated Vulkan
implementations it repeatedly increased tail response, ready wait, and
selection bypass without a compensating material fairness gain. Cooperative
slicing provides scheduling boundaries but has substantial cost, especially
with fine slices, so it should be selected only when those boundaries are
required and should use the coarsest effective geometry.

The final evidence does not justify an adaptive controller. Two predeclared
quantum comparisons did replicate, revealing a limited workload-dependent
trade-off that the Milestone 13 baseline did not resolve. That signal comes
from two implementations on one host, is not supported by most quantum
comparisons, and does not establish a portable online selection rule. Atlas
therefore completes the research prototype with explicit fixed policies.

## Questions and decision rule

The version-one study contract predeclared four questions:

1. What overhead does graph scheduling add relative to direct execution?
2. Does a fixed round-robin quantum produce a repeatable workload-dependent
   trade-off?
3. Does static priority improve fairness enough to justify its latency cost?
4. What execution and utilization cost accompanies cooperative slicing?

For each paired contrast, the analyzer computes a deterministic hierarchical
bootstrap over seeds and repetitions with 10,000 resamples. An effect is
stable when its 95% interval excludes zero and material when its mean magnitude
is at least 5%. `replicated` requires stability, materiality, and the same
direction in both environments. These are individual intervals; the study
does not apply a multiple-comparison correction.

## Method and provenance

The canonical suite produced 4,300 successful measured runs per environment,
8,600 in total. Both collections used source revision `49da375e5670`, suite
digest `a8635ad20b83cfc44f4c5bc93c32d1ac658bb717ce9a10d1fbf054b57835f1a8`,
a clean release build, all 186 tests, and the verified `atlas` example. Every
GPU-bearing run contained capability-checked Vulkan timestamps.

| Environment | Vulkan device | Role | Runs |
| --- | --- | --- | ---: |
| `milestone-16-intel-xe` | Intel Iris Xe Graphics (TGL GT2) | physical Vulkan hardware | 4,300 |
| `milestone-16-lavapipe` | llvmpipe, LLVM 22.1.8, 256 bits | Mesa software Vulkan | 4,300 |

The host used an Intel Core i7-1185G7, 16 GB of memory, Linux
7.2.2-arch1-1, Mesa 26.2.1, and the `balance_performance` energy preference.
The checked environment records contain the complete capture contract.

The raw bundles are immutable GitHub Release assets described by the checked
[artifact index](https://github.com/osdesa/Atlas/blob/develop/benchmarks/evaluation/final-v1/artifacts.json). The
[study manifest](https://github.com/osdesa/Atlas/blob/develop/benchmarks/evaluation/final-v1/study.json), generated
[machine result](https://github.com/osdesa/Atlas/blob/develop/benchmarks/evaluation/final-v1/derived/evaluation.json),
[contrast CSV](https://github.com/osdesa/Atlas/blob/develop/benchmarks/evaluation/final-v1/derived/contrasts.csv),
Markdown tables, and SVG plots remain in the repository. The release tag adds
only evidence and documentation after the evaluated source revision.

@startuml
skinparam backgroundColor transparent
skinparam componentStyle rectangle
rectangle "Clean source\n49da375e5670" as source
rectangle "Study + suite +\nenvironment contract" as contract
rectangle "Clean configure/build\n186 tests + atlas" as validation
rectangle "atlas_bench\n4,300 paired runs" as bench
rectangle "Hashed raw bundle\nper environment" as bundle
rectangle "Strict verification +\nhierarchical bootstrap" as analysis
rectangle "JSON / CSV / tables / plots\nresearch conclusions" as report
source --> validation
contract --> validation
validation --> bench
bench --> bundle
bundle --> analysis
contract --> analysis
analysis --> report
@enduml

The complete reproduction, analysis, and release-verification commands are in
the [evaluation tooling guide](https://github.com/osdesa/Atlas/blob/develop/benchmarks/evaluation/README.md).

## Results

### RQ1: scheduling overhead

Most direct-versus-unsliced-FIFO completion and throughput effects were below
5% or inconclusive in at least one environment. For the short CPU workload,
FIFO changed completion by 1.76% on Intel (95% CI -1.07% to 4.64%) and 0.42%
on Lavapipe (-2.20% to 2.69%). Scheduler-active fractions for that workload
were 0.0287 and 0.0272 respectively.

The Intel GPU-only cases had material throughput changes (+42.22% for the
short case and +10.93% for the long case), but these did not replicate on
Lavapipe and completion effects were not stably material. The result is modest
control overhead overall, with implementation-specific timing effects rather
than a portable scheduler cost claim. See the [RQ1 table](https://github.com/osdesa/Atlas/blob/develop/benchmarks/evaluation/final-v1/derived/tables/rq1.md)
and [plot](https://github.com/osdesa/Atlas/blob/develop/benchmarks/evaluation/final-v1/derived/plots/rq1.svg).

### RQ2: fixed quantum

Most quantum contrasts were inconclusive or environment-specific. Two effects
replicated across both implementations:

- For the short GPU workload sliced at 256 workgroups, quantum 4 improved
  throughput relative to quantum 1 by 15.89% on Intel (0.92% to 31.70%) and
  7.04% on Lavapipe (0.13% to 15.71%).
- For the long GPU workload sliced at 16,384 by 4 workgroups, quantum 4
  worsened p95 response relative to quantum 1 by 33.95% on Intel (9.95% to
  60.59%) and 16.80% on Lavapipe (8.61% to 26.25%).

This is a real but narrow trade-off: the corresponding absolute means overlap
substantially, completion-time evidence is not stable, paired percentages can
be volatile near small denominators, and no broader workload rule emerges.
Milestone 13's preregistered no-go was appropriate for its evidence; the final
study refines the characterization without supplying an adaptive-policy target.
See the [RQ2 table](https://github.com/osdesa/Atlas/blob/develop/benchmarks/evaluation/final-v1/derived/tables/rq2.md)
and [plot](https://github.com/osdesa/Atlas/blob/develop/benchmarks/evaluation/final-v1/derived/plots/rq2.svg).

### RQ3: static priority

Static priority consistently increased latency and starvation pressure. On the
long CPU case, p95 response rose 20.45% on Intel and 14.69% on Lavapipe;
p95 wait rose 39.63% and 33.50%, and maximum bypass rose about 65% in both.
CPU fairness gains were stable but only 1.88% and 1.66%, below materiality.

On the unsliced long GPU case, p95 response rose 55.51% and 56.14%, while
maximum bypass rose 72.5% in both. Against same-slice quantum-one execution,
the 16,384-workgroup priority variant raised p95 response by 205.69% and
147.27% and reduced GPU fairness by about 31% in both environments. Mixed
workloads show the same direction: unsliced p95 response rose 19.72% and
15.30%, and fine-sliced GPU fairness fell about 20% and 21%.

No replicated material fairness benefit offsets these costs. See the
[RQ3 table](https://github.com/osdesa/Atlas/blob/develop/benchmarks/evaluation/final-v1/derived/tables/rq3.md) and
[plot](https://github.com/osdesa/Atlas/blob/develop/benchmarks/evaluation/final-v1/derived/plots/rq3.svg).

### RQ4: slicing and utilization

Slicing was expensive in GPU-only workloads. Relative to unsliced FIFO, the
short GPU case increased completion by 833%/569% at a 1,024-workgroup slice and
4,558%/2,587% at 256 workgroups (Intel/Lavapipe). For the long case, completion
rose 247%/60% at 16,384 by 4 and 1,522%/374% at 4,096 by 2. Device timestamp
busy fraction generally fell as slicing became finer.

Mixed results expose the intended control trade-off but no free win. A coarse
4,096 by 2 slice changed balanced completion by +8.79% on Intel and an
inconclusive +3.14% on Lavapipe while increasing GPU host-busy fractions. At a
1,024 slice, balanced completion rose 322%/182%, response rose 648%/309%, and
CPU busy fraction fell about 66%/63%. The priority-shaped mixed workload had
similarly large fine-slice costs. Scheduler-active fractions nevertheless
remained below 1% in the reported long sliced variants, showing that dispatch
granularity, not policy bookkeeping alone, dominates the cost.

`immediate_slice_switch_mean_us` is intentionally absent for quantum-one
variants because it records only an immediate resubmission of the same task;
quantum one instead interleaves ready tasks. See the
[RQ4 table](https://github.com/osdesa/Atlas/blob/develop/benchmarks/evaluation/final-v1/derived/tables/rq4.md) and
[plot](https://github.com/osdesa/Atlas/blob/develop/benchmarks/evaluation/final-v1/derived/plots/rq4.svg).

## Robustness evidence

Performance interpretation rests on the Milestone 15 correctness envelope:
replayable generated DAGs, large graphs, malformed completion streams,
cancellation and shutdown matrices, sanitizer and soak workflows, and
fail-stop Vulkan device-loss handling. The final collection repeated the full
ordinary test suite and executable verification in each explicitly selected
Vulkan environment. See [Milestone 15 validation](milestone-15-validation.md).

## Limits

Both Vulkan implementations ran on one host, and only one was physical GPU
hardware. The study covers static, single-execution graphs, one compute queue,
and cooperative boundaries; it neither interrupts an active dispatch nor
evaluates runtime graph submission. Host queue wait is not task execution
duration. Vulkan timestamps are capability-checked device evidence but do not
remove driver and host variability.

The bootstrap models paired seeds and repetitions within this collection, not
independent machines. Reported 95% intervals are not simultaneous intervals,
so isolated borderline results deserve caution. Percentage effects can also
look large when baselines are small. These limits support fixed transparent
defaults and explicit workload measurement, not a claim of universal optimality.
