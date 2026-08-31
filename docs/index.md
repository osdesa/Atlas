# Atlas

Atlas is a C++20 CPU/Vulkan task-graph prototype with mandatory Vulkan compute,
resource-aware scheduling, cooperative dispatch slicing, cancellation,
measurement, and comparison-suite benchmarking.

- [User Guide](user-guide.md): requirements, build/run commands, CLI,
  benchmark JSON, output, failures, and current limitations.
- [Development](development.md): ownership, implementation boundaries, and
  contributor validation.
- [Task lifecycle](task-lifecycle.md): scheduler state transitions, completion,
  cancellation, and failure rules.
- [Remaining milestones](remaining-milestone.md): scoped steps for Milestones
  12–15; this is planning material, not current functionality documentation.

The generated API reference is organized into Tasking, Executor, Scheduling,
and Vulkan modules. Documentation describes the current implementation only.
