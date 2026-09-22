# Registering a graph node

Publish a node as a named callable beside its class declaration. Applications
include the header and write `FillDemoBuffer(Edit, Buffer, uint32_t{7})`; the call
records a fresh operation and registers its implementation on demand. The global
object contains no graph, parameter, device or execution state.

## Declare the class and its invocation name

Declare parameter fields and their dependency requirements together using the
node parameter macros. Select the execution contract with
`TArdaNodeDefinition<NodeClass, Parameters, EArdaNodeType::...>`. Place `ARDA_DECLARE_GRAPH_NODE(NodeClass, InvocationName);` immediately
after the complete class, in the same namespace. This declares an `extern const`
callable, so every translation unit refers to the same object.

The example fills an existing GPU buffer and exposes it as `Destination`. Save
this header as `ArdaDemoFillNode.h`:

```cpp
#pragma once
#include "Graph/Node/ArdaNodeDefinition.h"

namespace arda
{
    ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaDemoFillParameters)
        ARDA_NODE_BUFFER(mDestination, EArdaDependencyAccess::Write,
            EArdaRHIResourceState::UnorderedAccess)
        ARDA_NODE_PARAMETER_WITH_DEFAULT(uint32_t, mValue, 0)
    ARDA_END_NODE_PARAMETER_STRUCT()

    class FArdaDemoFillNode final
        : public TArdaNodeDefinition<FArdaDemoFillNode, FArdaDemoFillParameters, EArdaNodeType::Compute>
    {
    public:
        static FArdaDependencyNodeMetadata GetMetadata();
        static FArdaRHIStatus DeclareResources(
            FArdaDependencyResourceContext& Context, FArdaParameters& Parameters);
        static FArdaRHIStatus Record(FArdaDependencyExecutionContext& Context,
            const FArdaParameters& Parameters, const FArdaState& State,
            FArdaInstanceState& Instance);
    };
    ARDA_DECLARE_GRAPH_NODE(FArdaDemoFillNode, FillDemoBuffer);
}
```

The class name and invocation name are independent. Project node types keep the
`FArda` prefix; callable names describe the operation. All project APIs use
`arda`, so choose a callable name unique across that namespace. An exported
callable is a C++ object and cannot share its name with another object or an
overload set. Keep a name such as `CopyTo` for free-function overloads that
dispatch different resource permutations to distinct named node callables.

## Define the callable and implement the contract

Place the matching `ARDA_DEFINE_GRAPH_NODE(NodeClass, InvocationName);` once in
the owning `.cpp`, in the same namespace. Include the header rather than
redeclaring the class. Save this implementation as `ArdaDemoFillNode.cpp`:

```cpp
#include "ArdaDemoFillNode.h"

namespace arda
{
    ARDA_DEFINE_GRAPH_NODE(FArdaDemoFillNode, FillDemoBuffer);

    FArdaDependencyNodeMetadata FArdaDemoFillNode::GetMetadata()
    {
        return {"example.demo.fill-buffer", 1};
    }

    FArdaRHIStatus FArdaDemoFillNode::DeclareResources(
        FArdaDependencyResourceContext& Context, FArdaParameters& Parameters)
    {
        const auto* Resource = Context.Find(Parameters.mDestination);
        if (!Resource || Resource->mbTexture || Resource->mExternalAccelerationStructure ||
            Resource->mBuffer.mCpuAccess != EArdaRHICpuAccess::None ||
            !Resource->mBuffer.mByteSize || Resource->mBuffer.mByteSize % sizeof(uint32_t) ||
            !HasAnyFlags(Resource->mBuffer.mUsage,
                EArdaRHIBufferUsage::UnorderedAccess | EArdaRHIBufferUsage::AutoDeduction))
        {
            return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
                "FillDemoBuffer requires a GPU UAV buffer with a nonzero multiple-of-four size.");
        }
        auto Destination = Parameters.mDestination.GetHandle();
        return Context.Buffer(Destination, "Destination", Resource->mBuffer);
    }

    FArdaRHIStatus FArdaDemoFillNode::Record(FArdaDependencyExecutionContext& Context,
        const FArdaParameters& Parameters, const FArdaState&, FArdaInstanceState&)
    {
        return Context.GetCommands().ClearBufferUInt(
            *Context.GetBuffer(Parameters.mDestination), Parameters.mValue);
    }
}
```

`GetMetadata` supplies a nonempty process-wide registry name and a positive
implementation version. This runtime identity is separate from the C++ callable
symbol. Keep both stable; change the version when the implementation contract
changes. Registering a conflicting class, schema or version under an existing
name fails with `InvalidArgument`; it does not replace that definition.

`DeclareResources` validates and publishes logical outputs transactionally. An
empty output passed to `Context.Buffer` or `Context.Texture` can instead infer a
transient resource from a descriptor. Return a failure to reject invocation.
The parameter macros generate dependency descriptions and a canonical key for
supported parameter types, so this node needs neither `Describe` nor
`GetCanonicalKey`. The destination declaration generates a whole-buffer write in
`UnorderedAccess` state. `Record` resolves only declared resources and propagates
native command failures. Each invocation owns an independent frozen parameter
snapshot; the global callable itself retains none.

For custom parameter types, provide `GetCanonicalKey` with
`FArdaDependencyKeyBuilder` and include every behavior-affecting value, resource
and retained semantic object. Do not hash raw struct bytes, which may contain
padding. The builder's resource encoding allows equivalent fresh constructions
to reuse persistent graph revisions.

This operation uses the base's empty setup and instance states. Shader nodes can
declare an opaque nested `FArdaState` and implement `Prepare(Device)` for immutable
per-device setup, plus `FArdaInstanceState` / `CreateInstance` for private mutable
state. Shader registration can stay in the implementation; shader parameter
metadata referenced by node parameter macros must be visible at their declaration.
CUDA nodes select `EArdaNodeType::Cuda` and implement `PrepareCuda` instead of `Record`;
synchronization nodes may omit execution. See the [compiled node recipes](node-recipes.html)
for compute, raster, mesh, work-graph, CUDA and ray-tracing examples, and
[feature requirements](feature-requirements.html) for `GetRequirements` admission.

## Describe dependencies in parameter declarations

The macros are declared in `Graph/Node/ArdaNodeParameters.h`, also included by
`Graph/Node/ArdaNodeDefinition.h`. They generate an aggregate parameter struct,
static metadata, and a visitor used by the ordinary registration path. They do
not register a node or allocate resources during static initialization.
Declare every instance field between `BEGIN` and `END` through a node parameter
macro, including ordinary scalar/configuration values. Plain C++ fields added
there would not participate in generated metadata or canonical keys.

This pixel-stage parameter declaration includes its logical input, sampled
texture and framebuffer attachments:

```cpp
ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaMeshPSParameters)
    ARDA_NODE_STAGE_INPUT(FArdaGraphFragments, mInput)
    ARDA_NODE_SHADER_TEXTURE(mAlbedo, FArdaMeshPixelShader::FArdaParameters, mAlbedo)
    ARDA_NODE_COLOR_TARGET(mColor)
    ARDA_NODE_DEPTH_TARGET(mDepth, EArdaDependencyAccess::ReadWrite)
ARDA_END_NODE_PARAMETER_STRUCT()
```

`FArdaMeshPixelShader::FArdaParameters` is a shader parameter struct with an
`ARDA_SHADER_TEXTURE_SRV(mAlbedo, ..., EArdaRHIShaderStage::Pixel)` member. The node
declaration references that existing member, deriving its binding type, shader
visibility and read access. It also adds the color attachment and render-target
write, the depth attachment and depth read/write access, and the typed fragment
input. There is no corresponding `BindShader`, `mAccesses`, `mColorTargets` or
`mDepthTarget` code to maintain in `Describe`.

Compute and vertex shaders use the same mechanism:

```cpp
ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaTransformParameters)
    ARDA_NODE_SHADER_BUFFER(mInput, FArdaTransformShader::FArdaParameters, mInput)
    ARDA_NODE_SHADER_BUFFER(mOutput, FArdaTransformShader::FArdaParameters, mOutput)
    ARDA_NODE_PARAMETER(uint32_t, mElementCount)
ARDA_END_NODE_PARAMETER_STRUCT()

ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaMeshVSParameters)
    ARDA_NODE_SHADER_BUFFER(mPositions, FArdaMeshVertexShader::FArdaParameters, mPositions)
    ARDA_NODE_PARAMETER(uint32_t, mVertexCount)
ARDA_END_NODE_PARAMETER_STRUCT()
```

Here the shader schemas declare `mInput` and `mPositions` as buffer SRVs and
`mOutput` as a buffer UAV. Node resource members use typed graph handles while
shader parameter members continue to describe native shader bindings. Shader
member names need not match node member names: the final macro argument selects
the shader member.

| Declaration | Generated requirement |
| --- | --- |
| `ARDA_NODE_PARAMETER(Type, Member)` | Ordinary aggregate value and canonical-key entry. |
| `ARDA_NODE_PARAMETER_WITH_DEFAULT(Type, Member, Default)` | Ordinary value with an explicit default. |
| `ARDA_NODE_STAGE_INPUT(Type, Member)` | Logical graphics-stage dependency, without a physical allocation. |
| `ARDA_NODE_STAGE_INPUTS(Type, Member)` | An ordered vector of logical ray-shader predecessors. |
| `ARDA_NODE_BUFFER(Member, Access, State)` | Typed buffer and whole-buffer access. |
| `ARDA_NODE_TEXTURE(Member, Access, State)` | Typed texture and whole-texture access. |
| `ARDA_NODE_COLOR_TARGET(Member)` | Color attachment and render-target write; declaration order determines attachment order. |
| `ARDA_NODE_COLOR_TARGET_ACCESS(Member, Access)` | Color attachment with explicit access; use `ReadWrite` when loading or blending with existing contents. |
| `ARDA_NODE_DEPTH_TARGET(Member, Access)` | Depth attachment and `DepthRead` for read-only access, otherwise `DepthWrite`. |
| `ARDA_NODE_SHADER_BUFFER(Member, ShaderParameters, ShaderMember)` | Typed buffer bound through the selected shader parameter metadata. |
| `ARDA_NODE_SHADER_TEXTURE(Member, ShaderParameters, ShaderMember)` | Typed texture bound through the selected shader parameter metadata. |
| `ARDA_NODE_ACCELERATION_STRUCTURE(Member, Access, State)` | Typed acceleration structure with an explicit non-shader access. |
| `ARDA_NODE_SHADER_ACCELERATION_STRUCTURE(Member, ShaderParameters, ShaderMember)` | Acceleration structure bound through shader metadata. |
| `ARDA_NODE_CUDA_BUFFER(Member, CudaParameters, CudaMember)` | Buffer access derived from the CUDA/compute parameter schema. |
| `ARDA_NODE_CUDA_TEXTURE(Member, CudaParameters, CudaMember)` | Texture access derived from the CUDA/compute parameter schema. |

Shader SRVs and constant buffers default to `Read`; UAVs default to `Write`.
For a shader that also reads its UAV's previous contents, declare `ReadWrite`
once in the shader schema:

```cpp
ARDA_BEGIN_SHADER_PARAMETER_STRUCT(FArdaUpdateShaderParameters)
    ARDA_SHADER_BUFFER_UAV_ACCESS(mParticles, 0, 0,
        EArdaRHIShaderStage::Compute, EArdaShaderResourceAccess::ReadWrite)
ARDA_END_SHADER_PARAMETER_STRUCT()

ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaUpdateNodeParameters)
    ARDA_NODE_SHADER_BUFFER(mParticles, FArdaUpdateShaderParameters, mParticles)
ARDA_END_NODE_PARAMETER_STRUCT()
```

The node inherits `ReadWrite` from that member. `ARDA_SHADER_TEXTURE_UAV_ACCESS`
is the texture equivalent. The shader schema is the source of binding type,
array extent, visibility and required access. A node's optional
`ARDA_NODE_SHADER_BUFFER_ACCESS` / `ARDA_NODE_SHADER_TEXTURE_ACCESS` can add a
more conservative UAV dependency; it cannot discard a shader-required read or
write. SRV and constant-buffer mappings remain read-only. All these declarations
feed automatic resource-usage deduction, so callers can leave descriptors at
`AutoDeduction`.

A second mesh drawing into existing color/depth attachments should declare the
contents it consumes. Use `ARDA_NODE_COLOR_TARGET_ACCESS(mColor,
EArdaDependencyAccess::ReadWrite)` when the pass preserves or blends prior color,
and `ARDA_NODE_DEPTH_TARGET(mDepth, EArdaDependencyAccess::ReadWrite)` when depth
testing and writing. The default color macro describes a write-only attachment.

The shader `*_BINDING` forms also take a pipeline slot, access, array element and
immutable view. `ARDA_NODE_SHADER_BUFFER_ARRAY` and
`ARDA_NODE_SHADER_TEXTURE_ARRAY` map fixed arrays, with their element count and
access argument supplied explicitly; pass `EArdaDependencyAccess::AutoDeduction`
to inherit the shader schema's access. Shader samplers and immediate values can remain
in custom `Describe` binding declarations; macro resources join the binding
declaration for the matching shader metadata and pipeline slot.

The same rule applies to CUDA: declare resource kind and access in the schema
used by the kernel or external compute operation, then refer to its members:

```cpp
#define ARDA_UPDATE_CUDA_FIELDS(VALUE, BUFFER, SURFACE) \
    VALUE(uint32_t, mCount) \
    BUFFER(float, mInput, EArdaComputeAccess::Read) \
    BUFFER(float, mOutput, EArdaComputeAccess::Write)
ARDA_CUDA_PARAMETER_STRUCT(FArdaUpdateCudaParameters, ARDA_UPDATE_CUDA_FIELDS)
#undef ARDA_UPDATE_CUDA_FIELDS

ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaUpdateCudaNodeParameters)
    ARDA_NODE_CUDA_BUFFER(mSource, FArdaUpdateCudaParameters, mInput)
    ARDA_NODE_CUDA_BUFFER(mDestination, FArdaUpdateCudaParameters, mOutput)
    ARDA_NODE_PARAMETER(uint32_t, mCount)
ARDA_END_NODE_PARAMETER_STRUCT()
```

These mappings derive dependency direction from the kernel schema and use the
CUDA interop resource state. They do not duplicate `Read`/`Write` in the node
declaration. `ARDA_NODE_CUDA_TEXTURE` maps a schema surface/texture member.
`TArdaCudaOperandNode` also continues to rebind the complete CUDA schema directly
to graph arguments.

Inside `PrepareCuda`, initialize scalar host arguments, then resolve resources
through the same member mappings:

```cpp
FArdaUpdateCudaParameters Host;
Host.mCount = Parameters.mCount;
if (auto Status = ResolveNodeCudaParameters(Context, Parameters, Host); !Status)
{
    return Status;
}
// Append the CUDA operation using Host.
```

Resolution requires the host storage type and a mapping for every resource in
that exact schema; passing the device-side packed arguments is rejected.
It leaves `Host` unchanged on failure. Before dispatch, the graph also checks the
operations appended by `PrepareCuda`: their actual resource identities, accessed
ranges and read/write directions must be covered by that node's declarations.
This catches a custom hook that binds a different buffer or performs a write
where it only declared a read.

Node registration validates mappings before preparing device state: the named
member must exist, its resource kind must match the typed graph field, and array
elements must fit without duplicate mappings in the same pipeline slot. Invalid
access overrides are rejected. Custom `Describe` declarations are subject to the
same binding contract; they cannot bypass the canonical shader requirements.
During graph compilation, pipeline layouts and supplied resource arguments are
also checked against the metadata retained by the prepared shaders. Missing
bindings, incompatible layouts or visibility, and access narrower than the
shader contract reject compilation before execution.

The contract describes declared shader/CUDA requirements. It does not inspect
arbitrary shader instructions, native CUDA pointers, or external library internals
to discover hidden accesses. Authors must declare those requirements correctly;
the graph checks that its resource mappings and scheduled execution honor them.

Keep `Describe` only for additional requirements such as prepared shader stages,
pipeline requests, special ranges, dynamic resource collections, shader-table
records or observable side effects. The generated declaration is appended to
that custom description automatically; do not repeat the same binding or
attachment in both places. Use `ARDA_NODE_PARAMETER(FArdaGraphBuffer, Member)`
(or the texture equivalent) when a field's entire access declaration belongs in
the custom hook, for example to describe a parameter-dependent subrange. A stage
node typically only contributes its shader:

```cpp
static FArdaDependencyNodeDesc Describe(
    const FArdaParameters&, const FArdaState& State)
{
    FArdaDependencyNodeDesc Description;
    Description.mPipelineStages = {State.mStage};
    return Description;
}
```

The structs remain aggregates. Both `WriteMeshPixels(Edit, Parameters)` and
`WriteMeshPixels(Edit, Fragments, Albedo, Color, Depth)` use the same declarations;
the positional form follows field declaration order. The macros work with every
node execution contract, including compute, copy, CUDA, ray tracing and graphics
stages. Plain parameter structs and custom `Describe` remain supported for
definitions that need a fully custom description.

Generated canonical keys support scalar/enumeration values, graph resources and
stage values, strings, resource ranges/views, and arrays/vectors of supported
elements. Extend the encoder for a reusable application value type, in `arda`:

```cpp
template <>
struct TArdaNodeParameterKey<FArdaDispatchShape>
{
    static constexpr bool mSupported = true;

    static void Append(FArdaDependencyKeyBuilder& Key, const FArdaDispatchShape& Value)
    {
        Key.Value(Value.mX).Value(Value.mY).Value(Value.mZ);
    }
};
```

Here `FArdaDispatchShape` is an application struct with three integer dimensions.
Declare the specialization before using that type in a parameter macro. Encode
semantic fields rather than raw memory or padding. Alternatively, implement the
node's `GetCanonicalKey` to encode its complete parameter snapshot. Unsupported
fields require one of these choices; they are not silently omitted from identity.

## Declare individual graphics stages

`TArdaNodeDefinition<Node, Parameters, Type>` is the shared authoring entry for
`Compute`, `Cuda`, `Copy`, `Synchronization`, complete `Graphics`, `Meshlet`,
`RayTracing` and `WorkGraph` operations, and individual graphics/ray shader stages.
All choices share the registry, parameter snapshots, global invocation macros and
structured/positional calls. Stage-specific aliases remain available as shorthand.

Include `Graph/Node/ArdaNodeDefinition.h` and select the stage in the same definition template:

| `EArdaNodeType` | Input | Invocation result |
| --- | --- | --- |
| `Vertex` | Declared physical resources | `FArdaGraphVertices` |
| `Raster` | `FArdaGraphVertices` | `FArdaGraphFragments` |
| `Pixel` | `FArdaGraphFragments` | `FArdaGraphPixels` |

```cpp
class FArdaMyVertexNode final
    : public TArdaNodeDefinition<FArdaMyVertexNode, FArdaMyVertexParameters,
          EArdaNodeType::Vertex>
{
    // GetMetadata, Prepare and the shader contribution in Describe.
};
ARDA_DECLARE_GRAPH_NODE(FArdaMyVertexNode, MyVertex);
```

The same selector provides `Hull`, `Domain`, `Geometry`, `Amplification` and
`Mesh`. Their logical values describe the applicable graphics stage interfaces;
preprocessing validates stage order and pipeline-family compatibility. Shader
bindings are declared on their owner with that stage's visibility.

The intermediate types belong to the dependency graph module. Declare a raster
or pixel input with `ARDA_NODE_STAGE_INPUT(Type, Member)`; its member name is
independent of the stage contract. Plain structs can still use the `mInput`
convention or expose a custom layout through `static GetStageInput(Parameters)`.
The registration path includes the input in the construction key; generated
parameter keys cover the other fields automatically.

Keep the same metadata, preparation and global invocation macros as other nodes.
`Describe` contributes the shader through `mPipelineStages`; parameter macros
declare the stage's resources, bindings and attachments. A vertex stage can set
`mGraphicsInputLayout`. The raster stage supplies a complete fixed-state
configuration using a contribution with no shader. Do not add a manual pipeline
request for an individual graphics stage.

These nodes have no authored `Record` callback. To supply dynamic state, optionally
implement:

```cpp
static FArdaRHIStatus ConfigureGraphics(FArdaDependencyExecutionContext& Context,
    const FArdaParameters& Parameters, const FArdaState& State,
    FArdaInstanceState& Instance, FArdaGraphicsWork& Work);
```

For example, the vertex hook sets `Work.mState.mVertexBuffers`, an optional index
buffer, `Work.mDraw`, and `Work.mbIndexed`. The raster hook sets viewports and
scissors. The pixel hook may clear its declared target. The compiler collects
the stages' bindings and accesses; the executor applies graphics state and issues
one draw after the hooks succeed.

```cpp
const auto Vertices = TriangleVS(Edit, UploadedVertices, UploadedIndices);
const auto Fragments = TriangleRasterize(Edit, Vertices, Width, Height);
TrianglePS(Edit, Fragments, Color);
```

The variables establish stage dependencies without `Edit.Order`. Reusing a vertex
value in a second raster operation creates another draw and executes the vertex
shader again. See the [complete stage classes](Examples/Nodes/RasterTriangleNode.h)
and [their compiled implementation](Examples/Nodes/RasterTriangleNode.cpp).

Every declared graphics stage must belong to a complete chain ending at a pixel
operation, or to a chain explicitly consumed by a successful `FetchBuffer`
materializer. A vertex operation by itself, or a vertex/raster chain without a
pixel terminal, makes `Graph.Compile()` fail. This completeness check runs before
culling, so leaving a stage's result unused does not hide an incomplete pipeline.
Complete chains whose physical outputs are unused can still be culled normally.
Wrong stage order, incompatible input kinds, missing inputs and foreign or stale
stage values also fail validation. Explicit materialization is the exception for
intentionally stopping at an intermediate stage; it does not relax those checks.

For ordinary fixed-function rasterization, a library does not need its own raster
class. Include `Graph/DefaultNodes/ArdaGraphicsOperations.h` and invoke the built-in
`Rasterize` operation:

```cpp
const FArdaRHIViewport Viewport{0.f, float(Width), 0.f, float(Height), 0.f, 1.f};
FArdaRHIRasterState RasterState;
RasterState.mCullMode = EArdaRHICullMode::None;
const FArdaRHIDepthStencilState DepthState{true, true, EArdaRHIComparisonFunc::GreaterOrEqual};

// Reuse the same configuration with any vertex-stage output.
const FArdaRasterConfig Config{Viewport, RasterState, DepthState};
const auto Fragments = Rasterize(Edit, Vertices, Config);

// Equivalent positional form, with trailing fields defaulted.
const auto OtherFragments = Rasterize(Edit, OtherVertices, Viewport, RasterState, DepthState);
```

Both forms use the same registered node, validation, frozen parameters and cache
identity. The variadic form follows `FArdaRasterConfig` field order: viewport,
raster state, depth/stencil state, blend state, scissor, draw arguments, topology,
patch control-point count. Omitted trailing fields retain their defaults; wrong
argument types or ordering fail at C++ compilation. `FArdaRasterizeParameters`
packages `mInput` and `mConfig` for callers using a complete parameter snapshot.
Each returned fragment value must feed a pixel stage or explicit materialization.

The configuration exposes topology, raster, depth/stencil and blend state. A zero draw
count preserves the vertex stage's draw arguments. A custom raster class is useful
when a shader library also supplies its own fragment-capture representation.

## Compose ray-tracing stages

Select `RayMiss`, `RayClosestHit`, `RayAnyHit`, `RayIntersection` or `RayCallable`
to define a shader provider. Invocation returns `FArdaGraphRayShader`. The
provider describes one shader contribution with its `mExportName` (miss/callable)
or `mHitGroupName` (hit stages), plus that shader's bindings and resource accesses.
Providers do not issue dispatches or own shader tables.

Select `RayGeneration` for the executable terminal. Declare its provider list
with `ARDA_NODE_STAGE_INPUTS(FArdaGraphRayShader, mShaders)`. Plain structs can use
`eastl::vector<FArdaGraphRayShader> mShaders`, or a `GetRayInputs` hook can expose an
equivalent list. Those values select the miss, hit and callable providers; there
are no manual stage-order edges. A shared provider can participate in multiple
ray-generation pipelines.

```cpp
const auto Miss = EnvironmentMiss(Edit, EnvironmentParameters);
const auto Hit = SurfaceClosestHit(Edit, MaterialParameters);
TraceScene(Edit, FArdaTraceParameters{{Miss, Hit}, Scene, Output});
```

Here the operation names and parameter structs are library-defined. The terminal
contributes its ray-generation shader, optional ray configuration and shader-table
records, and implements `Record` to call `SetRayTracingState()` then
`DispatchRays(...)`. Preprocessing merges each selected provider's shader bindings
and accesses into that terminal and supplies its default ray-tracing pipeline
request if omitted. Each node's shader schema must use its owning stage's
visibility. Invalid input kinds, mismatched shader stages, conflicting exports,
hit groups or layouts fail compilation. The [shader-table GPU example](../../Source/ArdaInfra/ArdaDependencyGraph/Tests/ArdaInductorGpuTests.cpp)
uses a standalone miss node and ray-generation node through the unified template.

## Materialize stage results when a buffer is required

`FetchBuffer(Edit, StageValue, Request)` asks the producer's registered node
definition to materialize its logical results as a physical `FArdaGraphBuffer`.
A stage class opts in with this static hook:

```cpp
static FArdaGraphBuffer FetchBuffer(FArdaGraphEdit& Edit,
    const FArdaParameters& Parameters, const FArdaStageBufferRequest& Request);
```

The hook receives the original frozen parameters and records ordinary operations
through the facade. A vertex library can share one shader function between its
vertex shader and a compute materializer. A raster library can create a dedicated
raster/pixel capture branch that writes interpolated inputs to a storage buffer.
Define the element schema, capacity behavior and required device features in that
library. `Request.mElementCount` provides the requested capacity; a zero value
lets the materializer choose its documented default.

Fetching is explicit GPU work. It does not make normal stage variables allocate
buffers or reuse captured data in subsequent draws. It also does not instrument
an arbitrary opaque shader automatically. A node without a materializer reports
`Unsupported` through the edit status. `Readback(Edit, FetchBuffer(...))` uses the
normal buffer readback operation after materialization.

A successful materializer consumes that stage and its upstream stage chain for
the completeness check. This permits a vertex-only capture construction without
adding a raster/pixel draw. Merely implementing the hook, retrieving the logical
value, or making a failed `FetchBuffer` call does not complete the chain. Other
unused branches must still end at a pixel operation or their own successful
materialization.

The [compiled raster recipe](Examples/Nodes/RasterTriangleNode.cpp) implements
vertex capture by sharing `RecipeVertexPosition` between its VS and compute
materializer. `FetchBuffer(Edit, RecipeTriangleVS(Edit, uint32_t{3}))` returns
three clip-space `float4` positions (16-byte records). An explicit nonzero count
can capture a prefix and must not exceed the producer's count. The recipe's
assembly reads the positions back and checks them after execution.

The [GPU capture example](../../Source/ArdaInfra/ArdaDependencyGraph/Tests/ArdaGraphicsStageGpuTests.cpp)
also shows fragment capture and its shared [shader functions](../../Source/ArdaInfra/ArdaDependencyGraph/Tests/ArdaGraphicsStageCapture.hlsl).
Its fragment records contain interpolated position and color, with an explicit
counter and overflow handling. Fragment storage/atomics support is required for
that implementation. The raster recipe's raster/pixel nodes and RHITest's triangle
nodes do not implement buffer capture.

## Link and invoke the node

Resource usage defaults to `AutoDeduction`. Declare accesses, shader bindings and
graphics attachments through the parameter macros; the compiler combines those
requirements before allocation. Custom `Describe` can add requirements that need
runtime parameter-dependent logic. In `DeclareResources`, accept automatic usage
instead of requiring a usage bit that has not been deduced yet. Explicit usage
requirements supplied to `Context.Buffer`/`Context.Texture` are retained even
when the caller supplies an existing automatic resource. Keep shape, format,
stride and CPU-access validation. See [automatic resource usage](../../Source/ArdaInfra/ArdaDependencyGraph/README.md#automatic-resource-usage).

Compile `ArdaDemoFillNode.cpp` into the application or node library and link
`Ardashir::ArdaDependencyGraph`. Include `ArdaDemoFillNode.h` where the callable
is used. A missing definition produces an unresolved symbol; defining the same
callable twice violates the one-definition rule and is a link error. The macros
check that the class derives from the node base; registration or invocation
checks the complete hook contract at compile time. Neither macro registers a
node or prepares a device during static initialization.

```cpp
#include "ArdaDependencyGraph.h"
#include "ArdaDemoFillNode.h"

// Device was created by the application.
FArdaDependencyGraph Graph(Device);
FArdaGraphEdit Edit(Graph);
FArdaRHIBufferDesc Desc;
Desc.mByteSize = 16;
// mUsage defaults to AutoDeduction; FillDemoBuffer declares the UAV requirement.
auto Buffer = AllocateBuffer(Edit, Desc);

FillDemoBuffer(Edit, Buffer, uint32_t{7});
FArdaDemoFillParameters Parameters{Buffer, 9};
const auto Filled = FillDemoBuffer(Edit, Parameters);
const auto Output = Filled.GetBuffer("Destination");
const auto Bytes = Readback(Edit, Output);

if (auto Status = Edit.Commit(); !Status) { return Status; }
if (auto Status = Graph.Compile(); !Status) { return Status; }
return Graph.Execute().mStatus;
```

The two calls deliberately create two operations; the second write follows the
first. Positional arguments aggregate-initialize fields in declaration order;
omitted fields keep their defaults and narrowing is rejected at compile time.
An initializer callback is also supported. For an explicit diagnostic name,
use the parameter-struct or initializer form and append the name. Diagnostic
names identify operations within one construction and are separate from both
the global callable symbol and registry name.

Calls automatically run the class's registration path. Failures from registration,
feature admission, validation or preparation are retained by the edit; check
`Edit.Commit()` before compiling. Construction errors do not produce a partially
accepted edit. Resolve typed output slots while the edit is open. Execution
failures are reported by `Execute`, `Submit` or ticket completion.

## Register before discovery when needed

Named callable use requires no startup registration. Tools that enumerate nodes
or select by registry string must register their library first:

```cpp
if (auto Status = FArdaDemoFillNode::Register(); !Status) { return Status; }
// Safe to repeat for this same class and implementation version.
const auto Names = FArdaNodeRegistry::Get().GetNames();

FArdaGraphEdit Edit(Graph);
const auto Filled = Edit.Invoke("example.demo.fill-buffer", Parameters);
```

Registry invocation validates the public parameter type; a missing definition
or mismatched schema fails through the edit status. Registration is independent
of any graph or device and performs no GPU work. Library initialization can call
each class's `Register()` and propagate its status. Direct named calls, static
`NodeClass::Invoke` and generic `Edit.Invoke<NodeClass>` all use the same
registration and authoring path. Node authors do not expose raw graph attachment
or implement a separate operation registry.
