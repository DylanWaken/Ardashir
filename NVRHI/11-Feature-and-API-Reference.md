# Feature and API Reference

[Previous](10-Debugging-Performance-Practices.md) · [Home](README.md) · Next: [Recipes](12-Recipes.md)

This is a navigation reference, not a replacement for the authoritative comments in [`nvrhi.h`](../../ThirdParty/NVRHI/include/nvrhi/nvrhi.h). Runtime queries are authoritative for the current adapter, enabled native features/extensions, backend, and NVRHI build options.

## Global limits

- render targets: 8;
- viewports/scissors: 16;
- vertex attributes: 16;
- binding layouts per pipeline: 8;
- bindless register spaces: 16;
- volatile constant buffers per layout: 6;
- volatile constant buffers total: 32;
- push constants: 128 bytes;
- partial constant-buffer offset/size alignment: 256 bytes.

## Feature query catalog

Call `IDevice::queryFeatureSupport(feature)`. Never replace a query with “the backend should support it.”

| `Feature` | Meaning and usage |
|---|---|
| `ComputeQueue` | Dedicated/independent compute queue was supplied and supported. Create compute-queue command lists and synchronize with queue waits. |
| `ConservativeRasterization` | Conservative raster state can be enabled. Still validate desired tier/behavior in algorithm testing. |
| `ConstantBufferRanges` | Partial constant-buffer binding ranges are supported. Ranges require 256-byte alignment. |
| `CopyQueue` | Copy queue exists. Use queue-limited command lists and explicit consumer waits. |
| `DeferredCommandLists` | Backend supports deferred/parallel command-list recording behavior. D3D11 is constrained. |
| `FastGeometryShader` | NVAPI fast geometry shader and related semantics are available. |
| `HeapDirectlyIndexed` | HLSL direct resource/sampler heap indexing can be used with matching bindless layout/compiler setup. |
| `HlslExtensionUAV` | NVIDIA HLSL extension fake-UAV path is available. |
| `LinearSweptSpheres` | LSS ray-tracing geometry is supported. Specialized vendor/hardware path. |
| `Meshlets` | Amplification/task and mesh shader pipelines are supported. |
| `RayQuery` | Inline ray queries are supported in applicable shader stages. Requires AS support and correctly enabled native features. |
| `RayTracingAccelStruct` | BLAS/TLAS creation and build are supported. |
| `RayTracingClusters` | Cluster acceleration-structure operations are supported. This revision documents execution as D3D12 + NVAPI. |
| `RayTracingOpacityMicromap` | OMM creation/build/use is supported with matching build/native configuration. |
| `RayTracingPipeline` | Full RT pipeline and shader-table dispatch are supported. |
| `SamplerFeedback` | Sampler feedback creation, clear, decode, and binding are supported; public command comments mark this as D3D12-only. |
| `ShaderExecutionReordering` | NVIDIA SER shader extension path is available. Shader/compiler integration is also required. |
| `ShaderSpecializations` | `createShaderSpecialization` is supported for the backend/binary. |
| `SinglePassStereo` | NVIDIA single-pass stereo state/semantics are available. |
| `Spheres` | Hardware ray-tracing sphere geometry is supported. |
| `VariableRateShading` | VRS is supported. Pass `VariableRateShadingFeatureInfo` to query image tile size. |
| `VirtualResources` | Resource creation without backing memory and later heap binding is supported. |
| `WaveLaneCountMinMax` | Pass `WaveLaneCountMinMaxFeatureInfo` to obtain minimum/maximum wave width. |
| `CooperativeVectorInferencing` | Cooperative-vector inference path is available; query exact matmul formats. |
| `CooperativeVectorTraining` | Cooperative-vector training path is available; query exact component-type operations. |
| `EnhancedBarriers` | D3D12 enhanced-barrier path is active. It must also be requested in `d3d12::DeviceDesc`. |

Example information query:

```cpp
nvrhi::WaveLaneCountMinMaxFeatureInfo waves{};
if (device->queryFeatureSupport(
        nvrhi::Feature::WaveLaneCountMinMax, &waves, sizeof(waves)))
{
    // Choose a portable shader algorithm within [min, max].
}
```

## Backend overview

| Area | D3D11 | D3D12 | Vulkan |
|---|---|---|---|
| Baseline graphics/compute | Yes | Yes | Yes |
| Explicit resource barriers | Backend no-op model | Yes | Yes |
| Separate compute/copy queues | No | Optional | Optional |
| Mesh shaders | No | Query | Query |
| Ray tracing | No | Query | Query |
| Local RT bindings | No RT | Yes | No |
| Sampler feedback API path | No | Query | No in this revision's command implementation contract |
| Vulkan descriptor-set mapping | N/A | N/A | Yes |
| Enhanced barriers | No | Query | N/A |
| Cluster operations in this revision | No | Query + NVAPI | Not exposed |

“Query” means hardware, OS/driver, enabled native features, and NVRHI build options all matter.

## Core object inventory

| Interface | Handle | Created by |
|---|---|---|
| `IHeap` | `HeapHandle` | `createHeap` |
| `ITexture` | `TextureHandle` | `createTexture`, native wrapper |
| `IStagingTexture` | `StagingTextureHandle` | `createStagingTexture` |
| `ISamplerFeedbackTexture` | `SamplerFeedbackTextureHandle` | feedback creation/wrapper |
| `IBuffer` | `BufferHandle` | `createBuffer`, native wrapper |
| `IShader` | `ShaderHandle` | `createShader`, specialization, library entry |
| `IShaderLibrary` | `ShaderLibraryHandle` | `createShaderLibrary` |
| `ISampler` | `SamplerHandle` | `createSampler` |
| `IInputLayout` | `InputLayoutHandle` | `createInputLayout` |
| `IFramebuffer` | `FramebufferHandle` | `createFramebuffer` |
| `IBindingLayout` | `BindingLayoutHandle` | regular/bindless creation |
| `IBindingSet` | `BindingSetHandle` | `createBindingSet` |
| `IDescriptorTable` | `DescriptorTableHandle` | `createDescriptorTable` |
| `IGraphicsPipeline` | `GraphicsPipelineHandle` | `createGraphicsPipeline` |
| `IComputePipeline` | `ComputePipelineHandle` | `createComputePipeline` |
| `IMeshletPipeline` | `MeshletPipelineHandle` | `createMeshletPipeline` |
| `rt::IAccelStruct` | `rt::AccelStructHandle` | `createAccelStruct` |
| `rt::IOpacityMicromap` | `rt::OpacityMicromapHandle` | `createOpacityMicromap` |
| `rt::IPipeline` | `rt::PipelineHandle` | `createRayTracingPipeline` |
| `rt::IShaderTable` | `rt::ShaderTableHandle` | RT pipeline |
| `IEventQuery` | `EventQueryHandle` | `createEventQuery` |
| `ITimerQuery` | `TimerQueryHandle` | `createTimerQuery` |
| `ICommandList` | `CommandListHandle` | `createCommandList` |
| `ICommandListLifetimeTracker` | tracker handle | `createCommandListLifetimeTracker` |
| `IDevice` | `DeviceHandle` | backend factory / validation wrapper |

## Device method map

### Memory/resources

- heap creation;
- texture/buffer creation, memory requirements, virtual memory binding;
- native texture/buffer wrapping;
- staging texture/buffer mapping;
- texture tiling queries and mapping updates;
- sampler-feedback creation;
- OMM/AS creation, requirements, and memory binding.

### Shader and state objects

- shader, specialization, library;
- sampler and input layout;
- framebuffer;
- graphics, compute, meshlet, and RT pipelines;
- regular and bindless layouts;
- regular sets and descriptor tables.

### Submission and synchronization

- command-list/lifetime-tracker creation;
- execute one or many lists;
- queue wait by submission instance;
- event/timer query management;
- wait for idle;
- garbage collection.

### Capability and interop

- graphics API kind;
- feature and format queries;
- cooperative-vector queries/sizing;
- native queue;
- message callback;
- Aftermath state/helper.

## Command-list method map

### Transfer/clear

- clear color, integer, depth/stencil, buffers, feedback;
- copy texture variants and buffers;
- upload texture/buffer;
- resolve multisample textures;
- decode sampler feedback.

### Pipeline execution

- set push constants;
- set graphics state; direct/indirect/indexed/count drawing;
- set compute state; direct/indirect dispatch;
- set meshlet state; direct/indirect/count dispatch;
- set RT state; dispatch rays.

### RT and neural operations

- OMM build;
- BLAS/TLAS build/update, GPU-buffer TLAS build;
- BLAS compaction through RTXMU;
- AS clone;
- cluster operation;
- cooperative-vector matrix conversion.

### Diagnostics

- timer query begin/end;
- nested markers;
- state cache clear.

### Barrier/state tracking

- automatic barrier toggle;
- derive states from a set/framebuffer;
- UAV barrier policy toggle;
- begin texture/buffer tracking;
- texture/buffer/AS state setters;
- permanent states;
- barrier commit;
- tracked-state queries.

## Resource binding type map

| Shader declaration idea | Layout item | Set item |
|---|---|---|
| sampled/read texture | `Texture_SRV` | `Texture_SRV` |
| writable texture | `Texture_UAV` | `Texture_UAV` |
| typed buffer | `TypedBuffer_SRV/UAV` | same |
| structured buffer | `StructuredBuffer_SRV/UAV` | same |
| byte-address buffer | `RawBuffer_SRV/UAV` | same |
| persistent CB | `ConstantBuffer` | `ConstantBuffer` |
| volatile CB | `VolatileConstantBuffer` | `ConstantBuffer` detects volatile descriptor |
| sampler | `Sampler` | `Sampler` |
| TLAS | `RayTracingAccelStruct` | `RayTracingAccelStruct` |
| push constants | `PushConstants(slot, bytes)` | `PushConstants(slot, bytes)` |
| feedback UAV | `SamplerFeedbackTexture_UAV` | same |

## Pipeline call sequence

| Pipeline | Creation | State | Execute |
|---|---|---|---|
| Graphics | `createGraphicsPipeline` | `setGraphicsState` | `draw*` |
| Compute | `createComputePipeline` | `setComputeState` | `dispatch*` |
| Meshlet | `createMeshletPipeline` | `setMeshletState` | `dispatchMesh*` |
| Ray tracing | `createRayTracingPipeline` + table | `setRayTracingState` | `dispatchRays` |

## Format support flags

`queryFormatSupport` may report:

- `Buffer`, `IndexBuffer`, `VertexBuffer`;
- `Texture`, `DepthStencil`, `RenderTarget`, `Blendable`;
- `ShaderLoad`, `ShaderSample`;
- `ShaderUavLoad`, `ShaderUavStore`, `ShaderAtomic`.

Test the exact bit needed. Format support can differ by adapter even within one backend.

## Utility header highlights

`nvrhi/utils.h` provides:

- framebuffer attachment clear helpers;
- UAV-barrier helpers;
- enum-to-string functions;
- format/state utility routines;
- scoped marker and internal/common helpers.

Read the current header before duplicating a utility in engine code.

## Native object type highlights

`ObjectTypes` includes common shared handles plus D3D11 device/context/resources/views, D3D12 device/queue/command list/resource/descriptors/root signature/PSO, and Vulkan device/physical device/instance/queue/command buffer/memory/buffer/image/view/AS/sampler/shader/descriptors/layout/pipeline/micromap.

Native objects returned by `getNativeObject` are borrowed and are not reference-counted for the caller.

