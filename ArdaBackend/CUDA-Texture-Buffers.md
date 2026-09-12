# CUDA texture buffers

Use a shared linear buffer to process graphics texture data with pointer-based CUDA
kernels. This works with D3D12 and Vulkan, including their graphics-queue CUDA modes.
It provides an explicit alternative when the device cannot expose usable CUDA
surface objects. Existing `SURFACE` parameters and `surf2Dwrite` kernels still require
surface support; they are not translated into pointer operations.

The GPU work is ordered as follows:

```text
Graphics texture -> buffer copy -> complete CUDA sequence -> texture copy -> graphics
```

The two copies move texels on the GPU. There is no CPU readback and no graphics
handoff between the sequence's operations. Copies cost GPU bandwidth, so allocate
the buffer once, reuse it, and keep intermediate algorithm data in buffers. Skip
the input copy when CUDA completely overwrites the selected region. Skip the output
copy when the next consumer can use the buffer directly.

## Allocate and address the buffer

Include `Compute/ArdaCudaTextureBuffer.h` and call
`CreateArdaCudaTextureBuffer(Device, TextureDesc, Slice)`. The helper returns:

- `mBuffer`: device-local storage with CUDA interop enabled.
- `mLayout`: the copy offset and row pitch, both in bytes.
- `mExtent`: the selected region's width, height and depth in texels.
- `mSlicePitch`: the byte distance between successive depth slices.

The graphics texture itself does not need `mbCudaInterop`. Supported layouts use
uncompressed, single-sample CUDA-compatible color formats. Select one mip and array
layer per helper/copy; a 3D region may include several depth slices. Explicit regions
outside the selected subresource are rejected rather than clipped. The helper does
not perform color conversion, normalization or tensor packing.

Bind `mBuffer` with a `BUFFER` parameter and pass pitches and dimensions through
`VALUE` parameters. Buffer coordinates start at zero for the copied region, even
when its texture slice has a nonzero XYZ origin. For an `R32UInt` buffer, kernel
addressing can use:

```cpp
// Pixels is the resolved uint32_t* BUFFER parameter. All pitches are bytes.
if (X < Width && Y < Height && Z < Depth)
{
    auto* Row = reinterpret_cast<uint32_t*>(
        reinterpret_cast<uint8_t*>(Pixels) + Z * SlicePitch + uint64_t(Y) * RowPitch);
    Row[X] += Bias;
}
```

Use 64-bit arithmetic for slice offsets. Row padding is not texture data and is not
initialized by the copies. Kernels should only read valid texels; initialize padding
explicitly if an algorithm reads the entire allocation. External CUDA calls can use
the same buffer through declared bindings, provided their tensor strides and types
match this layout. See [external CUDA calls](CUDA-External-Calls.md).

## Native command lists

The following records into an already open graphics command list. `Sequence` has
been prepared with operations bound to `Transfer.mValue.mBuffer` and the returned
pitches. Allocate `Transfer` before preparing those operations, and reuse it across
frames only after the previous use is correctly ordered or complete.

```cpp
auto Transfer = CreateArdaCudaTextureBuffer(*Device, Texture->GetDesc(), Slice);
if (!Transfer)
{
    return Transfer.mStatus;
}
// Populate FArdaCudaSequence Sequence(Device) with this buffer and its pitches.

if (auto Status = Commands.CopyTextureToBuffer(*Transfer.mValue.mBuffer,
        Transfer.mValue.mLayout, *Texture, Slice); !Status)
{
    return Status;
}
if (auto Status = Sequence.DispatchDeferred(Commands); !Status)
{
    return Status;
}
return Commands.CopyBufferToTexture(*Texture, Slice,
    *Transfer.mValue.mBuffer, Transfer.mValue.mLayout);
```

The command list retains copied resources through submission. Automatic resource
state tracking handles the copy transitions. When automatic tracking is disabled,
the caller must establish `CopySource` and `CopyDest` states.

To request D3D12 CiG explicitly, set the backend configuration's
`mCudaExecutionMode` to `EArdaCudaExecutionMode::GraphicsQueue`. The current
`Automatic` policy can choose the ordinary CUDA context when native surface support
is unavailable; using this helper does not change that policy or the device's mode.

## Persistent dependency graph

Include `ArdaRenderGraph.h` and explicitly attach transfer nodes around the CUDA
nodes. The graph does not insert texture-to-buffer copies automatically.

```cpp
// Within Graph.BeginGraphEdit() / Graph.EndGraphEdit():
auto In = AttachArdaTextureToBuffer(Graph, "download texels", InputTexture,
    Slice, CudaInputBuffer, Layout);
if (!In) return In.mStatus;
// Attach registered CUDA operand nodes that read CudaInputBuffer and write
// separate logical output buffers. ArdaInductor coalesces the CUDA chain.
auto Out = AttachArdaBufferToTexture(Graph, "upload texels", OutputTexture,
    Slice, CudaOutputBuffer, Layout);
if (!Out) return Out.mStatus;
return Graph.MarkOutput(OutputTexture);
```

The handles refer to resources created or imported into the current graph. The
helpers validate the native descriptors and layout before attaching a node, declare
texture subresources and exact copied buffer rows, and retain those declarations.
Both transfers execute on the graphics queue and unused outputs may be culled.
Each logical resource has one producer. Use separate output values for consecutive
writes; duplicate imports of one native allocation cannot hide hazards.

Row padding is not produced by a texture copy. A CUDA node reading the whole buffer
must receive fully initialized storage, or declare only the texel ranges its operand
reads. Cropped writes do not initialize surrounding texels in a new texture. Texture
hazards are tracked at mip/layer/plane granularity. `AttachOrFind` identifies the
transfer by its name, resources, copy rectangle and pitch; changed parameters require
an explicit edit/removal instead of silently altering a compiled node.

## Provider contract and verification

Provider interface version **8** added `CopyTextureToBuffer` and
`CopyBufferToTexture`; the current version is **12**, including allocation metadata,
cached graph execution and queue-qualified timestamp capabilities. Rebuild providers
and consumers together. Providers that do
not implement the new optional methods return `Unsupported`.

Native copies accept `FArdaRHITextureBufferLayout` for caller-managed buffers too.
Portable layouts require a 256-byte-aligned row pitch no greater than `INT32_MAX`,
a 512-byte-aligned buffer offset, whole-format-element alignment and enough storage
through the final copied texel. `ValidateArdaRHITextureBufferCopy` checks these rules
before recording. Depth/stencil, compressed, typeless and multisampled copies are
outside this API's contract.

The GPU tests exercise D3D12/Vulkan ordinary CUDA and CiG modes with native validation
and numerical readback. Coverage includes two consecutive kernels, cropped regions,
mips, array layers, 3D depth slices, surrounding texels and resource lifetime after
recording. Render graph tests also check producer dependencies, layout rejection
and culling. See [CUDA sequences](CUDA-Sequences.md) for batching and library-call
requirements.
