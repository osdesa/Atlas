# Atlas

Atlas provides a heterogeneous CPU/Vulkan task graph, bounded execution
tracing, and capability-checked host/device profiling for its current
single-execution scheduler model.

Atlas is a C++20 CPU/Vulkan task-graph prototype with mandatory Vulkan compute,
resource-aware scheduling, cooperative dispatch slicing, cancellation,
measurement, and comparison-suite benchmarking.

- [User Guide](user-guide.md): requirements, build/run commands, CLI,
  benchmark JSON, output, failures, and current limitations.
- [Development](development.md): ownership, implementation boundaries, and
  contributor validation.
- [Task lifecycle](task-lifecycle.md): scheduler state transitions, completion,
  cancellation, and failure rules.
- [Milestone 13 evaluation](milestone-13-evaluation.md): preregistered baseline
  questions, physical/software Vulkan evidence, and adaptive no-go decision.
- [Milestone 15 validation](milestone-15-validation.md): deterministic stress,
  fault handling, sanitizer, soak, and device-loss evidence.
- [Remaining milestones](remaining-milestone.md): completed Milestones 12–15,
  skipped adaptive scheduling, and ordered scope for Milestones 16–18.

The generated API reference is organized into Tasking, Executor, Scheduling,
Vulkan, and Profiling modules. Documentation describes the current
implementation only.
