# 12. End-to-end examples

[← Executor source walkthrough](11-Executor-Source-Walkthrough.md) ·
[Documentation home](README.md) ·
[Next: Recipes and reference →](13-Recipes-and-Reference.md)

These examples progress from compiler-only topology to submitted GPU work. They
use ordinary NVRHI objects for shaders, pipelines, framebuffers, and binding
sets; ArdaRenderGraph owns the declarations, barriers, queue waits, lifetimes,
recording, and submission.

The render-graph unit tests deliberately separate compilation from execution.
Several tests use empty callbacks and call `Compile()` only. In particular,
[`DispatchPassApiRegistersComputeWork`](../../Source/ArdaRenderGraph/Tests/ArdaGraphTests.cpp)
does **not** execute its registered dispatch, and the copy-queue checks do
**not** issue a copy. Backend runtime tests execute buffer clears, while the
triangle integration executes real buffer uploads and a raster draw.

## 1. Produce, consume, and cull dead work

This device-free example shows the smallest useful dependency graph.

```text
GraphPrologue
     |
     v
  Produce --Intermediate--> Consume --Output--> GraphEpilogue

  Dead --DeadOutput--> (nothing)                 [culled]
```

```cpp
ARDG_BEGIN_PARAMETER_STRUCT(FTextureFlowParameters)
    ARDG_TEXTURE_ACCESS(mInput)
    ARDG_TEXTURE_ACCESS(mOutput)
ARDG_END_PARAMETER_STRUCT()

FARDGBuilder Graph; // Compile() does not require a device.

nvrhi::TextureDesc Desc;
Desc.setWidth(16).setHeight(16).setIsUAV(true);

Desc.setDebugName("Intermediate");
FARDGTextureRef Intermediate = Graph.CreateTexture(Desc);
Desc.setDebugName("Output");
FARDGTextureRef Output = Graph.CreateTexture(Desc);
Desc.setDebugName("Dead output");
FARDGTextureRef DeadOutput = Graph.CreateTexture(Desc);

FTextureFlowParameters ProduceParameters;
ProduceParameters.mOutput = {
    Intermediate,
    nvrhi::ResourceStates::UnorderedAccess,
    nvrhi::AllSubresources
};
FARDGPassHandle Produce = Graph.AddPass(
    "Produce",
    &ProduceParameters,
    EARDGPassFlags::Compute,
    [] {}); // This example inspects compilation; it records no GPU command.

FTextureFlowParameters ConsumeParameters;
ConsumeParameters.mInput = {
    Intermediate,
    nvrhi::ResourceStates::ShaderResource,
    nvrhi::AllSubresources
};
ConsumeParameters.mOutput = {
    Output,
    nvrhi::ResourceStates::UnorderedAccess,
    nvrhi::AllSubresources
};
FARDGPassHandle Consume = Graph.AddPass(
    "Consume",
    &ConsumeParameters,
    EARDGPassFlags::Compute,
    [] {});

FTextureFlowParameters DeadParameters;
DeadParameters.mOutput = {
    DeadOutput,
    nvrhi::ResourceStates::UnorderedAccess,
    nvrhi::AllSubresources
};
FARDGPassHandle Dead = Graph.AddPass(
    "Dead",
    &DeadParameters,
    EARDGPassFlags::Compute,
    [] {});

nvrhi::TextureHandle ExtractedOutput;
Graph.QueueTextureExtraction(
    Output,
    ExtractedOutput,
    nvrhi::ResourceStates::ShaderResource);

const FARDGCompileResult& Result = Graph.Compile();
assert(!Graph.TryGetPass(Produce)->GetState().mbCulled);
assert(!Graph.TryGetPass(Consume)->GetState().mbCulled);
assert(Graph.TryGetPass(Dead)->GetState().mbCulled);
```

Extraction makes `Output` observable. The compiler walks backward from its last
writer to `Produce`; `Dead` reaches no root. This mirrors
[`CompilerTracksProducersCullsDeadPassesAndPreservesSentinels`](../../Source/ArdaRenderGraph/Tests/ArdaGraphTests.cpp),
which is a compile-only test with empty callbacks.

## 2. Write an external swap-chain image

An external write is already observable, so no extraction or `NeverCull` is
needed.

```text
AcquireFrame
     |
     v
[Present back buffer] --Raster pass--> [RenderTarget]
     ^                                      |
     +----------- epilogue: Present <-------+
                                             |
                                       PrepareSubmit
                                             |
                                          Execute
                                             |
                                           Present
```

```cpp
ARDG_BEGIN_PARAMETER_STRUCT(FPresentParameters)
    ARDG_RENDER_TARGET_BINDING_SLOTS(mTargets)
ARDG_END_PARAMETER_STRUCT()

nvrhi::FramebufferHandle Framebuffer;
if (!SwapChain.AcquireFrame(Framebuffer))
    return;

const nvrhi::FramebufferAttachment Attachment =
    Framebuffer->getDesc().colorAttachments[0];

FARDGBuilder Graph(GraphContext);
FARDGTextureRef BackBuffer = Graph.RegisterExternalTexture(
    Attachment.texture,
    nvrhi::ResourceStates::Present,
    "Swap-chain color");

FPresentParameters Parameters;
Parameters.mTargets.mColor[0] = {
    BackBuffer,
    Attachment.subresources
};

Graph.AddPass(
    "Render to swap chain",
    &Parameters,
    EARDGPassFlags::Raster,
    [Framebuffer, Pipeline](FARDGPassExecutionContext& Context,
                            const FPresentParameters& Frozen)
    {
        // Confirms that the logical attachment was declared by this pass.
        (void)Context.GetTexture(Frozen.mTargets.mColor[0].mTexture);

        nvrhi::GraphicsState State;
        State.setPipeline(Pipeline).setFramebuffer(Framebuffer);
        Context.mCommandList.setGraphicsState(State);
        Context.mCommandList.draw(
            nvrhi::DrawArguments().setVertexCount(3));
    });

SwapChain.PrepareSubmit();
Graph.Execute();
SwapChain.Present();
```

The graph derives `Present → RenderTarget → Present`. The application still
owns acquire/present synchronization and must ensure the captured framebuffer
really refers to the declared attachment. See the actual
[`FArdaTriangleRenderer::RenderFrame`](../../Source/ArdaTests/NVRHITest/Private/ArdaTriangleRenderer.cpp)
flow and the compile-only external-write test in
[`ArdaGraphTests.cpp`](../../Source/ArdaRenderGraph/Tests/ArdaGraphTests.cpp).

## 3. Generate, triangulate, and render procedural terrain

This is the three-stage core of the series' pedagogical terrain graph. A noise
compute shader writes a heightmap, another compute shader reads that heightmap
and writes terrain vertex/index buffers, and a raster pass draws those buffers
to an imported back buffer:

```text
GenerateNoiseHeightmap        TriangulateTerrain               RenderTerrain
Heightmap UAV          -----> Heightmap SRV                    BackBuffer RT
dispatch(...)                 TerrainVertices/Indices UAV ---> drawIndexed(...)

Heightmap: Common -> UnorderedAccess -> NonPixelShaderResource
Buffers:   Common --------------------> UnorderedAccess -> VertexBuffer/IndexBuffer
```

Chapters 5 and 8 extend this core with the settings upload, intentionally dead
debug/minimap read, erosion rewrite, and overlay passes needed to demonstrate
the compiler's complete P0–P8 behavior.

The repository does not currently ship the terrain shaders, pipelines, or demo.
The `Build*State` helpers below are explicitly application placeholders for
creating NVRHI binding sets and state that match the graph-resolved resources.

```cpp
ARDG_BEGIN_PARAMETER_STRUCT(FGenerateNoiseHeightmapParameters)
    ARDG_TEXTURE_UAV(mHeightmap)
ARDG_END_PARAMETER_STRUCT()

ARDG_BEGIN_PARAMETER_STRUCT(FTriangulateTerrainParameters)
    ARDG_TEXTURE_SRV(mHeightmap)
    ARDG_BUFFER_UAV(mTerrainVertices)
    ARDG_BUFFER_UAV(mTerrainIndices)
ARDG_END_PARAMETER_STRUCT()

ARDG_BEGIN_PARAMETER_STRUCT(FRenderTerrainParameters)
    ARDG_BUFFER_ACCESS(mTerrainVertices)
    ARDG_BUFFER_ACCESS(mTerrainIndices)
    ARDG_RENDER_TARGET_BINDING_SLOTS(mTargets)
ARDG_END_PARAMETER_STRUCT()

const nvrhi::FramebufferAttachment BackBufferAttachment =
    Framebuffer->getDesc().colorAttachments[0];

FARDGBuilder Graph(GraphContext);
FARDGTextureRef BackBuffer = Graph.RegisterExternalTexture(
    BackBufferAttachment.texture,
    nvrhi::ResourceStates::Present,
    "BackBuffer");

nvrhi::TextureDesc HeightmapDesc;
HeightmapDesc.setDebugName("Heightmap")
    .setWidth(HeightmapWidth)
    .setHeight(HeightmapHeight)
    .setFormat(nvrhi::Format::R32_FLOAT)
    .setIsUAV(true);
FARDGTextureRef Heightmap = Graph.CreateTexture(HeightmapDesc);

nvrhi::BufferDesc VertexDesc;
VertexDesc.setDebugName("TerrainVertices")
    .setByteSize(MaxTerrainVertexCount * sizeof(FTerrainVertex))
    .setStructStride(sizeof(FTerrainVertex))
    .setCanHaveUAVs(true)
    .setIsVertexBuffer(true);
FARDGBufferRef TerrainVertices = Graph.CreateBuffer(VertexDesc);

nvrhi::BufferDesc IndexDesc;
IndexDesc.setDebugName("TerrainIndices")
    .setByteSize(MaxTerrainIndexCount * sizeof(uint32_t))
    .setStructStride(sizeof(uint32_t))
    .setCanHaveUAVs(true)
    .setIsIndexBuffer(true);
FARDGBufferRef TerrainIndices = Graph.CreateBuffer(IndexDesc);

FARDGTextureViewDesc HeightmapView;
HeightmapView.mTexture = Heightmap->GetHandle();
HeightmapView.mSubresources = nvrhi::TextureSubresourceSet(0, 1, 0, 1);
FARDGTextureUAVRef HeightmapUAV =
    Graph.CreateTextureUAV("Heightmap UAV", HeightmapView);
FARDGTextureSRVRef HeightmapSRV =
    Graph.CreateTextureSRV("Heightmap SRV", HeightmapView);

FARDGBufferViewDesc VertexView;
VertexView.mBuffer = TerrainVertices->GetHandle();
VertexView.mRange = nvrhi::EntireBuffer;
FARDGBufferUAVRef TerrainVerticesUAV =
    Graph.CreateBufferUAV("TerrainVertices UAV", VertexView);

FARDGBufferViewDesc IndexView;
IndexView.mBuffer = TerrainIndices->GetHandle();
IndexView.mRange = nvrhi::EntireBuffer;
FARDGBufferUAVRef TerrainIndicesUAV =
    Graph.CreateBufferUAV("TerrainIndices UAV", IndexView);

FGenerateNoiseHeightmapParameters Generate;
Generate.mHeightmap = HeightmapUAV;
Graph.AddDispatchPass(
    "GenerateNoiseHeightmap",
    &Generate,
    FARDGDispatchArguments{
        DivideRoundUp(HeightmapWidth, 8u),
        DivideRoundUp(HeightmapHeight, 8u),
        1
    },
    [NoisePipeline](FARDGPassExecutionContext& Context,
                    const FGenerateNoiseHeightmapParameters& Frozen)
    {
        nvrhi::ITexture* PhysicalHeightmap =
            Context.GetTexture(Frozen.mHeightmap);
        nvrhi::ComputeState State = BuildNoiseComputeState(
            NoisePipeline,
            PhysicalHeightmap); // Application placeholder.
        Context.mCommandList.setComputeState(State);
    },
    EARDGPassFlags::AsyncCompute);

FTriangulateTerrainParameters Triangulate;
Triangulate.mHeightmap = HeightmapSRV;
Triangulate.mTerrainVertices = TerrainVerticesUAV;
Triangulate.mTerrainIndices = TerrainIndicesUAV;
Graph.AddDispatchPass(
    "TriangulateTerrain",
    &Triangulate,
    FARDGDispatchArguments{
        DivideRoundUp(HeightmapWidth - 1u, 8u),
        DivideRoundUp(HeightmapHeight - 1u, 8u),
        1
    },
    [TriangulationPipeline](
        FARDGPassExecutionContext& Context,
        const FTriangulateTerrainParameters& Frozen)
    {
        nvrhi::ITexture* PhysicalHeightmap =
            Context.GetTexture(Frozen.mHeightmap);
        nvrhi::IBuffer* PhysicalVertices =
            Context.GetBuffer(Frozen.mTerrainVertices);
        nvrhi::IBuffer* PhysicalIndices =
            Context.GetBuffer(Frozen.mTerrainIndices);
        nvrhi::ComputeState State = BuildTriangulationComputeState(
            TriangulationPipeline,
            PhysicalHeightmap,
            PhysicalVertices,
            PhysicalIndices); // Application placeholder.
        Context.mCommandList.setComputeState(State);
    },
    EARDGPassFlags::AsyncCompute);

FRenderTerrainParameters Render;
Render.mTerrainVertices = {
    TerrainVertices,
    nvrhi::ResourceStates::VertexBuffer,
    nvrhi::EntireBuffer
};
Render.mTerrainIndices = {
    TerrainIndices,
    nvrhi::ResourceStates::IndexBuffer,
    nvrhi::EntireBuffer
};
Render.mTargets.mColor[0] = {
    BackBuffer,
    BackBufferAttachment.subresources
};
Graph.AddPass(
    "RenderTerrain",
    &Render,
    EARDGPassFlags::Raster,
    [TerrainPipeline, Framebuffer, TerrainIndexCount](
        FARDGPassExecutionContext& Context,
        const FRenderTerrainParameters& Frozen)
    {
        nvrhi::IBuffer* PhysicalVertices =
            Context.GetBuffer(Frozen.mTerrainVertices.mBuffer);
        nvrhi::IBuffer* PhysicalIndices =
            Context.GetBuffer(Frozen.mTerrainIndices.mBuffer);
        (void)Context.GetTexture(Frozen.mTargets.mColor[0].mTexture);

        nvrhi::GraphicsState State = BuildTerrainGraphicsState(
            TerrainPipeline,
            Framebuffer,
            PhysicalVertices,
            PhysicalIndices); // Application placeholder.
        Context.mCommandList.setGraphicsState(State);
        Context.mCommandList.drawIndexed(
            nvrhi::DrawArguments().setVertexCount(TerrainIndexCount));
    });

Graph.Execute();
```

Both `RenderTerrain` buffer-read declarations find `TriangulateTerrain` as the
last writer; edge deduplication keeps one pass edge, while barrier lowering
still transitions each whole buffer independently.
`Heightmap` state tracking is per mip/slice. The imported back-buffer write
keeps `RenderTerrain` live and the epilogue restores `Present`. These are real
graph declarations and dispatch/draw recording calls, but the placeholder
helpers must be supplied by an application before this example can execute.

## 4. Async compute with a graphics join

Request async compute with both `Compute` and `AsyncCompute`, and advertise the
real queue capability. Use non-pixel shader state for compute reads.

```text
Graphics:  ProduceInput -------- wait --------------------> Composite
                 |                                          ^
                 | fork                                     | join
                 v                                          |
Compute:     AsyncFilter ------------------------------------+

Queue edges:
  ProduceInput(Graphics) -> AsyncFilter(Compute)
  AsyncFilter(Compute)    -> Composite(Graphics)
```

```cpp
ARDG_BEGIN_PARAMETER_STRUCT(FAsyncCompositeParameters)
    ARDG_TEXTURE_SRV(mInput)
    ARDG_RENDER_TARGET_BINDING_SLOTS(mTargets)
ARDG_END_PARAMETER_STRUCT()

GraphContext.mQueueCapabilities.mbCompute = DeviceHasComputeQueue;
FARDGBuilder Graph(GraphContext);

// Input and Filtered are UAV-capable graph textures. PresentTarget is imported.
FTextureFlowParameters Produce;
Produce.mOutput = {
    Input,
    nvrhi::ResourceStates::UnorderedAccess,
    nvrhi::AllSubresources
};
Graph.AddPass(
    "Produce input",
    &Produce,
    EARDGPassFlags::Compute,
    RecordGraphicsProducer);

FTextureFlowParameters Filter;
Filter.mInput = {
    Input,
    nvrhi::ResourceStates::NonPixelShaderResource,
    nvrhi::AllSubresources
};
Filter.mOutput = {
    Filtered,
    nvrhi::ResourceStates::UnorderedAccess,
    nvrhi::AllSubresources
};
FARDGPassHandle AsyncFilter = Graph.AddDispatchPass(
    "Async filter",
    &Filter,
    FARDGDispatchArguments{32, 32, 1},
    BindFilterComputeState,
    EARDGPassFlags::AsyncCompute);

FAsyncCompositeParameters Composite;
Composite.mInput = FilteredSRV;
Composite.mTargets.mColor[0] = {
    PresentTarget,
    nvrhi::AllSubresources
};
FARDGPassHandle GraphicsJoin = Graph.AddPass(
    "Composite",
    &Composite,
    EARDGPassFlags::Raster,
    RecordComposite);

const FARDGCompileResult& Compiled = Graph.Compile();
const FARDGPassState& AsyncState =
    Graph.TryGetPass(AsyncFilter)->GetState();
// AsyncState.mAsyncJoin identifies GraphicsJoin when async placement succeeds.

const FARDGExecutionResult& Executed = Graph.Execute();
// Executed.mQueueWaitCount reports inserted cross-queue waits.
```

If no compute queue exists, placement falls back to graphics and no cross-queue
wait is needed for these edges. The compiler's fork/join and fallback behavior
is verified by
[`CompilerAssignsQueueFallbackAndAsyncForkJoinMetadata`](../../Source/ArdaRenderGraph/Tests/ArdaGraphTests.cpp).
That test calls `Compile()` with empty pass callbacks. Actual queue wait
submission is exercised separately by
[`RecordsIndependentPassesAndSubmitsCrossQueueWaits`](../../Source/ArdaRenderGraph/Tests/ArdaGraphTests.cpp);
its async callback is empty, so it validates submission synchronization rather
than a shader dispatch.

## 5. Copy queue

A copy pass may declare only `CopySource` and `CopyDest`. This example imports a
source buffer, creates a destination, records a real NVRHI copy, and extracts
the result.

```text
External source (CopySource)
          |
          v
     Copy buffer  --copy queue when available--> Destination
                                                   |
                                                   v
                                    Extract as CopySource
```

```cpp
ARDG_BEGIN_PARAMETER_STRUCT(FCopyBufferParameters)
    ARDG_BUFFER_ACCESS(mSource)
    ARDG_BUFFER_ACCESS(mDestination)
ARDG_END_PARAMETER_STRUCT()

FARDGBufferRef Source = Graph.RegisterExternalBuffer(
    PhysicalSource,
    nvrhi::ResourceStates::CopySource,
    "Upload source");

nvrhi::BufferDesc Desc;
Desc.setDebugName("Copied data").setByteSize(ByteCount);
FARDGBufferRef Destination = Graph.CreateBuffer(Desc);

FCopyBufferParameters Copy;
Copy.mSource = {
    Source,
    nvrhi::ResourceStates::CopySource,
    nvrhi::BufferRange(0, ByteCount)
};
Copy.mDestination = {
    Destination,
    nvrhi::ResourceStates::CopyDest,
    nvrhi::BufferRange(0, ByteCount)
};
Graph.AddPass(
    "Copy buffer",
    &Copy,
    EARDGPassFlags::Copy,
    [ByteCount](FARDGPassExecutionContext& Context,
                const FCopyBufferParameters& Frozen)
    {
        Context.mCommandList.copyBuffer(
            Context.GetBuffer(Frozen.mDestination.mBuffer),
            0,
            Context.GetBuffer(Frozen.mSource.mBuffer),
            0,
            ByteCount);
    });

nvrhi::BufferHandle CopiedData;
Graph.QueueBufferExtraction(
    Destination,
    CopiedData,
    nvrhi::ResourceStates::CopySource);
Graph.Execute();
```

Set `GraphContext.mQueueCapabilities.mbCopy` from the backend. Without a copy
queue, the pass runs on graphics. Copy compatibility and fallback are compiled
in
[`ArdaRenderGraphCompiler.cpp`](../../Source/ArdaRenderGraph/Private/ArdaRenderGraphCompiler.cpp).
The unit-test copy pass has an empty callback and calls `Compile()` only; it
proves queue selection, not that a copy command executes.

## 6. Transient committed-resource reuse

Two descriptor-identical transient resources can use one committed NVRHI
allocation when their live intervals do not overlap and all use is in one queue
domain.

```text
Execution index:       1          2
Transient A:          [A]
Transient B:                     [B]
Physical allocation: [========= reused =========]
```

```cpp
ARDG_BEGIN_PARAMETER_STRUCT(FClearParameters)
    ARDG_BUFFER_ACCESS(mBuffer)
ARDG_END_PARAMETER_STRUCT()

nvrhi::BufferDesc Desc;
Desc.setByteSize(256).setCanHaveUAVs(true);

Desc.setDebugName("Transient A");
FARDGBufferRef A = Graph.CreateBuffer(Desc); // Transient by default.
Desc.setDebugName("Transient B");
FARDGBufferRef B = Graph.CreateBuffer(Desc);

auto AddClear = [&Graph](const char* Name,
                         FARDGBufferRef Buffer,
                         uint32_t Value)
{
    FClearParameters Parameters;
    Parameters.mBuffer = {
        Buffer,
        nvrhi::ResourceStates::UnorderedAccess,
        nvrhi::EntireBuffer
    };
    Graph.AddPass(
        Name,
        &Parameters,
        EARDGPassFlags::Compute | EARDGPassFlags::NeverCull,
        [Value](FARDGPassExecutionContext& Context,
                const FClearParameters& Frozen)
        {
            Context.mCommandList.clearBufferUInt(
                Context.GetBuffer(Frozen.mBuffer.mBuffer),
                Value);
        });
};

AddClear("Clear A", A, 0u);
AddClear("Clear B", B, 1u);

const FARDGExecutionResult& Result = Graph.Execute();
// For these descriptor-compatible, non-overlapping, single-queue resources:
// Result.mBufferPoolReuseCount increases by one.
```

This is committed-resource pooling, not placed-resource memory aliasing.
`mbUsedTransientFallback` is set for transient candidates, while
`mbUsedVirtualHeaps` and `mbUsedTransientAliasing` currently remain false.
Immediate mode or extended lifetimes make the intervals overlap and disable
reuse. The backend runtime test
[`ExecutesAndExtractsOnAvailableBackend`](../../Source/ArdaRenderGraph/Tests/ArdaGraphTests.cpp)
executes this clear-and-reuse pattern.

## 7. Extract history, then import it next frame

Extraction crosses a graph boundary. Keep the returned NVRHI handle in
application state, wait through the renderer's normal frame synchronization,
and import it into the next builder with the state requested at extraction.

```text
Frame N graph
  BuildHistory (UAV) -> epilogue transition to ShaderResource
                           |
                           v
                    extracted handle
                           |
Frame N+1 graph            v
  prologue -> imported History (ShaderResource) -> ReadHistory
```

```cpp
// Frame N
nvrhi::TextureDesc HistoryDesc;
HistoryDesc.setDebugName("Temporal history")
    .setWidth(Width)
    .setHeight(Height)
    .setFormat(nvrhi::Format::RGBA16_FLOAT)
    .setIsUAV(true);

FARDGTextureRef NewHistory = FrameGraph.CreateTexture(
    HistoryDesc,
    EARDGResourceFlags::None);

FTextureFlowParameters WriteHistory;
WriteHistory.mOutput = {
    NewHistory,
    nvrhi::ResourceStates::UnorderedAccess,
    nvrhi::AllSubresources
};
FrameGraph.AddPass(
    "Build history",
    &WriteHistory,
    EARDGPassFlags::Compute,
    RecordHistory);

nvrhi::TextureHandle HistoryForNextFrame;
FrameGraph.QueueTextureExtraction(
    NewHistory,
    HistoryForNextFrame,
    nvrhi::ResourceStates::ShaderResource);
FrameGraph.Execute();

// Frame N+1, after the application's required GPU/frame synchronization.
FARDGBuilder NextGraph(GraphContext);
FARDGTextureRef PreviousHistory = NextGraph.RegisterExternalTexture(
    HistoryForNextFrame,
    nvrhi::ResourceStates::ShaderResource,
    "Previous temporal history");
```

Extraction removes transient eligibility, keeps the producer live, extends the
lifetime through the epilogue, and fills the output handle after submission.
It does not wait for GPU completion. Declaration behavior is covered by
[`BuilderCreatesLogicalResourcesViewsAndExtractionDeclarations`](../../Source/ArdaRenderGraph/Tests/ArdaGraphTests.cpp),
while backend execution and extraction are covered by
[`ExecutesAndExtractsOnAvailableBackend`](../../Source/ArdaRenderGraph/Tests/ArdaGraphTests.cpp).

## 8. Actual triangle renderer walkthrough

The full integration is
[`Source/ArdaTests/NVRHITest/Private/ArdaTriangleRenderer.cpp`](../../Source/ArdaTests/NVRHITest/Private/ArdaTriangleRenderer.cpp).
It has two separate graph submissions: a one-time upload during initialization
and one raster graph per frame.

### Initialization outside the graph

`Initialize`:

1. copies graphics/compute/copy capabilities from `FArdaDeviceContext`;
2. loads DXIL or SPIR-V vertex and pixel shaders;
3. creates the input layout and a framebuffer-compatible graphics pipeline;
4. creates persistent vertex and index buffers whose descriptors use automatic
   state tracking with `VertexBuffer` and `IndexBuffer`.

Shader loading and pipeline creation are ordinary NVRHI work. The graph begins
only for the geometry upload.

### Upload graph

```text
External vertex buffer: VertexBuffer -> CopyDest -> VertexBuffer
                                      \ writeBuffer(vertices)

External index buffer:  IndexBuffer  -> CopyDest -> IndexBuffer
                                      \ writeBuffer(indices)

Graph queue: Graphics (`EARDGPassFlags::None`)
After submit: device.waitForIdle()
```

The source declares:

```cpp
ARDG_BEGIN_PARAMETER_STRUCT(FArdaTriangleUploadParameters)
    ARDG_BUFFER_ACCESS(mVertexBuffer)
    ARDG_BUFFER_ACCESS(mIndexBuffer)
ARDG_END_PARAMETER_STRUCT()
```

Both persistent buffers are imported with their real entry states. The upload
parameters request `CopyDest` for the entire buffers. The callback obtains each
physical buffer through `context.GetBuffer(...)` and issues two real
`writeBuffer` commands.

The pass uses `EARDGPassFlags::None`, so it records on graphics. It is not a
`Copy` pass and does not claim to run on the copy queue. Because these are
external writes, the pass is observable. Their default external final states
remain the registered `VertexBuffer` and `IndexBuffer` states, so the epilogue
restores both after upload.

`graph.Execute()` submits the upload. The renderer then calls
`mDevice->waitForIdle()` because initialization must not return and begin
rendering until geometry is ready. This explicit wait is application policy;
the graph itself only promises submission.

### Per-frame render graph

```text
swapChain.AcquireFrame(framebuffer)
                |
                v
Import color: Present ------------------------------+
Import vertex: VertexBuffer ----+                   |
Import index:  IndexBuffer -----+--> Render triangle|
                                      clear + draw  |
                                             |      |
Epilogue: color -> Present <-----------------+------+
                |
swapChain.PrepareSubmit()
                |
graph.Execute()
                |
swapChain.Present()
```

Each `RenderFrame` call:

1. acquires a framebuffer and validates color attachment zero;
2. imports that attachment texture in `Present`;
3. imports the persistent geometry buffers in `VertexBuffer` and
   `IndexBuffer`;
4. declares whole-buffer geometry reads and color attachment subresources;
5. registers one `Raster` pass;
6. calls the swap chain's `PrepareSubmit`;
7. executes the graph; and
8. presents.

The raster callback:

- calls `GetTexture` for the declared color attachment;
- clears the captured framebuffer's color attachment;
- builds viewport/scissor state from current swap-chain dimensions;
- builds `nvrhi::GraphicsState` with the pre-created pipeline, captured
  framebuffer, and graph-resolved vertex/index buffers;
- calls `setGraphicsState`; and
- issues a real `drawIndexed` for three indices.

The imported color write keeps the pass live. Compilation transitions only the
acquired attachment subresources from `Present` to `RenderTarget`; the
epilogue returns them to `Present`. The geometry buffers are declared in the
same states in which they enter.

The framebuffer is captured independently because ArdaRenderGraph does not
create NVRHI framebuffers. Calling `GetTexture` validates that the logical color
attachment was declared, but the graph cannot prove that the captured
framebuffer contains the same physical texture and subresources. That
consistency remains the renderer's responsibility.

### What this integration proves

The triangle integration executes real upload and draw commands. It does not
exercise shader dispatch or a copy-queue command. The render-graph unit tests
cover dispatch registration, copy selection, culling, transitions, fork/join,
and queue-edge compilation separately; do not read those compile-only tests as
evidence that dispatch or copy callbacks ran.

---

[← Executor source walkthrough](11-Executor-Source-Walkthrough.md) ·
[Documentation home](README.md) ·
[Next: Recipes and reference →](13-Recipes-and-Reference.md)
