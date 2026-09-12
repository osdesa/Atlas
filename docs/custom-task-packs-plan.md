# Custom Task Packs: Decomposed Implementation Plan

## Implementation status and handoff

**Parts A, B, and C are implemented.** The library and runner execute trusted native
CPU/GPU tasks through the existing graph payloads. Graph v2 and run v2 replace
the old application contracts. The runner resolves exact digests, verifies
private snapshots before loading, prepares all nodes before insertion, and
emits provenance, structured errors, and bounded per-task summaries. Built-in
parameter forms and runner preparation share descriptor metadata and scalar
validation. Part C adds content-addressed import, per-digest QSettings trust, descriptor
palettes/forms, unresolved-node diagnostics, exact launch integration, and
expandable task summaries. Safe metadata export uses the runner inspection mode.

The completed library contract is the starting point for all later work:

- `atlas/Extension/TaskPack.h` provides `TaskPackManifest`, typed scalar field
  descriptors, `TaskPackRegistry`, and move-only `CustomTaskInstance`.
- `atlas/Extension/TaskPackAbi.h` is the version-one pure-C ABI. Its host-owned
  bounded writers copy metadata, initialization bytes, summaries, and errors
  while callbacks are active. Do not add a second ABI or pass C++/Vulkan types
  across this boundary.
- Inspection already parses the strict current `manifest.json`, validates the
  directory and referenced files, and computes the canonical content digest
  without loading native code. Loading is explicit and trusted.
- CPU packs prepare independent contexts and become ordinary `TaskFunction`s.
  GPU packs become Atlas-owned ordinary or sliced dispatches; Atlas retains the
  native module while callbacks or Vulkan resources may still use it.
- Pipeline creation now validates SPIR-V for Vulkan 1.1 and requires exact
  reflected storage-buffer bindings and access declarations. All new shader
  paths must use `ShaderBufferBinding`, not binding numbers alone.

Before changing any of these contracts, read the public header, implementation,
and its unit/feature tests together. In particular, preserve the explicit trust
boundary, per-node CPU contexts, bounded output, exact digest identity, and the
rule that raw Vulkan handles never reach a pack.

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

## A1. Common task-pack API — complete

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

## A2. Native C ABI — complete

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

## A3. CPU custom tasks — complete

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

## A4. GPU custom tasks — complete

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

## A5. SPIR-V and buffer-access hardening — complete

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

# Part B — Task-pack format and runner — complete

## Part B handoff and order of work

Part B must consume the completed library API; it must not reimplement native
loading, manifest parsing, parameter canonicalization, GPU preparation, or
SPIR-V reflection in `atlas_studio_runner`. The runner owns snapshotting,
document validation, provenance, and process-facing errors. The recommended
order is B1, B2 snapshotting, B5 graph document, B3 preflight, B4 built-in
unification, then the run-stream portion of B5. This keeps every graph path on
one descriptor/preparation route before the Studio begins using it.

Graph v2 and stream v2 are the only supported application formats. The old
parsing, schemas, examples, and hard-coded built-in parameter forms have been
removed. Trace event schema v1 and benchmark protocols are unchanged.

## B1. Pack directory format

Add `atlas-task-pack-v1.schema.json`.

The strict library parser is already the executable manifest contract. Make the
JSON schema describe that contract exactly, including current root keys,
platform triples, `.spv` shaders, buffer access values, CPU/GPU task-specific
fields, and flat scalar fields. Add schema contract tests that compare accepted
and rejected documents with `TaskPackRegistry::inspectDirectory()`. Do not
invent nested values, additional descriptor resources, optional backends, or a
second manifest parser for the runner.

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

The library has already completed direct-directory validation, referenced-file
hashing, safe-path checks, size/count bounds, and digest identity. The remaining
Part B work is runner-specific: copy only each selected, inspected directory to
a runner-private temporary directory; recompute and compare its digest there;
then call `loadDirectory()` only on that snapshot. Clean up the snapshot after
the run. Treat a missing platform binary as a pre-execution error, not a CPU
fallback. Test changed-after-inspection input and a digest mismatch after copy.

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

Implementation guide: keep this orchestration in
`apps/atlas_studio_runner/main.cpp`. Parse and validate the complete graph-v2
document before scheduling anything; use one `TaskPackRegistry` for the run;
resolve every `(pack_id, digest, task_id)` exactly; prepare every instance; add
all prepared instances and dependencies only after preparation succeeds; then
finalise and execute. Retain every `CustomTaskInstance` through terminal state
and summary collection. Report loader, trust-input, digest, descriptor, and
preparation failures as structured runner errors before emitting execution
records. Do not expose a task-pack argument in `atlas` or `atlas_bench`.

## B4. Built-in task unification

Represent CPU burn, GPU increment, and vector add as an internal
`atlas.builtin` descriptor collection. Use the same parameter validation,
dynamic form metadata, preparation interface, and result handling. Built-ins
require no installation, digest declaration, native module, or trust prompt.

Implement this as a small internal descriptor/preparation adapter, not as a
pretend native pack. It must use the same scalar validation, graph-v2 node
shape, result-summary path, and UI metadata as custom descriptors while keeping
built-ins free of native loading and trust state. Update the existing hard-coded
runner kernels and Studio forms in the same change; do not leave two execution
paths.

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

Start with `benchmarks/schema/atlas-studio-graph-v2.schema.json`,
`benchmarks/schema/atlas-studio-run-v2.schema.json`, runner JSONL emission, and
the matching `studio/atlas_studio/models/` protocol/document code. Define
bounded field sizes and an explicit v2 error record before implementation.
Provenance must contain the exact executed pack ID and digest, not a directory
path or display version. Add fixtures for built-in-only, CPU-pack, GPU-pack,
missing-pack, wrong-digest, and incomplete-crash streams before deleting v1.

# Part C — Atlas Studio GUI — implemented

## Part C handoff and order of work

Begin only after the runner accepts graph-v2 documents and emits a stable
stream-v2. The Python process must remain an untrusted-pack *manager* only: it
may inspect and copy directories, persist trust decisions, and launch the
runner, but it must never call `loadDirectory()` or import a native pack.

The main integration points are `studio/atlas_studio/models/documents.py`,
`models/graph.py`, `models/protocol.py`, `services/launch.py`,
`services/process.py`, and the graph/results views. Extend their tests alongside
each model/controller boundary rather than making a direct widget-to-process
shortcut.

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

Use a content-addressed per-user directory keyed by the library digest and
store trust by that digest in `QSettings`. Import must invoke only the safe
inspection path, reject unsafe inputs before copy, and verify the copied digest.
Removal must not delete a pack referenced by an active launch.

## C2. Task palette

Replace Add CPU/Add GPU with a palette grouped into Built-in and Installed
Packs. Show name, resource, pack/version, description, availability, and trust
status. Add selected tasks with descriptor defaults. Continue coloring canvas
nodes by CPU/GPU resource and show task name plus pack identity.

Build palette entries from serialized descriptor metadata rather than parsing
native manifests in Python. Add descriptor defaults through the graph-model
transaction API so undo/redo, validation, and save behavior remain coherent.

## C3. Dynamic node inspector

Generate parameter controls from descriptors:

- checkboxes for booleans;
- bounded numeric controls for integers/numbers;
- line edits with length validation for strings;
- combo boxes for enums.

Retain common ID, name, priority, and resource fields. Show slicing only for
slice-capable GPU tasks. Commit edits atomically through the existing
model/controller boundary.

Use the field types and bounds already defined by Part A. Keep arbitrary JSON
editing out of the normal UI; model validation must reject an invalid edit as a
single transaction and retain the previous node value.

## C4. Missing and untrusted packs

Allow structurally valid graphs to open when a pack is absent, untrusted,
incompatible, or unavailable on the current platform. Preserve unresolved task
data, mark affected nodes, disable Run, and report exact pack ID/version/digest,
platform, or trust problems. Saving preserves exact provenance.

Represent unresolved nodes explicitly in the document model. They are valid for
open/save but invalid for launch; never silently substitute a same-name or
newer-version pack.

## C5. Process launch and results

Pass only exact referenced installed directories to the runner and recheck
trust immediately before launch. Never load native code into the Python GUI
process.

Extend Results with an expandable per-task summary view. Attach
`task_summary` records to their task, display declared scalar fields and raw
bounded JSON, and distinguish incomplete native-crash streams from normal task
failures.

Pass snapshots selected by exact digest only. Keep parser limits on every
stream-v2 record and render summaries as declared scalar fields plus bounded raw
JSON, not arbitrary plugin-controlled rich text.

# Part D — Delivery sequence

Completed Parts A and B cover the library contracts, native ABI, CPU/GPU
preparation, mock-pack coverage, Vulkan hardening, runner schemas/snapshots,
and built-in descriptor unification. Part C completes GUI integration for Stages 2, 4, and 5. Stage 6 retains
release-wide platform validation beyond local Linux correctness checks.

## Completed Stage 1: runner contracts and skeleton

Keep the completed library manifest, C ABI, descriptor types, digest logic, and
mock modules unchanged. Add graph/run v2 schemas that match those contracts and
convert built-ins to internal descriptors without changing execution behavior.

Acceptance: built-in-only graph v2 executes with unchanged scheduler results.

## Completed Stage 2: CPU runner vertical slice

Implement snapshot-backed runner resolution of the completed native loading and
CPU-instance APIs, then add a sample CPU pack, provenance, and minimal Studio
import/trust/palette/form support.

Acceptance: a user can import, trust, add, run, and inspect a custom CPU task on
Linux and Windows.

## Completed Stage 3: GPU library hardening

Runtime SPIR-V validation/reflection and strict shader/buffer access contracts
are implemented for all callers, with updated built-ins and Vulkan tests.

Acceptance: all existing Vulkan behavior passes with reflected interfaces.

## Completed Stage 4: GPU runner vertical slice

Connect the completed GPU preparation callbacks, host-owned resources,
readbacks, slicing checks, and summaries to runner snapshots/protocols. Add a
sample GPU pack, Lavapipe coverage, and Studio GPU descriptors.

Acceptance: mixed custom CPU/GPU graphs run unsliced and sliced on Lavapipe
with verified summaries.

## Completed Stage 5: GUI and protocol completion

Finish pack management, missing-pack states, trust revocation, result
presentation, imports, diagnostics, JSONL bounds, and removal of v1 handling
and hard-coded forms.

Acceptance: saved graph v2 documents resolve reproducibly by digest and all
abnormal pack states are actionable without loading code into the GUI.

## Stage 6: robustness and delivery validation

Implementation and Linux validation are complete; cross-platform release
acceptance remains open until the Windows job is verified.

- The existing Studio Linux/Windows matrix now builds native fixtures and runner,
  requires headless native coverage, and uses real Lavapipe. Windows runs the full
  native suite and example before Studio. The redundant Linux-only pack test step
  is removed; it lacked the new PySide6 dependency.
- Windows loader compilation suppresses Windows min/max macros. Symlink rejection
  can no longer report success without executing its native check. Studio splits
  symlink and changed-copy tests, verifies QSettings persistence/revocation using
  separate settings instances, and checks unavailable-platform launch rejection.
- Manual robustness now includes all Studio tests against sanitized native packs,
  retains leak detection, and uploads JUnit evidence.
- The desktop delivery regression exercises safe import, both responses to the
  native trust warning, palette insertion, parameter edits, graph save/reopen,
  mixed built-in/custom CPU/GPU execution, exact provenance, summary selection,
  trust revocation, and removal while preserving unresolved documents. It runs
  with ordinary and sliced GPU work in the Linux/Windows Studio matrix and
  against sanitized native executables in robustness CI.
- Repeated pack-manager jobs assert worker destruction before completion is
  announced, covering the Qt worker-wrapper lifetime fix.

Validation on the current dirty checkout (2026-09-10, Linux x64, Mesa Lavapipe
LLVM 22.1.8): 183 normal C++ tests; 183 ASan/UBSan tests with leak detection
**enabled** outside the sandbox; 76 headless Studio tests against each of the
normal and sanitized builds, without skips; the 10,000-round seed-684453 soak
(all three STRESS tests); the verified `atlas` example and benchmark smoke suite.
The initial sandboxed ASan build failed LeakSanitizer discovery; rerunning outside
the sandbox succeeded without disabling leaks. Test evidence is under
`/tmp/atlas-stage6-*` locally and is not a published release artifact.

The retained September 10 TSan log reports all 30 concurrency tests passing
twenty repetitions, including independent native CPU contexts. The scripted
Linux desktop run completed import/trust, mixed execution, summaries, revocation,
missing-pack and changed/unavailable-pack checks; its screenshots and normal-exit
debugger log remain under `/tmp/atlas-stage6-*`.

September 11 follow-up validation adds the reproducible desktop regression:
183 normal native tests and 79 Studio tests against each of the normal and
ASan/UBSan builds pass on Lavapipe, without skips and with leak detection enabled
outside the sandbox. The custom CPU concurrency test passes another twenty TSan
repetitions. Current JUnit evidence is `/tmp/atlas-part-d-*.xml`.

Outstanding: Windows execution has not run on this Linux host. Run the modified
CI job on a Windows x64 host and retain its native/Studio JUnit logs. Workflow
configuration and Linux desktop automation do not establish Windows acceptance.

# Part E — Tests by boundary — implemented

Coverage now includes the following executable checks in addition to the
delivery workflow from Part D:

| Boundary | Coverage and test source |
| --- | --- |
| Common / CPU | `tests/unit/extension/TaskPack_tests.cpp` checks graph-owned callback lifetime after registry/instance destruction and independent, single-use contexts. `studio/tests/test_task_packs.py` checks relocated digests, changes to every referenced asset, filesystem bounds, POSIX special files, malformed ABI tables, callbacks, metadata, contexts, statuses, and bounded output. |
| GPU | `tests/feature/vulkan/TaskPackGpu_feature_tests.cpp` runs ordinary and four-unit sliced dispatches after registry destruction, then checks readback summaries and fail-stop device loss. Runner tests reject invalid GPU preparation structures, allocation/dispatch requests, readback declarations, initialization, writers, and summary callbacks. |
| SPIR-V | Runner tests compile valid Vulkan 1.1 shaders with missing/extra bindings, another set, uniforms, descriptor arrays, access mismatches, push constants, specialization constants, images, or a vertex entry point, and assert the intended reflection diagnostic. Existing Vulkan tests reject malformed binaries. |
| Runner / GUI | Native process exit leaves a headered stream with no result/footer. Callback faults retain the appropriate preflight, execution, or summary phase. Decoder tests reject misattributed, repeated, nested, nonfinite, or oversized UTF-8 summaries. Existing Studio tests cover trust, forms, launch arguments, display limits, and document preservation. |

Fault selection uses `ATLAS_TEST_PACK_FAULT` only in the test fixture library;
each runner fault case receives a separate process environment. Atlas itself
has no fault-selection option. Deliberate native exit uses `_Exit` to exercise
an incomplete stream without a crash dialog or core dump. Shader fixtures are
compiled with the existing required `glslc` dependency.

The expanded suite exposed an intermittent process-worker destruction crash.
Studio now joins the finished process thread before releasing its Python worker
wrapper or announcing run completion. A fifty-launch regression checks worker
destruction and permits each subsequent launch only after completion.

Linux validation uses real Lavapipe, normal and ASan/UBSan native executables,
and repeated TSan native-context tests. Windows test execution remains the
cross-platform acceptance item recorded in Part D; these tests are included in
the existing Windows matrix but have not been executed on this Linux host.

Verified September 11 on Linux x64 / Mesa Lavapipe: 185 native tests and 153
Studio tests pass against both normal and ASan/UBSan builds with leak detection
enabled. Four focused native lifetime/concurrency/device-loss tests pass twenty
TSan repetitions each. Ten repetitions of six Studio launch/teardown/delivery
cases also pass after the worker-lifetime fix. The `atlas` example, documentation
build, ClangFormat, Ruff, and whitespace checks pass. Local evidence is under
`/tmp/atlas-part-e-*`; it is not published cross-platform release evidence.

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
