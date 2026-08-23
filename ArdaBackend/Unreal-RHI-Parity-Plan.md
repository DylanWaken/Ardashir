# ArdaBackend and Unreal RHI/RDG parity review

> [ArdaRHI feature guide](rhi-feature-guide.html) is the feature-by-feature user
> guide with diagrams. This document is the research record, comparison,
> implementation plan, as-built status, and acceptance contract.

## Scope and method

This review compares ArdaBackend, ArdaRHI, and ArdaRenderGraph with the local
Unreal Engine 5.8.1 snapshot at `D:\UnrealEngine\Engine`. Research and planning
were completed before implementation. The result deliberately targets 64-bit
desktop D3D12 and desktop Vulkan; console, mobile, and Metal breadth are not
parity goals.

Support labels are strict:

- **Implemented** means the public facade reaches a native API and a
  non-interactive execution/lifecycle test exists.
- **Capability-gated** means the complete portable contract exists, native
  probing is truthful, and unavailable adapters return `Unsupported`.
- **Partial** means an executable subset is narrower than Unreal or the native
  API, and the missing semantics are named.
- **Out of scope** means the omission is deliberate for this desktop RT/ML
  renderer rather than an untracked implementation gap.

The main Unreal research locations were:

- `Engine/Source/Runtime/RHI/Public/RHIAccess.h`
- `Engine/Source/Runtime/RHI/Public/RHIPipeline.h`
- `Engine/Source/Runtime/RHI/Public/RHITransition.h`
- `Engine/Source/Runtime/RHI/Public/RHIResources.h`
- `Engine/Source/Runtime/RHI/Public/RHIContext.h`
- `Engine/Source/Runtime/RHI/Public/RHICommandList.h`
- `Engine/Source/Runtime/RHI/Public/RHIValidationCommon.h`
- `Engine/Source/Runtime/RHI/Public/RHIValidationContext.h`
- `Engine/Source/Runtime/RHI/Public/RHITransientResourceAllocator.h`
- `Engine/Source/Runtime/RenderCore/Public/RenderGraphBuilder.h`
- `Engine/Source/Runtime/RenderCore/Private/RenderGraphBuilder.cpp`
- `Engine/Source/Runtime/VulkanRHI/Private/VulkanBindlessDescriptorManager.cpp`
- `Engine/Source/Runtime/VulkanRHI/Private/VulkanPipeline.cpp`
- `Engine/Source/Runtime/VulkanRHI/Private/VulkanCommandBuffer.cpp`
- `Engine/Plugins/Tests/RHITests/Source/RHITests`

The main Arda implementation and evidence locations are:

- `Source/ArdaBackend/Public/RHI/ArdaRHICapabilities.h`
- `Source/ArdaBackend/Public/RHI/ArdaRHIDevice.h`
- `Source/ArdaBackend/Public/RHI/ArdaRHIResources.h`
- `Source/ArdaBackend/Public/RHI/ArdaRHIProvider.h`
- `Source/ArdaBackend/Private/RHI/ArdaRHIDevice.cpp`
- `Source/ArdaBackendImpls/D3D12/ArdaD3D12Backend.cpp`
- `Source/ArdaBackendImpls/Vulkan/ArdaVulkanBackend.cpp`
- `Source/ArdaRenderGraph/Private/ArdaRenderGraphExecutor.cpp`
- `Source/ArdaBackend/Tests/ArdaResourceStateConformanceTests.cpp`
- `Source/ArdaBackend/Tests/ArdaExtendedRHIParityTests.cpp`
- `Source/ArdaRenderGraph/Tests/ArdaGraphTests.cpp`

Native behavior was cross-checked against the D3D12 Agility SDK 1.619.5
headers, Vulkan-Headers 1.4.357, DXC 1.9, the local adapter feature data, the
[DirectX Raytracing specification](https://microsoft.github.io/DirectX-Specs/d3d/Raytracing.html),
[Sampler Feedback specification](https://microsoft.github.io/DirectX-Specs/d3d/SamplerFeedback.html),
[Work Graphs specification](https://microsoft.github.io/DirectX-Specs/d3d/WorkGraphs.html),
and the [current Vulkan specification](https://registry.khronos.org/vulkan/specs/latest/html/vkspec.html).

## Research conclusions that shaped the design

### Capabilities are a family, not a Boolean

Unreal reports ray tracing, descriptors, queues, residency, and advanced
pipelines as structured capability families. A single `mbRayTracing` value
cannot decide whether a module that needs compaction, local SBT data, indirect
trace, or hardware RT cores can run. Arda therefore publishes structured
sub-capabilities in `FArdaRHICapabilities`, which serves as the immutable
per-device ability table after initialization.

`FArdaRHICapabilities::Evaluate` compares the table with
`FArdaRHIFeatureRequirements` and returns every missing ability.
`GetArdaRHIProfileRequirements` maps `EArdaRHIDeviceProfile` to a standard
requirement set. Built-in profiles distinguish basic RT infrastructure,
real-time hardware RT, and
real-time RT plus ML facts. A future software tracer may omit
`mbRequireHardwareRayTracing`; real-time Ardashir modules do not.

### Ray tracing has four independent lifecycles

1. Geometry and instances determine BLAS/TLAS prebuild sizes.
2. Build/update consumes input and scratch storage and changes AS lifecycle
   state.
3. Optional native property queries determine compacted storage followed by a
   compact copy.
4. Libraries, hit groups, local layouts, SBT records, and direct/indirect
   dispatch form the execution ABI.

D3D12 uses DXR prebuild info, state-object subobjects, export/root-signature
associations, shader identifiers, `DISPATCH_RAYS`, and AS compact copy. Vulkan
uses KHR build sizes/commands/property queries, shader groups, aligned strided
device-address SBT regions, and `vkCmdTraceRaysIndirectKHR` when supported.

### Descriptor models must not be conflated

Vulkan descriptor indexing, `VK_EXT_descriptor_buffer`, and
`VK_EXT_descriptor_heap` are distinct mechanisms. Ordinary descriptor-set
support does not imply partially bound, update-after-bind, variable count, or
direct heap indexing. D3D12 direct indexing similarly requires the appropriate
root-signature/shader model flags. Arda reports each fact separately and uses
the selected layout ABI consistently from pipeline creation through command
binding.

### Queue identity is part of resource state

Dedicated Vulkan compute/transfer families require family-specific command
pools, timeline-semaphore dependencies, and paired release/acquire ownership
barriers. Multiple handles from the graphics family are not equivalent. The
Vulkan native state snapshot consequently includes layout, stage/access masks,
and queue-family owner.

### Sparse residency separates virtual shape from committed bytes

D3D12 reserved resources use tile mappings and DXGI memory-budget telemetry.
Vulkan sparse resources use sparse requirements and `vkQueueBindSparse`, with
budget telemetry from `VK_EXT_memory_budget`. Sparse buffers, 2D images, 3D
images, aliased mappings, reservation, and sparse-capable queue availability
must be queried independently.

### Platform-specific features stay platform-specific

- Native sampler feedback is D3D12-only in this contract. Vulkan reports false.
- Opacity micromaps use `VK_EXT_opacity_micromap`; D3D12 reports false.
- Work graphs use D3D12 Shader Model 6.8/options21. Vulkan AMDX shader enqueue
  is not claimed.
- Shader bundles remain a portable prepared-record abstraction independent of
  whether a future backend uses work graphs or device-generated commands.
- Custom present belongs to the swap-chain boundary, not merely device
  capability reporting.

## Frozen implementation plan

The plan was completed in this dependency order so support bits could not get
ahead of native execution:

1. Add structured capabilities, ability-table evaluation, RT/ML profiles, and
   admission/rejection tests.
2. Add per-queue native signaling, D3D12 GPU waits, Vulkan family selection,
   timeline waits, and ownership transfer.
3. Complete BLAS/TLAS sizing, creation, build/update, state tracking, compacted
   size queries, and compact copy on both APIs.
4. Complete hit groups, local export associations, aligned local SBT arguments,
   persistent records, and direct/indirect trace dispatch.
5. Enable Vulkan KHR ray tracing and EXT mesh shader pipelines/dispatch.
6. Implement unbounded/update-after-bind/variable-count descriptor tables and
   actual direct resource/sampler heap indexing on both APIs.
7. Implement D3D12 reserved and Vulkan sparse resources, mapping/commit,
   budgets/reservations, and mutable resource collections.
8. Implement native D3D12 sampler feedback and Vulkan opacity-micromap
   build/compaction/BLAS attachment.
9. Implement D3D12 work graphs, portable shader bundles, and desktop custom
   present.
10. Extend native/RDG conformance tests, regenerate API inventories, and update
    the feature guide with SVG explanations only after behavior stabilized.

## As-built feature comparison

### Resource states and RDG validation — implemented

Like Unreal, Arda separates abstract access from pipeline/queue context and
tracks mip/array/format-plane ranges. It supports explicit before/after
descriptors, read-only combinations, split begin/end transitions, discard,
UAV ordering, alias barriers, and persistent submitted-state validation.

The key acceptance improvement is independent evidence. Tests compare:

1. the compiled RDG expected state and owner;
2. the facade command-list tracker;
3. the common native-backend tracker; and
4. exact D3D12 state bits or Vulkan layout/stage/access/family values.

D3D12 has no driver query for current resource state, so “native state” means
the exact persisted `D3D12_RESOURCE_STATES` value used for emitted barriers plus
a clean debug-layer result. Vulkan evidence includes exact synchronization2
encoding and validation output.

### Copies, resolve, staging, and indirect work — implemented

Both backends execute buffer/texture regions, multisample resolve, pitched
staging upload/readback, direct draw/dispatch, indirect draw/indexed draw,
indirect compute, mesh dispatch, and indirect ray dispatch with range, usage,
format, sample, state, and native-output checks. Unreal remains broader in
multi-draw/count variants, conversion/reallocation policy, and platform copy
specializations.

### Heaps, aliasing, sparse residency, and collections — implemented

Both backends expose memory requirements, committed/virtual/placed resources,
explicit heaps, alias barriers, and RDG transient heap placement. D3D12
reserved resources and Vulkan sparse buffers/2D images support map, unmap,
prefix commit/decommit, streaming budgets, and reservation where native support
exists. Mutable resource collections retain validated mixed resource members
and can expose a direct descriptor base.

Arda's allocator and sparse shape breadth remain smaller than Unreal's many
platform policies. The capability structure identifies sparse 3D and aliased
mapping support rather than overclaiming them.

### Descriptor indexing and direct heaps — implemented and gated

Conventional sets and mutable bindless tables execute on D3D12 and Vulkan.
Tables support unbounded shader declarations, partially bound entries,
update-after-bind/update-unused-while-pending, variable live counts, typed null
descriptors, register-space/descriptor-set identity, and in-flight version
retention.

The direct path uses D3D12 resource/sampler heaps or actual
`VK_EXT_descriptor_heap` resource/sampler heaps. Vulkan encodes native
descriptors into mapped device-address storage, maps heap sources into every
pipeline stage, binds both heaps, and pushes each layout's allocation base.
Execution tests sample a texture through both a resource heap and sampler heap,
so nonzero base remapping is verified rather than inferred.

### Mesh and full ray-tracing pipelines — implemented and gated

D3D12 and Vulkan create and execute native mesh pipelines on capable adapters.
Both create ray pipelines with library exports, hit groups, payload/attribute
limits, recursion depth, global layouts, local export associations, shader-table
records, inline local bytes, and direct/indirect trace dispatch.

BLAS/TLAS creation, build/update, indirect instance-buffer build, native
compacted-size query, compact destination creation, and compact copy execute on
both APIs. State tests validate the facade/common/native lifecycle before and
after submission.

Vulkan opacity micromaps support native sizing, build, compact copy, and BLAS
attachment. On drivers that advertise compact copy but reject the optional
micromap compacted-size query-pool type, Arda returns the legal conservative
source storage size for the destination and still executes native compact mode.

### Queues and synchronization — implemented and gated

D3D12 owns direct, compute, and copy queues with queue-specific fences and GPU
waits. Vulkan selects dedicated compute and transfer families when available,
creates family-specific pools, submits timeline-semaphore wait edges, and
lowers release/acquire ownership transfer. Sparse binds share the same ordering
model. RDG uses queue fallback only when compilation policy permits it.

Physical RDG replay now performs a complete resource handoff whenever adjacent
uses select different queues: the producer transitions to `Common`, emits the
release, the consumer queue waits on the producer instance, then the consumer
acquires and transitions to its declared state. `QueueRelease` and
`QueueAcquire` conformance records prove that RDG, facade, common backend, and
native state agree at both boundaries. The dedicated-queue regression uploads
known bytes, exposes the buffer as an async-compute UAV, reads it back through
the copy queue, and validates both the bytes and every state checkpoint. This
also prevents D3D12 copy command lists from encoding an illegal transition whose
before-state is a shader-only UAV state.

### Sampler feedback — D3D12 implemented, Vulkan unsupported

D3D12 creates a native sampler-feedback map paired with a tiled texture, clears
it, resolves/decodes it to an ordinary texture, and tracks facade/common/native
states. Vulkan intentionally reports no native sampler feedback facility.

### Work graphs — D3D12 implemented, Vulkan unsupported

DXC emits Shader Model 6.8 libraries. D3D12 creates the executable graph state,
identifies the program/entry point, manages backing memory, and dispatches CPU
input records. Tests run a node shader and validate its GPU output. Vulkan
truthfully reports the feature unsupported.

### Shader bundles — implemented on D3D12 and Vulkan

The portable bundle owns persistent validated compute records and replays a
selected range with retained pipeline/binding/resource state. Tests execute
multiple records and verify independent output buffers. This is not a claim of
native Vulkan device-generated commands.

### Custom present — implemented on both desktop swap chains

`IArdaCustomPresent` receives resize, native back-buffer identity and extent,
native-present policy, and post-present callbacks. Hidden Win32 D3D12 and
Vulkan swap-chain tests validate acquisition, callback ordering,
native-present bypass, post-present, and resize. Vulkan presentation remains
dependent on a host-provided surface and reports the surface-clamped extent.

### Capability admission — implemented

`FArdaRHICapabilities::Evaluate` and named device profiles let every later
renderer or ML module ask whether its exact requirements are available. The report contains
all missing abilities. A software RT profile can omit hardware cores; the
real-time renderer profile can require them and refuse to run.

## Deliberate differences from Unreal

Arda is still intentionally smaller than Unreal RHI/RDG:

- only desktop D3D12 and Vulkan are supported;
- rasterization exists for tooling/presentation/compatibility but is not a
  required shading path for the RT/ML renderer;
- no console, mobile, Metal, or multi-GPU platform layer is planned here;
- HDR/display-mode/frame-pacing policy, render-pass/subpass breadth,
  multi-draw/count variants, and Unreal's full validation/replay ecosystem are
  outside this tranche; and
- platform-specific advanced facilities are never silently emulated and
  reported as native.

## Conformance and acceptance contract

Capabilities remain false until a native path exists. Capability-dependent
tests skip only when the adapter reports the named feature unavailable. When a
feature is true, tests must create native objects, execute native commands,
validate output or lifecycle evidence, compare all available state authorities,
and produce no D3D12 debug-layer or Vulkan validation error.

Required focused coverage includes:

- RT/ML profile admission and explicit missing-ability traces;
- BLAS/TLAS build, update, compaction, hit/local SBT records, and indirect ray
  dispatch on D3D12 and Vulkan;
- Vulkan mesh/ray pipelines and opacity-micromap build, compact copy, and BLAS
  use;
- unbounded/update-after-bind/variable descriptors and direct resource/sampler
  heap execution;
- compute/copy/graphics submission, GPU wait edges, and Vulkan family ownership;
- sparse map/unmap/commit and streaming-budget telemetry;
- resource collections, sampler feedback, work graphs, shader bundles, and
  custom-present ordering; and
- RDG initial/intermediate/final state, culling, parallel recording,
  cross-queue waits, extraction, and transient placement.

The non-interactive acceptance command is:

```text
ctest --test-dir build-state-parity -C Debug --output-on-failure
```

Hidden, frame-limited `RHITest` and `ARDGExample` invocations are registered as
unattended GPU tests. Interactive unbounded runs still require a desktop
window. Windows symbolic-link-policy tests may skip on hosts that cannot create
symbolic links.

## Advanced-feature execution test additions (2026-08-22)

The parity work now distinguishes declaration/lifecycle coverage from
known-result native execution:

- D3D12 and Vulkan build a triangle BLAS and one-instance TLAS, route rays
  through a closest-hit group and miss record, pack non-empty local
  shader-table arguments, dispatch indirectly from the portable
  `{width,height,depth}` GPU record, and read back deterministic hit/miss
  values.
- D3D12 translates that portable ray record into a native
  `D3D12_DISPATCH_RAYS_DESC` on the command list using the currently bound SBT
  address ranges. Facade and native trackers require and preserve
  `IndirectArgument` state.
- Vulkan builds and compacts an opaque two-state micromap, attaches it to a
  BLAS, builds a TLAS, traces the compacted result, and validates deterministic
  hit/miss output. This supplements size, identity, state, and lifecycle
  assertions.
- D3D12 sampler-feedback clear/decode ends in a mapped `R8UInt` staging
  readback whose texels must all equal `0xFF`.
- D3D12 and Vulkan sparse-buffer prefix commit writes and reads back
  `0x51A25EED` before decommit, in addition to tile-map and budget assertions.
- Empty and complete capability profiles exercise every RT/ML desktop
  admission requirement, including AS update/compaction, indirect rays, local
  SBT arguments, micromaps, descriptors, queues, sparse residency, feedback,
  work graphs, bundles, present, FP16, and INT8.
- D3D12 command submission uses independent per-queue fences and retains each
  native submission payload until its own fence completes. A 256-list
  immediate-release unit test and a 120-frame ARDG example test guard this
  lifetime rule.

These checks are additive to existing deterministic readbacks for copy and
resolve, explicit heap aliasing, bindless and direct heap indexing, dedicated
compute/copy work, mesh pipelines, work graphs, and shader bundles.
Capability-dependent tests skip only when the probed desktop adapter reports
the native feature unavailable.
