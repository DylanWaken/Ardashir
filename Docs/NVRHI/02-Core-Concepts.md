# Core Concepts

[Previous](01-Getting-Started.md) · [Home](README.md) · Next: [Resources and memory](03-Resources-and-Memory.md)

## Object model and handles

Every GPU-facing NVRHI object derives from `IResource`, which provides intrusive `AddRef`/`Release` reference counting and native-object access. Use typed handles such as `TextureHandle`, `BufferHandle`, `GraphicsPipelineHandle`, and `CommandListHandle`; they are `RefCountPtr<T>` aliases.

```cpp
nvrhi::TextureHandle texture = device->createTexture(desc);
nvrhi::ITexture* borrowed = texture; // no ownership transfer
```

Important consequences:

- Copying a handle keeps the object alive.
- Binding sets strongly reference their resources by default.
- Command-list submission keeps referenced objects alive until GPU use ends.
- Releasing the last application handle does not necessarily destroy a native object immediately.
- `runGarbageCollection()` polls completed work and performs safe deferred destruction.
- Cycles are not expected in normal NVRHI object relationships; do not build application-level ownership cycles around handles.

Keep long-lived handles as members of the pass or subsystem that owns their logical use. Local handles are also safe for one-shot work because submitted command lists retain what they need.

## Descriptors are the vocabulary

Creation uses value-like descriptor structures:

```cpp
auto desc = nvrhi::TextureDesc()
    .setWidth(1920)
    .setHeight(1080)
    .setFormat(nvrhi::Format::RGBA16_FLOAT)
    .setIsRenderTarget(true)
    .setIsUAV(true)
    .setDebugName("HDR color");
```

Most descriptors have defaults and fluent setters. Do not assume the defaults match your pass:

- `TextureDesc::isShaderResource` defaults to true.
- `BufferDesc::initialState` defaults to `Common`.
- `GraphicsPipelineDesc` defaults to `TriangleList`.
- `DepthStencilState` defaults to depth test and writes enabled.
- `CommandListParameters` defaults to a graphics queue and immediate execution.

Creation objects generally retain a copy of their descriptor, available through `getDesc()`. Descriptors that contain raw pointers are copied according to each API's contract; do not assume arbitrary pointed-to application memory is retained.

## Immutable objects, dynamic state

NVRHI moves expensive or highly structured configuration into immutable objects:

- binding layouts and regular binding sets;
- framebuffers;
- graphics, compute, meshlet, and ray-tracing pipelines;
- samplers and input layouts.

Command-time state structures combine those objects with dynamic values:

- `GraphicsState`: pipeline, framebuffer, viewport/scissor, bindings, vertex/index buffers, blend constant, stencil reference, and indirect buffers;
- `ComputeState`: pipeline, bindings, and indirect argument buffer;
- `MeshletState`: meshlet pipeline, framebuffer, viewport, bindings, and indirect buffers;
- `rt::State`: shader table and global bindings.

Calling one `set*State` invalidates the previously active pipeline kind. Set the complete state again when switching from compute to graphics, graphics to ray tracing, and so on.

## Command-list lifecycle

```text
created → open → record operations → close → submit
                    ↑                         |
                    └──── open and reuse ─────┘
```

Rules:

- Record only while open.
- Submit only after close.
- A command-list object can be reopened immediately after submission; modern backends internally rotate native command lists/buffers.
- Multiple command lists can be recorded concurrently.
- Queue-limited command lists may only use operations legal for that queue.
- `clearState()` resets NVRHI's state cache after direct native command recording.

`writeBuffer`, `writeTexture`, and acceleration-structure builds allocate from managers owned by or associated with a command list. Those managers reuse memory but do not shrink their working sets. Use and then destroy a dedicated upload/build command list after exceptional bulk work.

## State setting and ordering

The safe order for pipeline work is:

```cpp
commandList->open();

// Volatile CB must be written in each opened command-list instance.
commandList->writeBuffer(volatileConstants, &constants, sizeof(constants));

commandList->setComputeState(computeState);

// Push constants are valid only after set*State and are invalidated by a new state.
commandList->setPushConstants(&smallConstants, sizeof(smallConstants));
commandList->dispatch(groupsX, groupsY, 1);

commandList->close();
```

Between `set*State` and draw/dispatch, limit operations to volatile-buffer writes and `setPushConstants`. Other state-setting or transfer operations may invalidate assumptions and barriers.

## Resource-state model

Modern APIs require states such as `RenderTarget`, `ShaderResource`, `UnorderedAccess`, `CopySource`, and `CopyDest`. NVRHI can track and transition resources automatically, but it needs a known state at each command-list boundary.

Choose one boundary policy per resource:

1. **Keep initial state:** set `initialState` and `keepInitialState = true`; each command list assumes that entry state and restores it on close.
2. **Explicit tracking:** call `beginTrackingTextureState` or `beginTrackingBufferState` after open, then leave it in an explicitly chosen state.
3. **Permanent state:** after initialization call `setPermanentTextureState` or `setPermanentBufferState`; future compatible uses skip tracking.

`Unknown` is metadata meaning “the tracker does not know.” It is not a usable hardware state.

## Automatic versus manual barriers

Automatic barriers are enabled by default. NVRHI derives required states from:

- framebuffer attachments;
- binding-set resource types;
- vertex/index/indirect buffers;
- copy, write, clear, resolve, RT build, and conversion operations.

Manual mode:

```cpp
commandList->setEnableAutomaticBarriers(false);
commandList->setTextureState(output, nvrhi::AllSubresources,
                             nvrhi::ResourceStates::UnorderedAccess);
commandList->setResourceStatesForBindingSet(bindingSet);
commandList->commitBarriers();
```

Manual mode is an optimization and control tool, not a recommended starting point. Validation cannot infer every bindless or native-interop access.

## Binding model

A regular shader interface has two symmetric objects:

```text
BindingLayout: slot/type/visibility declaration
       ↕ exact compatibility
BindingSet:    slot/type/concrete resource
```

Pipelines store layouts. State objects supply sets in the same order. Regular sets are immutable and can provide liveness and barrier information. Bindless descriptor tables are mutable and intentionally omit those safety services.

See [Bindings and shaders](04-Bindings-and-Shaders.md) for the complete model.

## Native interop

All resources support `getNativeObject(ObjectType)`. The device also exposes native queues, and textures can return native views.

Use interop for extensions or commands NVRHI does not expose. Observe these rules:

1. Transition resources into the states expected by native code and commit barriers.
2. Record native commands on the native command list associated with the open NVRHI command list.
3. Do not alter objects NVRHI assumes are immutable.
4. Call `clearState()` before returning to NVRHI state setting.
5. Restore or communicate final resource states to NVRHI.
6. Keep native objects alive through submitted execution.

## Feature queries are part of initialization

Backend selection is not enough to prove support. Query each optional path:

```cpp
if (device->queryFeatureSupport(nvrhi::Feature::Meshlets))
    CreateMeshletPath();
else
    CreateIndexedPath();
```

Some queries return data through a correctly typed output structure:

```cpp
nvrhi::VariableRateShadingFeatureInfo info{};
bool supported = device->queryFeatureSupport(
    nvrhi::Feature::VariableRateShading, &info, sizeof(info));
```

Also use `queryFormatSupport(format)` before relying on format-specific render-target, sampling, UAV, atomic, or buffer behavior.

## Error model

Many creation functions return an empty handle on failure and report details through the message callback. Some methods return `bool`; many command-recording methods return `void` and diagnose misuse through validation/messages.

Production code should:

- check every creation result;
- make validation errors fatal in tests;
- handle `waitForIdle() == false` as a device problem;
- treat unsupported optional features as controlled fallback, not exceptional failure; and
- preserve enough context in debug names and markers to identify the failing pass.

## Design guidance

- Keep native-device/swap-chain management behind a backend boundary.
- Keep pass code backend-independent and consume `IDevice`, resource handles, and framebuffer information.
- Cache immutable layouts, sets, samplers, and pipelines.
- Use one explicit owner for each mutable descriptor table.
- Separate initialization uploads from per-frame command lists.
- Express cross-pass dependencies in a render graph or equivalent scheduler; NVRHI tracks low-level states, not high-level pass ordering.
- Keep frame-in-flight ownership visible: per-frame constants, queries, and readback slots should be indexed by frame.

