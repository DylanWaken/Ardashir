# ArdaRenderGraph node library

Reusable typed nodes owned by the dependency graph module. The
[memory operations](Public/NodeLibrary/ArdaMemoryNodes.h) provide ten nodes: buffer and texture
uploads, GPU copies, CPU readbacks, and clears, plus native acceleration-structure
cloning and compaction. Nodes use ArdaBackend's provider-neutral commands;
ArdaInductor schedules their queues, dependencies, barriers, and allocations.

## Layout

Prebuilt nodes belong to the module whose operations they implement. This
module keeps its node library alongside the dependency graph:

- `Public/NodeLibrary/`: public node definitions and the node-library header.
- `Private/NodeLibrary/`: node implementations.
- `Tests/NodeLibrary/`: node tests.

Memory operations live directly in these directories. They compile into
`ArdaRenderGraph` and share its public interface and test target.

## Link and include

Link the dependency graph module:

```cmake
target_link_libraries(MyApp PRIVATE Ardashir::ArdaRenderGraph)
```

Include `ArdaRenderGraph.h`, which exports the prebuilt nodes through
[`NodeLibrary/ArdaDependencyGraphNodeLibrary.h`](Public/NodeLibrary/ArdaDependencyGraphNodeLibrary.h).
For a narrower include, use `NodeLibrary/ArdaMemoryNodes.h`. All node types use
the same module target. The application creates its device using its selected
backend provider.

Typed `Graph.AttachOrFind<Node>()` calls register definitions automatically.
`RegisterArdaMemoryNodes()` explicitly registers all ten types for registry
enumeration or name-based attachment; it is safe to call repeatedly.

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
#include "ArdaRenderGraph.h"
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
    Check(Graph.BeginGraphEdit());

    FArdaRHIBufferDesc Desc;
    Desc.mByteSize = 16;
    Desc.mUsage = EArdaRHIBufferUsage::UnorderedAccess;
    const auto Buffer = Graph.CreateBuffer("memory", Desc);
    Check(Buffer.mStatus);

    // Initialize the whole buffer before patching four of its bytes.
    Check(Graph.AttachOrFind<FArdaMemoryClearBufferNode>(
        "clear", {Buffer.mValue, 0}).mStatus);
    Check(Graph.AttachOrFind<FArdaMemoryUploadBufferNode>(
        "upload", {Buffer.mValue, {1, 2, 3, 4}, 4}).mStatus);

    const auto Copy = Graph.AttachOrFind<FArdaMemoryCopyBufferNode>(
        "copy", {Buffer.mValue, {}});
    Check(Copy.mStatus);
    const auto Copied = Graph.FindOutput(Copy.mValue, "Destination");

    auto Bytes = eastl::make_shared<eastl::vector<uint8_t>>();
    Check(Graph.AttachOrFind<FArdaMemoryReadbackBufferNode>(
        "readback", {Copied, Bytes, 4, 4}).mStatus);
    Check(Graph.EndGraphEdit());
    Check(Graph.Execute().mStatus);
    return *Bytes;
}
```

## Ownership and execution

Attach nodes inside `BeginGraphEdit()` / `EndGraphEdit()`. Buffer upload and copy
expose their supplied or inferred buffer through
`Graph.FindOutput(Node, "Destination")`. Texture upload, copy, and clear expose
their supplied destination texture through the same output slot.
An output slot names a resource; use `MarkOutput` to retain a result with no
consumer. Readback already retains its chain as a side effect.

Attachment freezes upload bytes. Modifying the caller's vector afterwards does
not change future executions. Reattaching an existing name returns the same
node only when the type, resources, and behavior match; changed inputs require
removing and replacing that node. Removing a producer also removes consumers
of its produced values, so rebuild the affected chain in the same edit.

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

[Texture parameters](Public/NodeLibrary/ArdaMemoryTextureNodes.h) select a mip, array
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
#include "ArdaRenderGraph.h"
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
    Check(Graph.BeginGraphEdit());
    FArdaRHITextureDesc Desc;
    Desc.mWidth = Desc.mHeight = 2;
    Desc.mFormat = EArdaRHIFormat::RGBA8UNorm;
    Desc.mUsage = EArdaRHITextureUsage::RenderTarget;
    const auto Source = Graph.CreateTexture("source", Desc);
    const auto Destination = Graph.CreateTexture("destination", Desc);
    Check(Source.mStatus);
    Check(Destination.mStatus);

    Check(Graph.AttachOrFind<FArdaMemoryClearTextureNode>(
        "clear texture", {Source.mValue, {0, 0, 0, 1}}).mStatus);
    FArdaRHITextureSlice Pixel;
    Pixel.mX = 1;
    Pixel.mWidth = Pixel.mHeight = Pixel.mDepth = 1;
    Check(Graph.AttachOrFind<FArdaMemoryUploadTextureNode>(
        "upload pixel", {Source.mValue, {255, 0, 0, 255}, Pixel}).mStatus);
    Check(Graph.AttachOrFind<FArdaMemoryCopyTextureNode>(
        "copy texture", {Source.mValue, Destination.mValue}).mStatus);

    auto Bytes = eastl::make_shared<eastl::vector<uint8_t>>();
    Check(Graph.AttachOrFind<FArdaMemoryReadbackTextureNode>(
        "read texture", {Destination.mValue, Bytes}).mStatus);
    Check(Graph.EndGraphEdit());
    Check(Graph.Execute().mStatus);
    return *Bytes;
}
```

For explicit GPU buffer/texture transfers, the umbrella still exposes
[`FArdaGraphBufferToTextureNode` and `FArdaGraphTextureToBufferNode`](Public/ArdaDependencyGraphNodes.h).
Those lower-level nodes take a pitched buffer layout; the direct memory host
transfer nodes handle packing and completion for you.

## Acceleration-structure nodes

[Acceleration-structure parameters](Public/NodeLibrary/ArdaMemoryAccelerationStructureNodes.h)
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

Import both objects using `Graph.ImportAccelerationStructure()`, check the
import statuses, then pass their handles as `{SourceHandle, DestinationHandle}`
to the chosen node. Add consumers of the destination or call
`Graph.MarkOutput(DestinationHandle)` to retain the operation. The RHI checks
source build state and destination capacity when recording; compaction also
rechecks the completed compact-size query.

Cloning or compacting a TLAS preserves its BLAS addresses, so keep those BLAS
objects alive for as long as the resulting TLAS is used. Acceleration-structure
contents are opaque native data: these nodes use native clone/compact commands.
Generic buffer uploads, byte copies, clears, and readbacks do not initialize or
serialize a BLAS or TLAS; geometry and instance input buffers use the ordinary
buffer nodes before their separate build operations.
