# Debugging, Performance, and Programming Practices

[Previous](09-Backends-and-Interop.md) · [Home](README.md) · Next: [Feature/API reference](11-Feature-and-API-Reference.md)

## Validation stack

Use all available layers in development:

1. NVRHI validation wrapper;
2. D3D debug layer or Vulkan validation layers;
3. NVRHI message callback promoted to test failure;
4. GPU-based validation selectively for difficult issues;
5. Nsight Graphics, PIX, or RenderDoc capture;
6. Aftermath for deployed GPU crash diagnosis when integrated.

NVRHI validation catches abstraction contracts that a native layer may not see: layout/set mismatch, illegal command ordering, unsupported state combinations, and AS capacity/update mistakes. Native validation catches backend misuse after translation. Neither can understand application semantics such as a wrong matrix or stale descriptor index.

## Debug names and markers

Name every long-lived or pass-local GPU object:

```cpp
desc.setDebugName("GBuffer/NormalRoughness mip0 1920x1080");
```

Names should identify subsystem, role, and important variant. Avoid per-frame random suffixes that make captures hard to compare.

Bracket command regions:

```cpp
commandList->beginMarker("Lighting/Deferred");
// work
commandList->endMarker();
```

Use nested markers for frame → phase → pass. Always balance marker calls, including early-return paths; a small RAII wrapper around `beginMarker`/`endMarker` is useful.

## GPU timer queries

```cpp
auto query = device->createTimerQuery();

commandList->beginTimerQuery(query);
RecordPass();
commandList->endTimerQuery(query);
```

After completion:

```cpp
if (device->pollTimerQuery(query))
{
    float seconds = device->getTimerQueryTime(query);
    device->resetTimerQuery(query);
}
```

`getTimerQueryTime` can block. Rotate query sets by frame and poll old frames. `DeviceDesc::maxTimerQueries` sizes the backend pool; plan the maximum simultaneous unresolved queries.

Timers measure GPU time on one command list/queue. They do not directly describe overlap across queues or CPU submission cost.

## Device loss and fatal errors

Centralize message handling and preserve:

- severity;
- backend/API;
- current frame and pass marker;
- recent resource creations;
- selected features;
- driver/device identifiers.

Treat `waitForIdle() == false`, native present failure, or fatal callback messages as a device-loss path. Stop submission before tearing down. If Aftermath is enabled, keep crash-dump processing objects alive until collection completes.

## Object and lifetime practices

- Prefer typed handles, never manual `AddRef`/`Release`.
- Let a pass/subsystem own immutable resources it creates.
- Keep the message callback alive longer than all devices.
- Keep wrapped native objects alive according to both ownership systems.
- Call `runGarbageCollection()` at least once per frame.
- Use per-thread/queue lifetime trackers for high-volume parallel submission.
- Never disable `trackLiveness` without a written retirement policy.
- Descriptor tables never own resources; maintain a descriptor-index-to-handle registry.
- GPU addresses do not own their source objects.

## Creation and cache practices

Create/cache outside hot recording loops:

- shaders and input layouts;
- samplers;
- binding layouts;
- regular binding sets;
- framebuffers;
- pipelines;
- static descriptor tables.

Cache keys must include complete semantic configuration. Hashing only pointers or shader names causes variant collisions.

Do not cache objects forever without budget/eviction. NVRHI defers native destruction, so short bursts of evictions can temporarily increase memory.

## Command-list practices

- Reuse ordinary per-frame command-list objects.
- Use a temporary list for massive uploads/AS builds, then destroy it to release manager working sets.
- Record independent lists in parallel; serialize only submission/dependencies.
- Match `CommandListParameters::queueType` with submission queue.
- Set upload/scratch chunk sizes based on measured allocation patterns.
- Cap RT scratch memory intentionally.
- Avoid tiny command lists that inflate submission overhead.
- Avoid one huge command list that prevents CPU parallelism or queue overlap.
- After native commands, call `clearState()` before NVRHI state setting.

## Resource-state practices

- Assign one documented boundary policy per resource.
- Prefer permanent states for truly immutable data.
- Prefer subresource ranges for mip/array pipelines.
- Use automatic barriers until profiling identifies CPU/barrier pressure.
- If using a render graph, let it own explicit entry/exit states.
- Treat UAV barriers as data-dependency edges, not boilerplate.
- Include bindless and native accesses in application-level tracking.
- Never use `Unknown` to avoid choosing a real state.

## Constants

Choose by behavior:

- push constants: ≤128 bytes, tiny and high frequency;
- volatile CB: changing blocks, written one or more times per command list;
- regular CB: persistent, copied, shared, or explicitly ranged data;
- SRV buffer: large structured parameter arrays.

Write volatile CBs before `set*State` when possible; writing afterward can cost more. Set push constants only after state. On Vulkan, compute worst-case versions:

```text
maxVersions ≥ framesInFlight × maximum writes per frame before retirement
```

Account for parallel lists and exceptional passes, not only average usage.

## Binding practices

- Group layouts by update frequency.
- Reuse stable sets.
- Keep shader declarations and C++ layout definitions generated from or checked against one source of truth.
- Use explicit register-space/descriptor-set policy across all layouts.
- Keep descriptor-table writes deferred or frame-versioned.
- Never overwrite a bindless descriptor still visible to an in-flight frame.
- Use permanent states for read-only bindless assets where practical.
- Validate every resource view format/range/subresource in debug builds.

## Graphics practices

- Sort work to minimize pipeline and set changes without breaking correctness.
- Build pipeline variants lazily, warm known variants before gameplay.
- Keep framebuffer info in pipeline cache keys.
- Use reverse-Z consistently if selected.
- Do not use copy as a substitute for a conversion/blit shader.
- Prefer counted indirect drawing for GPU-driven work, with D3D11 fallback.
- Capability-gate VRS, meshlets, conservative raster, and stereo.

## Compute practices

- Keep bounds checks.
- Establish every UAV dependency.
- Combine small adjacent kernels only when it improves measured performance and maintainability.
- Avoid immediate readback.
- Prefer generated indirect arguments.
- Use async compute only when measured overlap exceeds synchronization/contention cost.
- Query exact cooperative-vector formats rather than one broad boolean.

## Ray-tracing practices

- Keep build input buffers alive through build execution.
- Keep BLASes alive when TLAS instance buffers contain only GPU addresses.
- Match AS creation capacity to worst legal build.
- Do not update compacted structures.
- Keep payload, attributes, and recursion depth minimal.
- Make geometry opaque only when semantically valid.
- Keep shader-table index generation next to TLAS instance generation.
- Isolate advanced vendor features behind fallbacks.

## Readback without stalls

For each frame slot:

1. Copy results to that slot's staging object.
2. Record an event query after the copy.
3. Submit and continue.
4. In later frames poll the old event.
5. Map only after completion.
6. Copy to CPU-owned memory, unmap, reset, and recycle.

This applies to screenshots, counters, picking, timestamps, and debug data.

## Common failure patterns

### Empty creation handle

Likely descriptor incompatibility, unsupported feature/format, invalid shader binary, or exhausted descriptor capacity. Read the message callback; do not continue with a null handle.

### Vulkan-only validation errors

Usually descriptor-set/register-space or DXC binding-offset mismatch, missing enabled feature/extension, volatile CB version exhaustion, or stricter layout/state rules.

### Intermittent flicker/corruption

Suspect missing UAV ordering, descriptor overwrite while in flight, incorrect queue wait, recycled staging/frame data, or released bindless/GPU-address resources.

### Device removal during RT

Suspect AS creation capacity smaller than build, invalid GPU instance descriptors/addresses, released BLAS, unsupported geometry extension, or scratch-memory pressure. Enable NVRHI RT validation and native validation where available.

### Memory grows after loading

Command-list upload/scratch managers do not shrink, deferred objects await garbage collection, caches have no eviction, or in-flight frames prevent retirement. Destroy one-shot lists and inspect trackers.

### State looks correct but native commands break later draws

NVRHI's state cache no longer matches native state. Call `clearState()` after native recording and fully set NVRHI state again.

## Review checklist

- All creation return values checked.
- Validation enabled in debug and CI GPU smoke tests.
- Optional features and formats queried.
- No steady-state `waitForIdle`.
- No unbounded pipeline/set/descriptor cache.
- No descriptor-table write races.
- No raw GPU address without explicit handle ownership.
- Garbage collection and query recycling happen every frame.
- Queue dependency graph is acyclic and documented.
- Upload/readback/build peaks have bounded memory.
- Native interop has state handoff and cache reset.

