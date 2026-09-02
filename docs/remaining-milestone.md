# Remaining milestones

Planning document recording completed Milestones 12–16, skipped Milestone 14,
and ordered optional Milestones 17–18 after the current CPU/Vulkan task-graph
implementation. The
[User Guide](user-guide.md) documents shipped functionality.

## Milestone 12 — Profiling and visualisation (complete)

1. Define a versioned event schema for submission/readiness, selection, backend start/end, pause, resume, cancellation, completion, and policy decisions.
2. Add monotonic host timestamps and capability/valid-bit checked Vulkan timestamp queries.
3. Add a non-blocking trace sink and Python timeline/summary scripts.
4. Preserve measurement-disabled builds with negligible overhead.
5. Test ordering, schema validation, timestamp validity, bounded buffering, and disabled tracing.

## Milestone 13 — Baseline characterisation and adaptive go/no-go (complete)

1. State the research questions and pre-register the target metrics, practical
   effect thresholds, and fixed-policy comparisons before collecting results.
2. Run the canonical suite on Lavapipe and the intended physical-GPU evaluation
   host, capturing the versioned environment metadata for both.
3. Characterise run-to-run variance and policy-sensitive effects with the
   existing paired 95% bootstrap summaries.
4. Select a repeatable latency, fairness, utilisation, or scheduler-overhead
   target and record its workload, metric, fixed baseline, and acceptance
   threshold.
5. Publish an explicit go/no-go decision. If no stable target exists, skip
   Milestone 14 and proceed to robustness rather than inventing an adaptive
   mechanism without evidence.

The [Milestone 13 evaluation](milestone-13-evaluation.md) records the
preregistration, physical Intel and Lavapipe results, and no-go decision.

## Milestone 14 — Adaptive scheduling (skipped)

Milestone 13 found no stable material trade-off or workload-dependent fixed
quantum optimum. Quantum 1 won every stable material quantum comparison, while
static priority materially worsened response without a material stable fairness
gain. Atlas therefore adds no adaptive controller, tuning state, or dynamic
slice geometry and proceeds directly to robustness.

## Milestone 15 — Robustness and validation (complete)

1. Add randomized DAG/property tests and very large graph tests.
2. Add cancellation/shutdown fault matrices, malformed executor streams, Vulkan failure injection, and device-loss handling.
3. Add long-duration soak tests and repeated sanitizer stress runs.
4. Use Milestone 13 variance evidence to add performance-regression thresholds
   only for metrics and environments shown to be stable.
5. Continue real Lavapipe execution and add optional physical-GPU runs without making them required for ordinary development.

The [Milestone 15 validation](milestone-15-validation.md) records the
replayable stress contract, fault coverage, sanitizer and soak commands,
device-loss behavior, and the decision not to apply portable timing gates.

## Milestone 16 — Final evaluation (complete)

1. Check in experiment manifests, environment capture, raw result layout, analysis scripts, tables, and plots.
2. Provide a one-command clean-build reproduction path using a selected Vulkan implementation.
3. Analyze latency, throughput, fairness, scheduler overhead, slice cost, and utilization with uncertainty.
4. Document software Vulkan versus hardware, cooperative boundaries, static graph model, one queue, and lack of active-dispatch interruption.
5. Tie conclusions directly to stated research questions and update the User Guide, development docs, AGENTS.md, Doxygen, UML, CI, and benchmark contracts.

The [Milestone 16 final evaluation](milestone-16-evaluation.md) records the
8,600-run physical Intel and Lavapipe study, immutable raw bundles, checked
derived evidence, and final fixed-policy conclusions.

## Milestone 17 — Local desktop task studio (implemented)

1. Begin only after the research evaluation is complete; the studio is optional
   productisation rather than a dependency of the scheduler evaluation.
2. Keep the studio in this repository as an independent PySide6 Python project
   with Linux and Windows CI. Normal CMake configuration and the core C++ build
   must not require Python.
3. Add a strict versioned explicit-graph runner whose user-authored DAG nodes
   use only built-in deterministic CPU and Vulkan kernels. Emit live JSONL on
   standard output, reserve standard error for diagnostics, and keep the Atlas
   library free of GUI or Python bindings.
4. Use a PySide6 desktop application and `QProcess` to supervise one runner
   directly without an HTTP service, browser, or Python/C++ binding.
5. Provide DAG editing, complete structured and JSON benchmark editing,
   validation, import/export, policy and slicing controls, whole-run
   start/termination, live task state, timelines, events, and summaries.
6. Load saved versioned traces and benchmark results without accepting arbitrary
   C++, shaders, per-task live control, runtime graph mutation, authentication,
   or multi-user execution.
7. Validate the source-based application headlessly on Linux and Windows.

The implementation is in `apps/atlas_studio_runner/` and `studio/`. It
preserves the benchmark suite v1 contract and adds checked graph and run-stream
schemas under `benchmarks/schema/`.

## Milestone 18 — Hosted multi-user execution

1. Begin only after robustness work, the local research evaluation, and the
   local studio are complete; preserve local execution as the ordinary
   development path.
2. Add authentication, persistent jobs, Vulkan worker isolation, resource
   quotas, concurrency limits, cancellation, and abuse controls.
3. Reuse the versioned runner and document contracts while implementing a
   separately secured hosted web client; do not expose the Atlas C++ library
   directly to untrusted clients.
4. Separate public read-only trace viewing from authenticated task execution
   and disclose that shared-host contention invalidates canonical benchmark
   comparisons.
5. Keep hosted code in this repository until independent ownership or release
   cadence justifies separating deployment infrastructure.

## Completion criteria

Each milestone is complete only when its public formats are versioned, relevant
behavior is tested, and current documentation describes the shipped contract.
Milestone 16 additionally requires reproducible experiments, the documented
Milestone 13 no-go evidence, robustness evidence, and uncertainty-supported
conclusions. Milestones 17 and 18 are optional productisation and are not
required for the research evaluation.
