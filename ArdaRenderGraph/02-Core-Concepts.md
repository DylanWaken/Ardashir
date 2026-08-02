# 2. Core concepts: the graph is a plan, not the GPU

[← Getting started](01-Getting-Started.md) · [Documentation home](README.md) ·
[Next: Resources and parameters →](03-Resources-and-Parameters.md)

Return to the canonical runtime graph from the introduction:

```text
P0 --TerrainSettingsUpload--> P1 Upload -> P2 Generate -> P4 Erode -> P5 Triangulate
                                            |
                                            +-> P3 Debug heightmap (dead)

P0 --imported BackBuffer---------------------------------------> P6 Render -> P7 Overlay -> P8
                                                                   ^
                                                   terrain buffers--+
```

The runnable declaration and callbacks are in
[`ArdaTerrainRenderer.cpp`](../../Source/ArdaTests/ARDGExample/Private/ArdaTerrainRenderer.cpp);
the D3D12 and Vulkan smoke-test registration is in
[`ARDGExample/CMakeLists.txt`](../../Source/ArdaTests/ARDGExample/CMakeLists.txt).

While this graph is being built, none of its graph-created resources need to
exist on the GPU. They are descriptions that answer: what should run, what does
it access, and which result matters? Only after compilation answers those
questions does execution allocate resources and record NVRHI commands.

## Logical and physical are different domains

A *logical resource* is a graph-owned C++ record. For a texture it stores an
NVRHI descriptor, graph flags, a typed registry handle, initial/final states,
the latest writer, readers since that writer, and a use interval. A physical
resource is the `nvrhi::ITexture` or `nvrhi::IBuffer` on which commands
actually operate.

```text
Build                              Execute
-----                              -------
FARDGTexture T0 "Heightmap"        ---> nvrhi::ITexture* 0x...
FARDGBuffer  B0 "TerrainVertices"  ---> nvrhi::IBuffer*  0x...
```

For graph-created resources, the physical handle is initially empty. Execution
may allocate a new object or reuse a descriptor-compatible object whose earlier
logical lifetime has ended. For imported resources, the logical record wraps a
physical handle that already exists.

This split allows compilation to delete the unused `DebugHeightmap` pass before
materialization. It also allows two non-overlapping logical resources to share
one physical allocation without making them the same logical dependency.
The records are defined in
[`ArdaRenderGraphResources.h`](../../Source/ArdaRenderGraph/Public/ArdaRenderGraphResources.h);
materialization occurs in
[`ArdaRenderGraphExecutor.cpp`](../../Source/ArdaRenderGraph/Private/ArdaRenderGraphExecutor.cpp).

## One builder owns one graph submission

`FARDGBuilder` owns:

- an arena for graph-scoped records and frozen parameters;
- append-only registries for passes, textures, buffers, views, and uniform
  buffers;
- the typed blackboard;
- import and extraction records;
- immutable context and compile/execution results; and
- lifecycle state that prevents late mutation or repeated execution.

The private layout is visible in
[`ArdaRenderGraphBuilderInternal.h`](../../Source/ArdaRenderGraph/Private/ArdaRenderGraphBuilderInternal.h).
The builder is neither copyable nor movable. A pointer such as
`FARDGTextureRef`, a view reference, or a frozen parameter pointer belongs to
that builder and must not outlive it.

## Why there are both references and handles

The convenient public references are pointers to logical records:

```cpp
FARDGTextureRef
FARDGBufferRef
FARDGTextureSRVRef
FARDGTextureUAVRef
FARDGBufferSRVRef
FARDGBufferUAVRef
FARDGUniformBufferRef
```

Pointers make setup readable: `Texture->GetDesc()` and
`Texture->GetHandle()` are ordinary C++. Internally, dense typed handles make
stable graph indices:

```cpp
FARDGPassHandle
FARDGTextureHandle
FARDGBufferHandle
FARDGViewHandle
FARDGUniformBufferHandle
```

`TARDGHandle<Tag>` contains one `uint32_t`. Its tag makes handle categories
incompatible at compile time, so a texture handle cannot be passed where a
buffer handle is expected. A default-constructed handle has `InvalidIndex`;
test it with `IsValid()` or its explicit Boolean conversion.

### How a registry assigns a handle

Each registry is an append-only vector of arena-owned pointers:

```text
texture registry
index 0 -> FARDGTexture "Heightmap"   -> FARDGTextureHandle(0)
index 1 -> FARDGTexture "BackBuffer"  -> FARDGTextureHandle(1)
```

On `Emplace`, the current vector size becomes the handle index, the object is
allocated in the arena, and its pointer is appended. No erase operation exists,
so handles remain stable for the builder's lifetime. See
[`TARDGHandleRegistry`](../../Source/ArdaRenderGraph/Private/ArdaRenderGraphRegistry.h)
and the handle definition in
[`ArdaRenderGraphDefinitions.h`](../../Source/ArdaRenderGraph/Public/ArdaRenderGraphDefinitions.h).

The builder's `TryGetPass`, `TryGetTexture`, `TryGetBuffer`, `TryGetView`, and
`TryGetUniformBuffer` return `nullptr` for invalid or out-of-range handles.

## A pass has a declaration half and an execution half

The declaration half exists during build and compile:

- name and `EARDGPassFlags`;
- frozen parameter object and generated metadata;
- producer and synchronization edges;
- texture/buffer state requirements, views, and uniform buffers;
- culling and sentinel state; and
- compiled transitions, selected pipeline, async fork/join, and raster group.

The execution half is a type-erased callback invoked while recording a command
list. The callback sees the same frozen parameters from which edges and states
were discovered.

The public pass records and execution context are in
[`ArdaRenderGraphPass.h`](../../Source/ArdaRenderGraph/Public/ArdaRenderGraphPass.h).

## Registration order is the topological order

This invariant is easy to miss and crucial to using the graph correctly:

> Register every producer before every consumer. ArdaRenderGraph does not run a
> general topological sort to repair arbitrary registration order.

The surviving execution order is the original registry order with culled
passes removed:

```text
registered: P0 GraphPrologue, P1 UploadTerrainSettings, P2 GenerateNoiseHeightmap,
            P3 DebugHeightmap, P4 ErodeHeightmap, P5 TriangulateTerrain,
            P6 RenderTerrain, P7 TerrainOverlay, P8 GraphEpilogue
live:       P0, P1, P2, P4, P5, P6, P7, P8
```

Resource edges are naturally forward because a pass can only depend on the
resource's current last writer. `AddDependency` also rejects a producer that
was registered after its consumer. Compilation validates the same invariant
before constructing reverse consumer edges. See
[`FARDGBuilder::AddDependency`](../../Source/ArdaRenderGraph/Private/ArdaRenderGraphBuilder.cpp)
and
[`BuildConsumerEdges`](../../Source/ArdaRenderGraph/Private/ArdaRenderGraphCompiler.cpp).

## Frozen parameters make the declaration trustworthy

Suppose a caller registers `TriangulateTerrain` with a stack parameter pointing
at `Heightmap`, then reuses the stack object for another pass. If the graph kept
that pointer directly, compilation and execution could observe different
resources.

`AddPass` avoids that problem:

```text
caller's stack object --copy--> graph arena --const ref--> pass callback
                              |
                              +--> metadata traversal during AddPass
```

- `AllocateParameters<T>()` constructs directly in graph arena storage.
- A parameter pointer not known to the arena is copied there.
- Parameter structs must be standard-layout types.
- Non-trivial destructors are registered and run in reverse allocation order
  when the builder dies.
- The callback receives a `const` frozen object.

The template path is implemented by `AllocateParameters`, `FreezeParameters`,
and `AddPass` in
[`ArdaRenderGraphBuilder.h`](../../Source/ArdaRenderGraph/Public/ArdaRenderGraphBuilder.h).
The tests verify both stack freezing and reverse destruction in
[`ArdaGraphTests.cpp`](../../Source/ArdaRenderGraph/Tests/ArdaGraphTests.cpp).

## The blackboard shares build products by type

Large renderers often have independent setup functions. A depth prepass may
create a texture that later lighting setup needs, but threading that reference
through every function makes graph construction noisy. `FARDGBlackboard`
stores one copy-constructible value per exact C++ type:

```cpp
struct FSceneGraphData
{
    FARDGTextureRef mDepth = nullptr;
    uint32_t mViewIndex = 0;
};

FSceneGraphData& Scene =
    Graph.GetBlackboard().Emplace<FSceneGraphData>();
Scene.mDepth = Depth;
Scene.mViewIndex = 2;

if (const FSceneGraphData* Shared =
        Graph.GetBlackboard().TryGet<FSceneGraphData>())
{
    FARDGTextureRef SharedDepth = Shared->mDepth;
    (void)SharedDepth;
}
```

Available operations are:

- `Contains<T>()`;
- `Set(T)`;
- `Emplace<T>(...)`;
- `Get<T>()`, which fails when absent;
- `TryGet<T>()`, which returns `nullptr` when absent; and
- `GetOrCreate<T>()`.

The mutable builder accessor is only available while building; the const
accessor remains usable later. The implementation uses
`eastl::unordered_map<std::type_index, eastl::any>` in
[`ArdaRenderGraphBlackboard.h`](../../Source/ArdaRenderGraph/Public/ArdaRenderGraphBlackboard.h).

The blackboard is a communication channel, not an access declaration. Putting
`mDepth` in `FSceneGraphData` creates no dependency. A pass must still include
the texture or one of its views in its parameter struct.

## Sentinels give the graph boundaries

Every builder starts with `GraphPrologue` at pass handle 0. Compilation appends
`GraphEpilogue` after every user pass:

```text
GraphPrologue -> user passes -> GraphEpilogue
```

They have no user callbacks, but they simplify boundary reasoning:

- an imported resource uses the prologue as its initial producer;
- the epilogue depends on the last writer of every external or extracted
  resource;
- readers of those boundary resources contribute synchronization edges to the
  epilogue;
- final-state transitions are attached to the epilogue; and
- both sentinels remain live.

This is why writing an imported back buffer or extracting a graph-created
history texture roots its producer chain. Sentinel construction is in
[`ArdaRenderGraphBuilderInternal.h`](../../Source/ArdaRenderGraph/Private/ArdaRenderGraphBuilderInternal.h),
and epilogue wiring is at the start of
[`FARDGCompiler::Compile`](../../Source/ArdaRenderGraph/Private/ArdaRenderGraphCompiler.cpp).

## Pass flags in plain language

- `None` — no operation category. A utility callback still needs an observable
  dependent, a manual edge to a live pass, or `NeverCull`.
- `Raster` — graphics work with declared attachment bindings.
- `Compute` — compute work; by itself it uses the graphics-capable pipeline.
- `AsyncCompute` — asks for a distinct compute queue and must be combined with
  `Compute`. Capability or state incompatibility causes graphics fallback.
- `Copy` — asks for the copy queue. Declared states must contain only
  `CopySource` and/or `CopyDest`; unavailable copy capability falls back.
- `NeverCull` — creates a culling root for an intentional side effect.
- `SkipRenderPass` — valid only with `Raster`; opts out of raster compatibility
  grouping for explicitly managed framebuffer behavior.
- `NeverParallel` — records this callback serially on the CPU. It does not
  choose a GPU queue.

`Raster`, compute/async-compute, and `Copy` are mutually exclusive operation
categories. Invalid combinations are rejected when the pass is added. Flags
are defined in
[`ArdaRenderGraphDefinitions.h`](../../Source/ArdaRenderGraph/Public/ArdaRenderGraphDefinitions.h).

## Callback forms

Typed `AddPass` accepts:

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

Parameterless `AddPass` accepts a pass context, command list, or no arguments.
Prefer `FARDGPassExecutionContext` when accessing graph resources. Its
`GetTexture`, `GetBuffer`, and `GetUniformBuffer` methods verify that the
current pass declared the requested logical object. A raw command-list callback
can use independently retained NVRHI handles, which the graph cannot prove
complete.

`AddDispatchPass` uses the same setup callback forms, always adds `Compute`,
then calls `ICommandList::dispatch` with its `FARDGDispatchArguments`.

## Lifecycle recap

All of these concepts are scoped by the builder's one-way lifecycle:

```text
Building -> Compiled -> Executed
    \          \           \
     allowed    immutable    cannot execute again
```

Build declarations, blackboard mutation, and parameter allocation stop at
successful compilation. Compilation itself is idempotent. Execution may invoke
compilation, but only one execution attempt is allowed. A failed graph is not a
recovery container; discard it and construct the next graph.

---

[← Getting started](01-Getting-Started.md) · [Documentation home](README.md) ·
[Next: Resources and parameters →](03-Resources-and-Parameters.md)
