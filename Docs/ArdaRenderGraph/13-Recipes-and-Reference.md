# 13. Recipes and reference

[← End-to-end examples](12-End-to-End-Examples.md) ·
[Documentation home](README.md)

This chapter is the quick lookup for code reviews and day-to-day graph
construction. Include the aggregate public header:

```cpp
#include "ArdaRenderGraph.h"
using namespace arda::render_graph;
```

## Recipes

### Create a builder from a backend device

```cpp
FARDGRenderGraphContext Context;
Context.mDevice = DeviceContext.mDevice;
Context.mQueueCapabilities.mbGraphics =
    DeviceContext.mQueueCapabilities.mbGraphics;
Context.mQueueCapabilities.mbCompute =
    DeviceContext.mQueueCapabilities.mbCompute;
Context.mQueueCapabilities.mbCopy =
    DeviceContext.mQueueCapabilities.mbCopy;

FARDGBuilder Graph(Context);
```

Graphics capability is required. A device is required by `Execute()`, but not
by `Compile()` or `DumpGraph()`.

### Create a transient output and keep it

```cpp
nvrhi::BufferDesc Desc;
Desc.setDebugName("Visible objects")
    .setByteSize(64 * 1024)
    .setCanHaveUAVs(true);
FARDGBufferRef Output = Graph.CreateBuffer(Desc); // Transient by default.

nvrhi::BufferHandle OutputAfterSubmit;
Graph.QueueBufferExtraction(
    Output,
    OutputAfterSubmit,
    nvrhi::ResourceStates::ShaderResource);
```

The graph-created buffer still needs a declared writer. Extraction roots that
writer, removes transient eligibility, requests the final state, and fills the
handle after submission.

### Import persistent or swap-chain data

```cpp
FARDGTextureRef BackBuffer = Graph.RegisterExternalTexture(
    SwapChainTexture,
    nvrhi::ResourceStates::Present,
    "Swap-chain color");

FARDGBufferRef SceneData = Graph.RegisterExternalBuffer(
    PersistentBuffer,
    nvrhi::ResourceStates::ShaderResource,
    "Persistent scene data");
```

Pass the state that is true on graph entry. An external write is observable and
returns to its registered state in the epilogue unless extraction requests a
different final state.

### Declare a runtime-selected state and range

```cpp
ARDG_BEGIN_PARAMETER_STRUCT(FRangeParameters)
    ARDG_TEXTURE_ACCESS(mTexture)
    ARDG_BUFFER_ACCESS(mBuffer)
ARDG_END_PARAMETER_STRUCT()

FRangeParameters Parameters;
Parameters.mTexture = {
    Texture,
    nvrhi::ResourceStates::NonPixelShaderResource,
    nvrhi::TextureSubresourceSet(2, 1, 0, 1)
};
Parameters.mBuffer = {
    Buffer,
    nvrhi::ResourceStates::UnorderedAccess,
    nvrhi::BufferRange(4096, 4096)
};
```

Use access macros when state or range is not fixed by the member kind. The
state cannot be `Unknown`.

### Create and declare a logical view

```cpp
FARDGTextureViewDesc ViewDesc;
ViewDesc.mTexture = Texture->GetHandle();
ViewDesc.mSubresources = nvrhi::TextureSubresourceSet(1, 1, 0, 1);
FARDGTextureSRVRef Mip1 =
    Graph.CreateSRV("Texture mip 1 SRV", ViewDesc);

ARDG_BEGIN_PARAMETER_STRUCT(FSampleParameters)
    ARDG_TEXTURE_SRV(mInput)
ARDG_END_PARAMETER_STRUCT()
```

Put `Mip1` in `mInput`, then call `Context.GetTexture(Frozen.mInput)` in the
callback. The getter returns the physical parent texture. Use the logical view
descriptor to construct the matching NVRHI binding item.

### Add a direct dispatch

```cpp
Graph.AddDispatchPass(
    "Build light list",
    &Parameters,
    FARDGDispatchArguments{GroupCountX, GroupCountY, 1},
    [ComputeState](FARDGPassExecutionContext& Context,
                   const FParameters&)
    {
        Context.mCommandList.setComputeState(ComputeState);
    });
```

The setup callback runs first; `AddDispatchPass` then calls
`ICommandList::dispatch`. It always adds `Compute`. Add `AsyncCompute` through
the optional flags argument to request a compute queue.

### Request async compute safely

```cpp
Parameters.mInput = {
    Input,
    nvrhi::ResourceStates::NonPixelShaderResource,
    nvrhi::AllSubresources
};

Graph.AddDispatchPass(
    "Async filter",
    &Parameters,
    Dispatch,
    BindState,
    EARDGPassFlags::AsyncCompute);
```

Advertise `mbCompute` only when the backend exposes the queue. Avoid
graphics-only states. If the queue is unavailable, eligible work falls back to
graphics.

### Record a copy pass

```cpp
ARDG_BEGIN_PARAMETER_STRUCT(FCopyParameters)
    ARDG_BUFFER_ACCESS(mSource)
    ARDG_BUFFER_ACCESS(mDestination)
ARDG_END_PARAMETER_STRUCT()

FCopyParameters Parameters;
Parameters.mSource = {
    Source, nvrhi::ResourceStates::CopySource, SourceRange };
Parameters.mDestination = {
    Destination, nvrhi::ResourceStates::CopyDest, DestinationRange };

Graph.AddPass(
    "Copy data",
    &Parameters,
    EARDGPassFlags::Copy,
    [](FARDGPassExecutionContext& Context,
       const FCopyParameters& Frozen)
    {
        Context.mCommandList.copyBuffer(
            Context.GetBuffer(Frozen.mDestination.mBuffer),
            0,
            Context.GetBuffer(Frozen.mSource.mBuffer),
            0,
            Frozen.mDestination.mRange.byteSize);
    });
```

Every declared state on a `Copy` pass must be `CopySource` or `CopyDest`.
Placement falls back to graphics if no copy queue exists.

### Declare raster attachments

```cpp
ARDG_BEGIN_PARAMETER_STRUCT(FRasterParameters)
    ARDG_RENDER_TARGET_BINDING_SLOTS(mTargets)
ARDG_END_PARAMETER_STRUCT()

FRasterParameters Parameters;
Parameters.mTargets.mColor[0] = {
    Color,
    nvrhi::AllSubresources
};
Parameters.mTargets.mDepthStencil = {
    Depth,
    nvrhi::AllSubresources
};
```

Color implies `RenderTarget`; depth/stencil implies `DepthWrite`. The callback
still binds an NVRHI framebuffer and graphics pipeline.

### Upload and declare a uniform buffer

```cpp
ARDG_BEGIN_PARAMETER_STRUCT(FViewConstants)
    ARDG_PARAMETER(float, mExposure)
    ARDG_TEXTURE(mBlueNoise)
ARDG_END_PARAMETER_STRUCT()

ARDG_BEGIN_PARAMETER_STRUCT(FPassParameters)
    ARDG_UNIFORM_BUFFER(mView)
ARDG_END_PARAMETER_STRUCT()

FViewConstants Constants;
Constants.mExposure = 1.0f;
Constants.mBlueNoise = BlueNoise;
FARDGUniformBufferRef View =
    Graph.CreateUniformBuffer("View constants", &Constants);

FPassParameters Parameters;
Parameters.mView = View;
```

The contents are frozen. Nested metadata is traversed, so declaring `mView`
also declares the `BlueNoise` dependency. Execution uploads graph uniform
buffers on graphics before dependent work.

### Add ordering not represented by resources

```cpp
FARDGPassHandle Setup = Graph.AddPass(
    "CPU-visible setup",
    EARDGPassFlags::None,
    SetupCallback);
FARDGPassHandle Consume = Graph.AddPass(
    "Consume setup",
    EARDGPassFlags::NeverCull,
    ConsumeCallback);
Graph.AddDependency(Setup, Consume);
```

Both passes must already exist, and the producer must have been registered
first. Manual dependencies affect ordering and culling reachability.

### Share graph-building data

```cpp
struct FSceneGraphData
{
    FARDGTextureRef mDepth = nullptr;
};

Graph.GetBlackboard().Emplace<FSceneGraphData>().mDepth = Depth;
FARDGTextureRef SharedDepth =
    Graph.GetBlackboard().Get<FSceneGraphData>().mDepth;
```

The blackboard does not declare resource use. Every callback still needs its
resources in parameter metadata.

### Inspect compilation and execution

```cpp
const FARDGCompileResult& Compiled = Graph.Compile();
Log(Graph.DumpGraph());

FARDGExecuteOptions Options;
Options.mbParallelRecording = true;
Options.mMaxRecordingThreads = 0; // Hardware concurrency.
const FARDGExecutionResult& Executed = Graph.Execute(Options);
```

`Execute()` calls `Compile()` if needed. It submits once and does not wait for
GPU completion.

## Pass flags

`EARDGPassFlags` is a bit mask:

- `None`: no implied operation category. It still records a callback if live.
- `Raster`: graphics work with raster attachment declarations.
- `Compute`: compute work on the graphics-capable pipeline.
- `AsyncCompute`: request the compute queue; valid only with `Compute`.
- `Copy`: request the copy queue with copy-only declared states.
- `NeverCull`: make this pass a culling root.
- `SkipRenderPass`: valid only with `Raster`; exclude it from raster grouping.
- `NeverParallel`: record this callback serially on the CPU.

Operation categories are exclusive: do not combine `Raster` with `Compute`,
`AsyncCompute`, or `Copy`, and do not combine `Copy` with compute flags.
`AddDispatchPass` adds `Compute` automatically.

Flag definitions:
[`ArdaRenderGraphDefinitions.h`](../../Source/ArdaRenderGraph/Public/ArdaRenderGraphDefinitions.h).

## Resource flags

`EARDGResourceFlags` describes ownership and allocation:

- `None`: graph-created, non-transient resource for this execution.
- `External`: physical storage is owned outside the graph. Set only by
  `RegisterExternalTexture/Buffer`.
- `Extracted`: storage survives graph completion. Set by
  `QueueTextureExtraction/QueueBufferExtraction`.
- `Transient`: descriptor-compatible storage may be reused after its live
  interval. This is the default for `CreateTexture/Buffer`.

Creation accepts only `None` or `Transient`. External and extracted resources
are not transient.

## Complete parameter macro reference

Public macros are defined in
[`ArdaRenderGraphParameters.h`](../../Source/ArdaRenderGraph/Public/ArdaRenderGraphParameters.h).
Null resource members are ignored.

### Struct boundaries

- `ARDG_BEGIN_PARAMETER_STRUCT(StructType)` begins a standard-layout parameter
  struct.
- `ARDG_END_PARAMETER_STRUCT()` closes it and adds `GetStaticMetadata()`.

### Ordinary values and nesting

- `ARDG_PARAMETER(CppType, Name)` declares one ordinary value.
- `ARDG_PARAMETER_ARRAY(CppType, Name, Count)` declares an `eastl::array` of
  ordinary values.
- `ARDG_PARAMETER_STRUCT(StructType, Name)` embeds one ARDG parameter struct.
- `ARDG_PARAMETER_STRUCT_ARRAY(StructType, Name, Count)` embeds an
  `eastl::array` of ARDG parameter structs.

Ordinary values add no graph access. Nested structs are traversed recursively.

### Direct resources

- `ARDG_TEXTURE(Name)` and `ARDG_TEXTURE_ARRAY(Name, Count)` store
  `FARDGTextureRef` with default `ShaderResource`.
- `ARDG_BUFFER(Name)` and `ARDG_BUFFER_ARRAY(Name, Count)` store
  `FARDGBufferRef` with default `ShaderResource`.

### Shader-resource views

- `ARDG_TEXTURE_SRV(Name)` and `ARDG_TEXTURE_SRV_ARRAY(Name, Count)` store
  `FARDGTextureSRVRef` with default `ShaderResource`.
- `ARDG_BUFFER_SRV(Name)` and `ARDG_BUFFER_SRV_ARRAY(Name, Count)` store
  `FARDGBufferSRVRef` with default `ShaderResource`.

### Unordered-access views

- `ARDG_TEXTURE_UAV(Name)` and `ARDG_TEXTURE_UAV_ARRAY(Name, Count)` store
  `FARDGTextureUAVRef` with default `UnorderedAccess`.
- `ARDG_BUFFER_UAV(Name)` and `ARDG_BUFFER_UAV_ARRAY(Name, Count)` store
  `FARDGBufferUAVRef` with default `UnorderedAccess`.

### Runtime state and range

- `ARDG_TEXTURE_ACCESS(Name)` and
  `ARDG_TEXTURE_ACCESS_ARRAY(Name, Count)` store `FARDGTextureAccess`:
  `mTexture`, `mState`, and `mSubresources`.
- `ARDG_BUFFER_ACCESS(Name)` and
  `ARDG_BUFFER_ACCESS_ARRAY(Name, Count)` store `FARDGBufferAccess`:
  `mBuffer`, `mState`, and `mRange`.

### Uniform buffers

- `ARDG_UNIFORM_BUFFER(Name)` and
  `ARDG_UNIFORM_BUFFER_ARRAY(Name, Count)` store `FARDGUniformBufferRef` with
  default `ConstantBuffer`.

### Raster attachments

- `ARDG_RENDER_TARGET_BINDING_SLOTS(Name)` stores
  `FARDGRenderTargetBindingSlots`. It has
  `mColor[nvrhi::c_MaxRenderTargets]` and `mDepthStencil`. There is no public
  array counterpart for this macro.

## Callback forms

Typed `AddPass(Name, Parameters, Flags, Callback)` accepts:

```cpp
[](FARDGPassExecutionContext&, const FParameters&) {}
[](const FParameters&, FARDGPassExecutionContext&) {}
[](nvrhi::ICommandList&, const FParameters&) {}
[](const FParameters&, nvrhi::ICommandList&) {}
[](FARDGPassExecutionContext&) {}
[](nvrhi::ICommandList&) {}
[](const FParameters&) {}
[]() {}
```

Parameterless `AddPass(Name, Flags, Callback)` accepts:

```cpp
[](FARDGPassExecutionContext&) {}
[](nvrhi::ICommandList&) {}
[]() {}
```

`AddDispatchPass` accepts the same typed setup forms. After setup, it issues
`dispatch(mGroupCountX, mGroupCountY, mGroupCountZ)`.

Prefer context callbacks when touching graph resources:

- `GetTexture(FARDGTextureRef/SRVRef/UAVRef)`
- `GetBuffer(FARDGBufferRef/SRVRef/UAVRef)`
- `GetUniformBuffer(FARDGUniformBufferRef)`
- `GetGraph()` and `GetPass()`
- `mCommandList` and selected `mPipeline`

The resource, exact view, or uniform buffer must appear in that pass's frozen
parameters. Raw command-list callbacks cannot validate independently retained
physical handles.

## Common NVRHI resource states

Use the narrowest state that describes the callback:

- `Common`: neutral state; graph-created `Unknown` entry states normalize here.
- `VertexBuffer`, `IndexBuffer`, `IndirectArgument`: fixed-function reads.
- `ConstantBuffer`: uniform-buffer read.
- `PixelShaderResource`: pixel-shader read; graphics-only.
- `NonPixelShaderResource`: vertex/compute and other non-pixel shader read.
- `ShaderResource`: combined pixel and non-pixel shader read. It normalizes to
  non-pixel when an async-compute pass is selected.
- `UnorderedAccess`: shader read/write; descriptor must permit UAV use.
- `RenderTarget`: color attachment write.
- `DepthWrite`, `DepthRead`: depth/stencil attachment access.
- `CopySource`, `CopyDest`: copy read and write.
- `ResolveSource`, `ResolveDest`: multisample resolve access.
- `Present`: swap-chain presentation state.
- `AccelStructRead`, `AccelStructWrite`: acceleration-structure access.
- `ShadingRateSurface`: graphics shading-rate image access.

`Unknown` is not a valid pass access or external entry/final state. Render
target, depth, present, and shading-rate states are not valid on async compute.
A `Copy` pass may use only copy states.

The compiler treats `UnorderedAccess`, `RenderTarget`, `DepthWrite`,
`CopyDest`, `ResolveDest`, `AccelStructWrite`, `OpacityMicromapWrite`, and
`ConvertCoopVecMatrixOutput` as writes. Other legal states are reads.

## Compile result reference

`FARDGCompileResult` contains:

- `mPrologue`: synthetic graph-entry pass handle.
- `mEpilogue`: synthetic graph-exit pass handle.
- `mExecutionOrder`: live pass handles in registration order, including
  sentinels.
- `mRasterGroupCount`: count of compatible consecutive raster groups.
- `mResourceLifetimes`: live texture and buffer intervals.
- `mQueueDependencies`: cross-queue producer/consumer edges.

Each `FARDGResourceLifetime` has:

- `mType`: texture or buffer;
- `mResourceIndex`: registry index for that type;
- `mFirstUse` and `mLastUse`: inclusive execution-order indices; and
- `mbTransient`: whether execution may use transient pooling.

Each `FARDGQueueDependency` has producer/consumer pass handles and their
`mProducerPipeline`/`mConsumerPipeline`.

Detailed per-pass compilation is in
`Graph.TryGetPass(Handle)->GetState()`:

- `mProducers`, `mConsumers`;
- `mSynchronizationProducers`, `mSynchronizationConsumers`;
- `mTextureStates`, `mBufferStates`, `mUniformBuffers`, `mViews`;
- `mTextureTransitions`, `mBufferTransitions`;
- `mPipeline`, `mAsyncFork`, `mAsyncJoin`, `mRasterGroup`;
- `mRasterBindings`, `mbCulled`, and `mbSentinel`.

Definitions:
[`ArdaRenderGraphBuilder.h`](../../Source/ArdaRenderGraph/Public/ArdaRenderGraphBuilder.h)
and
[`ArdaRenderGraphPass.h`](../../Source/ArdaRenderGraph/Public/ArdaRenderGraphPass.h).

## Execution options and result reference

`FARDGExecuteOptions` contains:

- `mbParallelRecording`: allow independent passes at one dependency level to
  record concurrently; default `true`.
- `mMaxRecordingThreads`: worker cap; zero selects hardware concurrency.

`FARDGExecutionResult` contains:

- `mSubmittedCommandListCount`: submitted pass and boundary-transition command
  lists.
- `mQueueWaitCount`: explicit waits inserted between different queues.
- `mTexturePoolReuseCount`: textures served by a descriptor-compatible reused
  allocation.
- `mBufferPoolReuseCount`: buffers served by a descriptor-compatible reused
  allocation.
- `mbUsedParallelRecording`: at least two pass lists actually recorded
  concurrently.
- `mbUsedVirtualHeaps`: NVRHI virtual heaps were used; currently false.
- `mbUsedTransientAliasing`: physical memory was placed/aliased; currently
  false.
- `mbUsedTransientFallback`: transient candidates used committed-resource
  fallback.
- `mbUsedImmediateMode`: immediate mode selected this execution.
- `mClobberedResourceCount`: diagnostic first-write clears issued.
- `mLastSubmittedInstances`: latest NVRHI instance for graphics, compute, and
  copy queue indices.

The result describes completed CPU submission, not GPU completion.
`GetLastExecutionResult()` returns null before successful execution.

## Debug option reference

Set options on the context before constructing the builder:

- `mbImmediateMode`: keep all passes, force graphics, extend lifetimes, and
  record/submit serially with NVRHI immediate execution requested.
- `mbConservativeBarriers`: force equal non-`Common`, non-UAV states through
  `Common`.
- `mbExtendResourceLifetimes`: expand every live interval across the graph.
- `mbClobberFirstWrites`: clear supported non-external resources before their
  first write.

See [Debugging and recommended practices](07-Debugging-and-Practices.md) for
symptom-led use.

## Source symbol index

### Public API

- [`ArdaRenderGraph.h`](../../Source/ArdaRenderGraph/Public/ArdaRenderGraph.h):
  aggregate include.
- [`ArdaRenderGraphDefinitions.h`](../../Source/ArdaRenderGraph/Public/ArdaRenderGraphDefinitions.h):
  typed handles, pipelines, queue capabilities, debug options, pass/resource
  flags, resource types, and graph context.
- [`ArdaRenderGraphResources.h`](../../Source/ArdaRenderGraph/Public/ArdaRenderGraphResources.h):
  textures, buffers, views, access records, raster bindings, uniform buffers,
  and reference aliases.
- [`ArdaRenderGraphParameters.h`](../../Source/ArdaRenderGraph/Public/ArdaRenderGraphParameters.h):
  parameter metadata, enumeration, and all public `ARDG_*` macros.
- [`ArdaRenderGraphPass.h`](../../Source/ArdaRenderGraph/Public/ArdaRenderGraphPass.h):
  pass state, transitions, execution context, lambda passes, and sentinels.
- [`ArdaRenderGraphBuilder.h`](../../Source/ArdaRenderGraph/Public/ArdaRenderGraphBuilder.h):
  builder API, extraction, dispatch arguments, compile products, execution
  options, and execution results.
- [`ArdaRenderGraphBlackboard.h`](../../Source/ArdaRenderGraph/Public/ArdaRenderGraphBlackboard.h):
  typed graph-building blackboard.
- [`ArdaRenderGraphLog.h`](../../Source/ArdaRenderGraph/Public/ArdaRenderGraphLog.h):
  assertion/check integration used by validation.

### Implementation

- [`ArdaRenderGraphBuilder.cpp`](../../Source/ArdaRenderGraph/Private/ArdaRenderGraphBuilder.cpp):
  lifecycle, registration, dependency discovery, physical access gates, and
  graph dumps.
- [`ArdaRenderGraphValidation.cpp`](../../Source/ArdaRenderGraph/Private/ArdaRenderGraphValidation.cpp):
  descriptors, ownership, state/range, queue, production, and transition
  validation.
- [`ArdaRenderGraphCompiler.cpp`](../../Source/ArdaRenderGraph/Private/ArdaRenderGraphCompiler.cpp):
  queue assignment, culling, lifetimes, barriers, fork/join, queue edges, and
  raster groups.
- [`ArdaRenderGraphExecutor.cpp`](../../Source/ArdaRenderGraph/Private/ArdaRenderGraphExecutor.cpp):
  materialization, committed-resource pools, clobbering, recording, uniform
  uploads, waits, submission, and extraction completion.
- [`ArdaRenderGraphAllocator.h`](../../Source/ArdaRenderGraph/Private/ArdaRenderGraphAllocator.h):
  transient heap layout model.

### Source-backed examples and tests

- [`ArdaGraphTests.cpp`](../../Source/ArdaRenderGraph/Tests/ArdaGraphTests.cpp):
  metadata, compilation, culling, transitions, queues, diagnostics, runtime
  clear/extraction/reuse, access-gate, and queue-wait coverage.
- [`ArdaTriangleRenderer.cpp`](../../Source/ArdaTests/NVRHITest/Private/ArdaTriangleRenderer.cpp):
  actual geometry upload and swap-chain raster submission.

## Current limitations

- A builder is one-shot, immutable after compilation, and permanently failed
  after a compile/execute exception.
- Passes remain in registration order after culling; no general reordering or
  workload-cost scheduling is performed.
- Texture producer/reader dependencies and all buffer dependencies are
  conservative at whole-resource scope. Texture state and production are
  subresource-aware; buffer state and production are whole-buffer.
- Portable placed-resource aliasing is disabled. Descriptor-compatible,
  non-overlapping transient resources can reuse committed allocations only
  within one execution and one queue domain.
- `mbUsedVirtualHeaps` and `mbUsedTransientAliasing` currently remain false.
- Raster groups are diagnostic/future metadata; execution does not create
  framebuffers, merge render passes, infer load/store operations, or create
  subpasses.
- Execution generally records one command list per live pass, plus required
  boundary-transition lists and a separate graph uniform upload list.
- Logical SRV/UAV records do not materialize NVRHI view objects or binding
  sets.
- Uniform buffers are dedicated per graph and are not pooled.
- The graph cannot validate independently captured framebuffers, binding sets,
  or raw handles against parameter declarations.
- The graph does not create shaders, layouts, pipelines, swap chains, or
  presentation synchronization.
- `Execute()` completes submission and garbage collection, but does not wait
  for GPU completion.

---

[← End-to-end examples](12-End-to-End-Examples.md) ·
[Documentation home](README.md)
