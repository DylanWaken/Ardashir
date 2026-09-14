# Device-wide GPU allocator

## Scope

ArdaBackend's `Allocator` section owns reusable memory planning and shares
native heap/resource caches between all graphs and ordinary RHI resource callers
on the same device. It adapts Unreal's persistent heap cache, placed-resource
cache, and delayed reclamation patterns to Arda's provider and submission
ownership contracts. It is enabled automatically for concrete backend devices.

## Ownership and behavior

- One allocator belongs to each concrete RHI device. No global singleton spans
  devices or backend implementations.
- Compiled graphs retain their frame-slot leases while live. Their heaps become
  available to other graphs when those leases and GPU references are released;
  the cache does not move or alias storage that another graph still owns.
- Public resources and provider commands retain allocation leases. Submitted
  work keeps those leases until its existing queue completion mechanism retires.
  The last lease returns an object to its device cache. Cache callbacks use weak
  references so resources do not create device/cache cycles.
- Explicit heaps preserve the requested capacity and memory type contract.
  Graph allocation budgets remain exact; retained unused global cache bytes are
  reported separately by the device. The backend memory planner suballocates
  these heaps with lifetime-aware aliasing.
- Resource caches match the complete storage descriptor, excluding debug names;
  placed resources additionally match native heap identity and offset. Active
  resources are never lent to a second independent caller.
- Cached native objects are reused only if the provider verifies their actual
  state and queue ownership satisfy the requested initial state. Reuse never
  silently resets GPU state. An incompatible object is replaced.
- The cache retains empty heaps for 16 collection cycles, and considers aged
  resource entries for eviction after 32 cycles when their per-kind, per-heap
  cache exceeds 64 entries. Committed resources have a separate pool per kind.
  A 512 MiB idle-byte limit and explicit trimming reclaim unused storage.
- Graphs use atomic placed-resource acquisition, avoiding creation of disposable
  native virtual objects before discovering a placed-resource cache hit.

## Integration and controls

Include `Allocator/ArdaMemoryPlanner.h` for the backend-neutral planner and
`Allocator/ArdaGpuAllocator.h` for cache policy and statistics. Existing RHI
`CreateBuffer`, `CreateTexture`, `CreateHeap`, and virtual-resource binding calls
use the device allocator. Graph materialization uses `CreatePlacedBuffer` and
`CreatePlacedTexture` to acquire a cached placed object in one operation.

`IArdaRHIDevice::RunGarbageCollection()` first retires completed provider work,
then advances the allocator collection cycle. Graph execution already calls it;
direct RHI clients should call it during their normal frame maintenance.
`GetGpuAllocatorStats()` reports cache hits, native creation counts, active plus
cached backing bytes, and idle bytes. Graph `mAllocatedBytes` measures its leased
footprint, including cache hits; it is not a native allocation counter.

`SetGpuAllocatorOptions()` changes retention ages, soft object capacities and the
idle-byte limit. Setting the byte limit to zero disables retention and releases
idle entries immediately. Setting an object capacity to zero still honors its
retention age. `TrimGpuAllocator()` releases idle objects and heaps without
waiting for or invalidating active GPU work.

Native upload/readback and temporary ray-tracing scratch/instance/indirect
buffers use the same allocator. Internal buffer pools distinguish the requested
Graphics, Compute and Copy queues; Vulkan verifies the actual owning queue
family before reuse. The cached path records actual synchronization state and
restores the initial state with barriers where necessary.

External imports, CUDA interop, sparse resources and versioned buffers remain
outside reuse. Specialized native storage (such as acceleration-structure
results, shader tables, workgraph backing, staging textures and timestamp
readback) retains its existing allocation path. Allocator statistics cover only
managed storage, not total device VRAM or residency. Explicit heaps retain exact
requested sizes; ordinary committed resources are cached whole rather than
suballocated into fixed-size slabs.

## Validation

Use the existing Ninja Debug build with MSVC and both native providers:

```bat
call "D:\VisualStudio\Common7\Tools\VsDevCmd.bat" -no_logo -arch=x64 -host_arch=x64
"C:\Program Files\CMake\bin\cmake.exe" --build build --config Debug --target ArdaBackendTests ArdaGraphTests ArdaBackendPublicHeaders --parallel 4
build\Source\ArdaInfra\ArdaBackend\ArdaBackendTests.exe --gtest_filter=FArdaGpuAllocatorTest.*:ArdaMemoryPlanner.*:NativeProviders/FArdaGpuAllocatorGpuTest.*
build\Source\ArdaInfra\ArdaRenderGraph\ArdaGraphTests.exe
```

The graph test binary is under `build/Source/ArdaInfra/ArdaRenderGraph` (the
module retains its existing `ArdaGraphTests` executable name). Run targeted
native allocator, memory, submission and resource-state checks first, then the
backend and graph suites. Allocator tests cover concurrent leases and trimming,
failed creation/binding, descriptor and placement validation, GPU submission
retention, native state compatibility, and eviction. The graph regression
recompiles and recreates graphs with two frame slots and verifies that native
heap and buffer creation counts stay flat after warm-up.

Verified on 2026-09-14 with the configured D3D12 and Vulkan Debug build:

- Full project build, including public-header checks and configured examples: passed.
- Backend suite: 415 passed, 145 unavailable-feature skips, zero failures.
- Graph suite: 167 passed, one unavailable Work Graph feature skip, zero failures.
- RHI suite: 17 passed.
- RHITest, ARDGExample and CornellBox smoke/validation checks: 17 passed.
- The 35 allocator-specific CPU/GPU tests all passed (included in the backend
  totals), including creation plateaus for upload/readback on all supported queues.
- Project formatter checks and generated API inventory synchronization: passed.

## Source reference

The local Unreal 5.8.1 checkout provides the architectural reference:

- `Engine/Source/Runtime/RHICore/Private/RHICoreTransientResourceAllocator.cpp`
  and its public header: persistent heap and placed-object caches.
- `Engine/Source/Runtime/RenderCore/Private/RenderGraphResourcePool.cpp`:
  reusable RDG resource pools.
- `Engine/Source/Runtime/D3D12RHI/Private/D3D12TransientResourceAllocator.cpp`:
  native heap and resource creation.

This implementation uses Arda provider objects, portable descriptors and
existing completion retention; it has no Unreal build or runtime dependency.
