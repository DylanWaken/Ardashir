# ArdaDependencyGraph

Use ArdaDependencyGraph to declare GPU operations and their resource accesses.
Preprocessing expands the declarations into a graph that ArdaInductor can
schedule. The executor uses the resulting schedule and memory plan to manage
native resources and track each frame. Follow the [graph quickstart](../../../Docs/ArdaRDG/getting-started.html)
to clear a buffer and read the result from the CPU.

Check the status from `Compile()` before submitting. `Submit()` records and
submits commands on the calling thread and returns a GPU frame ticket.
Pass the ticket to `Wait(ticket)` to retire the frame and publish readbacks,
or use `Execute()` to submit and retire in one call. The existing
`CompileAndWait`, `SubmitAndWait`, and `ExecuteAndWait` names forward to these
operations.

## Runtime kernels, tensor ports and model packages

Class-authored operations and imported CUDA kernels use the same registry, graph
edits, dependency analysis, resource planning and executor. Applications can keep
using `TArdaNodeDefinition` and named callables. Imported kernels use
[`FArdaRuntimeNodeDefinition`](Public/Graph/Node/ArdaRuntimeNodeDefinition.h): an
immutable descriptor supplies PTX/cubin, entry point, target, scalar/buffer ABI and
declared access modes. Invocation supplies scalar bytes, graph buffer ranges and
launch dimensions. Registering a library is atomic; repeated registration requires
identical definitions. No C++ operation class is generated for each model operator.

[`FArdaGraphPackage`](Public/Graph/Node/ArdaGraphPackage.h) loads an `.ardagpu` model
descriptor containing a reusable subgraph recipe and named typed state slots.
`LoadStateDict` loads a separate `.ardastate` checkpoint; `Invoke` joins the loaded
dispatch/fill/copy program into the caller's edit. Weights and graph constants use
the general `AllocateInitializedBuffer` mechanism, which initializes
persistent storage once when a new physical allocation is prepared. Model inference
does not load PyTorch, ONNX Runtime, TensorRT, OIDN or IREE runtime libraries.

For an OIDN package, given an `Input` tensor already produced or bound in this edit:

```cpp
const auto Descriptor = FArdaGraphPackage::Load("oidn_hdr_small_256.ardagpu");
if (!Descriptor) return Descriptor.mStatus;
const auto Package = Descriptor.mValue->LoadStateDict("oidn_hdr_small_256.ardastate");
if (!Package) return Package.mStatus;

// Graph and Edit are the same ones used by the renderer.
const auto Model = Package.mValue->Invoke(Edit, {{"noisy_hdr", Input}}, "Denoise");
if (!Model) return Model.mStatus;
const auto* Output = Model.mValue.Find("denoised_hdr");
if (!Output)
    return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "Missing model output.");

// Consume Output with another graph operation, or retain it as a terminal result.
Edit.Keep(Output->mBuffer);
if (auto Status = Edit.Commit(); !Status) return Status;
if (auto Status = Graph.Compile(); !Status) return Status;
return Graph.Execute().mStatus;
```

Inspect the loaded package with `GetTensors()` before binding ports. `mDesc` includes dtype,
dimensions, element strides and byte origin; `mStorageByteSize` is the full required
backing allocation. Ports with the same `mBuffer` index are views of the same
storage. Preserve their relative offsets and allocation identity when supplying
outputs, or let `Invoke` allocate outputs. A tensor is a view of an ordinary buffer,
not a separate resource kind; graphics packing, copies and readback use that buffer.

`GetStateSlots()` reflects checkpoint names, types and shapes. Loading validates the
complete key set, dtype, dimensions and byte counts before returning a new immutable
instance. `HasStateDict()` must be true before invoking a descriptor with required
state. Load another checkpoint through the same descriptor and invoke the returned
instance in the next edit; existing invocations retain their original weights.
`Invoke` can also author a typed subgraph's body through `Context.GetEdit()`, so model
operations participate in normal parent-graph preprocessing and scheduling.

### Implemented and remaining scope

| Area | Implemented | Remaining work |
| --- | --- | --- |
| Runtime definitions | Immutable ABI-described kernels, atomic registration, driver module loading and shared native modules | More execution providers and a shared code-asset table in graph archives |
| Tensor metadata | Backend dtype, dimensions, strides and byte offsets; graph views share buffer storage; static dense FP32/FP16/BF16/FP8/INT8/INT32 export | Dynamic dimensions, arbitrary strided exported ports and broader operator coverage |
| Checkpoints and archives | Separate named state dicts, immutable loaded instances and portable runtime definitions. Graph archive v4 reads v2 and v3 | Larger/streamed weight assets and migration tooling |
| Offline model export | Supported static ONNX lowered to standalone CUDA kernels; pretrained OIDN and NPPD examples | Universal PyTorch/operator coverage, dynamic shapes and data-dependent host control flow |
| Graphics integration | Cornell rendering, feature packing, CUDA inference and presentation in one graph; temporal state feedback | Moving-camera motion/reprojection in the Cornell demonstration |

The compiler adapter targets one CUDA device and supported dense typed ports with
fixed dimensions. FP16/BF16/FP8/INT8 export is gated by the requested architecture;
FP8 conversions currently use compiler-generated bit arithmetic, not guaranteed
native FP8 acceleration. Backend storage support is independent of arithmetic
capability. Unsupported operators, host operations and unresolved external
device calls fail export. This path compiles a supported model offline; it does not
extract a complete portable kernel program from an arbitrary `torch.compile` model
or a CUDA Graph capture.

Architecture exports are individual artifacts plus a manifest, not an automatic
runtime variant selector. Select a compatible package in the application and validate
it on the deployment GPU. PTX may be JIT-compiled by the installed driver; a declared
target is not proof of compatibility with every GPU or driver.

Package and graph archive readers currently cap input at 512 MiB. A package stores
each module once and the CUDA provider shares native modules, but each expanded
portable runtime definition still carries its code in graph serialization. A saved
expanded graph can therefore be much larger than its original descriptor. Named
model state is externalized before compilation and can be replaced without changing
the descriptor. Topology, static input shapes, dtypes or frozen configuration changes
require a new export. Legacy version-one packages still load as embedded snapshots.

The installed offline Python package exposes an ONNX-style call:
`arda.export(model, args=(), f=None, *, kwargs=None, target_archs=("sm_80",), ...)`.
Actual tensor arguments define shapes and dtypes; nested inputs are flattened and
scalar configuration is frozen. Each selected target produces its own package.
`f` is a bundle directory; when omitted, `result.save(directory)` persists the
temporary result. The default ONNX opset is 22. See [offline installation, export and validation](../../../Tools/ModelExport/README.md)
and the [Cornell denoising example](../../ArdaTests/Examples/CornellDenoising/README.md).

## Independent preprocessing and execution

`Graph.Preprocess()` and `FArdaPreprocessor::Preprocess(const Graph&)` copy the
closed source and apply its ordered preprocessing rules. The returned
`unique_ptr<FArdaDependencyGraph>` has no execution plan. It is marked by
`IsPreprocessed()`, can be inspected, serialized, or extended through normal edits,
and retains graph optimizations and culling flags. Editing invalidates the marker.
The original graph is unchanged even when a rule fails or throws.

`FArdaInductor::Compile(const FArdaDependencyGraph&)` requires a preprocessed graph
with its external slots bound. It returns `TArdaRHIResult<FArdaInductorCompileResult>`
without changing the input graph. The plan retains the graph declarations it needs;
`FArdaGraphExecutor` can own and execute it after the source graphs are destroyed.
A compile result, including all copies of it, can be claimed by one executor;
compile again to create an independent execution lifetime. Pass a destination
`Device` to `Compile(Graph, Device)` for an offline or loaded graph. The one-argument
overload uses the graph's optional authoring device.

```cpp
auto Preprocessed = Graph.Preprocess();
if (!Preprocessed) return Preprocessed.mStatus;
auto Compiled = FArdaInductor::Compile(*Preprocessed.mValue, Device);
if (!Compiled) return Compiled.mStatus;
FArdaGraphExecutor Executor(eastl::move(Compiled.mValue));
if (!Executor.GetStatus()) return Executor.GetStatus();
return Executor.Execute().mStatus;
```

Use `Graph.Compile()` and `Graph.Execute()` to let the graph coordinate
preprocessing, compilation, revision reuse and execution.
Use the standalone calls when storing optimized graphs or owning plans separately.

## Recursive subgraph nodes and archives

`TArdaNodeDefinition<Node, Parameters, EArdaNodeType::Subgraph>` defines a named
operation whose `Build(FArdaSubgraphContext&, const Parameters&)` hook records its
body with ordinary graph edits. Invocation retains the interface and frozen
parameters; `FArdaExpandSubgraphsRule` expands the body recursively as the first
default preprocessing rule, before stage assembly, dependency analysis, culling
and scheduling. Custom rule programs may replace or repeat expansion, but must
leave no unresolved subgraph nodes before compile rules run. Cached revisions
reuse the expanded result.

Declare the interface through the existing parameter macros and
`DeclareResources`. In `Build`, use `Context.GetEdit()` to create transient
resources and invoke operations, including other subgraphs. Request inputs by
their parameter field names, such as `Context.RequestBufferInput("mSource")`.
Publish every declared output with `Context.DeclareOutput("Destination", Value)`.
The value may be the parent's predeclared output, a compatible transient
created in the body, or a declared readable input passed through to a node-owned
output; preprocessing unifies storage without a copy. Explicit
operation and resource names inside the body are scoped to the parent instance.
Missing outputs, undeclared parent accesses and unbounded recursion fail
preprocessing while preserving the source construction. Build must derive its
contents from frozen parameters and declared inputs so cached revisions remain
valid. Resource and scalar boundary parameters are supported; compose shader
stage connections entirely inside the body. Expansion is limited to 64 levels.

For example, a subgraph's build hook can copy its declared input through an
internal transient and publish the result:

```cpp
static FArdaRHIStatus Build(FArdaSubgraphContext& Context, const FArdaParameters&)
{
    auto& Edit = Context.GetEdit();
    const auto Source = Context.RequestBufferInput("mSource");
    const auto Result = Clone(Edit, Source);
    return Context.DeclareOutput("Destination", Result);
}
```

The parameter schema declares readable `mSource` and writable output fields;
`DeclareResources` declares a `Destination` buffer matching the source shape.
The preprocessor owns this content edit, so the hook returns its status without
committing or canceling it. See the [subgraph tests](Tests/ArdaSubgraphTests.cpp)
for complete recursive node definitions.

`ARDA_DECLARE_GRAPH_NODE` automatically registers the named node type without
preparing device state. The [graph's archive interfaces](Public/Graph/Node/ArdaDependencyGraph.h)
use that registry to restore public parameter snapshots and connections:

```cpp
auto Status = Graph.Save("pipeline.ardg");
if (!Status)
{
    return Status;
}
auto Loaded = FArdaDependencyGraph::Load("pipeline.ardg", Device);
if (!Loaded)
{
    return Loaded.mStatus;
}
return Loaded.mValue->Compile();
```

`Graph.Serialize()` and `FArdaDependencyGraph::Deserialize(Data, Device)` provide
the same format in memory. The existing `FArdaGraphSerialization` service remains
available and shares the same implementation. Save uses the
backend's shared atomic-write helper and Load uses its binary-read helper.
Archives contain resource declarations, registered node type names and versions,
public parameters, connections, and registered rule options. Preprocessed graphs
retain their transformed topology and culling annotations. Nested bodies in an
authored graph expand from their registered implementation during preprocessing.
Every external import requires a stable nonempty name. Native resources become
typed placeholders retaining the required descriptor, without saving pointers.
To author without a device, use `DeclareExternalBuffer(Edit, Expected, Name)`,
`DeclareExternalTexture`, or `DeclareExternalAccelerationStructure` to record
an expected shape directly. `BindBuffer`, `BindTexture`, and
`BindAccelerationStructure` declare the same named slots with existing objects.
Loading leaves these slots unbound; inspect `Graph.GetExternalResourceSlots(true)`
and supply compatible device resources through `Graph.BindExternalResource(Name, Resource)`.
Unbound graphs support preprocessing; compilation reports missing binding names.
Loading reconstructs declarations without preparing node or device state. A graph
can remain offline until `FArdaInductor::Compile(*PreprocessedGraph, Device)`
creates its execution plan for the destination device.
Unknown types, incompatible versions, malformed connections and fields without a
portable codec produce explicit errors. Additional value types can implement
`TArdaGraphCodec`; process addresses and prepared execution state are never persisted.

Graphs retain semantic node descriptions, public parameters, connections and
culling annotations. Compilation prepares result-owned parameter wrappers, native
binding layouts and per-draw state in `FArdaInductorCompileResult::mNodes`; schedules,
allocations, runtime and telemetry belong to that result. `Graph.GetNodeDescription`
exposes graph semantics before compilation and the accepted execution overlay on
the persistent facade after compilation.

Opaque external values also need stable names and type names. Declare slots with
`Graph.DeclareExternalValue<T>(Name, TypeName)` and supply values through
`Graph.BindExternalValue<T>(Name, Value)`. `GetExternalValueSlots(true)` reports
unbound values after loading. Built-in rule configuration has portable codecs;
custom rule implementations register their configuration with
`RegisterArdaGraphRuleArchive`. Unknown rule codecs fail explicitly rather than
silently replacing custom programs with defaults.

Name a host readback destination after committing the edit that uses it. The
application owns the portable type name; keep its meaning stable across processes:

```cpp
// Destination is the shared byte vector passed to ReadbackBuffer's parameters.
auto Status = Graph.BindExternalValue("Readback output", Destination, "host.bytes.v1");
if (!Status) { return Status; }
Status = Graph.Save("pipeline.ardg");
if (!Status) { return Status; }
auto Loaded = FArdaDependencyGraph::Load("pipeline.ardg", Device);
if (!Loaded) { return Loaded.mStatus; }
auto NewDestination = eastl::make_shared<eastl::vector<uint8_t>>();
return Loaded.mValue->BindExternalValue("Readback output", NewDestination);
```

The same mechanism names raw pointers, retained host objects and externally owned
shader or pipeline references in public parameters. Loading restores their slot
names, then binding supplies objects from the new context. Binding checks both
the C++ parameter type and the portable type name and invalidates cached execution.
Raw pointers remain borrowed; shared and native references retain their objects.

For values resolved during recording, including callbacks, store a named
`TArdaGraphExternal<T>` parameter. For example, a parameter field of
`TArdaGraphExternal<eastl::function<void()>>` can hold `{"On recorded"}`. Bind the
slot on the closed graph with
`Graph.BindExternalValue("On recorded", Callback, "callback.recorded.v1")`;
inside `Record`, call `Context.ResolveExternal(Parameters.mOnRecorded)`, check
the returned status, and invoke its `mValue`. The archive contains the slot's name
and type, while each compiled executor retains the binding from its own context.

## Program with resource variables

Use `FArdaGraphEdit` to record operations on typed buffer and texture variables.
The scope begins a transaction, each operation adds its registered node and
resource dependencies, and `Commit()` closes and queues the edit. `Graph.Compile()`
then selects or compiles a persistent revision. For example, this
function records a clear followed by a copy into an existing native texture:

```cpp
#include "ArdaDependencyGraph.h"

arda::FArdaRHIStatus RecordTextureCopy(arda::FArdaDependencyGraph& Graph,
    const arda::FArdaRHITextureDesc& Desc, arda::FArdaRHITextureRef NativeOutput)
{
    using namespace arda;
    FArdaGraphEdit Edit(Graph);
    const auto Image = AllocateTexture(Edit, Desc);
    const auto Output = BindTexture(Edit, NativeOutput, "Output");
    Clear(Edit, Image, {0, 0, 0, 1});
    CopyTo(Edit, Image, Output);
    Edit.Keep(Output);
    if (auto Status = Edit.Commit(); !Status)
    {
        return Status;
    }
    return Graph.Compile();
}
```

`Desc` must describe a supported color render target matching `NativeOutput`.
`AllocateBuffer` / `AllocateTexture` declare graph-owned resources;
`BindBuffer` / `BindTexture` import existing native resources and require a stable
nonempty name for each external slot. Allocations accept an optional diagnostic
name after the descriptor. Allocation
accepts `EArdaGraphResourceLifetime::Persistent` as a fourth argument when storage
must retain contents across executions of that revision. Typed
`FArdaGraphBuffer` and `FArdaGraphTexture` variables identify resources owned or
retained by the graph; allocating a variable does not perform GPU work. Call
`Graph.Execute()` after successful compilation, or submit and wait asynchronously.

`Clear`, `Upload`, `CopyTo`, `Clone`, and `Readback` compose the default nodes.
Overloads route the same function name to different nodes according to the
resource types. `CopyTo` writes an existing destination and returns it; buffer
`Clone` infers a new destination.
`Edit.Keep(Result)` retains an output with no consumer. Readback operations retain
their producer chains automatically and return a shared byte vector populated
after successful execution.

| `CopyTo(Edit, Source, Destination)` resource types | Registered node |
| --- | --- |
| Buffer to buffer | `FArdaMemoryCopyBufferNode` |
| Texture to texture | `FArdaMemoryCopyTextureNode` |
| Buffer to texture | `FArdaGraphBufferToTextureNode` |
| Texture to buffer | `FArdaGraphTextureToBufferNode` |
| Acceleration structure to acceleration structure | `FArdaMemoryCopyAccelerationStructureNode` |

Mixed buffer/texture copies accept the texture slice and buffer layout as their
fourth and fifth arguments. An omitted layout or zero row pitch infers a portable
aligned row pitch for the selected texture region while preserving the layout's
byte offset. The buffer must fit that pitched footprint. `Upload` and `Readback`
handle packing when transferring ordinary CPU byte vectors.

Set compiler options with `Edit.SetOptions(Options)`. `Edit.GetGraph()` exposes
only read-only inspection. Check `Commit()` once after authoring: the edit retains its first error, later
operations stop mutating the graph, and a failed authoring transaction cannot
be queued. `GetStatus()` exposes that error earlier when needed. A failed commit
leaves the edit open for rollback. `Cancel()` discards the transaction explicitly;
an uncommitted scope also cancels on destruction. Resource variables may connect
multiple queued edits in one construction. Recreate them when beginning the next
frame's construction; handles from an older revision are not inputs to a fresh
construction. Removed or canceled resources cannot be reused, even if their
storage slots are later recycled.

## Automatic resource usage

Buffer and texture descriptors default to `mUsage = AutoDeduction`. Specify the
resource's shape, format and any structured-buffer stride; node declarations
provide its access requirements. `FArdaDeduceResourceUsageRule` runs after stage
assembly and before access validation and memory planning. It combines usages
from resource states, shader bindings, framebuffer attachments and node output
requirements across the construction.

```cpp
FArdaRHITextureDesc Desc;
Desc.mWidth = Width;
Desc.mHeight = Height;
Desc.mFormat = EArdaRHIFormat::RGBA16Float;
const auto Color = AllocateTexture(Edit, Desc); // AutoDeduction by default.
Clear(Edit, Color, {0, 0, 0, 1});               // Requires RenderTarget.
// A later shader SRV binding adds ShaderResource to this same allocation.
```

Assign a concrete `mUsage` to constrain the allocation explicitly. Missing flags
are an error rather than silently added. Combine `AutoDeduction` with explicit
flags to retain requirements such as `Raw`, `Structured` or `Typeless` while
allowing the compiler to add the consumers' usages. A structured binding still
requires a valid stride; deduction does not invent buffer element layouts.
Read/write direction and access ranges remain node declarations and continue to
control graph dependencies; usage deduction does not infer them from command code.
Node parameter macros declare those requirements alongside each resource field,
including shader bindings and graphics attachments, without a duplicate access
list in `Describe`.

Copy-only or unused resources can resolve to `None`, which is a concrete usage.
Imported resources retain their native flags and must already support every
consumer. Successful compilation leaves no unresolved `AutoDeduction` in the
compiled resource descriptions. Custom rule programs must also satisfy this
invariant, and direct RHI creation requires concrete usage flags because it has
no graph of consumers to inspect. Deduction changes only the private compilation
snapshot, so identical automatic declarations still reuse the construction cache.

## Frame construction and persistent revisions

Keep one graph and describe the desired work each frame. Conditional operations
can simply be omitted next frame; the application does not need to find and
remove their old nodes. The lifecycle is:

1. Open an edit, record operations, and call `Edit.Commit()`.
   Each successful close appends its canonical key to the graph's ordered queue.
   Multiple edits may contribute resources and operations before compilation.
2. Call `Graph.Compile()`. It combines the ordered edit hashes into a construction
   hash and reuses a matching persistent revision, including its compilation
   results and prepared runtime. Otherwise it compiles and caches a new revision.
   Successful compilation clears the queue and locks editing.
3. Call `Graph.Execute()` or `Graph.Submit()`. Attempting execution unlocks editing.
   The next ordinary edit begins an empty construction while cached revisions
   remain available. Switching revisions at compilation retires outstanding work
   from the outgoing runtime; reusing the same runtime preserves frame overlap.

The cache retains up to four revisions by default, so an A/B/A sequence can reuse
the earlier A revision. Repeating the same edit sequence, resource identities and
semantic parameters skips compilation. Edit boundaries and order contribute to
the construction hash. Calling `Execute()` again without recording new edits
also replays the selected revision.

The graph retains the latest accumulated construction. The queue stores only the
keys needed to preserve edit identity and order; active editors retain their own
bases until they close. Committing many edits does not retain a full graph copy
for every earlier prefix.

To submit an intentionally empty frame, close an empty edit and compile it.
Calling `Compile()` without recording edits re-stages the selected construction.

Native shader pipeline objects use one graph-wide PSO cache shared by all revisions.
Each revision retains its own compiled schedule, resource placements and prepared
runtime. A construction miss can therefore reuse existing PSOs while building a
different runtime; a revision hit reuses the complete existing runtime as well.

```cpp
// Repeat for each frame, using the same Graph and NativeOutput.
FArdaGraphEdit Edit(Graph);
const auto Image = AllocateTexture(Edit, Desc);
const auto Output = BindTexture(Edit, NativeOutput, "Output");
Clear(Edit, Image, bHighlight ? FArdaRHIColor{1, 0, 0, 1} : FArdaRHIColor{0, 0, 0, 1});
CopyTo(Edit, Image, Output);
Edit.Keep(Output);
Check(Edit.Commit());
Check(Graph.Compile());
Check(Graph.Execute().mStatus);
```

`Graph.Compile(true)` forces compilation even when a matching revision exists.
`Graph.InvalidatePersistentCache()` clears reusable cache entries while keeping the
selected executable valid for replay. The next `Compile()` rebuilds; it can re-stage
the selected construction without another edit. `SetRevisionCacheCapacity(Count)`
changes the retained revision limit (at least one). `GetRevisionStats()` exposes cache and compilation
activity; its persistent revision identity remains stable when background timing optimization
updates that revision's schedule. A failed compile retains the queued construction for correction with
another edit or `Graph.DiscardPendingEdits()`; `Edit.Cancel()` only cancels its open transaction.

Imported native resource identities participate in construction identity. Rebinding
a different swap-chain image therefore selects a different revision. Reuse retained
semantic objects and readback destinations when their node keys use object identity;
allocating a new object each frame can prevent cache hits. Custom node canonical
keys must include every value or pointee identity that affects the description or
execution. Keep semantic pointees immutable. Explicitly dynamic frame inputs may
change under their node's documented synchronization contract when those values do
not affect the compiled description.

The convenience `Readback(Edit, Resource)` allocates a new output vector on every
call, and that vector's identity participates in the node key. Recording it again
each frame therefore prevents an otherwise identical construction from hitting
the revision cache. When rebuilding an identical readback construction, allocate
the sink once and pass it through the named readback operation:

```cpp
const auto Bytes = eastl::make_shared<eastl::vector<uint8_t>>();
for (unsigned Frame = 0; Frame != 3; ++Frame)
{
    FArdaGraphEdit Edit(Graph);
    const auto Buffer = AllocateBuffer(Edit, BufferDesc);
    Clear(Edit, Buffer, 7);
    ReadbackBuffer(Edit, {Buffer, Bytes});
    Check(Edit.Commit());
    Check(Graph.Compile());
    Check(Graph.Execute().mStatus);
    // Consume *Bytes here, before the next execution can replace its contents.
}
```

Here `BufferDesc` remains unchanged and describes an unordered-access buffer with
a nonzero byte size divisible by four. The first frame compiles; the following
frames reuse its revision. Synchronous execution keeps accesses to the shared
sink serialized. The texture equivalent is `ReadbackTexture`.

The operations facade is the only public authoring interface. Rebuild the desired
construction to change its structure; cached revisions preserve reusable work.

## Callable nodes and registration

Every class derived from `TArdaNodeDefinition<Node, Parameters, EArdaNodeType::...>`
inherits a call operator. Parameters must use the node parameter declaration macros. Each public node publishes one named global callable;
include its header and pass a parameter struct or its fields directly:

```cpp
ClearBuffer(Edit, Buffer, uint32_t{7});

FArdaMemoryClearBufferNode::FArdaParameters Parameters{Buffer, 9};
ClearBuffer(Edit, Parameters);
CopyBuffer(Edit, Source, Destination);
```

`ClearBuffer` is the global `const FArdaMemoryClearBufferNode` declared beside
its class and defined once in the owning `.cpp`. It owns no graph or execution
state; each call records one operation. Positional arguments are
forwarded into `FArdaParameters{...}` in declaration order, preserving omitted
field defaults and C++17 aggregate initialization checks. A temporary struct or
initializer callback remains useful when many optional fields need named assignment.

Retrieve an operation's typed output and pass it to the next operation:

```cpp
const auto Operation = CopyBuffer(Edit, {Source, {}});
const auto Copied = Operation.GetBuffer("Destination");
const auto Bytes = Readback(Edit, Copied);
```

Calls return `FArdaGraphOperation`. Use `GetBuffer(Slot)` or `GetTexture(Slot)`
to obtain typed outputs before committing or canceling the edit; their default slot is
`"Destination"`. `GetHandle()` exposes the node handle for read-only diagnostics. Use
`Edit.Order(Producer, Consumer)` when an ordering constraint has no resource edge. Keep resource variables typed in custom node parameter schemas; use `GetHandle()`
only when inspecting their logical identity.

Every invocation creates a fresh operation. Names are generated automatically.
For an explicit diagnostic name, use the parameter-struct or initializer form
and pass the name after it; the name must be unique within the construction.
The positional form treats its arguments as parameter fields.
It uses the existing registry, validation, resource declaration, parameter
snapshot, and preparation path. A node may provide domain-specific static
functions that build its parameters, call inherited `Invoke`, and return typed
outputs. Free function overloads may dispatch one operation name to several node
classes, as `CopyTo` does. Keep those functions thin: registration and dependency
discovery stay in the existing node definition, with no separate facade registry.
Registry-driven tools may use `Edit.Invoke(DefinitionName, Parameters)` after
registering the definition; the parameter type must match its public schema.

`Edit.Invoke<Node>(Parameters)` provides the same typed entry as `Node::Invoke`.
Both accept an initializer callback that fills a fresh `Node::FArdaParameters`
snapshot and may return a status. `Node::Register()` remains useful for registry
enumeration before any invocation. No operation lookup, replacement or removal is
needed to describe the next frame.

Publish custom callables with matching macros in the same namespace:

```cpp
// ArdaMyNode.h: immediately after the complete node class.
ARDA_DECLARE_GRAPH_NODE(FArdaMyNode, RunMyOperation);

// ArdaMyNode.cpp: one definition, after including ArdaMyNode.h.
ARDA_DEFINE_GRAPH_NODE(FArdaMyNode, RunMyOperation);
```

The invocation name must be unique across the `arda` namespace. The separate
`GetMetadata().mName` identifies the runtime implementation in the node registry.
Declaration registers the named type automatically without device setup;
definition establishes one C++ callable object. `FArdaMyNode::Register()` remains
idempotent and reports registration errors explicitly. See [registering a graph node](../../../Docs/ArdaRDG/registering-nodes.html)
for a complete header, implementation, linking and error-handling walkthrough.

## Declare node parameters and dependencies together

Use the macros in `Graph/Node/ArdaNodeParameters.h` (also included by
`Graph/Node/ArdaNodeDefinition.h`) to define aggregate parameters with dependency
metadata. Plain parameter structs are rejected during node registration. For example, a pixel node that samples albedo and writes color/depth
declares:

```cpp
ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaMeshPSParameters)
    ARDA_NODE_STAGE_INPUT(FArdaGraphFragments, mInput)
    ARDA_NODE_SHADER_TEXTURE(mAlbedo, FArdaMeshPixelShader::FArdaParameters, mAlbedo)
    ARDA_NODE_COLOR_TARGET(mColor)
    ARDA_NODE_DEPTH_TARGET(mDepth, EArdaDependencyAccess::ReadWrite)
ARDA_END_NODE_PARAMETER_STRUCT()
```

The shader parameter metadata must be visible here and declare an albedo texture
SRV with pixel-stage visibility. The node macros generate that binding and its
read access, the fragment-stage dependency, and the color/depth attachments and
accesses. They also generate canonical keys for supported field types. Both the
parameter-struct form and `WriteMeshPixels(Edit, Fragments, Albedo, Color, Depth)`
retain aggregate initialization and field order.

`ARDA_NODE_SHADER_BUFFER` does the same for compute/VS buffer bindings.
SRVs and constant buffers default to read access; UAVs default to write access.
Declare a read/write UAV once in its shader schema with
`ARDA_SHADER_BUFFER_UAV_ACCESS` or `ARDA_SHADER_TEXTURE_UAV_ACCESS`, passing
`EArdaShaderResourceAccess::ReadWrite`; the node mapping inherits that access.
Node `*_ACCESS` overrides can conservatively add UAV access but cannot narrow
the shader contract. `ARDA_NODE_CUDA_BUFFER` / `ARDA_NODE_CUDA_TEXTURE` similarly
derive resource kinds and access from an existing CUDA/compute parameter schema.
Inside `PrepareCuda`, call `ResolveNodeCudaParameters(Context, Parameters, Host)`
to resolve those exact mappings into the operand's host arguments before appending
the operation to its sequence.
`ARDA_NODE_BUFFER` and `ARDA_NODE_TEXTURE` declare non-shader accesses with an
explicit access direction and RHI state. `ARDA_NODE_PARAMETER` and
`ARDA_NODE_PARAMETER_WITH_DEFAULT` declare ordinary values.
Declare every field through a parameter macro so it participates in metadata
and generated identity. `ARDA_NODE_STAGE_INPUTS` declares ray-provider lists.
Use `ARDA_NODE_COLOR_TARGET_ACCESS(..., EArdaDependencyAccess::ReadWrite)` for
color attachments that consume previous contents, such as a second mesh drawing
into a shared target. Custom value types can specialize `TArdaNodeParameterKey`
or supply the node's complete `GetCanonicalKey`.

`Describe` is optional when parameter metadata provides the entire description.
Shader nodes retain a small `Describe` to contribute prepared shader stages and
pipeline configuration; the generated resource declarations are appended
automatically. Custom descriptions remain available for ranges, dynamic resource
collections and other specialized requirements. Registration rejects malformed
resource mappings, conflicting access declarations, and duplicate/out-of-range
array bindings before device preparation. The canonical shader/CUDA schema is
the source of required resource access; the graph does not infer hidden accesses
from shader instructions or arbitrary native pointers. See the [parameter declaration
guide](../../../Docs/ArdaRDG/registering-nodes.html#describe-dependencies-in-parameter-declarations)
for the complete macro list and node examples.

## Module layout and compilation

`Public/ArdaDependencyGraph.h` is the module entry point. Public declarations and
private implementations share the following ownership boundaries:

| Directory | Responsibility |
| --- | --- |
| `Graph/Resources` | Logical resource descriptions, handles, imports and output declarations. |
| `Graph/Node` | Node definitions, registration, requirements, topology and graph edits. |
| `Graph/DefaultNodes` | Reusable buffer, texture and acceleration-structure operations. |
| `Inductor/Config` | Compiler, execution, budgeting and telemetry options. |
| `Inductor/Preprocess` | Standalone graph transforms, recursive expansion, dependencies and culling. |
| `Inductor/Rules` | Shared helpers and compatibility includes for moved rule headers. |
| `Inductor/Compiler` | Const graph compilation, result rules, scheduling and memory planning. |
| `Inductor/Runtime` | Resource materialization, shader bindings, command recording, submission and retirement. |

`Edit.Commit()` validates and queues an edit.
`Graph.Compile()` publishes or reuses a pending plan. It does not allocate
graph-owned GPU buffers or textures. The first `Submit()` materializes that plan's
native resources and execution state; subsequent frames reuse them. `Execute()`
performs submission and waiting. Attachment-time node preparation can still load
shaders or create node-owned setup objects.

Configure `FArdaInductorOptions::mRules.mPreprocessRules` and `mCompileRules` with
ordered instances created by `MakeArdaGraphRule<Rule>(configuration...)`.
`TArdaGraphPreprocessRule<Derived>` forwards to `Derived::Preprocess(Graph) const`;
`TArdaGraphCompileRule<Derived>` forwards to `Derived::Compile(Graph, Result) const`.
Both use `static GetRuleName()` and may own immutable configuration. Their public
headers and implementations live under `Inductor/Preprocess` and
`Inductor/Compiler`, respectively; old category includes under `Inductor/Rules`
forward to those locations.

Preprocessing can invoke registered node types and allocate logical resources
through `Graph.Edit([](FArdaGraphEdit& Edit) { ... })`, remove nodes with
`Graph.RemoveNode`, or edit topology connections and resource descriptions.
Callbacks return status; the preprocessor owns their borrowed edit and publication.
The default rules expand subgraphs, assemble stages, deduce resource usage, validate
accesses, resolve dependencies, check cycles and mark unreachable nodes culled.

Compilation reads this graph through a const view. Flattening respects the stored
culling flags; subsequent rules infer pipelines, order nodes, plan memory, assign
queues, batch work, flatten dispatches and resolve fences. Ordering changes require
rebuilding downstream products. Compiler rules never publish graph mutations.

Rule instances remain immutable while retained. Reuse instances for unchanged
settings; replacement and list order affect revision-cache identity. Failures
discard only the phase's private output. Timing-based optimization requires every
selected rule to explicitly allow adaptive reordering. See the
[compiler guide](../../../Docs/ArdaRDG/compilation.html#rules) for examples and
failure handling.

## Default node library

The
[memory operations](Public/Graph/DefaultNodes/ArdaMemoryNodes.h) provide ten nodes: buffer and texture
uploads, GPU copies, CPU readbacks, and clears, plus native acceleration-structure
cloning and compaction. Nodes use ArdaBackend's provider-neutral commands;
ArdaInductor schedules their queues, dependencies, barriers, and allocations.

Prebuilt nodes live in `Public/Graph/DefaultNodes` and `Private/Graph/DefaultNodes`,
with tests under `Tests/Graph/DefaultNodes`. They compile into `ArdaDependencyGraph`
and share its public interface and test target.

## Link and include

Link the dependency graph module:

```cmake
target_link_libraries(MyApp PRIVATE Ardashir::ArdaDependencyGraph)
```

Include `ArdaDependencyGraph.h`, which exports the operation facade and the prebuilt node library
[`Graph/DefaultNodes/ArdaDependencyGraphNodeLibrary.h`](Public/Graph/DefaultNodes/ArdaDependencyGraphNodeLibrary.h).
For a narrower facade include, use
[`Graph/DefaultNodes/ArdaGraphOperations.h`](Public/Graph/DefaultNodes/ArdaGraphOperations.h).
For just the node definitions, use `Graph/DefaultNodes/ArdaMemoryNodes.h`. All node types use
the same module target. The application creates its device using its selected
backend provider.

Named operation calls such as `CopyBuffer(Edit, Source, Destination)` register definitions automatically.
`RegisterArdaMemoryNodes()` explicitly registers all ten types for registry
enumeration or registry-driven invocation; it is safe to call repeatedly.

## Buffer nodes

- `FArdaMemoryUploadBufferNode`: copy owned `mBytes` to a GPU buffer at
  `mDestinationOffset`. An empty `mDestination` creates a buffer sized to
  `mDestinationOffset + mBytes.size()`, with no shader usage flags. Supply a
  buffer descriptor yourself when it needs vertex, structured, or shader usage.
  Constant/uniform buffers use the same operation: set the destination's
  `mUsage` to `EArdaRHIBufferUsage::Constant` and pack bytes to the shader's layout.
- `FArdaMemoryCopyBufferNode`: copy `mByteSize` bytes between distinct resources,
  using `mSourceOffset` and `mDestinationOffset`. An empty destination creates
  ordinary GPU storage with the source's shape and usage (excluding `Volatile`)
  and enough capacity for the copied range. CPU access, initial-state retention,
  and backing-allocation policy are not inherited.
- `FArdaMemoryReadbackBufferNode`: copy a source range into a required retained
  `shared_ptr<eastl::vector<uint8_t>>`. Result bytes begin at index zero. The node
  is a side effect, so compilation retains its producer chain automatically.
- `FArdaMemoryClearBufferNode`: fill a complete GPU buffer with the repeated
  32-bit `mValue`. The buffer needs `UnorderedAccess` usage and a nonzero size
  divisible by four. Zero clears every byte.

Copy and readback default `mByteSize` to `ArdaRHIWholeBuffer`, meaning all bytes
from `mSourceOffset` to the source's end. Ranges must be nonempty and within the
resource; invalid ranges and arithmetic overflow fail attachment. Buffer
uploads and copies support byte offsets without four-byte alignment.

## Example: clear, upload, copy, and read back

This function takes an initialized device and returns `{1, 2, 3, 4}`.

```cpp
#include "ArdaDependencyGraph.h"
#include <stdexcept>

eastl::vector<uint8_t> MemoryRoundTrip(arda::FArdaRHIDeviceRef Device)
{
    using namespace arda;
    const auto Check = [](const FArdaRHIStatus& Status)
    {
        if (!Status)
        {
            throw std::runtime_error(Status.mMessage.c_str());
        }
    };

    FArdaDependencyGraph Graph(Device);
    FArdaGraphEdit Edit(Graph);

    FArdaRHIBufferDesc Desc;
    Desc.mByteSize = 16;
    Desc.mUsage = EArdaRHIBufferUsage::UnorderedAccess;
    const auto Buffer = AllocateBuffer(Edit, Desc, "memory");

    // Initialize the whole buffer before patching four of its bytes.
    Clear(Edit, Buffer, 0);
    Upload(Edit, Buffer, {1, 2, 3, 4}, 4);
    const auto Copied = Clone(Edit, Buffer);
    const auto Bytes = Readback(Edit, Copied, 4, 4);
    Check(Edit.Commit());
    Check(Graph.Compile());
    Check(Graph.Execute().mStatus);
    return *Bytes;
}
```

## Ownership and execution

Record operations inside `FArdaGraphEdit`, check `Commit()`, then check
`Graph.Compile()` before executing.
Buffer upload and copy expose their supplied or inferred buffer through the
`"Destination"` output slot. Texture upload, copy, and clear expose their supplied
destination through that same slot. The facade returns typed variables directly;
static invocation exposes them through `GetBuffer` / `GetTexture`. Graph mutation is available only through the edit facade.

Invocation freezes upload bytes. Modifying the caller's vector afterwards does
not change future executions. Describe changed inputs and omit unwanted operations
in the next frame's fresh construction.

Every execution repeats the operations. Exact byte ranges determine data
dependencies; writes preserve other bytes but do not initialize them. When
multiple nodes write overlapping ranges, their attachment order determines
which value a later reader consumes. Buffer copies within one resource are
rejected, even for disjoint ranges, because its native buffer state is shared.
Texture dependencies cover mip/layer subresources; partial writes preserve
previously initialized texels outside the selected region.

`Execute()` submits and waits. For asynchronous execution, use `Submit()`, keep
the returned ticket, and check `Graph.Wait(Ticket).mStatus` before consuming the
readback. Buffer and texture uploads retain their host byte snapshots.
Readback destinations remain retained through completion and are replaced only
when the frame succeeds; failed frames clear registered destinations. No extra
device-idle wait is needed.

Serialize executions that share a readback destination. Do not read or mutate
that vector while completion may publish, including when `Submit()` recycles a
frame slot. Independent overlapping work should use separate destinations.

Buffer upload and clear destinations require `mCpuAccess == None`. Buffer copy permits
CPU upload heaps as sources and CPU readback heaps as destinations; it rejects
the opposite directions. Readback sources cannot be CPU readback heaps. These
checks preserve native heap restrictions and GPU ordering.

## Texture nodes

[Texture parameters](Public/Graph/DefaultNodes/ArdaMemoryTextureNodes.h) select a mip, array
layer, and texel region with `FArdaRHITextureSlice`. Create or import the source
and destination textures before attaching these graphics nodes.

- `FArdaMemoryUploadTextureNode`: upload an owned, tightly packed byte vector.
  Its byte count must exactly match the selected region, ordered by increasing
  row, then depth slice. Native row-pitch padding is handled internally.
- `FArdaMemoryReadbackTextureNode`: publish tightly packed texels to a retained
  vector after successful frame completion. Its ownership and failure behavior
  match buffer readback, including retaining its producer chain.
- `FArdaMemoryCopyTextureNode`: copy between distinct subresources with matching
  formats and native dimension classes (1D, 2D, or 3D). Typed, single-sample color
  textures are supported, including BC block-compressed formats. Explicit source
  and destination extents must match and fit without clipping; BC regions must
  obey the format's block alignment. Different subresources of one texture are
  allowed; copying a subresource onto itself is rejected.
- `FArdaMemoryClearTextureNode`: fill a nonempty mip/layer range with `mColor`.
  The texture must have `RenderTarget` usage and a typed, noninteger,
  uncompressed color format. Depth/stencil and typeless clears are rejected.

Host uploads and readbacks require typed, single-sample, uncompressed color
formats. They transfer one region per node; use additional nodes for more mips
or layers. Partial uploads and GPU copies require initialized destination
contents outside the region. Full-subresource writes can initialize a newly
created texture.

Transfer scratch is declared as graph-owned transient workspace, included in
ArdaInductor's memory planning and budgets, and allocated per frame. Callers
provide packed texels; the library handles the native pitched layout. The
derived `mFootprint`, `mWorkspaceBytes`, and `mbWholeSubresource` parameter fields
are recomputed at attachment and should be left at their defaults.

### Example: packed texture round trip

This function clears a 2-by-2 RGBA texture, uploads one red pixel, copies the
texture, and returns 16 packed bytes. The top-right pixel occupies bytes 4–7.

```cpp
#include "ArdaDependencyGraph.h"
#include <stdexcept>

eastl::vector<uint8_t> TextureRoundTrip(arda::FArdaRHIDeviceRef Device)
{
    using namespace arda;
    const auto Check = [](const FArdaRHIStatus& Status)
    {
        if (!Status)
        {
            throw std::runtime_error(Status.mMessage.c_str());
        }
    };

    FArdaDependencyGraph Graph(Device);
    FArdaGraphEdit Edit(Graph);
    FArdaRHITextureDesc Desc;
    Desc.mWidth = Desc.mHeight = 2;
    Desc.mFormat = EArdaRHIFormat::RGBA8UNorm;
    Desc.mUsage = EArdaRHITextureUsage::RenderTarget;
    const auto Source = AllocateTexture(Edit, Desc, "source");
    const auto Destination = AllocateTexture(Edit, Desc, "destination");

    Clear(Edit, Source, {0, 0, 0, 1});
    FArdaRHITextureSlice Pixel;
    Pixel.mX = 1;
    Pixel.mWidth = Pixel.mHeight = Pixel.mDepth = 1;
    Upload(Edit, Source, {255, 0, 0, 255}, Pixel);
    CopyTo(Edit, Source, Destination);
    const auto Bytes = Readback(Edit, Destination);
    Check(Edit.Commit());
    Check(Graph.Compile());
    Check(Graph.Execute().mStatus);
    return *Bytes;
}
```

For explicit GPU buffer/texture transfers, the umbrella still exposes
[`FArdaGraphBufferToTextureNode` and `FArdaGraphTextureToBufferNode`](Public/Graph/DefaultNodes/ArdaDependencyGraphNodes.h).
Those lower-level nodes take a pitched buffer layout; the direct memory host
transfer nodes handle packing and completion for you.

## Acceleration-structure nodes

[Acceleration-structure parameters](Public/Graph/DefaultNodes/ArdaMemoryAccelerationStructureNodes.h)
operate on imported native BLAS (geometry acceleration structure) and TLAS
(instance acceleration structure) objects. Both nodes require distinct source
and destination objects with the same kind and build flags, and declare
whole-object read/write dependencies on the graphics queue. The device must
support native acceleration structures; compaction also requires compaction
support.

- `FArdaMemoryCopyAccelerationStructureNode`: clone a complete native object.
  Create the destination from the source descriptor with at least its allocation
  size. The source must already be built, or be produced by an earlier build node
  in this graph execution.
- `FArdaMemoryCompactAccelerationStructureNode`: compact a source built with
  `EArdaRHIAccelStructBuildFlags::AllowCompaction`. Submit its build before
  attaching compaction. Call `Device->GetAccelStructCompactedSize(Source)`, check
  the returned status, copy the source descriptor, and set its
  `mResultSizeOverride` to the queried size before creating the destination.
  The completed-size query can wait for prior GPU work. Keep the source unchanged
  until compaction completes; build or update it in a separate execution.

Bind both objects using `BindAccelerationStructure(Edit, NativeObject, Name)`
with distinct stable nonempty names, then pass their variables to `CopyTo(Edit, Source, Destination)` for cloning. Use
`CompactAccelerationStructure(Edit, {Source, Destination})`
for compaction.
Add consumers of the destination or call `Edit.Keep(Destination)` to retain
the operation. The RHI checks
source build state and destination capacity when recording; compaction also
rechecks the completed compact-size query.

Cloning or compacting a TLAS preserves its BLAS addresses, so keep those BLAS
objects alive for as long as the resulting TLAS is used. Acceleration-structure
contents are opaque native data: these nodes use native clone/compact commands.
Generic buffer uploads, byte copies, clears, and readbacks do not initialize or
serialize a BLAS or TLAS; geometry and instance input buffers use the ordinary
buffer nodes before their separate build operations.
