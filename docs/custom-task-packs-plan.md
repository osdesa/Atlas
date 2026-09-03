# Custom Task Packs: Decomposed Implementation Plan

## Recommended breakdown

Use a hybrid decomposition:

| Breakdown | Use |
| --- | --- |
| Atlas library / runner / GUI | Primary ownership boundary |
| Common / CPU / GPU | Internal library and ABI organization |
| CPU then GPU vertical slices | Delivery order and review checkpoints |

A two-part GUI/library split is insufficient because
`atlas_studio_runner` is the security, process, protocol, and resource-ownership
boundary. It should remain a separate workstream.

## Architecture decision: keep one Atlas library

Keep the public `Atlas::Atlas` library target intact.

- Vulkan is mandatory, and `TaskGraph` and `KahnScheduler` intentionally model
  CPU and Vulkan work together.
- Splitting into public CPU and GPU libraries would add target, export,
  ownership, and dependency complexity without enabling a supported CPU-only
  configuration.
- Instead, separate implementation by namespace, headers, source directories,
  tests, and C ABI callback tables:
  - common task-pack discovery and lifetime;
  - CPU task preparation and execution;
  - GPU task preparation and Vulkan validation.
- Internal object libraries may be considered later for build-time reasons, but
  are not part of this feature.

The scheduler payload remains exactly one `TaskFunction`, `VulkanDispatch`, or
`SlicedVulkanDispatch`. Custom tasks are prepared into those existing types
before graph finalisation.

# Part A — Atlas library

## A1. Common task-pack API

Add an `atlas/Extension` module with:

- `TaskPackManifest`: pack identity, version, digest, supported platforms,
  tasks, shaders, and typed fields.
- `CustomTaskDescriptor`: qualified pack/task ID, display metadata, resource,
  parameters, summaries, and slicing capability.
- `TaskPackRegistry`:
  - `inspectDirectory()` validates and hashes without loading native code;
  - `loadDirectory()` loads an explicitly trusted pack;
  - `findTask()` resolves one exact pack digest and task ID;
  - `createTask()` returns a prepared custom-task instance.
- `CustomTaskInstance`: move-only ownership of plugin context, module state,
  buffers, pipelines, callbacks, and summary state.
- `CustomTaskInstance::addToGraph()` adds the prepared existing payload to a
  `TaskGraph` once.
- `CustomTaskInstance::collectSummary()` is valid only after terminal execution
  and returns a bounded, validated result.

Use a shared internal module-state object so the native library cannot unload
while a callable, Vulkan resource, or result callback still depends on it.

## A2. Native C ABI

Add a pure-C ABI version 1 header.

- Use fixed-width types, explicit structure sizes, opaque context pointers, byte
  and string views, and status codes.
- Export one known entry point returning the pack’s callback table.
- Never pass C++ classes, STL objects, exceptions, allocators, or Vulkan handles
  across the boundary.
- The host copies all returned strings and bytes before callbacks return.
- Cross-check the plugin’s task IDs and resource types against the manifest.
- Reject ABI version, structure-size, null-callback, duplicate-ID, and
  invalid-status violations before graph execution.
- Require plugins to catch their own exceptions. A crash or exception crossing
  the ABI remains an unrecoverable runner-process failure.

Split the callback table into common metadata, CPU callbacks, and GPU callbacks
rather than using one resource-ambiguous function.

## A3. CPU custom tasks

The CPU callback receives validated parameter JSON, the graph seed and stable
node index, an opaque per-node plugin context, and a bounded output writer for
its summary and error.

Wrap execution in an ordinary `TaskFunction`:

- callback success produces normal task success;
- callback error becomes a C++ task exception and follows existing
  `TaskFailed` fail-stop behavior;
- each graph node receives an independent plugin context so worker-pool
  execution is safe when the pack observes the ABI contract;
- running callbacks remain non-preemptible; no cancellation-token API is added;
- native callbacks may access the user’s filesystem and network, allocate
  memory, create threads, hang, or terminate the process. The API documentation
  must describe them as trusted native code.

## A4. GPU custom tasks

Keep the first GPU extension within Atlas’s existing storage-buffer compute
model.

A GPU descriptor selects one manifest-listed shader. Its preparation callback
supplies dispatch dimensions, sizes and initial bytes for required storage
buffers, and bindings requested for post-execution readback.

Atlas remains responsible for shader modules and pipelines, buffer allocation
and upload, descriptor binding, ordinary or sliced dispatch creation,
synchronization and timestamp queries, device loss handling, and result-buffer
download.

Do not expose raw handles or support push constants, uniforms, images, samplers,
arbitrary descriptor sets, specialization constants, or command recording.

## A5. SPIR-V and buffer-access hardening

Strengthen the existing Vulkan API for all callers:

- replace binding-number-only shader declarations with binding plus expected
  read/write access;
- validate SPIR-V against Vulkan 1.1 at runtime using SPIRV-Tools;
- reflect the selected compute entry point and require descriptor set zero,
  storage buffers only, one descriptor per declared binding, exact binding and
  compatible access declarations, and no unsupported interface features;
- require `VulkanDispatch` buffer access to match the validated pipeline
  interface;
- continue rejecting cross-runtime resources and device-limit violations;
- update built-in shaders and tests to use the strengthened declarations.

A custom GPU task may expose slicing only when its descriptor declares
`supports_slicing`. The pack author remains responsible for proving that
separate dispatches preserve the algorithm’s semantics.

# Part B — Task-pack format and runner

## B1. Pack directory format

Add `atlas-task-pack-v1.schema.json`.

A pack directory contains `manifest.json`, Linux and/or Windows native
libraries indexed by platform/architecture triple, and manifest-listed `.spv`
assets.

The manifest defines pack identity, display information, ABI version, platform
binaries, shader entry points and storage-buffer interfaces, CPU/GPU task
descriptors, scalar parameter and summary fields, GPU result bindings, and
slicing support.

Initial field types are boolean, signed/unsigned integer, finite number,
bounded string, and enum. Nested objects and arrays are excluded from version
one.

## B2. Integrity and filesystem validation

Calculate a canonical SHA-256 digest over the manifest and all referenced
files.

- Reject absolute paths, traversal, symlinks, special files, excessive file
  counts, oversized manifests, oversized packs, and unsupported host binaries.
- Treat the content digest—not the display version—as executable identity.
- Snapshot the selected pack into runner-private temporary storage before
  loading.
- Recompute and compare the digest after copying.
- Permit multiple installed digests, but resolve only one digest per pack ID in
  a graph.
- Do not implement signing or automatic publisher trust in this version.

## B3. Runner preflight

Extend `atlas_studio_runner` with repeated `--task-pack <directory>` arguments.

Before creating the graph:

1. Parse graph schema v2.
2. Resolve every declared pack by exact ID and digest.
3. Snapshot, hash, inspect, and load the required native modules.
4. Resolve every task descriptor.
5. Validate parameters, resource, platform availability, and slicing.
6. Prepare all CPU callables and GPU resources.
7. Only then add tasks, dependencies, and finalise the graph.

Pack loading or preparation failure is a pre-execution runner error. CPU
callback, Vulkan dispatch, policy, cancellation, and device-loss failures
continue through existing scheduler behavior.

## B4. Built-in task unification

Represent CPU burn, GPU increment, and vector add as an internal
`atlas.builtin` descriptor collection. Use the same parameter validation,
dynamic form metadata, preparation interface, and result handling. Built-ins
require no installation, digest declaration, native module, or trust prompt.

## B5. Graph and run protocols

Replace graph schema v1 with graph schema v2. Each node contains ID, name,
resource, priority, pack ID, task ID, parameters, and optional slice
dimensions. The graph contains exact IDs, versions, and SHA-256 digests for
referenced custom packs. Remove graph-v1 parsing and update examples.

Replace the Studio run stream v1 with v2:

- header includes executed pack provenance;
- task records include pack and task IDs;
- result records retain scheduler measurements;
- separate bounded `task_summary` records prevent large aggregate result
  records;
- footer retains completion and trace-drop reporting;
- trace event schema v1 remains unchanged because lifecycle semantics do not
  change.

# Part C — Atlas Studio GUI

## C1. Pack management

Add a Task Packs manager that imports a directory into a content-addressed
per-user application-data store, lists installed versions/digests/tasks/
capabilities/trust/platform availability, and supports trust, revocation, and
removal. Store trust in `QSettings` by SHA-256 digest.

Inspecting a pack must never load its native library. Before first execution,
show a mandatory warning that native code runs with user privileges, process
isolation does not restrict files or network, CPU tasks may hang or crash, GPU
tasks may hang or lose the Vulkan device, and validation does not make hostile
code safe. A changed digest always requires new trust.

## C2. Task palette

Replace Add CPU/Add GPU with a palette grouped into Built-in and Installed
Packs. Show name, resource, pack/version, description, availability, and trust
status. Add selected tasks with descriptor defaults. Continue coloring canvas
nodes by CPU/GPU resource and show task name plus pack identity.

## C3. Dynamic node inspector

Generate parameter controls from descriptors:

- checkboxes for booleans;
- bounded numeric controls for integers/numbers;
- line edits with length validation for strings;
- combo boxes for enums.

Retain common ID, name, priority, and resource fields. Show slicing only for
slice-capable GPU tasks. Commit edits atomically through the existing
model/controller boundary.

## C4. Missing and untrusted packs

Allow structurally valid graphs to open when a pack is absent, untrusted,
incompatible, or unavailable on the current platform. Preserve unresolved task
data, mark affected nodes, disable Run, and report exact pack ID/version/digest,
platform, or trust problems. Saving preserves exact provenance.

## C5. Process launch and results

Pass only exact referenced installed directories to the runner and recheck
trust immediately before launch. Never load native code into the Python GUI
process.

Extend Results with an expandable per-task summary view. Attach
`task_summary` records to their task, display declared scalar fields and raw
bounded JSON, and distinguish incomplete native-crash streams from normal task
failures.

# Part D — Delivery sequence

## Stage 1: contracts and skeleton

Add the pack manifest, C ABI, descriptor types, digest logic, mock modules, and
graph/run v2 schemas. Convert built-ins to descriptors without changing
execution behavior.

Acceptance: built-in-only graph v2 executes with unchanged scheduler results.

## Stage 2: CPU vertical slice

Implement native loading, CPU instances, a sample CPU pack, runner resolution,
parameter validation, summaries, provenance, and minimal Studio
import/trust/palette/form support.

Acceptance: a user can import, trust, add, run, and inspect a custom CPU task on
Linux and Windows.

## Stage 3: GPU library hardening

Add runtime SPIR-V validation/reflection and strict shader/buffer access
contracts. Update built-ins and Vulkan tests first.

Acceptance: all existing Vulkan behavior passes with reflected interfaces.

## Stage 4: GPU vertical slice

Add GPU builder callbacks, host-owned resources, readbacks, slicing checks,
summaries, a sample GPU pack, Lavapipe coverage, and Studio GPU descriptors.

Acceptance: mixed custom CPU/GPU graphs run unsliced and sliced on Lavapipe
with verified summaries.

## Stage 5: GUI and protocol completion

Finish pack management, missing-pack states, trust revocation, result
presentation, imports, diagnostics, JSONL bounds, and removal of v1 handling
and hard-coded forms.

Acceptance: saved graph v2 documents resolve reproducibly by digest and all
abnormal pack states are actionable without loading code into the GUI.

## Stage 6: robustness and documentation

Run sanitizers, repeated TSan, real Vulkan tests, Studio headless tests, and
platform loader CI. Update README, User Guide, Development Guide, Task
Lifecycle, Studio README, AGENTS.md, Doxygen, UML, schemas, and examples.

# Part E — Tests by boundary

## Atlas common and CPU

Test manifest/schema/digest determinism, unsafe paths and files, changed
content, Linux/Windows loading, ABI failures, CPU callback errors, concurrent
instances, module lifetime, and summary bounds.

## Atlas GPU

Test invalid SPIR-V, wrong execution models, missing/extra/wrong-set/wrong-type
bindings, access mismatches, device/allocation limits, ordinary and sliced
dispatches, readback/summary failures, and device loss.

## Runner

Test built-in-only and mixed graph v2 documents, missing/wrong-digest packs,
unsupported platforms, unreferenced arguments, preparation failure, scheduler
failure, cancellation, complete footers, incomplete native-crash streams, and
pack provenance.

## GUI

Test safe import without module loading, trust persistence/revocation and
changed-content retrust, dynamic widgets, atomic invalid-edit rollback,
missing-pack presentation, exact launch arguments, summaries, display limits,
and Linux/Windows headless behavior.

# Alternatives and deferred work

- More built-in kernels or a custom-runner template are safest for known
  workloads but are not runtime-extensible.
- An expression/compute DSL is safer and portable but less expressive.
- WebAssembly CPU tasks are preferable if untrusted third-party packs become a
  requirement.
- Child-process tasks improve CPU crash isolation but add overhead and awkward
  scheduler integration.
- Inline C++/GLSL compilation is deferred because of compiler orchestration,
  provenance, and safety concerns.
- Typed data ports are deferred because they require new ownership,
  serialization, synchronization, and dependency semantics.
- Public CPU/GPU library targets are deferred until independent packaging or
  consumers require them; mandatory Vulkan and the heterogeneous scheduler
  remain the governing architecture.

# Fixed assumptions

- Task Graph Studio only; benchmark contracts remain unchanged.
- Trusted local directory packs with explicit per-digest trust; no sandbox or
  signatures in version one.
- Native C ABI CPU callbacks and precompiled SPIR-V storage-buffer GPU tasks.
- Linux and Windows platform loaders.
- Ordering-only edges and bounded display summaries.
- No hot reload, hosted execution, raw Vulkan access, or automatic fallback.
