# 7. Debugging and recommended practices

[← Execution and queues](06-Execution-and-Queues.md) ·
[Documentation home](README.md) ·
[Next: Build and edge walkthrough →](08-Build-and-Edge-Walkthrough.md)

Render-graph failures are usually declaration failures: the callback uses a
resource, state, range, or side effect that the graph was not told about. Start
from the symptom, inspect the compiled graph, and enable one diagnostic option
at a time.

## First response: dump the graph

```cpp
const FARDGCompileResult& Compiled = Graph.Compile();
const eastl::string DebugText = Graph.DumpGraph();
```

`DumpGraph()` requires a compiled graph and is deterministic for the same
declarations. Save normal and diagnostic dumps with bug reports or golden
tests. Read them in this order:

1. `ExecutionOrder`: is the pass live?
2. Pass `pipeline`, `producers`, and `sync`: is it on the expected queue and
   ordered after the expected work?
3. Texture and buffer transitions: are before/after states and `uav=1`
   reasonable?
4. `fork`, `join`, and `QueueDependencies`: where does cross-queue work wait?
5. Lifetimes: do `first`, `last`, and `transient` match the intended use?
6. The epilogue: does an external or extracted resource return to its required
   final state?

The dump format comes from
[`FARDGBuilder::DumpGraph`](../../Source/ArdaRenderGraph/Private/ArdaRenderGraphBuilder.cpp).

## Symptom: a pass is missing

The compiler removes work that does not reach an observable root. A pass stays
live when it contributes through producer edges to:

- a write of an imported external resource;
- an extracted graph-created resource; or
- a pass marked `NeverCull`.

A read-only pass, or a write to an unused graph-created resource, is normally
dead. Synchronization-only edges do not keep a pass alive.

Fix the data flow: add the real consumer, extract the result, or write the
external destination. Use `NeverCull` only for a genuine side effect that
resource declarations cannot express. Immediate mode can confirm culling as
the difference because it retains every registered pass.

## Symptom: compilation says “reads ... before it is produced”

A graph-created texture subresource or buffer has no earlier declared write.
Check:

- producer registration comes before the reader;
- texture mip and array-slice ranges overlap;
- the producer state is a write state;
- the reader is not relying on `NeverCull` as initialization; and
- persistent input was imported rather than recreated as an empty resource.

Texture production is checked per mip and array slice. Buffer production is
currently whole-buffer. Validation runs before culling, so even a dead invalid
pass fails compilation.

## Symptom: normal mode is wrong, immediate mode works

```cpp
FARDGRenderGraphContext Context = MakeContext();
Context.mDebugOptions.mbImmediateMode = true;
FARDGBuilder Graph(Context);
```

Immediate mode is the broad correctness oracle. It:

- disables pass culling;
- forces passes and sentinels to graphics;
- extends every live resource lifetime across the complete graph;
- records and submits serially; and
- requests immediate execution on NVRHI command lists.

It also prevents transient pool reuse because every interval overlaps. A bug
that disappears points to one or more of:

- an output that was not rooted;
- an undeclared resource or view;
- use of a physical handle outside its logical lifetime;
- missing cross-queue ordering;
- a callback that is unsafe during parallel recording; or
- an independently captured NVRHI object that does not match the declarations.

Immediate mode changes several variables together. Narrow the cause with the
options below, or disable parallel recording separately:

```cpp
FARDGExecuteOptions Options;
Options.mbParallelRecording = false;
Graph.Execute(Options);
```

## Symptom: ordering or state looks suspicious

```cpp
Context.mDebugOptions.mbConservativeBarriers = true;
```

For an equal non-`Common`, non-UAV state, conservative barriers force a
transition through `Common` before returning to the required state. Repeated
UAV access already receives explicit UAV ordering and is unchanged.

If this fixes the output, compare normal and conservative transition records.
Look for a callback using a state that differs from its parameter declaration,
an unlisted physical resource, or external work whose state the graph cannot
see. This mode intentionally adds barriers and is not a shipping performance
setting.

## Symptom: corruption changes when resources are reused

```cpp
Context.mDebugOptions.mbExtendResourceLifetimes = true;
```

Lifetime extension changes every compiled live interval to
`[0, executionOrder.size() - 1]`. Descriptor-compatible transient resources can
no longer reuse one allocation.

If the bug disappears, look for:

- a physical graph handle retained after its declared last use;
- a framebuffer or binding set captured independently from a graph resource;
- incomplete nested uniform-buffer declarations;
- a callback using a resource absent from its parameters; or
- assumptions that execution-local physical allocations persist across graphs.

The correct fix is a complete declaration or explicit extraction, not shipping
with extended lifetimes.

## Symptom: output depends on old or uninitialized contents

```cpp
Context.mDebugOptions.mbClobberFirstWrites = true;
```

Before a supported first write to a non-external resource, execution clears it
with recognizable data:

- floating or normalized color texture: magenta;
- integer color texture: `0xCDCDCDCD`;
- graphics depth/stencil target: depth `0.12345`, stencil `0xCD`;
- whole UAV buffer: `0xCDCDCDCD`.

Clobbering is skipped on copy queues. Texture clearing requires a supported
render-target/UAV color format, or a render-target depth/stencil format. A
buffer must be UAV-capable and the first declared write must cover the entire
buffer in `UnorderedAccess`. `mClobberedResourceCount` reports issued clears.

A visible pattern means the pass failed to overwrite everything its consumers
read. Clobbering is a diagnostic, not general initialization.

## Symptom: async compute or copy runs on graphics

Queue flags are requests constrained by capabilities and states. Inspect the
pass `pipeline` in `DumpGraph()` and the context:

```cpp
Context.mQueueCapabilities.mbCompute = DeviceHasComputeQueue;
Context.mQueueCapabilities.mbCopy = DeviceHasCopyQueue;
```

An `AsyncCompute` pass falls back to graphics when no compute queue is
available or its shader-resource access is pixel-only. Use
`NonPixelShaderResource` for compute-only reads. Combined `ShaderResource` is
normalized to non-pixel access after async selection.

A `Copy` pass falls back when no copy queue is available. Its declarations must
contain only `CopySource` and `CopyDest`; any other state is a validation error,
not a fallback. Likewise, a graphics-only state on a pass explicitly marked
`AsyncCompute` is rejected. Immediate mode always forces graphics.

Queue selection is deterministic and has no workload-cost heuristic. The
source is
[`AssignPipelines`](../../Source/ArdaRenderGraph/Private/ArdaRenderGraphCompiler.cpp).

## Symptom: a context getter reports an access-gate failure

Use `FARDGPassExecutionContext::GetTexture`, `GetBuffer`, and
`GetUniformBuffer` only while that pass callback is running. The context opens
one physical-access gate on construction and closes it on destruction.

Common messages have different causes:

- **gate cannot be opened**: execution state is invalid, the pass is culled or
  a sentinel, or the same pass gate is already active;
- **unavailable resource**: null, foreign, unmaterialized, or accessed outside
  the active callback;
- **absent from parameter declarations**: add the exact logical resource to the
  frozen parameter struct;
- **view absent from parameters**: declaring the parent does not substitute
  for declaring the exact SRV/UAV passed to the view getter;
- **uniform buffer absent from parameters**: include it with
  `ARDG_UNIFORM_BUFFER`.

Prefer context getters over independently captured physical handles. Raw
`nvrhi::ICommandList&` callbacks are supported, but the graph cannot prove
declaration completeness for handles used through them. Access-gate checks are
implemented in
[`ArdaRenderGraphBuilder.cpp`](../../Source/ArdaRenderGraph/Private/ArdaRenderGraphBuilder.cpp).

## Symptom: ownership, extraction, or lifecycle fails

- Import externally owned handles with `RegisterExternalTexture/Buffer`; do not
  pass `External` to `CreateTexture/Buffer`.
- Register the actual known entry state. The convenience overload reads the
  descriptor state but still rejects `Unknown`.
- Queue each resource and each output address for extraction once.
- Produce a graph-created resource before extracting it.
- Remember that extraction fills a handle after submission; it does not wait
  for GPU completion.
- Build a new `FARDGBuilder` for each workload. Compilation freezes topology,
  execution is one-shot, and any compile/execute failure permanently fails that
  builder.

## Symptom: work is unexpectedly serialized

Check producer and synchronization edges. Reads depend on the latest writer;
a later write also waits for readers since that writer. Current dependency
history is conservative for a logical texture as a whole and for every buffer
range. Other serialization sources are `NeverParallel`, manual dependencies,
immediate mode, and shared uniform-buffer upload waits.

`NeverParallel` affects CPU command-list recording, not GPU queue assignment.
Use it for callbacks touching non-thread-safe CPU state.

## What the graph cannot diagnose

The graph validates generated metadata, logical ownership, declared states and
ranges, production order, compiled transition continuity, and context-getter
access. It cannot prove:

- shader code agrees with declarations;
- a captured framebuffer, binding set, or raw handle names the declared
  resource and subresources;
- swap-chain acquire, pre-submit, and present ordering is correct;
- callback captures are thread-safe;
- external work changed an imported resource's state;
- an extracted resource is used only after the required GPU synchronization.

Keep declaration and binding construction close together and enable backend and
NVRHI validation.

## Current implementation limitations

- Builders are one-shot; there is no reset, incremental rebuild, or replay.
- Live passes retain registration order; there is no general topological
  reorder or scheduling-cost heuristic.
- Texture producer/readers and all buffer dependencies are conservative at
  whole-resource scope. Texture state/production validation is subresource
  aware; buffer state is whole-buffer.
- Portable placed aliasing is not active. Execution uses committed-resource
  descriptor pools; `mbUsedVirtualHeaps` and `mbUsedTransientAliasing` remain
  false.
- Pools are local to one execution, and resources crossing queue domains do not
  reuse pooled allocations.
- Raster groups are metadata only. There is no framebuffer creation,
  render-pass merge, load/store inference, or subpass execution.
- Execution records one command list per recorded pass, plus boundary lists
  when transitions require them and a separate uniform upload list.
- `Execute()` completes CPU submission, not GPU execution.
- Logical views describe parents and ranges but do not materialize NVRHI view
  objects or binding sets.
- Uniform buffers use dedicated allocations and uploads.
- Shaders, layouts, pipelines, framebuffer compatibility, swap chains, and
  presentation remain application responsibilities.

## Debugging checklist

1. Compile and save `DumpGraph()`.
2. Confirm the pass is live and the output is external, extracted, or consumed.
3. Check producer/synchronization edges and declared ranges.
4. Check selected pipelines, queue dependencies, fork/join, and queue
   capabilities.
5. Reproduce in immediate mode.
6. Disable parallel recording or mark the suspect callback `NeverParallel`.
7. Try conservative barriers.
8. Try extended lifetimes.
9. Try first-write clobbering and inspect `mClobberedResourceCount`.
10. Confirm imported initial and extracted final states.
11. Use context getters and declare each exact resource, view, and uniform
    buffer.
12. Run backend/NVRHI validation and retain execution-result telemetry.

---

[← Execution and queues](06-Execution-and-Queues.md) ·
[Documentation home](README.md) ·
[Next: Build and edge walkthrough →](08-Build-and-Edge-Walkthrough.md)
