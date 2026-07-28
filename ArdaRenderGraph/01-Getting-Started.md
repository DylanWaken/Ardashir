# 1. Getting started: build procedural terrain

[← Documentation home](README.md) · [Next: Core concepts →](02-Core-Concepts.md)

The fastest way to understand a render graph is to follow a small result from
declaration to GPU submission. This chapter uses the three-stage core of the
series' pedagogical procedural-terrain graph:

```text
GraphPrologue
      |
      v
GenerateNoiseHeightmap --Heightmap--> TriangulateTerrain
       compute UAV                    compute SRV + buffer UAVs
                                               |
                                  TerrainVertices/TerrainIndices
                                               |
                                               v
                                        RenderTerrain
                                               |
                                    imported BackBuffer
                                               |
                                               v
                                        GraphEpilogue
```

The first compute pass generates a noise-based heightmap. The second turns that
heightmap into vertex and index buffers. The raster pass draws those buffers to
an imported back buffer. That external write is observable, so the complete
producer chain survives culling.

This is documentation code, not a claim that the repository currently ships
terrain shaders or a terrain demo. The application must provide shaders,
pipelines, binding sets, a framebuffer, and presentation.
[Chapter 12](12-End-to-End-Examples.md) contains the longer terrain listing.

## Prerequisites

The example assumes:

- a C++17 target linked to `Ardashir::ArdaRenderGraph`;
- an initialized `nvrhi::IDevice`;
- an acquired framebuffer whose color attachment is the current back buffer;
- correct graphics and compute queue capabilities; and
- application-provided NVRHI terrain shaders, binding layouts, pipelines, and
  state-building helpers.

ArdaRenderGraph does not create the device or graphics pipelines. The
[NVRHI programming guide](https://github.com/NVIDIA-RTX/NVRHI/blob/main/doc/ProgrammingGuide.md)
covers that setup. `ArdaBackend` can supply a device and queue capabilities, but
the graph only needs the resulting `FARDGRenderGraphContext`.

## A complete three-stage graph

The graph declarations below are complete. `BuildNoiseComputeState`,
`BuildTriangulationComputeState`, and `BuildTerrainGraphicsState` are clearly
marked application placeholders: each must create NVRHI binding/state objects
that refer to the physical resources passed to it. `DivideRoundUp`,
`FTerrainVertex`, the pipelines, and terrain dimensions are also ordinary
application definitions.

```cpp
#include "ArdaRenderGraph.h"

using namespace arda::render_graph;

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

void BuildAndSubmitTerrain(
    const FARDGRenderGraphContext& GraphContext,
    nvrhi::FramebufferHandle Framebuffer)
{
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
        .setIsVertexBuffer(true)
        .setCanHaveUAVs(true);
    FARDGBufferRef TerrainVertices = Graph.CreateBuffer(VertexDesc);

    nvrhi::BufferDesc IndexDesc;
    IndexDesc.setDebugName("TerrainIndices")
        .setByteSize(MaxTerrainIndexCount * sizeof(uint32_t))
        .setStructStride(sizeof(uint32_t))
        .setIsIndexBuffer(true)
        .setCanHaveUAVs(true);
    FARDGBufferRef TerrainIndices = Graph.CreateBuffer(IndexDesc);

    FARDGTextureViewDesc HeightmapView;
    HeightmapView.mTexture = Heightmap->GetHandle();
    HeightmapView.mSubresources =
        nvrhi::TextureSubresourceSet(0, 1, 0, 1);
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
    (void)Graph.AddDispatchPass(
        "GenerateNoiseHeightmap",
        &Generate,
        FARDGDispatchArguments{
            DivideRoundUp(HeightmapWidth, 8u),
            DivideRoundUp(HeightmapHeight, 8u),
            1
        },
        [NoisePipeline](FARDGPassExecutionContext& PassContext,
                        const FGenerateNoiseHeightmapParameters& Frozen)
        {
            nvrhi::ITexture* PhysicalHeightmap =
                PassContext.GetTexture(Frozen.mHeightmap);
            nvrhi::ComputeState State = BuildNoiseComputeState(
                NoisePipeline,
                PhysicalHeightmap); // Application placeholder.
            PassContext.mCommandList.setComputeState(State);
        },
        EARDGPassFlags::AsyncCompute);

    FTriangulateTerrainParameters Triangulate;
    Triangulate.mHeightmap = HeightmapSRV;
    Triangulate.mTerrainVertices = TerrainVerticesUAV;
    Triangulate.mTerrainIndices = TerrainIndicesUAV;
    (void)Graph.AddDispatchPass(
        "TriangulateTerrain",
        &Triangulate,
        FARDGDispatchArguments{
            DivideRoundUp(HeightmapWidth - 1u, 8u),
            DivideRoundUp(HeightmapHeight - 1u, 8u),
            1
        },
        [TriangulationPipeline](
            FARDGPassExecutionContext& PassContext,
            const FTriangulateTerrainParameters& Frozen)
        {
            nvrhi::ITexture* PhysicalHeightmap =
                PassContext.GetTexture(Frozen.mHeightmap);
            nvrhi::IBuffer* PhysicalVertices =
                PassContext.GetBuffer(Frozen.mTerrainVertices);
            nvrhi::IBuffer* PhysicalIndices =
                PassContext.GetBuffer(Frozen.mTerrainIndices);
            nvrhi::ComputeState State = BuildTriangulationComputeState(
                TriangulationPipeline,
                PhysicalHeightmap,
                PhysicalVertices,
                PhysicalIndices); // Application placeholder.
            PassContext.mCommandList.setComputeState(State);
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
    (void)Graph.AddPass(
        "RenderTerrain",
        &Render,
        EARDGPassFlags::Raster,
        [TerrainPipeline, Framebuffer, TerrainIndexCount](
            FARDGPassExecutionContext& PassContext,
            const FRenderTerrainParameters& Frozen)
        {
            nvrhi::IBuffer* PhysicalVertices =
                PassContext.GetBuffer(Frozen.mTerrainVertices.mBuffer);
            nvrhi::IBuffer* PhysicalIndices =
                PassContext.GetBuffer(Frozen.mTerrainIndices.mBuffer);
            (void)PassContext.GetTexture(
                Frozen.mTargets.mColor[0].mTexture);
            nvrhi::GraphicsState State = BuildTerrainGraphicsState(
                TerrainPipeline,
                Framebuffer,
                PhysicalVertices,
                PhysicalIndices); // Application placeholder.
            PassContext.mCommandList.setGraphicsState(State);
            PassContext.mCommandList.drawIndexed(
                nvrhi::DrawArguments().setVertexCount(TerrainIndexCount));
        });

    const FARDGCompileResult& Compiled = Graph.Compile();
    const FARDGExecutionResult& Executed = Graph.Execute();
    // Both references describe CPU compilation/submission, not GPU completion.
    (void)Compiled;
    (void)Executed;
}
```

### Step 1: describe the context

`FARDGRenderGraphContext` stores the device, queue capabilities, and optional
debug behavior. The builder takes the context by value and stores it for its
entire one-shot lifetime.

`mbGraphics` must be true. `mbCompute` means a distinct compute queue is
available. If it is false, the two eligible asynchronous-compute passes fall
back deterministically to graphics; compute commands do not become impossible.

The constructor enforces the graphics requirement in
[`FARDGBuilder::FARDGBuilder`](../../Source/ArdaRenderGraph/Private/ArdaRenderGraphBuilder.cpp).
A device is optional while building or calling `Compile()`, which is why the
CPU-only tests can inspect graph behavior. `Execute()` requires a device.

### Step 2: import the output and create logical resources

`RegisterExternalTexture` wraps the physical back-buffer texture in a logical
`FARDGTexture`. Because that texture already exists, its logical record keeps
the imported NVRHI handle and starts in the declared `Present` state.
`GraphPrologue` is its initial producer.

`CreateTexture` and `CreateBuffer` are different: they register graph-created
logical records whose physical handles stay empty until `Execute()`:

```text
Heightmap        logical texture, R32_FLOAT, UAV-capable
TerrainVertices logical buffer, vertex-buffer + UAV-capable
TerrainIndices  logical buffer, index-buffer + UAV-capable
BackBuffer      logical wrapper around imported nvrhi::ITexture
```

Names and dimensions/byte sizes must be valid. UAV declarations require
`TextureDesc::isUAV` or `BufferDesc::canHaveUAVs`. The creation paths are in
[`ArdaRenderGraphBuilder.cpp`](../../Source/ArdaRenderGraph/Private/ArdaRenderGraphBuilder.cpp);
the logical records are defined in
[`ArdaRenderGraphResources.h`](../../Source/ArdaRenderGraph/Public/ArdaRenderGraphResources.h).

### Step 3: create views and declare every access

The heightmap UAV and SRV are logical views of the same parent texture. The
terrain-buffer UAVs similarly select their parent buffers and ranges. They are
not separate physical allocations:

```text
Heightmap
  +-- HeightmapUAV: mip 0, UnorderedAccess
  +-- HeightmapSRV: mip 0, ShaderResource

TerrainVertices +-- TerrainVerticesUAV: EntireBuffer
TerrainIndices  +-- TerrainIndicesUAV: EntireBuffer
```

The parameter macros tell the graph how each pass uses those records:

- `ARDG_TEXTURE_UAV` and `ARDG_BUFFER_UAV` declare `UnorderedAccess` writes;
- `ARDG_TEXTURE_SRV` declares a shader-resource read;
- `ARDG_BUFFER_ACCESS` carries an explicit buffer, state, and range, which the
  raster pass uses for `VertexBuffer` and `IndexBuffer`; and
- `ARDG_RENDER_TARGET_BINDING_SLOTS` declares the imported back buffer as a
  `RenderTarget` write.

The callback alone is not inspected. Capturing a physical handle or mentioning
one only inside a helper would not create dependencies, states, barriers, or
lifetime information.

### Step 4: register producers before consumers

`AddDispatchPass` registers compute work and automatically adds the `Compute`
flag. Passing `AsyncCompute` requests the distinct compute queue when available
and state-compatible. It records the setup callback and then issues the
specified `dispatch`.

The passes are registered in dependency order:

```text
P0 GraphPrologue
P1 GenerateNoiseHeightmap
P2 TriangulateTerrain
P3 RenderTerrain
P4 GraphEpilogue       appended by Compile()
```

These are local handles for this compact graph. The extended P0–P8 graph in
Chapters 5 and 8 inserts settings upload, debug, erosion, and overlay passes.

Registration order is already the topological order; compilation does not move
an arbitrarily registered producer before its consumer. Parameter traversal
derives `P1 -> P2` from `Heightmap` and deduplicates the two buffer-derived
`P2 -> P3` producer edges.

Each registration also:

1. copies the stack parameter object into graph-owned arena storage;
2. registers a named pass and returns a typed handle;
3. walks the generated parameter metadata;
4. records resource states, views, producers, and latest writers; and
5. stores a type-erased callback that receives the frozen parameters later.

The metadata walk occurs in
[`FARDGSetupContext`](../../Source/ArdaRenderGraph/Private/ArdaRenderGraphBuilder.cpp).
The macro definitions and their generated metadata are in
[`ArdaRenderGraphParameters.h`](../../Source/ArdaRenderGraph/Public/ArdaRenderGraphParameters.h).

`FARDGPassExecutionContext::GetTexture` and `GetBuffer` resolve logical records
or views to their materialized parent `nvrhi::ITexture`/`nvrhi::IBuffer` during
recording. They also check that the current pass declared the requested object.
The application placeholders must build NVRHI bindings/state from these
resolved objects and matching logical view descriptors.

### Step 5: make the imported back-buffer write observable

External resources cross the graph boundary. Compilation connects their last
writer to `GraphEpilogue`, so `RenderTerrain` roots the complete producer chain:

```text
GenerateNoiseHeightmap -> TriangulateTerrain -> RenderTerrain -> GraphEpilogue
                                                    |
                                             BackBuffer external
```

The back buffer starts in `Present`, transitions to `RenderTarget` for
`RenderTerrain`, and returns to `Present` in the epilogue. No extraction or
`NeverCull` flag is needed. The application still owns swap-chain acquisition,
the framebuffer, any pre-submit hook, presentation, and the requirement that
the captured framebuffer matches the imported attachment.

### Step 6: compile and execute

The sample calls `Compile()` explicitly so its phases are visible; calling only
`Execute()` would compile automatically. Compilation validates the graph,
appends `GraphEpilogue`, performs backward culling, assigns pipelines, computes
lifetimes, lowers barriers, and emits cross-queue dependencies.

With a distinct compute queue, the important transitions are:

```text
P1 Heightmap:        Common -> UnorderedAccess
P2 Heightmap:        UnorderedAccess -> NonPixelShaderResource
P2 terrain buffers:  Common -> UnorderedAccess
P3 terrain buffers:  UnorderedAccess -> VertexBuffer / IndexBuffer
P3 BackBuffer:       Present -> RenderTarget
P4 BackBuffer:       RenderTarget -> Present
```

The `ARDG_TEXTURE_SRV` state remains `ShaderResource` if compute falls back to
graphics; async placement normalizes it to `NonPixelShaderResource`.

Execution then:

1. materializes the heightmap and both terrain buffers;
2. rebuilds transitions against the selected physical objects;
3. records one command list per live terrain pass with automatic barriers
   disabled, plus a boundary list for the epilogue transition;
4. invokes the two application setup helpers and records both dispatches;
5. invokes the graphics helper and records `drawIndexed`;
6. submits in live registration order, inserting a compute-to-graphics wait
   before `RenderTerrain` when the queues differ; and
7. submits the epilogue transition that restores `Present`.

The actual execution sequence begins in
[`FARDGExecutor::Execute`](../../Source/ArdaRenderGraph/Private/ArdaRenderGraphExecutor.cpp).
The repository tests validate these graph mechanisms separately; they do not
execute this terrain pipeline. The actual executable rendering example remains
the triangle renderer described in
[Chapter 12](12-End-to-End-Examples.md).

## Stack parameters are intentionally safe

This is valid:

```cpp
FGenerateNoiseHeightmapParameters Parameters;
Parameters.mHeightmap = HeightmapUAV;

(void)Graph.AddDispatchPass(
    "GenerateNoiseHeightmap",
    &Parameters,
    FARDGDispatchArguments{128, 128, 1},
    [](FARDGPassExecutionContext&,
       const FGenerateNoiseHeightmapParameters&) {},
    EARDGPassFlags::AsyncCompute);
Parameters.mHeightmap = nullptr; // Does not change the registered pass.
```

The underlying `AddPass` path freezes a copy before returning. The stored
callback receives `const FGenerateNoiseHeightmapParameters&`, so graph topology
cannot silently change when the original stack variable changes.
`AddDispatchPass` uses that same freezing path. `AllocateParameters<T>()` is
available when constructing the object directly in graph-owned arena storage
is more convenient.

## The builder's one-way lifecycle

![The one-way builder lifecycle](assets/graph-lifecycle.svg)

Think of the builder as a transaction with no rewind:

```text
Building --Compile()--> Compiled --Execute()--> Executed
    \-------------------Execute()--------------/
                              execution failure -> Failed
```

### Building

You may create or import resources, create views and uniform buffers, allocate
parameters, mutate the blackboard, register passes and manual dependencies, and
queue extraction.

### Compiled

Topology is fixed. `Compile()` is idempotent after success and returns the same
`FARDGCompileResult`. Build-time mutations are rejected. The result includes
the prologue and epilogue handles, live execution order, resource lifetimes,
cross-queue dependencies, and raster-group count.

### Executed

A builder executes at most once. On success, command lists have been submitted,
any queued extraction outputs have been filled, and NVRHI garbage collection
has run. The GPU may still be working.

If graph validation, compilation, or execution fails, discard the builder.
Execution uses a failure guard that marks an interrupted execution as failed;
compilation does not promise recovery after a failed validation path.

## Inspect a graph without a GPU

Compilation is device-independent:

```cpp
const FARDGCompileResult& Compiled = Graph.Compile();
const eastl::string Description = Graph.DumpGraph();

for (FARDGPassHandle Pass : Compiled.mExecutionOrder)
{
    const FARDGPass* Record = Graph.TryGetPass(Pass);
    // Inspect Record->GetName(), pipeline, transitions, and culling state.
    (void)Record;
}
```

`DumpGraph()` requires successful compilation and returns an `eastl::string`.
Its deterministic text includes execution order, logical resources, pass
pipelines and culling, producer and synchronization edges, transitions,
lifetimes, raster groups, and cross-queue dependencies.

## Build and test

From the repository root:

```powershell
cmake -S . -B build
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

Set `ARDASHIR_BUILD_TESTS=OFF` to omit tests. Render-graph compilation tests can
run without a device. Backend execution tests skip when a supported GPU backend
or required queue is unavailable.

---

[← Documentation home](README.md) · [Next: Core concepts →](02-Core-Concepts.md)
