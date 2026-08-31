# Remaining milestones

Planning document for Milestones 12–15 after the current CPU/Vulkan task-graph implementation. The [User Guide](user-guide.md) documents shipped functionality.

## Milestone 12 — Profiling and visualisation

1. Define a versioned event schema for submission/readiness, selection, backend start/end, pause, resume, cancellation, completion, and policy decisions.
2. Add monotonic host timestamps and capability/valid-bit checked Vulkan timestamp queries.
3. Add a non-blocking trace sink and Python timeline/summary scripts.
4. Preserve measurement-disabled builds with negligible overhead.
5. Test ordering, schema validation, timestamp validity, bounded buffering, and disabled tracing.

## Milestone 13 — Adaptive scheduling

1. Begin only after baselines identify a concrete optimization target.
2. Adapt policy quantum or weights from queue depth and measured latency while retaining fixed atomic slice boundaries.
3. Add bounded update intervals, minimum/maximum values, hysteresis, and deterministic fixed-policy comparison.
4. Consider dynamic slice geometry only after quantum adaptation is evaluated.

## Milestone 14 — Robustness and validation

1. Add randomized DAG/property tests and very large graph tests.
2. Add cancellation/shutdown fault matrices, malformed executor streams, Vulkan failure injection, and device-loss handling.
3. Add long-duration soak tests and repeated sanitizer stress runs.
4. Characterize benchmark variance before adding performance-regression thresholds.
5. Continue real Lavapipe execution and add optional physical-GPU runs without making them required for ordinary development.

## Milestone 15 — Final evaluation

1. Check in experiment manifests, environment capture, raw result layout, analysis scripts, tables, and plots.
2. Provide a one-command clean-build reproduction path using a selected Vulkan implementation.
3. Analyze latency, throughput, fairness, scheduler overhead, slice cost, and utilization with uncertainty.
4. Document software Vulkan versus hardware, cooperative boundaries, static graph model, one queue, and lack of active-dispatch interruption.
5. Tie conclusions directly to stated research questions and update the User Guide, development docs, AGENTS.md, Doxygen, UML, CI, and benchmark contracts.

## Completion criteria

Milestones 12–15 are complete only when formats are versioned, experiments are reproducible, adaptive behavior is compared against fixed policies, failure and concurrency behavior is stress-tested, and conclusions are supported by uncertainty-aware measurements.
