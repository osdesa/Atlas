# Remaining milestones

Planning document recording completed Milestones 12–13, skipped Milestone 14,
and ordered Milestones 15–18 after the current CPU/Vulkan task-graph
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

## Milestone 15 — Robustness and validation

1. Add randomized DAG/property tests and very large graph tests.
2. Add cancellation/shutdown fault matrices, malformed executor streams, Vulkan failure injection, and device-loss handling.
3. Add long-duration soak tests and repeated sanitizer stress runs.
4. Use Milestone 13 variance evidence to add performance-regression thresholds
   only for metrics and environments shown to be stable.
5. Continue real Lavapipe execution and add optional physical-GPU runs without making them required for ordinary development.

## Milestone 16 — Final evaluation

1. Check in experiment manifests, environment capture, raw result layout, analysis scripts, tables, and plots.
2. Provide a one-command clean-build reproduction path using a selected Vulkan implementation.
3. Analyze latency, throughput, fairness, scheduler overhead, slice cost, and utilization with uncertainty.
4. Document software Vulkan versus hardware, cooperative boundaries, static graph model, one queue, and lack of active-dispatch interruption.
5. Tie conclusions directly to stated research questions and update the User Guide, development docs, AGENTS.md, Doxygen, UML, CI, and benchmark contracts.

## Milestone 17 — Local web task studio

1. Begin only after the research evaluation is complete; the studio is optional
   productisation rather than a dependency of the scheduler evaluation.
2. Keep the studio in this repository with independent Python and Node lockfiles
   and CI jobs. Normal CMake configuration and the core C++ build must not
   require either toolchain.
3. Add a strict versioned explicit-graph runner whose user-authored DAG nodes
   use only built-in deterministic CPU and Vulkan kernels. Emit live JSONL on
   standard output, reserve standard error for diagnostics, and keep the Atlas
   library free of GUI or Python bindings.
4. Use a FastAPI/Pydantic ASGI service to supervise one runner process and serve
   relative versioned HTTP/WebSocket APIs. Bind only to loopback, validate Host
   and Origin, and protect control requests with a per-launch capability.
5. Use a Vite-built React/TypeScript frontend with React Flow for DAG editing,
   validation, import/export, policy and slicing controls, whole-run
   start/termination, live task state, timelines, events, and summaries.
6. Load saved versioned traces and benchmark results without accepting arbitrary
   C++, shaders, server filesystem paths, per-task live control, runtime graph
   mutation, authentication, or multi-user execution.
7. Validate the packaged local application on Linux and Windows. Keep generated
   frontend bundles as release artifacts rather than tracked source files.

## Milestone 18 — Hosted multi-user execution

1. Begin only after robustness work, the local research evaluation, and the
   local studio are complete; preserve local execution as the ordinary
   development path.
2. Add authentication, persistent jobs, Vulkan worker isolation, resource
   quotas, concurrency limits, cancellation, and abuse controls.
3. Reuse the Milestone 17 frontend and transport contracts rather than adding a
   second GUI or exposing the Atlas C++ library directly to untrusted clients.
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
