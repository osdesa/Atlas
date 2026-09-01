# Milestone 13 baseline evaluation

## Preregistration

This evaluation decides whether Atlas has an evidence-supported target for
adaptive scheduling. The criteria below were fixed before the canonical
Milestone 13 runs were collected.

### Research questions

1. Do fixed round-robin quanta produce a repeatable response-time versus
   completion-time or fairness trade-off for one slice geometry?
2. Does static priority materially change response time or fairness in the
   bursty priority workloads?
3. Is scheduler control overhead small enough that an adaptive policy could
   improve a task-level metric without being dominated by scheduler cost?

The primary metric is `response_p95_us`. Completion time, throughput, and Jain
fairness are guardrails. Ready wait, selection bypass, scheduler active
fraction, and host/device GPU busy fractions are diagnostic metrics.

### Fixed comparisons

- Direct versus FIFO measures the cost of scheduler coordination.
- FIFO versus static priority tests priority sensitivity in
  `cpu-long-priority-bursty`, `gpu-long-contention`, and
  `mixed-priority-bursty`.
- Round-robin quantum 1, 2, and 4 variants are compared only within the same
  slice geometry in the GPU and mixed cases. Slice geometries are not treated
  as quantum comparisons.

The canonical suite uses direct execution as its paired reference. Direct-
referenced paired intervals establish whether each scheduled effect is stable.
Quantum variants are compared conservatively through their absolute 95%
intervals because the version-two summary does not claim pairwise intervals
between two non-reference variants.

### Practical thresholds and decision rule

An effect is material when its mean magnitude is at least 5%. A paired effect
is stable when its 95% interval excludes zero. A difference between two
non-reference quantum variants is stable only when their absolute 95%
intervals do not overlap.

The adaptive decision is **go** only when the physical-GPU results contain at
least one stable, material policy-sensitive trade-off: one fixed choice
improves primary response while another improves a guardrail, or the preferred
fixed choice changes between canonical workloads. Lavapipe must reproduce the
suite successfully and show that the signal is not solely missing-data or
timestamp capability, but it need not select the same optimum as hardware.
Driver-dependent optima are themselves admissible evidence for runtime
adaptation.

The decision is **no-go** when physical-GPU effects are below the practical
threshold, their intervals are inconclusive, or one fixed policy dominates the
candidate workloads without a material trade-off. A no-go skips adaptive
scheduling and advances Atlas to robustness work.

## Environments and commands

Milestone 13 uses the release `ci-linux` build with profiling enabled. The two
strict user environment files are:

- `benchmarks/environments/milestone-13-intel-xe.json` for the physical Intel
  Iris Xe device;
- `benchmarks/environments/milestone-13-lavapipe.json` for Mesa Lavapipe.

The canonical command is:

```bash
./build/ci-linux/apps/atlas_bench/atlas_bench \
  --suite benchmarks/manifests/baseline-canonical-v1.json \
  --environment-file <environment.json> \
  --output-dir <results-directory>
```

Select the intended implementation with `VK_DRIVER_FILES`. The result summary
and decision below record the exact resolved device and build metadata; raw run
and task records remain reproducible outputs rather than source files.

## Results and decision

Both canonical runs completed all 4,300 measured trials successfully at Git
revision `4f7b4aa9cbb3`. CPU-only run records reported
`gpu_timestamp_supported=false`; every GPU and mixed case reported supported
device timestamps on both implementations.

| Environment | Resolved Vulkan device | Measured runs | Result |
| --- | --- | ---: | --- |
| `milestone-13-intel-xe` | Intel Iris Xe Graphics (TGL GT2) | 4,300 | All `Success` |
| `milestone-13-lavapipe` | llvmpipe (LLVM 22.1.8, 256 bits) | 4,300 | All `Success` |

The following are all same-slice quantum differences that met both the 5%
materiality threshold and the conservative non-overlapping-interval rule.
Lower is better for duration metrics; higher is better for Jain fairness.

| Environment | Workload and slice | Metric | Best fixed quantum, mean [95% CI] | Worst fixed quantum, mean [95% CI] | Effect |
| --- | --- | --- | --- | --- | ---: |
| Intel Xe | GPU short, `1024x1x1` | GPU Jain fairness | q1, 0.8019 [0.7710, 0.8328] | q4, 0.7040 [0.6569, 0.7494] | 13.91% |
| Intel Xe | GPU short, `256x1x1` | GPU Jain fairness | q1, 0.9345 [0.9272, 0.9409] | q4, 0.8485 [0.8272, 0.8668] | 10.14% |
| Intel Xe | GPU long, `16384x4x1` | p95 response (us) | q1, 48,674.8 [46,396.0, 51,089.7] | q4, 54,188.4 [51,625.6, 57,129.4] | 10.17% |
| Lavapipe | GPU long, `16384x4x1` | p95 response (us) | q1, 20,260.2 [19,364.4, 21,241.5] | q4, 24,525.1 [23,212.7, 25,841.0] | 17.39% |
| Lavapipe | GPU long, `4096x2x1` | Completion (us) | q1, 280,166 [254,058, 304,597] | q4, 514,412 [492,687, 532,884] | 45.54% |
| Lavapipe | GPU long, `4096x2x1` | p95 response (us) | q1, 86,739.2 [77,108.4, 95,398.1] | q4, 145,294 [142,298, 147,804] | 40.30% |
| Lavapipe | Mixed balanced, `1024x1x1` | Completion (us) | q1, 64,222.0 [63,459.0, 65,101.6] | q4, 68,321.3 [67,307.6, 69,495.6] | 6.00% |
| Lavapipe | Mixed priority, `1024x1x1` | Completion (us) | q1, 94,464.3 [93,146.6, 95,865.3] | q4, 102,087 [100,678, 103,626] | 7.47% |

Every stable material quantum separation selected q1. The physical GPU showed
no material, stable completion-time loss paired with its q1 fairness or
response gains. Other apparent differences between q1, q2, and q4 had
overlapping absolute intervals or remained below 5%, so they cannot establish
a workload-dependent optimum under the preregistered rule.

Static priority also failed to provide an adaptive target. Against FIFO on
Intel Xe, it increased mean p95 response by 17.12% in the long CPU case and
42.09% in the long GPU case, with non-overlapping absolute intervals. The CPU
fairness gain was only 2.31%, while the 5.48% GPU fairness loss had overlapping
intervals. The mixed-case response and fairness intervals also overlapped.
Lavapipe reproduced non-overlapping response increases of 18.14% for long CPU
and 54.88% for long GPU; in the GPU case static priority also reduced fairness
by 6.35% with non-overlapping intervals. It therefore never supplied a stable
material improvement to trade against response.

Scheduler cost was workload-dependent but did not reveal an adaptive target.
On Intel Xe, FIFO increased the short independent CPU completion time by 5.72%
[2.67%, 8.79%], while the long CPU and mixed completion effects remained below
5%. The maximum mean scheduler-active fraction among scheduled variants was
3.92% on Intel Xe and 5.41% on Lavapipe, both in the short independent CPU
case. Fine-grained work therefore remains sensitive to fixed scheduler cost,
but changing quantum cannot remove that cost.

### Decision: no-go

Milestone 14 adaptive scheduling is skipped. The physical results contain no
stable material trade-off and no stable workload-dependent change in the
preferred fixed quantum. Quantum 1 is the only winner in every stable material
quantum comparison, while static priority exchanges materially worse response
for sub-threshold or inconclusive fairness changes. Adding an adaptive
controller would therefore add state, tuning parameters, and measurement
overhead without an evidence-supported optimisation target.

Atlas proceeds to robustness and validation with its fixed FIFO,
work-unit-round-robin, and static-priority policies unchanged.

These results select research direction; they are not general performance
claims. They cover one Intel integrated GPU and one software Vulkan
implementation on one host and power profile. Milestone 16 retains the raw-data
layout, broader interpretation, and final evaluation responsibilities.
