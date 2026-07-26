# 5. Compilation

[← Passes and dependencies](04-Passes-and-Dependencies.md) ·
[Documentation home](README.md) ·
[Next: Execution and queues →](06-Execution-and-Queues.md)

`Compile()` is device-independent. It runs once, returns
`const FARDGCompileResult&`, and makes graph topology immutable.

## Compilation sequence

The current implementation performs these stages in order:

1. Validate resource ownership, descriptors, flags, states, ranges, and
   produced-before-read rules.
2. Reject extraction of a non-external resource with no producer.
3. Append `GraphEpilogue` and connect external/extracted outputs.
4. Assign graphics, async-compute, or copy pipelines with deterministic
   fallback.
5. Build consumer and synchronization-consumer edges.
6. Cull passes and form deterministic execution order.
7. Rebuild live resource use after culling.
8. Compile resource lifetime intervals.
9. Compile texture-subresource and whole-buffer transitions.
10. Validate transition continuity and required/final states.
11. Compute async fork/join diagnostics.
12. Lower cross-queue edges.
13. Group compatible consecutive raster passes.

![Graph lifecycle](assets/graph-lifecycle.svg)

## Validation

Compilation rejects errors early, including:

- unknown or incompatible pass/resource flags;
- graph-created resources incorrectly marked external/extracted;
- missing external backing or external+transient combinations;
- null/foreign resources and views in parameter structs;
- `Unknown`, illegal, or texture/buffer-incompatible states;
- multiple write states or write+read combinations in one state;
- UAV use without `isUAV`/`canHaveUAVs`;
- attachment use without `isRenderTarget`;
- out-of-range texture subresources or buffer byte ranges;
- graphics-only state on an `AsyncCompute` pass;
- non-copy state on a `Copy` pass;
- reading a graph-created texture subresource before production;
- reading a graph-created buffer before any write;
- duplicate/invalid extraction; and
- discontinuous or unsatisfied compiled transitions.

A pass may read and write the same not-yet-produced resource in one pass; the
write makes that pass's read legal. External resources count as produced at
entry.

Textures validate production per mip and array slice. Buffers use a whole-buffer
produced bit, so one range write makes later range reads pass this specific
check.

Validation runs before culling and checks every registered pass. An otherwise
dead pass with an invalid state, range, or read-before-produce error still makes
compilation fail.

## Queue selection and fallback

Graphics capability is mandatory. Sentinels and immediate-mode passes use
graphics.

A `Copy` pass uses the copy queue only when:

- `mbCopy` is true; and
- every declared texture and buffer state consists only of `CopySource` and/or
  `CopyDest`.

A `Compute | AsyncCompute` pass uses async compute only when:

- `mbCompute` is true;
- no declared state is graphics-only (`RenderTarget`, depth states, `Present`,
  or `ShadingRateSurface`); and
- a pixel-shader-resource state also includes non-pixel shader access.

Otherwise eligible requests fall back to graphics. Note that validation is
stricter for flags explicitly claiming a queue: a `Copy` pass containing UAV
state or an `AsyncCompute` pass containing a graphics-only state is rejected,
not silently repaired.

For async compute, combined `ShaderResource`
(`PixelShaderResource | NonPixelShaderResource`) is normalized to
`NonPixelShaderResource`.

## Culling and live intervals

Culling follows producer edges backward from the epilogue and `NeverCull`
passes. Live passes retain registration order, including prologue and epilogue.
Resource use is rebuilt from only that live order.

`FARDGResourceLifetime` contains:

- texture/buffer type and registry index;
- inclusive first and last execution-order indices; and
- transient eligibility.

External/extracted resources include the epilogue. Debug lifetime extension
makes every live resource span the complete execution order.

![Resource lifetime and pool reuse](assets/resource-lifetime.svg)

## State compilation

Unknown resource entry states become `Common`. The compiler tracks:

- one current state per texture mip/array-slice pair; and
- one current state per whole buffer.

Within a pass, compatible read states merge with bitwise OR. Equal states still
produce transition records because execution uses them to establish physical
tracking and ordering.

### UAV barriers

When a resource remains in `UnorderedAccess` across accesses, the transition has
`mbUAVBarrier = true`. During recording, automatic NVRHI barriers are disabled,
UAV barriers are enabled explicitly for the resource, and the requested state
is committed.

This applies per texture subresource and per whole buffer.

### Conservative equal-state barriers

With `mbConservativeBarriers`, an equal, non-`Common`, non-UAV state gets
`mbForceBarrier`. Execution forces a transition through `Common` and commits it
before returning to the required state. This is a diagnostic correctness mode,
not a performance setting.

### Final transitions

The epilogue transitions every used external or extracted resource to its final
state. For imports that is initially the registered state; extraction changes
it to the requested state. Equal final UAV state still emits an ordering
barrier.

## Async metadata and queue edges

For each live async-compute pass:

- `mAsyncFork` is the latest reachable graphics producer, or prologue; and
- `mAsyncJoin` is the earliest reachable graphics consumer, or epilogue.

These are diagnostics. Actual submission synchronization comes from
`mQueueDependencies`, generated for every live producer or synchronization edge
whose endpoints use different pipelines.

![Cross-queue synchronization](assets/queue-sync.svg)

## Raster groups

Compatible, consecutive graphics raster passes share an integer raster-group
index. Compatibility is exact equality of logical color/depth attachment
handles and their subresource sets. `SkipRenderPass` and non-raster passes break
groups. Grouping currently does not merge command lists or render passes.

## Compile result

`FARDGCompileResult` exposes:

- `mPrologue` and `mEpilogue`;
- `mExecutionOrder`, including sentinels;
- `mRasterGroupCount`;
- `mResourceLifetimes`; and
- `mQueueDependencies`.

Detailed per-pass products are available through
`Graph.TryGetPass(Handle)->GetState()`: culling, pipeline, producers,
synchronization edges, texture/buffer states, views, uniform buffers,
transitions, fork/join, and raster group.

## Deterministic graph dump

```cpp
const FARDGCompileResult& Result = Graph.Compile();
std::string Dump = Graph.DumpGraph();
```

The dump reports debug options, execution order, resources and last producers,
all passes (including culled and sentinel state), transitions with readable
state names/UAV/forced flags, lifetimes, and cross-queue dependencies. It is
stable for the same declarations and useful in tests or bug reports.

---

[← Passes and dependencies](04-Passes-and-Dependencies.md) ·
[Documentation home](README.md) ·
[Next: Execution and queues →](06-Execution-and-Queues.md)
