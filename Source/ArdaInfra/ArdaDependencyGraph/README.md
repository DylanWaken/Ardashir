# ArdaDependencyGraph

The graph describes resources, nodes and their dependencies. ArdaInductor applies
an explicit rule program to compile those declarations into a schedule and memory
plan. Runtime materialization and GPU execution are separate from compilation.

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
    const auto Output = BindTexture(Edit, NativeOutput);
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
`BindBuffer` / `BindTexture` import existing native resources. Both accept an
optional diagnostic name after their descriptor or native reference. Allocation
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

## Frame construction and persistent revisions

Keep one graph and describe the desired work each frame. Conditional operations
can simply be omitted next frame; the application does not need to find and
remove their old nodes. The lifecycle is:

1. Open an edit, record operations, and call `Edit.Commit()`.
   Each successful close appends an edit hash to the graph's ordered queue.
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
const auto Output = BindTexture(Edit, NativeOutput);
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

Every class derived from `TArdaDependencyNode` or its domain specializations
inherits a call operator. Each public node publishes one named global callable;
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
`Edit.Order(Producer, Consumer)` when an ordering constraint has no resource edge. Typed resources also convert to raw resource handles when filling
a custom node's parameter struct.

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
Macros establish one C++ callable object without startup registration; the first
call registers on demand. Use `FArdaMyNode::Register()` explicitly for discovery
before invocation. See [registering a graph node](../../../Docs/ArdaRDG/Registering-Nodes.md)
for a complete header, implementation, linking and error-handling walkthrough.

## Module layout and compilation

`Public/ArdaDependencyGraph.h` is the module entry point. Public declarations and
private implementations share the following ownership boundaries:

| Directory | Responsibility |
| --- | --- |
| `Graph/Resources` | Logical resource descriptions, handles, imports and output declarations. |
| `Graph/Node` | Node definitions, registration, requirements, topology and graph edits. |
| `Graph/DefaultNodes` | Reusable buffer, texture and acceleration-structure operations. |
| `Inductor/Config` | Compiler, execution, budgeting and telemetry options. |
| `Inductor/Rules` | Explicit compiler stages, pipeline inference and ordering policies. |
| `Inductor/Compiler` | Rule orchestration and immutable schedule/memory-plan results. |
| `Inductor/Runtime` | Resource materialization, shader bindings, command recording, submission and retirement. |

`Edit.Commit()` validates and queues an edit.
`Graph.Compile()` publishes or reuses a pending plan. It does not allocate
graph-owned GPU buffers or textures. The first `Submit()` materializes that plan's
native resources and execution state; subsequent frames reuse them. `Execute()`
performs submission and waiting. Attachment-time node preparation can still load
shaders or create node-owned setup objects.

Configure `FArdaInductorOptions::mRules.mPreprocessRules` and `mCompileRules` with
ordered instances created by `MakeArdaGraphRule<Rule>(configuration...)`. Every
rule derives from `FArdaGraphRule`, owns its configuration and uses non-static
`Apply`. Preprocess rules transform a working graph snapshot; compile rules
transform the current result with a read-only graph view. Public category headers
under `Inductor/Rules` declare the individual transformations. No stage enum,
fixed slot list or completion bits control execution.

The default program validates and resolves dependencies, flattens and culls nodes,
infers pipeline patterns, selects an order, plans memory, assigns queues, batches
CUDA operations, flattens dispatches and resolves fences. Rules can be omitted or
repeated while satisfying their data contracts. For example, a configured
`FArdaMinimizeMemoryRule` only refines node order; `FArdaPlanMemoryRule` separately
plans that order. Changing order later requires rebuilding downstream products.
The result retains the accepted allocation plan, per-queue/stream dispatch IDs
and cross-domain synchronization dependencies for runtime lowering.

Instances are immutable while retained by graphs or snapshots. Reuse them for
unchanged settings; replacement and list order affect revision-cache identity.
Compilation scratch stays local. Failed rules discard the working snapshot, and
forced recompilation starts from original declarations. Timing-based optimization
requires every selected rule to explicitly allow adaptive reordering.
See the [compiler guide](../../../Docs/ArdaRDG/compilation.html#rules) for
composition, custom policies and failure handling.

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

Bind both objects using `BindAccelerationStructure(Edit, NativeObject)`, then pass
their variables to `CopyTo(Edit, Source, Destination)` for cloning. Use
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
