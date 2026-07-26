# 2. Core concepts

[← Getting started](01-Getting-Started.md) · [Documentation home](README.md) ·
[Next: Resources and parameters →](03-Resources-and-Parameters.md)

## Declare intent, then execute

ArdaRenderGraph separates two domains:

- **Logical domain:** graph-owned records describe resources, views, passes,
  accesses, and dependencies. Creating these records does not allocate GPU
  resources.
- **Physical domain:** execution binds imported or newly allocated NVRHI
  handles, records command lists, and submits work.

This separation lets compilation reason about dead passes, queue placement,
state transitions, and non-overlapping resource lifetimes before GPU work is
created.

![ArdaRenderGraph architecture](assets/architecture.svg)

## Builder

`FARDGBuilder` owns one complete graph:

- graph-scoped arena storage;
- dense registries for passes, textures, buffers, views, and uniform buffers;
- the typed blackboard;
- compile and execution results; and
- external-resource and extraction records.

The builder is non-copyable and non-movable. Pointers such as
`FARDGTextureRef` and frozen parameter pointers are valid only for the
builder's lifetime.

## References and handles

The public convenience references are pointers to logical records:

```cpp
FARDGTextureRef
FARDGBufferRef
FARDGTextureSRVRef
FARDGTextureUAVRef
FARDGBufferSRVRef
FARDGBufferUAVRef
FARDGUniformBufferRef
```

Each record also has a compact, typed, 32-bit registry handle:

```cpp
FARDGPassHandle
FARDGTextureHandle
FARDGBufferHandle
FARDGViewHandle
FARDGUniformBufferHandle
```

Handles are stable because registries are append-only. Handle types are
deliberately incompatible, so a buffer handle cannot accidentally be used as a
texture handle. A default-constructed handle is invalid; use `IsValid()`,
`operator bool`, or `GetIndex()`.

`TryGetPass`, `TryGetTexture`, `TryGetBuffer`, `TryGetView`, and
`TryGetUniformBuffer` return null for invalid handles.

## Passes

A pass contains:

- a diagnostic name;
- `EARDGPassFlags`;
- an optional immutable parameter object and static metadata;
- a type-erased callback;
- derived producers, consumers, resource states, views, and uniform buffers;
- compiled transitions, pipeline, async fork/join, and raster group; and
- culling/sentinel status.

Registration order is the deterministic execution order after culled passes are
removed. Manual edges must also point forward in registration order; the
implementation does not topologically reorder arbitrary input.

### Callback forms

Typed `AddPass` accepts all of these shapes:

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

Parameterless `AddPass` accepts a pass context, a command list, or no arguments.
Prefer `FARDGPassExecutionContext` when touching graph resources: its getters
check that the resource or view appears in the pass parameters. A callback that
uses independently retained physical NVRHI handles cannot be completely
validated by the graph.

`AddDispatchPass` runs a setup callback and then calls
`ICommandList::dispatch` with `FARDGDispatchArguments`. It always adds the
`Compute` flag.

## Pass flags

- `None`: no implied pipeline operation. Parameterless utility passes still
  need `NeverCull`, an observable dependent, or a manual edge to a root.
- `Raster`: graphics pass with declared attachment bindings.
- `Compute`: compute dispatch/work on the graphics-capable pipeline.
- `AsyncCompute`: requests a distinct compute queue and must be combined with
  `Compute`.
- `Copy`: requests a copy queue; every declared state must be `CopySource`
  and/or `CopyDest`.
- `NeverCull`: makes the pass a culling root.
- `SkipRenderPass`: valid only with `Raster`; excludes the pass from raster
  compatibility grouping for explicitly managed framebuffer behavior.
- `NeverParallel`: records the pass serially on the CPU.

Illegal combinations are rejected. `Raster`, `Compute`/`AsyncCompute`, and
`Copy` are mutually exclusive operation categories. `AsyncCompute` without
`Compute`, or `SkipRenderPass` without `Raster`, is invalid.

## Frozen parameter storage

Parameter structs must be standard-layout types. `AddPass` behaves as follows:

- a pointer returned by `AllocateParameters<T>()` is already graph-owned and is
  retained;
- any other parameter object is copied into the graph arena;
- non-trivial destructors are registered and run in reverse order when the
  builder is destroyed.

The pass callback receives a `const` frozen object. This prevents later stack
changes from changing graph topology after dependency discovery.

## Typed blackboard

The graph blackboard stores one copy-constructible value per exact C++ type:

```cpp
struct FSceneGraphData
{
    FARDGTextureRef mDepth = nullptr;
    uint32_t mViewIndex = 0;
};

auto& Data = Graph.GetBlackboard().Emplace<FSceneGraphData>();
Data.mDepth = Depth;

if (Graph.GetBlackboard().Contains<FSceneGraphData>())
{
    FARDGTextureRef SharedDepth =
        Graph.GetBlackboard().Get<FSceneGraphData>().mDepth;
}
```

Available operations are `Contains`, `Set`, `Emplace`, `Get`, `TryGet`, and
`GetOrCreate`. `Get` throws when absent. The non-const builder accessor is
available only while building; the const accessor can inspect the blackboard
later.

The blackboard shares data between graph-building subsystems. It does not by
itself declare resource access: a pass must still place referenced resources in
its parameter struct.

## Sentinels

Every graph starts with `GraphPrologue` at pass handle 0. Compilation appends
`GraphEpilogue`.

- Imported resources treat the prologue as their initial producer.
- External and extracted outputs connect to the epilogue, keeping required
  producers live and applying final-state transitions.
- Sentinels are always live but do not invoke user callbacks.

---

[← Getting started](01-Getting-Started.md) · [Documentation home](README.md) ·
[Next: Resources and parameters →](03-Resources-and-Parameters.md)
