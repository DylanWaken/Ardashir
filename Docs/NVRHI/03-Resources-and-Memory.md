# Resources and Memory

[Previous](02-Core-Concepts.md) · [Home](README.md) · Next: [Bindings and shaders](04-Bindings-and-Shaders.md)

## Formats

`nvrhi::Format` covers integer, normalized, floating-point, depth/stencil, sRGB, and BC-compressed formats. Use:

```cpp
const nvrhi::FormatInfo& info = nvrhi::getFormatInfo(format);
nvrhi::FormatSupport support = device->queryFormatSupport(format);
```

`FormatInfo` describes block size, bytes per block, component presence, signedness, sRGB, and depth/stencil nature. `FormatSupport` reports independent capabilities such as texture use, render targets, blending, sampling, UAV load/store, atomics, and vertex/index-buffer use.

Practices:

- Do not infer UAV support merely because a format can be sampled.
- Use sRGB formats for encoded color textures, not linear data or UAV arithmetic.
- Copies do not perform scaling, channel conversion, or linear/sRGB conversion.
- Compressed texture pitches and copy regions operate in blocks.
- Use typeless resources only when different compatible view formats are required.

## Buffers

`BufferDesc` controls storage and all intended usages. Declare usages before creation:

```cpp
auto buffer = device->createBuffer(
    nvrhi::BufferDesc()
        .setByteSize(elementCount * sizeof(Element))
        .setStructStride(sizeof(Element))
        .setCanHaveUAVs(true)
        .setDebugName("Particle state")
        .enableAutomaticStateTracking(nvrhi::ResourceStates::UnorderedAccess));
```

### Buffer view kinds

- **Typed:** set `canHaveTypedViews`; provide a compatible `Format`.
- **Structured:** set nonzero `structStride`; shaders use structured buffer types.
- **Raw:** set `canHaveRawViews`; shaders address byte ranges.
- **Constant:** set `isConstantBuffer`; partial ranges require 256-byte-aligned offsets and sizes.
- **Vertex/index:** set the matching flags and bind in `GraphicsState`.
- **Indirect arguments:** set `isDrawIndirectArgs`; bind as `indirectParams`.
- **AS build input/storage/SBT:** set the dedicated ray-tracing flags.

Flags are creation requirements, not current states. A UAV-capable buffer still needs `ResourceStates::UnorderedAccess` when used as a UAV.

### Regular and volatile constant buffers

Regular constant buffers are ordinary resources. Volatile constant buffers optimize frequently replaced, short-lived constants:

```cpp
auto cb = device->createBuffer(
    nvrhi::BufferDesc()
        .setByteSize(sizeof(FrameConstants))
        .setIsConstantBuffer(true)
        .setIsVolatile(true)
        .setMaxVersions(framesInFlight * writesPerFrame)
        .setDebugName("Frame constants"));
```

Volatile semantics:

- Write after each `commandList->open()` before first use.
- Multiple writes in one command list create API-ordered versions.
- Bind through `VolatileConstantBuffer` in the layout.
- On Vulkan, `maxVersions` must cover peak in-flight writes; insufficient versions are an error.
- D3D12 suballocates versions from the command-list upload buffer.
- They cannot be copied or treated like arbitrary persistent data.

Use push constants for up to 128 bytes when no resource object or persistent storage is needed.

### CPU-visible buffers

Set `cpuAccess` to `Read` or `Write` for staging/upload-style behavior, then use `mapBuffer` and `unmapBuffer`. Mapping a readback buffer can block until its last GPU access completes. Avoid mapping the just-submitted frame; rotate through per-frame readback buffers and poll completion.

## Textures

Supported dimensions include 1D, arrays, 2D, multisampled 2D, cubes, cube arrays, and 3D.

```cpp
auto hdr = device->createTexture(
    nvrhi::TextureDesc()
        .setDimension(nvrhi::TextureDimension::Texture2D)
        .setWidth(width).setHeight(height)
        .setMipLevels(mips)
        .setFormat(nvrhi::Format::RGBA16_FLOAT)
        .setIsRenderTarget(true)
        .setIsUAV(true)
        .setClearValue(nvrhi::Color(0.f))
        .setDebugName("HDR scene color"));
```

Key flags:

- `isShaderResource` (true by default): allows SRV use.
- `isRenderTarget`: allows color or depth/stencil attachment use.
- `isUAV`: allows unordered access.
- `isTypeless`: allows compatible format reinterpretation through views.
- `isShadingRateSurface`: allows VRS attachment use.
- `isVirtual`: create without backing memory.
- `isTiled`: sparse/tiled mapping support where available.
- `sharedResourceFlags`: native sharing and cross-adapter policy.

`TextureSlice` identifies one spatial region of one mip and array slice. `TextureSubresourceSet` identifies mip/slice ranges for views, states, clears, resolves, and attachments.

### Upload

```cpp
commandList->writeTexture(texture, arraySlice, mipLevel,
    pixels, rowPitchBytes, depthPitchBytes);
```

This uploads a complete mip level for one array slice. For region uploads or explicit scheduling, create a writable staging texture, map it, fill pitch-linear data, unmap, and `copyTexture` to the destination.

### Copy, clear, and resolve

- `copyTexture`: 1:1 compatible region copies; no filtering or color conversion.
- `clearTextureFloat` / `clearTextureUInt`: color clears subject to usage/format restrictions.
- `clearDepthStencilTexture`: depth and/or stencil clear.
- `resolveTexture`: multisample color resolve to a single-sample texture.
- `nvrhi::utils::ClearColorAttachment` and related helpers target framebuffer attachments conveniently.

For a scaled blit, tone map, channel conversion, or sRGB conversion, render a fullscreen pass or dispatch compute.

## Framebuffers

A framebuffer is an immutable collection of:

- up to eight color attachments;
- one optional depth/stencil attachment; and
- one optional shading-rate attachment.

Each attachment can select format, mip levels, array slices, and read-only depth behavior. A graphics/meshlet pipeline is compatible when its `FramebufferInfo` exactly matches formats, sample count, and sample quality. Dimensions are not part of pipeline compatibility.

Use `FramebufferInfoEx` when dimensions and a convenient viewport are also needed.

## Staging textures and readback

```cpp
auto staging = device->createStagingTexture(texture->getDesc(),
                                             nvrhi::CpuAccessMode::Read);

commandList->copyTexture(staging, nvrhi::TextureSlice(),
                         texture, nvrhi::TextureSlice());
// Submit and wait/poll before mapping.

size_t rowPitch = 0;
void* data = device->mapStagingTexture(
    staging, nvrhi::TextureSlice(), nvrhi::CpuAccessMode::Read, &rowPitch);
// Copy rows using rowPitch.
device->unmapStagingTexture(staging);
```

The mapped row pitch may exceed tightly packed pixel width. Never derive row addresses from width alone.

## Heaps and virtual resources

Virtual resources separate resource description from memory allocation:

```cpp
auto texture = device->createTexture(
    nvrhi::TextureDesc()
        .setWidth(width).setHeight(height)
        .setFormat(format).setIsVirtual(true));

auto requirements = device->getTextureMemoryRequirements(texture);
auto heap = device->createHeap(
    nvrhi::HeapDesc()
        .setType(nvrhi::HeapType::DeviceLocal)
        .setCapacity(requirements.size)
        .setDebugName("Transient heap"));

bool ok = device->bindTextureMemory(texture, heap, alignedOffset);
```

Equivalent buffer and acceleration-structure APIs exist. Requirements include size and alignment. Binding must occur before use.

Virtual resources enable custom allocation and aliasing, but NVRHI does not schedule aliases for you. Your allocator must ensure:

- non-overlapping live ranges for resources sharing memory;
- required offsets are aligned;
- correct heap type;
- no command list references an old alias when memory is reused; and
- native aliasing barriers are handled where the backend requires them.

Check `Feature::VirtualResources`.

## Tiled textures

Tiled resources map selected texture tiles to heap ranges:

1. Create a texture with `isTiled = true`.
2. Call `getTextureTiling` for total tiles, packed mips, tile shape, and per-subresource tiling.
3. Allocate a compatible heap.
4. Build one or more `TextureTilesMapping` arrays.
5. Call `updateTextureTileMappings` on the queue that will consume the mapping.

Packed mip tails have backend-defined grouping and must be treated according to `PackedMipDesc`. Mapping updates are queue operations; synchronize them with sampling and uploads.

Header version 26 has no dedicated tiled-resource value in `nvrhi::Feature`. Do not imply support from `Feature::VirtualResources`: virtual placement and tiled mapping are different capabilities. Gate this path through the underlying API's sparse/tiled-resource capability checks during native-device setup, before creating the tiled texture. `getTextureTiling` and `updateTextureTileMappings` return `void` and are not recoverable support probes.

## Sampler feedback

Sampler feedback records texture sampling demand for streaming systems. In this NVRHI revision it is exposed through:

- `createSamplerFeedbackTexture`;
- `clearSamplerFeedbackTexture`;
- `SamplerFeedbackTexture_UAV` bindings;
- `decodeSamplerFeedbackTexture`; and
- `setSamplerFeedbackTextureState`.

The paired texture and mip-region dimensions define feedback granularity. Decode into an `R8_UINT`-compatible buffer, then read/process results asynchronously. The public API comments identify this path as D3D12-only; always guard with `Feature::SamplerFeedback`.

## Shared and native resources

`createHandleForNativeTexture` and `createHandleForNativeBuffer` wrap externally owned native resources. The descriptor must accurately describe the native object and starting-state policy. Wrapping does not transfer external ownership unless the backend contract explicitly says so.

Use `SharedResourceFlags` for resources intended for OS/native sharing:

- `Shared`;
- `Shared_NTHandle` (D3D11-specific behavior); and
- `Shared_CrossAdapter` (D3D12-specific behavior).

External synchronization is still the application's responsibility.

## Resource creation checklist

- Declare every required usage flag up front.
- Give each resource a stable debug name.
- Choose and document its command-list boundary state policy.
- Use `queryFormatSupport` for nontrivial format operations.
- Align partial constant-buffer views to 256 bytes.
- Size Vulkan volatile-buffer versions for worst-case in-flight writes.
- Keep upload and readback paths asynchronous.
- Recycle staging resources per frame instead of forcing CPU waits.
- Use virtual/tiled resources only behind a tested allocator and synchronization policy.

