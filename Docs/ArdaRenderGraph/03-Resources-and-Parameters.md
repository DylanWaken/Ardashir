# 3. Resources and parameters

[← Core concepts](02-Core-Concepts.md) · [Documentation home](README.md) ·
[Next: Passes and dependencies →](04-Passes-and-Dependencies.md)

## Logical textures and buffers

`CreateTexture` and `CreateBuffer` register descriptors now and defer physical
allocation until execution:

```cpp
nvrhi::TextureDesc ColorDesc;
ColorDesc.setDebugName("Lighting")
    .setWidth(1920)
    .setHeight(1080)
    .setFormat(nvrhi::Format::RGBA16_FLOAT)
    .setIsUAV(true)
    .setIsRenderTarget(true);
FARDGTextureRef Color = Graph.CreateTexture(ColorDesc);

nvrhi::BufferDesc DataDesc;
DataDesc.setDebugName("Visible objects")
    .setByteSize(64 * 1024)
    .setStructStride(sizeof(uint32_t))
    .setCanHaveUAVs(true);
FARDGBufferRef Data = Graph.CreateBuffer(DataDesc);
```

Names and dimensions/byte sizes must be non-empty/non-zero. Graph-created
resources accept `EARDGResourceFlags::None` or `Transient` (the default).
`External` and `Extracted` are assigned through import and extraction APIs, not
direct creation.

An `Unknown` initial state for a graph-created resource is normalized to
`Common` for compilation and execution.

## Views

Views are logical declarations over a parent resource:

```cpp
FARDGTextureViewDesc MipView;
MipView.mTexture = Color->GetHandle();
MipView.mSubresources = nvrhi::TextureSubresourceSet(1, 1, 0, 1);
MipView.mFormat = nvrhi::Format::RGBA16_FLOAT; // optional override
FARDGTextureUAVRef ColorMip1 =
    Graph.CreateUAV("Lighting mip 1 UAV", MipView);

FARDGBufferViewDesc RangeView;
RangeView.mBuffer = Data->GetHandle();
RangeView.mRange = nvrhi::BufferRange(0, 4096);
FARDGBufferSRVRef FirstPage =
    Graph.CreateSRV("Visible object page", RangeView);
```

`CreateTextureSRV/UAV` and `CreateBufferSRV/UAV` are the explicit names;
overloaded `CreateSRV` and `CreateUAV` are aliases. Texture views can select
mips, array slices, format, and dimension. Buffer views can select a byte range
and typed format.

Views do not create separate physical resources. During a pass,
`Context.GetTexture(View)` or `Context.GetBuffer(View)` returns the physical
parent. Use the view descriptor when building the NVRHI binding item.

## Parameter structs

Metadata-generating macros turn ordinary standard-layout C++ structs into graph
access declarations:

```cpp
ARDG_BEGIN_PARAMETER_STRUCT(FLightingParameters)
    ARDG_PARAMETER(uint32_t, mLightCount)
    ARDG_TEXTURE_SRV(mDepth)
    ARDG_TEXTURE_UAV(mOutput)
ARDG_END_PARAMETER_STRUCT()
```

The generated struct has a default constructor and public members.
`GetStaticMetadata()` exposes name, size, alignment, declaration-ordered member
records, defaults, array counts/strides, and nested metadata.

Null resource members are ignored. Ordinary value members affect neither
dependencies nor states.

Tooling can call `FindMember("mName")` or `Enumerate(&Parameters, Visitor)`.
Enumeration reports leaf members in declaration order with dotted/indexed
paths. Pass `true` as the third argument to visit nested struct containers
before their children as well. A resolved `FARDGParameter` exposes the member
record, address, path, array index, and typed `GetValue<T>()`.

## Complete parameter macro reference

All scalar macros have an `_ARRAY(..., Count)` counterpart where shown.
Arrays are `std::array` members and are enumerated element by element.

### Values and nesting

- `ARDG_PARAMETER(CppType, Name)`
- `ARDG_PARAMETER_ARRAY(CppType, Name, Count)`
- `ARDG_PARAMETER_STRUCT(StructType, Name)`
- `ARDG_PARAMETER_STRUCT_ARRAY(StructType, Name, Count)`

Nested parameter structs are recursively traversed. Metadata paths look like
`mLighting.mDepth` and `mLayers[1].mOutput`.

### Direct logical resources

- `ARDG_TEXTURE(Name)` / `ARDG_TEXTURE_ARRAY(Name, Count)` — default
  `ShaderResource`.
- `ARDG_BUFFER(Name)` / `ARDG_BUFFER_ARRAY(Name, Count)` — default
  `ShaderResource`.

These declare the whole logical resource with the macro's default state.

### Shader-resource and unordered-access views

- `ARDG_TEXTURE_SRV(Name)` / `ARDG_TEXTURE_SRV_ARRAY(Name, Count)` — selected
  texture subresources in `ShaderResource`.
- `ARDG_TEXTURE_UAV(Name)` / `ARDG_TEXTURE_UAV_ARRAY(Name, Count)` — selected
  texture subresources in `UnorderedAccess`.
- `ARDG_BUFFER_SRV(Name)` / `ARDG_BUFFER_SRV_ARRAY(Name, Count)` — selected
  buffer range in `ShaderResource`.
- `ARDG_BUFFER_UAV(Name)` / `ARDG_BUFFER_UAV_ARRAY(Name, Count)` — selected
  buffer range in `UnorderedAccess`.

### Runtime state/range access

- `ARDG_TEXTURE_ACCESS(Name)` /
  `ARDG_TEXTURE_ACCESS_ARRAY(Name, Count)` stores `FARDGTextureAccess`.
- `ARDG_BUFFER_ACCESS(Name)` /
  `ARDG_BUFFER_ACCESS_ARRAY(Name, Count)` stores `FARDGBufferAccess`.

Use these when the state or range is selected at runtime:

```cpp
Parameters.mInput = {
    Texture,
    nvrhi::ResourceStates::NonPixelShaderResource,
    nvrhi::TextureSubresourceSet(0, 1, 0, 1)
};
Parameters.mOutput = {
    Buffer,
    nvrhi::ResourceStates::UnorderedAccess,
    nvrhi::BufferRange(4096, 4096)
};
```

The state must not be `Unknown`.

### Uniform buffers

- `ARDG_UNIFORM_BUFFER(Name)` /
  `ARDG_UNIFORM_BUFFER_ARRAY(Name, Count)` — declares `ConstantBuffer` use.

Create one from another ARDG parameter struct:

```cpp
ARDG_BEGIN_PARAMETER_STRUCT(FViewConstants)
    ARDG_PARAMETER(float, mExposure)
    ARDG_TEXTURE(mBlueNoise)
ARDG_END_PARAMETER_STRUCT()

FViewConstants Constants;
Constants.mExposure = 1.0f;
Constants.mBlueNoise = BlueNoise;
FARDGUniformBufferRef ViewCB =
    Graph.CreateUniformBuffer("View constants", &Constants);
```

The contents are frozen. Execution creates a dedicated constant buffer, uploads
all graph uniform buffers on graphics, transitions them to `ConstantBuffer`,
and makes compute/copy queues wait for that upload before their first submitted
pass.

Important: uniform-buffer metadata is recursively traversed. `mBlueNoise` above
therefore also declares a shader-resource dependency in every pass that
references `ViewCB`.

### Raster attachments

- `ARDG_RENDER_TARGET_BINDING_SLOTS(Name)` stores
  `FARDGRenderTargetBindingSlots`.

It contains `mColor[nvrhi::c_MaxRenderTargets]` and `mDepthStencil`. Color
attachments imply `RenderTarget`; depth/stencil implies `DepthWrite`. Each
binding can select texture subresources.

The macro declares dependencies, states, and a raster compatibility signature.
The pass callback still binds its NVRHI framebuffer and graphics state.

## Resource states and dependency meaning

The implementation classifies these as writes:

- `UnorderedAccess`, `RenderTarget`, `DepthWrite`, `CopyDest`,
  `ResolveDest`, `AccelStructWrite`, `OpacityMicromapWrite`, and
  `ConvertCoopVecMatrixOutput`.

Other legal states are reads. Multiple read states for the same resource in one
pass can be merged. A write state cannot be combined with another incompatible
state in the same pass.

Textures track state per mip and array slice. Buffers validate ranges for
access/dependency discovery, but transitions and produced-before-read status are
whole-buffer. Two disjoint buffer writes still serialize and produce a
whole-buffer UAV barrier when needed.

See the
[NVRHI programming guide](https://github.com/NVIDIA-RTX/NVRHI/blob/main/doc/ProgrammingGuide.md)
for the underlying state model.

## External resources

Import physical handles owned outside the graph:

```cpp
FARDGTextureRef BackBuffer = Graph.RegisterExternalTexture(
    SwapChainTexture,
    nvrhi::ResourceStates::Present,
    "Swap-chain color");
```

The initial state must be known. The overload without an explicit state uses
the handle descriptor's `initialState`; it still rejects `Unknown`.

Importing the same physical handle twice in one builder returns the same
logical record if the states agree, and throws if they conflict. Imported
resources:

- carry `External`, never `Transient`;
- begin with the prologue as producer;
- keep writes observable through the epilogue; and
- return to their final state, initially the imported state.

## Extraction

Extraction transfers a graph-created physical handle to caller-owned output
storage after submission:

```cpp
nvrhi::TextureHandle HistoryForNextFrame;
Graph.QueueTextureExtraction(
    History,
    HistoryForNextFrame,
    nvrhi::ResourceStates::ShaderResource);
```

Extraction:

- adds `Extracted`;
- removes transient eligibility;
- makes the last producer observable;
- extends the lifetime through the epilogue;
- emits an epilogue transition to the requested non-`Unknown` state; and
- fills the output handle after all graph command lists are submitted.

Each logical resource and each output address can be extracted only once.
A non-external resource must have a producer before compilation. Extraction
does not wait for GPU completion.

## Lifetimes, pooling, and transient fallback

![Resource lifetime and pool reuse](assets/resource-lifetime.svg)

Compilation computes inclusive first/last execution-order indices for every
live texture and buffer. External and extracted resources extend to the
epilogue. A resource is transient only when it has `Transient` and is neither
external nor extracted.

At execution, descriptor-compatible transient resources may reuse one committed
NVRHI texture/buffer when:

- their lifetimes do not overlap (`previous.last < next.first`); and
- all uses stay in one queue reuse domain.

Cross-queue resources and non-transient resources do not participate in this
reuse. Pooling is local to a single `Execute()` call.

The allocator can calculate an ideal virtual-heap layout when NVRHI reports
virtual-resource support. Actual placed-resource aliasing is currently disabled
because portable NVRHI aliasing barriers and heap-compatibility queries are not
available. Transient candidates therefore use committed-resource pooling and
set `mbUsedTransientFallback`. `mbUsedVirtualHeaps` and
`mbUsedTransientAliasing` currently remain false.

---

[← Core concepts](02-Core-Concepts.md) · [Documentation home](README.md) ·
[Next: Passes and dependencies →](04-Passes-and-Dependencies.md)
