# Recipes

[Previous](11-Feature-and-API-Reference.md) · [Home](README.md)

These patterns focus on sequencing. Adapt descriptors, error handling, and queue integration to the application.

## Static GPU buffer upload

```cpp
auto buffer = device->createBuffer(
    nvrhi::BufferDesc()
        .setByteSize(dataSize)
        .setCanHaveRawViews(true)
        .setDebugName("Static mesh data"));

auto upload = device->createCommandList();
upload->open();
upload->writeBuffer(buffer, data, dataSize);
upload->setPermanentBufferState(
    buffer, nvrhi::ResourceStates::ShaderResource);
upload->close();
device->executeCommandList(upload);
```

Use a dedicated upload list for a large content batch, wait/poll for its completion as needed, then release the list so its upload working set can be freed.

## Per-frame volatile constants

Creation:

```cpp
auto constants = device->createBuffer(
    nvrhi::BufferDesc()
        .setByteSize(sizeof(FrameConstants))
        .setIsConstantBuffer(true)
        .setIsVolatile(true)
        .setMaxVersions(framesInFlight * maxWritesPerFrame)
        .setDebugName("Frame constants"));
```

Every opened list:

```cpp
commandList->open();
commandList->writeBuffer(constants, &frame, sizeof(frame));
commandList->setGraphicsState(state);
commandList->drawIndexed(args);
commandList->close();
```

The regular binding-set constructor `ConstantBuffer(slot, constants)` detects that the buffer is volatile and matches a `VolatileConstantBuffer` layout item.

## Tiny per-draw push constants

```cpp
commandList->setGraphicsState(state);
for (const Draw& draw : draws)
{
    commandList->setPushConstants(&draw.constants, sizeof(draw.constants));
    commandList->drawIndexed(draw.args);
}
```

The layout and binding set must both declare the same push-constant slot and byte size. Keep size ≤128 bytes.

## Compute writes, graphics samples

```cpp
commandList->setComputeState(computeState); // output implied UAV
commandList->dispatch(groupsX, groupsY, 1);

// A different state call derives UAV ordering + transition to SRV.
commandList->setGraphicsState(graphicsState); // output bound as SRV
commandList->draw(fullscreenTriangle);
```

This works with automatic barriers and regular sets. If the texture is bindless, explicitly issue the UAV barrier and transition because NVRHI cannot infer access from the descriptor index.

## Repeated compute pass with one state

```cpp
commandList->setComputeState(state);
for (uint32_t phase = 0; phase < phaseCount; ++phase)
{
    commandList->setPushConstants(&phase, sizeof(phase));
    commandList->dispatch(groups, 1, 1);
    if (phase + 1 < phaseCount)
        nvrhi::utils::BufferUavBarrier(commandList, workingBuffer);
}
```

A push-constant change does not call `setComputeState`, so add ordering manually when each phase consumes the previous phase.

## Async compute producer

```cpp
auto computeList = device->createCommandList(
    nvrhi::CommandListParameters()
        .setQueueType(nvrhi::CommandQueue::Compute));

computeList->open();
computeList->beginTrackingBufferState(
    sharedBuffer, nvrhi::ResourceStates::Common); // actual producer-entry state
computeList->setComputeState(producerState);
computeList->dispatch(x, y, z);
computeList->setBufferState(
    sharedBuffer, nvrhi::ResourceStates::Common);
computeList->close();

uint64_t producer = device->executeCommandList(
    computeList, nvrhi::CommandQueue::Compute);

device->queueWaitForCommandList(
    nvrhi::CommandQueue::Graphics,
    nvrhi::CommandQueue::Compute,
    producer);

device->executeCommandList(graphicsConsumer,
                           nvrhi::CommandQueue::Graphics);
```

The graphics consumer must begin tracking `sharedBuffer` from the same `Common` boundary state before first use; its graphics state then transitions the buffer to `ShaderResource`. An equivalent `keepInitialState(Common)` policy also works. The queue wait orders execution; it does not communicate resource states between independently recorded command lists. Ending in `Common` also avoids putting a pixel-shader state on the compute queue.

Guard this path with `Feature::ComputeQueue`; use the graphics queue otherwise.

## GPU buffer readback without a frame stall

At setup, create one CPU-readable staging buffer and event query per frame slot:

```cpp
auto readback = device->createBuffer(
    nvrhi::BufferDesc()
        .setByteSize(byteSize)
        .setCpuAccess(nvrhi::CpuAccessMode::Read)
        .setDebugName("Readback"));
auto complete = device->createEventQuery();
```

Record:

```cpp
commandList->copyBuffer(readback, 0, gpuBuffer, 0, byteSize);
// close and submit command list
device->setEventQuery(complete, nvrhi::CommandQueue::Graphics);
```

In a later frame:

```cpp
if (device->pollEventQuery(complete))
{
    void* mapped = device->mapBuffer(readback, nvrhi::CpuAccessMode::Read);
    std::memcpy(cpuDestination, mapped, byteSize);
    device->unmapBuffer(readback);
    device->resetEventQuery(complete);
}
```

Do not map until the query reports completion.

## Screenshot readback

1. Create a readback `IStagingTexture` matching the source.
2. Copy the desired source slice into it.
3. Submit and signal/poll an event.
4. Map the staging slice and obtain `rowPitch`.
5. Copy each image row separately.
6. Unmap and recycle.

If the back buffer is HDR, multisampled, or not in the desired file color space, render/compute into an intermediate texture first. `copyTexture` does no conversion.

## Render to one mip and sample another

```cpp
auto framebuffer = device->createFramebuffer(
    nvrhi::FramebufferDesc().addColorAttachment(
        nvrhi::FramebufferAttachment()
            .setTexture(texture)
            .setMipLevel(destinationMip)));

auto srv = nvrhi::BindingSetItem::Texture_SRV(
    0, texture, nvrhi::Format::UNKNOWN,
    nvrhi::TextureSubresourceSet(sourceMip, 1, 0, 1));
```

Select exact subresources in attachments and bindings. Avoid an SRV view that overlaps the currently written mip unless the algorithm/API explicitly allows it.

## Texture mip chain with compute

```cpp
for (uint32_t mip = 1; mip < mipCount; ++mip)
{
    // Cached set: source SRV mip-1, destination UAV mip.
    commandList->setComputeState(states[mip]);
    MipConstants c{ mip, dstWidth, dstHeight };
    commandList->setPushConstants(&c, sizeof(c));
    commandList->dispatch((dstWidth + 7) / 8, (dstHeight + 7) / 8, 1);
    nvrhi::utils::TextureUavBarrier(commandList, texture);
}
```

The barrier is necessary when the just-written mip becomes the next dispatch's source and state transitions do not otherwise supply the required ordering.

## GPU-generated indirect draw

1. Create argument and count buffers with UAV and indirect-argument capability.
2. Compute clears/writes packed `DrawIndexedIndirectArguments` records and count.
3. Add UAV ordering.
4. Transition both to `IndirectArgument`.
5. Set `GraphicsState::indirectParams` and `indirectCountBuffer`.
6. Call `drawIndexedIndirectCount`.

On D3D11 the count path falls back to `maxDrawCount`; make unused records harmless or use a CPU-compatible fallback.

## Swap-chain resize

```text
pause frame submission
→ wait for old back-buffer users
→ release NVRHI framebuffers
→ release wrapped back-buffer textures
→ native swap-chain resize/recreate
→ wrap new images with accurate descriptors
→ recreate framebuffers and size-dependent depth/color resources
→ update pipeline variants only if formats/sample counts changed
→ resume
```

Dimensions do not affect `FramebufferInfo`; format or sample changes do.

## Graphics pipeline cache key

Include:

- shader identities/versions;
- input layout and primitive/patch configuration;
- full blend, depth/stencil, raster, stereo, and VRS state;
- ordered binding-layout identities;
- framebuffer color/depth formats and sample count/quality.

Exclude viewport dimensions and concrete framebuffer identity.

## Safe bindless descriptor replacement

Use frame-versioned descriptor regions:

```text
frame N writes region N mod framesInFlight
→ GPU reads only that region for frame N
→ region is reused only after frame N completion
```

Keep a strong handle registry for every live descriptor. When replacing index `i`, retire the old handle only after all frames that could read old `i` complete.

## One-time BLAS/TLAS build

```cpp
auto buildList = device->createCommandList(
    nvrhi::CommandListParameters()
        .setScratchChunkSize(4 * 1024 * 1024)
        .setScratchMaxMemory(scratchBudget));

buildList->open();
for (auto& mesh : meshes)
    buildList->buildBottomLevelAccelStruct(
        mesh.blas, mesh.geometries.data(), mesh.geometries.size());
buildList->buildTopLevelAccelStruct(tlas, instances.data(), instances.size());
buildList->close();

device->executeCommandList(buildList);
```

Wait/poll before first consumption if it is on another queue. Release the dedicated build list after completion to free its peak scratch working set.

## Fullscreen presentation pass

Prefer a fullscreen triangle over a raw texture copy when the source requires:

- scaling;
- tone mapping;
- gamma/sRGB conversion;
- channel remapping;
- filtering;
- UI composition; or
- debug visualization.

Bind the source SRV and sampler, render to the swap-chain framebuffer, and let automatic state tracking return the back buffer to its configured `Present` initial state on close.

