# Backends and Native Interop

[Previous](08-State-and-Synchronization.md) · [Home](README.md) · Next: [Debugging and practices](10-Debugging-Performance-Practices.md)

## Backend boundary

Keep these tasks in a platform/backend layer:

- adapter and native-device creation;
- native validation/debug-layer configuration;
- queue creation;
- window surface and swap chain;
- image acquisition and presentation;
- resize and fullscreen behavior;
- external semaphore/fence integration; and
- wrapping native back buffers.

Pass only `nvrhi::IDevice`, framebuffers, dimensions, and frame lifecycle signals into backend-independent renderer code.

## D3D11

Device creation needs the immediate context:

```cpp
nvrhi::d3d11::DeviceDesc desc;
desc.messageCallback = &callback;
desc.context = immediateContext;
desc.aftermathEnabled = false;
auto device = nvrhi::d3d11::createDevice(desc);
```

Characteristics:

- one immediate native context;
- NVRHI command lists must use immediate execution;
- only one can be open at a time;
- no modern compute/copy queue model;
- no hardware RT or meshlet path;
- resource-state transition methods are effectively no-ops;
- some NVIDIA features require NVAPI.

D3D11 remains useful for a broad compatibility renderer, but build explicit fallbacks for every modern feature.

## D3D12

```cpp
nvrhi::d3d12::DeviceDesc desc;
desc.errorCB = &callback;
desc.pDevice = nativeDevice;
desc.pGraphicsCommandQueue = graphicsQueue;
desc.pComputeCommandQueue = computeQueue; // optional
desc.pCopyCommandQueue = copyQueue;       // optional
desc.renderTargetViewHeapSize = 1024;
desc.depthStencilViewHeapSize = 1024;
desc.shaderResourceViewHeapSize = 16384;
desc.samplerHeapSize = 1024;
desc.maxTimerQueries = 256;
desc.enableHeapDirectlyIndexed = useDirectIndexing;
desc.enableEnhancedBarriers = true;
auto device = nvrhi::d3d12::createDevice(desc);
```

Choose descriptor-heap sizes for peak live descriptors. Heap growth/reallocation can affect native view assumptions; avoid retrieving and retaining native descriptors across unrelated resource creation.

D3D12-specific `nvrhi::d3d12::IDevice` adds:

- root-signature building;
- wrappers for native graphics/meshlet PSOs;
- descriptor-heap access.

The D3D12-specific command-list interface adds upload allocation, descriptor-heap commitment, buffer GPU VA, and volatile-buffer updates for expert native interop.

`enableRayTracingValidation` requires NVAPI and must be enabled before any RT capability query or operation. `enableEnhancedBarriers` requests the path; query `Feature::EnhancedBarriers` for actual support.

## Vulkan

```cpp
nvrhi::vulkan::DeviceDesc desc;
desc.errorCB = &callback;
desc.instance = instance;
desc.physicalDevice = physicalDevice;
desc.device = nativeDevice;
desc.graphicsQueue = graphicsQueue;
desc.graphicsQueueIndex = graphicsFamily;
desc.computeQueue = computeQueue;
desc.computeQueueIndex = computeFamily;
desc.transferQueue = transferQueue;
desc.transferQueueIndex = transferFamily;
desc.instanceExtensions = instanceExtensions.data();
desc.numInstanceExtensions = instanceExtensions.size();
desc.deviceExtensions = deviceExtensions.data();
desc.numDeviceExtensions = deviceExtensions.size();
desc.bufferDeviceAddressSupported = enabledBDA;
desc.maxTimerQueries = 256;
auto device = nvrhi::vulkan::createDevice(desc);
```

Report the exact enabled extensions and `bufferDeviceAddress` feature. NVRHI uses this information to select legal feature paths.

Vulkan-specific device methods expose the backend's queue progress semaphore and allow external semaphore bridging:

- `getQueueSemaphore`;
- `queueWaitForSemaphore`;
- `queueSignalSemaphore`;
- `queueGetCompletedInstance`.

`getQueueSemaphore` returns NVRHI's internal queue progress semaphore, and its timeline values are submission instances. `queueWaitForSemaphore` and `queueSignalSemaphore` can also receive application-owned binary semaphores (use value `0`) or timeline semaphores (use application-managed monotonic values). This is how the local backend bridges binary acquire/present semaphores. Do not directly signal values on NVRHI's internal semaphore.

Vulkan descriptor-set/register-space mapping and DXC offsets are covered in [Bindings and shaders](04-Bindings-and-Shaders.md).

## Swap-chain integration

NVRHI owns no swap-chain abstraction. A backend frame normally does:

1. Wait for/recycle the frame slot.
2. Acquire a native back-buffer index.
3. Expose its pre-created NVRHI framebuffer.
4. Record NVRHI work.
5. Arrange native acquire semaphore/fence wait before queue execution.
6. Submit through NVRHI.
7. Arrange native signal for presentation.
8. Present through DXGI or Vulkan.

On D3D12, fences usually protect frame allocators and back buffers. On Vulkan, binary acquire/present semaphores are commonly bridged to the NVRHI queue timeline. Follow the concrete local backend implementations rather than assuming `executeCommandList` automatically understands swap-chain semaphores.

## Wrapping native resources

```cpp
auto wrapped = device->createHandleForNativeTexture(
    nvrhi::ObjectTypes::VK_Image,
    nvrhi::Object(uint64_t(nativeImage)),
    accurateTextureDesc);
```

Use the matching `ObjectTypes` constant:

- D3D11 resource/buffer;
- D3D12 resource;
- Vulkan image/buffer.

The descriptor must match dimensions, mips, arrays, samples, format, and usage. It also supplies NVRHI's state-boundary assumptions. A mismatch can cause invalid views or transitions.

Wrapping native buffers follows the same model.

## Retrieving native objects

```cpp
auto native = texture->getNativeObject(nvrhi::ObjectTypes::D3D12_Resource);
ID3D12Resource* resource = native;
```

Available categories include:

- native devices, queues, contexts, command lists/buffers;
- resources, images, buffers, and memory;
- views/descriptors, samplers, shader modules;
- pipeline layouts, pipelines, root signatures, and PSOs;
- Vulkan acceleration structures and micromaps; and
- shared OS handles.

`getNativeObject` does not add a native reference. Keep the NVRHI resource alive for the entire use. A null object means that representation is unavailable.

Use `IDevice::getNativeQueue` for a queue and `ITexture::getNativeView` for a specific format/subresource/dimension view.

## Mixing native commands

Safe sequence:

```cpp
commandList->setTextureState(resource, subresources, expectedNativeState);
commandList->commitBarriers();

auto nativeCmd = commandList->getNativeObject(nativeCommandListType);
RecordNativeCommands(nativeCmd);

commandList->clearState();
commandList->beginTrackingTextureState(resource, subresources, nativeFinalState);
```

Exact tracking handoff depends on whether NVRHI already knew the resource state. The essential contract is:

- flush NVRHI barriers before native use;
- do not let native code silently change state;
- reset cached pipeline state before returning;
- retain every object referenced by native commands.

Avoid mixing native descriptor heap/set binding with NVRHI state unless you restore it; NVRHI's cache may otherwise skip a binding it believes is still active.

## Native pipelines

The D3D12 backend can wrap native graphics and meshlet pipeline states with an NVRHI root signature and corresponding NVRHI descriptors/framebuffer info. This supports features not represented by common pipeline creation while retaining NVRHI state/binding integration.

The wrapped descriptor must truthfully describe the native PSO. NVRHI cannot reflect arbitrary native state to verify it.

## Optional integrations

### NVAPI

Build with `NVRHI_WITH_NVAPI` and provide the SDK. Features include selected D3D11/D3D12 raster extensions, HLSL intrinsics, single-pass stereo, shader execution reordering, OMM, spheres/LSS, and clusters depending on hardware/backend.

### RTXMU

Build with `NVRHI_WITH_RTXMU` to delegate BLAS memory management and compaction. It changes AS management behavior and currently conflicts with some specialized geometry/OMM paths.

### Nsight Aftermath

Build with `NVRHI_WITH_AFTERMATH`, enable it in the backend descriptor, retain the crash-dump helper, and use markers/debug names. Check `isAftermathEnabled()`.

## Portability strategy

Define capability tiers, not backend name checks:

- baseline: graphics + compute on graphics queue;
- modern raster: indirect count, meshlets, VRS;
- RT baseline: AS + ray query;
- RT pipeline;
- vendor advanced: SER, OMM, spheres/LSS, clusters;
- neural: cooperative-vector inference/training.

Query features and exact formats at startup, choose implementations once, and log the selected tier. Backend checks remain appropriate only for genuinely backend-specific operations such as swap chains or local RT bindings.

