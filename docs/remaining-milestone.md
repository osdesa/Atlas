# Remaining milestones

Planning document recording completed Milestone 12 and the ordered Milestones
13–17 after the current CPU/Vulkan task-graph implementation. The
[User Guide](user-guide.md) documents shipped functionality.

## Milestone 12 — Profiling and visualisation (complete)

1. Define a versioned event schema for submission/readiness, selection, backend start/end, pause, resume, cancellation, completion, and policy decisions.
2. Add monotonic host timestamps and capability/valid-bit checked Vulkan timestamp queries.
3. Add a non-blocking trace sink and Python timeline/summary scripts.
4. Preserve measurement-disabled builds with negligible overhead.
5. Test ordering, schema validation, timestamp validity, bounded buffering, and disabled tracing.

## Milestone 13 — Local web task studio

1. Add a loopback-only Python service and bundled browser frontend for launching
   Atlas, viewing live traces, and loading saved traces and benchmark results.
2. Add a strict explicit-graph runner format whose user-authored DAG nodes use
   only built-in deterministic CPU and Vulkan kernels.
3. Provide DAG editing, validation, import/export, policy and slicing controls,
   whole-run start/stop, live task state, timelines, events, and summaries.
4. Keep the C++ runtime compute-only and keep arbitrary C++ or shader input,
   per-task live control, runtime graph mutation, authentication, and
   multi-user execution outside this milestone.
5. Validate the local application on Linux and Windows while keeping its
   relative HTTP/WebSocket boundary suitable for later hosted deployment.

## Milestone 14 — Adaptive scheduling

1. Begin only after baselines identify a concrete optimization target.
2. Adapt policy quantum or weights from queue depth and measured latency while retaining fixed atomic slice boundaries.
3. Add bounded update intervals, minimum/maximum values, hysteresis, and deterministic fixed-policy comparison.
4. Consider dynamic slice geometry only after quantum adaptation is evaluated.

## Milestone 15 — Robustness and validation

1. Add randomized DAG/property tests and very large graph tests.
2. Add cancellation/shutdown fault matrices, malformed executor streams, Vulkan failure injection, and device-loss handling.
3. Add long-duration soak tests and repeated sanitizer stress runs.
4. Characterize benchmark variance before adding performance-regression thresholds.
5. Continue real Lavapipe execution and add optional physical-GPU runs without making them required for ordinary development.

## Milestone 16 — Final evaluation

1. Check in experiment manifests, environment capture, raw result layout, analysis scripts, tables, and plots.
2. Provide a one-command clean-build reproduction path using a selected Vulkan implementation.
3. Analyze latency, throughput, fairness, scheduler overhead, slice cost, and utilization with uncertainty.
4. Document software Vulkan versus hardware, cooperative boundaries, static graph model, one queue, and lack of active-dispatch interruption.
5. Tie conclusions directly to stated research questions and update the User Guide, development docs, AGENTS.md, Doxygen, UML, CI, and benchmark contracts.

## Milestone 17 — Hosted multi-user execution

1. Begin only after robustness work and the local research evaluation are
   complete; preserve local execution as the ordinary development path.
2. Add authentication, persistent jobs, Vulkan worker isolation, resource
   quotas, concurrency limits, cancellation, and abuse controls.
3. Reuse the Milestone 13 frontend and transport contracts rather than adding a
   second GUI or exposing the Atlas C++ library directly to untrusted clients.
4. Separate public read-only trace viewing from authenticated task execution
   and disclose that shared-host contention invalidates canonical benchmark
   comparisons.

## Completion criteria

Each milestone is complete only when its public formats are versioned, relevant
behavior is tested, and current documentation describes the shipped contract.
Milestone 16 additionally requires reproducible experiments, adaptive-versus-
fixed comparisons, robustness evidence, and uncertainty-supported conclusions.
Milestone 17 is optional productisation and is not required for the research
evaluation.
