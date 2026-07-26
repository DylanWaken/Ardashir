# 7. Debugging and recommended practices

[← Execution and queues](06-Execution-and-Queues.md) ·
[Documentation home](README.md)

## Debug options

Set `FARDGRenderGraphContext::mDebugOptions` before constructing the builder.

### Immediate mode

```cpp
Context.mDebugOptions.mbImmediateMode = true;
```

Immediate mode is a correctness oracle. It:

- disables pass culling;
- forces every pass and sentinel to graphics;
- extends every live resource lifetime across the complete graph;
- records/submits serially; and
- requests immediate execution on NVRHI command lists.

It also disables transient pool reuse because all intervals overlap. If a bug
disappears in immediate mode, investigate missing declarations, lifetime
assumptions, queue synchronization, parallel callback safety, and application
use of independently captured NVRHI handles.

### Conservative barriers

```cpp
Context.mDebugOptions.mbConservativeBarriers = true;
```

Equal non-`Common`, non-UAV states are forced through `Common`. Existing UAV
ordering remains explicit. This helps expose a missing state/order assumption,
but increases barriers and is not intended for shipping performance.

### Extended resource lifetimes

```cpp
Context.mDebugOptions.mbExtendResourceLifetimes = true;
```

Every compiled live resource interval becomes `[0, last execution index]`.
This disables lifetime-based pool reuse and helps identify use-after-lifetime or
incorrect externally retained handles.

### First-write clobbering

```cpp
Context.mDebugOptions.mbClobberFirstWrites = true;
```

Before a supported first write to a non-external resource, execution fills it
with recognizable data:

- floating/normalized color textures: magenta;
- integer color textures: `0xCDCDCDCD`;
- depth/stencil render targets on graphics: depth `0.12345`, stencil `0xCD`;
- whole UAV buffers: `0xCDCDCDCD`.

Clobbering is skipped on copy queues. Texture clears require a supported
render-target/UAV color format, or a render-target depth/stencil format.
Buffers require an entire-buffer UAV write. The result counts each issued
clobber in `mClobberedResourceCount`.

Clobbering catches accidental dependence on uninitialized data; it is not a
general resource initialization feature.

## Start with a graph dump

```cpp
Graph.Compile();
WriteDebugText(Graph.DumpGraph());
```

Check:

- Was the expected pass culled?
- Which producer or synchronization edge is missing?
- Did an async/copy request fall back to graphics?
- Are fork/join and cross-queue edges expected?
- Did the texture subresource reach the right state?
- Is a repeated UAV access marked `uav=1`?
- Does an external/extracted resource transition in the epilogue?
- Are lifetimes longer than expected?

`DumpGraph()` is deterministic, so it can be captured in golden tests.

## Common errors

### "reads ... before it is produced"

A graph-created resource has no earlier write for the requested texture
subresource or buffer. Add the producer first, correct the declared range, or
import initialized external data. `NeverCull` does not initialize a resource.

### Pass unexpectedly culled

Its output does not reach an external/extracted resource or `NeverCull` root.
Extract the intended output, write the external destination, add a real
consumer, or mark a genuine side-effect pass `NeverCull`.

### Pass unexpectedly serialized

Look for a shared logical resource. Reads depend on the latest write; writes
also synchronize after all prior readers. Buffers and dependency producer
tracking are whole-resource even when ranges differ. `NeverParallel`,
immediate mode, or a manual dependency also serializes recording.

### Async/copy pass runs on graphics

Confirm queue capabilities and state compatibility. Async compute rejects
graphics-only use and requires non-pixel access for shader resources. Copy
selection requires copy-only states. Immediate mode always forces graphics.

### Physical getter throws

Use `FARDGPassExecutionContext::GetTexture/GetBuffer/GetUniformBuffer` only
during that pass's callback, and include the exact resource/view/uniform buffer
in its frozen parameter struct. A parent resource declaration does not
substitute for declaring a particular view when the view getter is used.

### Invalid ownership or extraction

Use `RegisterExternalTexture/Buffer` for externally owned handles; do not pass
`External` to `CreateTexture/Buffer`. Queue extraction exactly once while
building, with a unique output address and known final state. A graph-created
resource must be written before extraction.

### Graph cannot be reused

This is intentional. A builder is one-shot and becomes immutable after
compilation. Any compile/execute exception makes it permanently failed. Build a
new graph, normally once per frame or workload.

## Recommended practices

1. Give every resource and pass a stable, descriptive name.
2. Declare every graph resource touched by a callback, including indirect use
   inside nested uniform-buffer metadata.
3. Prefer context getters over captured physical handles.
4. Use the narrowest correct texture subresource and buffer range, while
   remembering current buffer transitions/dependencies are whole-buffer.
5. Use explicit `NonPixelShaderResource` for compute-only reads.
6. Mark UAV-capable descriptors correctly before declaring UAV state.
7. Import persistent/history resources with their actual entry state and
   extract newly created history with its required next-frame state.
8. Reserve `NeverCull` for real side effects. Let unused pure GPU work cull.
9. Use manual dependencies only for ordering not represented by resource use.
10. Mark callbacks `NeverParallel` when their CPU-side captures are not
    thread-safe.
11. Inspect `FARDGExecutionResult` in debug/performance telemetry.
12. Test compilation separately from GPU execution where possible.

## Validation boundaries

The graph validates what it can see:

- generated parameter metadata;
- logical ownership and handle identity;
- declared NVRHI states and ranges;
- produced-before-read ordering;
- compiled transition continuity;
- context-based physical access.

It cannot prove correctness for:

- a framebuffer, binding set, or raw physical handle captured independently;
- shader code versus the declarations;
- application-side swap-chain acquire/present ordering;
- CPU thread safety inside pass callbacks; or
- use of an extracted resource after submission.

Keep declaration and binding construction close together so mismatches are easy
to review.

## Current implementation limitations

These are important when evaluating compiler output:

- **One-shot graph:** no reset, incremental rebuild, or repeated execution.
- **Registration-ordered schedule:** passes are culled but not generally
  topologically reordered; manual dependencies must point forward.
- **Whole-resource dependency history:** texture producer/readers and all
  buffer producer/readers are conservative across ranges. Texture state and
  initialization validation is subresource-aware; buffer state is whole-buffer.
- **No portable placed aliasing yet:** an ideal transient heap layout can be
  calculated, but execution uses committed-resource descriptor pools.
  `mbUsedVirtualHeaps` and `mbUsedTransientAliasing` remain false.
- **Execution-local pools:** physical resource pools do not persist across
  builders/frames.
- **Queue-domain reuse restriction:** a transient resource used on multiple
  queues cannot reuse a pooled allocation.
- **Raster groups are metadata only:** there is no automatic framebuffer
  creation, render-pass merge, load/store inference, or subpass execution.
- **One command list per recorded pass:** plus sentinel command lists when
  boundary transitions are required, and a separate uniform upload list.
- **No GPU completion wait:** execution completes CPU submission only.
- **No scheduling heuristic:** queue assignment is flag/capability/state based;
  the compiler does not estimate costs or reorder for maximum overlap.
- **No view-object materialization:** logical SRV/UAV records identify parents,
  subresources/ranges, and overrides; pass code still builds appropriate NVRHI
  binding items.
- **Uniform buffers are dedicated:** they are created/uploaded per graph and are
  not pooled.
- **No automatic NVRHI pipeline setup:** shaders, layouts, binding sets,
  pipelines, framebuffer compatibility, and presentation remain application
  responsibilities.

For NVRHI API details, use the
[NVRHI programming guide](https://github.com/NVIDIA-RTX/NVRHI/blob/main/doc/ProgrammingGuide.md)
and [repository](https://github.com/NVIDIA-RTX/NVRHI).

## Debugging checklist

- Reproduce with `mbImmediateMode`.
- Compare normal and immediate `DumpGraph()` output.
- Enable `mbConservativeBarriers`.
- Enable `mbExtendResourceLifetimes`.
- Enable `mbClobberFirstWrites`.
- Disable parallel recording or mark the suspect pass `NeverParallel`.
- Confirm imported initial and extracted final states.
- Check context getter use and parameter completeness.
- Inspect queue capabilities, selected pipelines, queue dependencies, and wait
  counts.
- Run with backend/NVRHI validation enabled.

---

[← Execution and queues](06-Execution-and-Queues.md) ·
[Documentation home](README.md)
