# ArdaScene Ray-Traced Scene Architecture Plan

## Status and scope

This document is an architectural plan. It does not define an implemented API
and does not authorize coupling `ArdaScene` to Unreal Engine.

The target renderer is:

- purely ray traced;
- driven by a custom RHI and ArdaRenderGraph;
- based on ReSTIR GI and temporal reuse;
- able to run as a standalone Ardashir application;
- able to share Unreal Engine's D3D12 device and consume Unreal-owned GPU
  resources without mandatory copying or repacking;
- independent of Unreal's raster passes and frame graph.

The initial interop target is Unreal Engine on D3D12. Vulkan interop is a later
backend implementation of the same contracts.

## Primary decision

`ArdaScene` will be an engine-neutral, renderer-facing scene database. It will
not be:

- a gameplay ECS;
- an Unreal object mirror;
- an owner of NVRHI or RDG objects;
- a list of RHI command callbacks;
- a fixed GPU struct layout that every host must repack into.

It will publish immutable snapshots containing semantic scene records and
versioned views of resource data. A separate GPU scene layer will resolve each
record to either:

1. Ardashir-owned resources in standalone mode; or
2. borrowed, externally owned resources in Unreal mode.

![Overall architecture](Architecture-Overview.svg)

This separation is what keeps standalone operation first-class. Unreal is one
producer of `FArdaSceneUpdateBatch` records, not the owner of the scene model.

## Design principles

### Standalone and hosted execution are peers

The core renderer receives the same `FArdaSceneSnapshot` and
`FArdaGpuSceneView` in both modes. Only resource production differs:

- `FArdaNativeSceneProducer` creates semantic records and Ardashir-owned GPU
  resources.
- `FArdaUnrealSceneExtractor` creates the same semantic records while referring
  to Unreal-owned buffers, textures, and optional acceleration structures.

No `#if UNREAL`, `FRHI*`, `UObject`, or Unreal layout definition is allowed in
`Source/ArdaScene`.

### Scene semantics and physical storage are separate

A geometry record means "these triangles with these material segments." It
does not imply that positions, indices, transforms, or materials use an
Ardashir-specific buffer layout.

Physical data is described through:

- a stable resource identity;
- a typed resource view;
- a versioned structure layout;
- semantic fields such as `Position`, `PreviousTransform`, or
  `MaterialIndex`.

The canonical Ardashir layout remains the fast path. Foreign layouts are a
supported path, not the common denominator forced on standalone execution.

### Zero-copy is a capability, not a promise

The boundary must be able to consume a compatible foreign layout without a GPU
copy. It must also reject an incompatible declaration clearly.

It must not silently allocate a canonical buffer and repack the source. A
conversion is permitted only when an explicit conversion policy or pass has
been selected by the renderer.

Zero-copy still requires small CPU-side declarations, handle tables, layout
metadata, descriptor updates, and synchronization.

### Stable handles, immutable snapshots

External pointers and RDG references are not stable scene identities.
Generational handles identify semantic records. Every mutable source publishes
a new immutable snapshot and a deterministic change set.

### Rendering logic stays in the renderer

The scene declares data and changes. `ArdaGI` and the ray-traced renderer own
the sequence of:

- GPU scene updates;
- BLAS/TLAS construction;
- primary visibility;
- candidate generation;
- temporal and spatial reservoir reuse;
- final shading;
- denoising;
- presentation.

Host callbacks that must participate in the frame are optional interop work
providers. They are not serialized into scene records.

## Proposed module topology

### `ArdaScene`

Purpose:

- semantic object identity;
- geometry, instance, material, light, environment, and view records;
- resource and layout declarations;
- update transactions and dirty tracking;
- immutable render snapshots.

Public dependencies:

- EASTL;
- project-owned math or utility types only.

Forbidden public dependencies:

- NVRHI;
- ArdaBackend;
- ArdaRenderGraph;
- Unreal Engine.

The current public `nvrhi` dependency in `Source/ArdaScene/CMakeLists.txt` is
therefore planned to be removed when the module is implemented.

### `ArdaInterop`

Purpose:

- external device and queue capabilities;
- stable foreign resource registry;
- borrowed-resource lifetime tokens;
- host-to-Arda and Arda-to-host synchronization;
- optional host work providers.

This module is host-neutral. Unreal implements its interfaces, but the
interfaces do not mention Unreal types.

### `ArdaSceneGPU` or `ArdaRenderer`

Purpose:

- NVRHI resource ownership;
- import of external resources into each Arda render graph;
- bindless descriptor tables;
- persistent GPU scene tables;
- acceleration structure management;
- translation from a scene snapshot to graph declarations;
- validation of foreign layouts and resource revisions.

Dependencies:

- ArdaScene;
- ArdaInterop;
- ArdaBackend;
- ArdaRenderGraph;
- NVRHI.

### `ArdaGI`

Purpose:

- ReSTIR GI algorithms;
- reservoir and radiance history;
- path-tracing pipelines and shader tables;
- temporal validity policy;
- denoising and reconstruction.

`ArdaGI` consumes `FArdaGpuSceneView`. It must not own the scene database or
contain Unreal extraction code.

### `Integrations/Unreal/ArdaUnreal`

Purpose:

- shared Unreal D3D12 device and queue host;
- `FRHITexture` and `FRHIBuffer` wrapping;
- Unreal scene extraction;
- Unreal GPU layout descriptions;
- Unreal material adaptation;
- frame-entry integration and fence handoff.

This is the only layer that includes Unreal headers. It is built by Unreal
Build Tool and remains optional to the standalone CMake build.

## Source layout plan

The following names are proposed API boundaries, not files to implement as part
of this plan.

```text
Source/
  ArdaScene/
    Public/
      ArdaScene.h
      ArdaSceneHandles.h
      ArdaSceneMath.h
      ArdaSceneResources.h
      ArdaSceneGeometry.h
      ArdaSceneMaterials.h
      ArdaSceneLights.h
      ArdaSceneView.h
      ArdaSceneUpdates.h
      ArdaSceneSnapshot.h
      ArdaSceneDatabase.h
    Private/
      ArdaSceneDatabase.cpp
      ArdaSceneRegistry.h

  ArdaInterop/
    Public/
      ArdaInterop.h
      ArdaExternalDeviceHost.h
      ArdaExternalResourceRegistry.h
      ArdaGpuSynchronization.h
      ArdaExternalWorkProvider.h
      ArdaFeatureCapabilities.h

  ArdaSceneGPU/
    Public/
      ArdaGpuScene.h
      ArdaGpuSceneView.h
    Private/
      ArdaBindlessRegistry.*
      ArdaAccelerationStructureManager.*
      ArdaGpuSceneUploader.*
      ArdaExternalResourceResolver.*

Integrations/
  Unreal/
    ArdaUnreal/
      Source/ArdaUnreal/
        Public/
        Private/
```

## Handle and identity model

Scene handles must be typed and generational:

```cpp
template <typename TagType>
struct TArdaSceneHandle
{
    uint32_t mIndex;
    uint32_t mGeneration;
};

using FArdaGeometryHandle = TArdaSceneHandle<FArdaGeometryTag>;
using FArdaInstanceHandle = TArdaSceneHandle<FArdaInstanceTag>;
using FArdaMaterialHandle = TArdaSceneHandle<FArdaMaterialTag>;
using FArdaLightHandle = TArdaSceneHandle<FArdaLightTag>;
using FArdaResourceHandle = TArdaSceneHandle<FArdaResourceTag>;
using FArdaLayoutHandle = TArdaSceneHandle<FArdaLayoutTag>;
```

The existing `TARDGHandle` is a useful type-safety precedent, but it is scoped
to an append-only graph registry. Scene handles add a generation because scene
slots are removed and reused over many frames.

Every foreign resource also carries:

```cpp
struct FArdaExternalResourceIdentity
{
    FArdaSourceId mSource;
    FArdaExternalResourceId mResource;
    uint64_t mAllocationRevision;
    uint64_t mContentRevision;
};
```

- `mSource` distinguishes standalone, Unreal, and future producers.
- `mResource` is stable across frames and is not a native pointer.
- `mAllocationRevision` changes when native allocation or descriptor identity
  changes.
- `mContentRevision` changes when contents change without reallocation.

## Resource declarations

### Resource ownership

```cpp
enum class EArdaResourceOwnership
{
    ArdaOwned,
    HostBorrowed,
    SharedPersistent
};
```

- `ArdaOwned`: allocated and retired by `ArdaSceneGPU`.
- `HostBorrowed`: owned by Unreal or another host; Arda only retains a wrapper
  and lifetime token.
- `SharedPersistent`: allocated through an agreed shared service and retained
  across frames, with explicit destruction authority.

### Buffer and texture views

`FArdaBufferView` contains:

- `FArdaResourceHandle`;
- byte offset and size;
- stride and element count;
- typed, structured, or raw interpretation;
- optional format;
- access capability;
- associated `FArdaLayoutHandle`.

`FArdaTextureView` contains:

- resource handle;
- format;
- dimension;
- mip range;
- array range;
- intended semantic.

Views refer to scene resource identities. They do not contain NVRHI handles,
RDG references, or native API objects.

### Structure layout descriptors

`FArdaStructLayoutDesc` is a versioned shader-visible schema:

```cpp
enum class EArdaDataSemantic
{
    Position,
    Normal,
    Tangent,
    TexCoord0,
    CurrentTransform,
    PreviousTransform,
    MaterialIndex,
    GeometryIndex,
    InstanceMask,
    Custom
};

struct FArdaStructFieldDesc
{
    EArdaDataSemantic mSemantic;
    uint32_t mByteOffset;
    EArdaDataFormat mFormat;
    uint32_t mArrayCount;
};
```

The descriptor also records stride, alignment, endianness, matrix convention,
and a layout version/hash.

Standalone resources normally use compile-time canonical layouts. Unreal
resources may use adapter-supplied layout descriptors and shader permutations
or generated accessors.

All foreign layout assumptions are validated when the adapter starts and when
an allocation revision changes.

## Scene record types

### Geometry

`FArdaGeometryRecord` describes traceable geometry:

- geometry kind: triangles, procedural AABBs, curves, spheres, or extension;
- vertex and optional index views;
- position layout and optional attribute layouts;
- primitive count;
- topology and winding;
- object-space bounds;
- material segments;
- opacity and any-hit policy;
- BLAS build policy;
- residency and revision.

`FArdaGeometrySegment` describes a primitive range, material slot, geometry
flags, and hit-group category. Multiple segments can share the same buffers and
BLAS.

`EArdaBlasPolicy`:

- `StaticCompact`;
- `DynamicRefit`;
- `DynamicRebuild`;
- `External`.

### Instances

`FArdaInstanceRecord` contains:

- stable instance identity;
- geometry handle;
- current and previous transforms;
- optional foreign transform view and element index;
- material override;
- ray visibility mask;
- instance flags;
- SBT category or hit-group offset;
- producer payload ID;
- world bounds;
- motion classification;
- independent transform, material, and visibility revisions.

The record supports two transform sources:

1. inline CPU-authored transforms for ordinary standalone scenes;
2. a GPU-resident transform buffer view for GPU-driven or Unreal scenes.

The second path enables GPU generation of TLAS instance descriptors without
reading transforms back to the CPU or repacking them.

### Materials

An Unreal material graph cannot generally be represented by a fixed PBR
structure. The scene model must therefore support several binding strategies:

```cpp
enum class EArdaMaterialBindingMode
{
    CanonicalClosure,
    ForeignDataLayout,
    ForeignShaderBinding,
    Fallback
};
```

- `CanonicalClosure`: Ardashir closure parameters and bindless texture IDs.
- `ForeignDataLayout`: an external material buffer plus a semantic layout,
  evaluated by Arda hit shaders.
- `ForeignShaderBinding`: a host-compatible hit-group/shader identity and its
  resources.
- `Fallback`: an explicit diagnostic material.

`FArdaMaterialRecord` contains the mode, shader or closure identity, parameter
views, texture/sampler identities, opacity mode, emissive metadata, and
revision.

The initial standalone renderer should use `CanonicalClosure`. The first Unreal
adapter can translate a supported subset into canonical closures. Direct
compatible Unreal hit shaders are a later capability and must not shape the
core scene API.

### Lights and emissive geometry

`FArdaLightRecord` uses a tagged representation for:

- directional;
- point;
- spot;
- rectangle;
- disk;
- environment;
- mesh emissive.

Mesh emissives reference instance, geometry segment, and material identities.
They do not duplicate triangle data.

Sampling-specific data structures such as CDFs, light trees, and ReSTIR
candidate tables belong to `ArdaSceneGPU` or `ArdaGI`, because they are derived
GPU representations rather than scene truth.

### Views

`FArdaSceneView` contains:

- current and previous view/projection matrices;
- inverse matrices;
- world-space camera origin;
- viewport and output extent;
- jitter and frame index;
- aperture, focus distance, exposure, and clipping policy;
- history validity and camera-cut flags.

Reservoirs, radiance history, variance, and denoiser history remain in
`ArdaGI`.

## Update, snapshot, and threading model

![Scene publication and frame flow](Frame-Flow.svg)

The mutable scene database is updated through transactions:

```cpp
class FArdaSceneDatabase
{
public:
    [[nodiscard]] FArdaSceneUpdateWriter BeginUpdate();
    [[nodiscard]] FArdaSceneSnapshotRef Publish(
        FArdaSceneUpdateWriter&& Update);
};
```

`FArdaSceneUpdateWriter` supports deterministic create, update, and remove
operations. Publishing produces:

- a new immutable `FArdaSceneSnapshot`;
- an `FArdaSceneChangeSet`;
- a monotonically increasing scene epoch.

Dirty categories are independent:

- resource allocation;
- geometry contents;
- geometry layout;
- instance transform;
- instance material;
- visibility;
- material data;
- material shader binding;
- light data;
- environment;
- view;
- residency.

The render thread retains snapshot N while the producer creates snapshot N+1.
Publication uses double buffering or RCU-style reference retirement. Graph
construction performs no per-record locking.

The snapshot provides:

- dense spans for geometry, instances, materials, lights, and views;
- handle-to-dense-index maps;
- the change set from the preceding epoch;
- external resource and layout declarations;
- source/provider identities;
- history invalidation flags.

## GPU scene realization

`FArdaGpuScene` is the physical bridge:

```cpp
class FArdaGpuScene
{
public:
    [[nodiscard]] FArdaGpuSceneUpdatePlan Prepare(
        const FArdaSceneSnapshot& Scene,
        IArdaExternalResourceRegistry& ExternalResources);

    void DeclareUpdatePasses(
        FARDGBuilder& Graph,
        const FArdaGpuSceneUpdatePlan& Plan);

    [[nodiscard]] FArdaGpuSceneView GetView() const;
};
```

`Prepare`:

- resolves Arda-owned and host-borrowed resources;
- validates allocation and layout revisions;
- creates or updates NVRHI wrappers;
- computes descriptor-table changes;
- determines upload ranges;
- chooses BLAS rebuild, refit, or reuse;
- chooses TLAS rebuild or update;
- computes history invalidation.

`DeclareUpdatePasses`:

- imports persistent and borrowed resources into the frame graph;
- uploads only dirty Ardashir-owned records;
- updates bindless tables safely;
- declares AS build work;
- creates the frame's traceable scene view.

`FArdaGpuSceneView` exposes:

- TLAS;
- geometry metadata table;
- material table;
- light/emissive table;
- layout table;
- bindless descriptor tables;
- current and previous instance mappings;
- scene epoch and history validity.

## Acceleration structure ownership

The preferred first Unreal path is:

- borrow Unreal vertex, index, and instance-data buffers;
- build Ardashir-owned BLAS and TLAS from those buffers;
- keep Ardashir instance IDs, masks, hit groups, and update policy.

This avoids copying geometry while keeping the ray-tracing representation
under control of the custom renderer.

`FArdaAccelerationStructureManager` owns:

- BLAS cache keyed by geometry handle, revision, and policy;
- BLAS compaction and refit policy;
- TLAS instance descriptor generation;
- TLAS update/rebuild policy;
- scratch buffer pools;
- referenced BLAS retention until GPU completion;
- optional external-AS providers.

Importing Unreal's existing acceleration structures is a later optimization.
It requires a reliable native AS wrapping path and first-class AS tracking in
ArdaRenderGraph.

## Required ArdaRenderGraph extensions

The existing graph already imports external textures and buffers and recognizes
acceleration-structure-related buffer states. Production ray tracing should add:

- `FARDGAccelStruct`;
- `EARDGResourceType::AccelStruct`;
- `RegisterExternalAccelStruct`;
- AS extraction and lifetime tracking;
- acceleration-structure parameter metadata;
- validated `GetAccelStruct`;
- AS build/read transitions;
- a ray-tracing dispatch pass category or explicit compute/RT pipeline type;
- bindless descriptor-table declarations.

Raw command-list passes may prototype AS work, but they are transitional
because the graph cannot prove all resource accesses or barriers.

## Standalone execution path

Standalone mode must be implemented and tested before Unreal integration.

The native frame path is:

1. `FArdaNativeSceneProducer` updates `FArdaSceneDatabase`.
2. The database publishes an immutable snapshot.
3. `FArdaNativeResourceRegistry` resolves Ardashir-owned resources.
4. `FArdaGpuScene` realizes the snapshot on the device created by
   `ArdaBackend`.
5. The custom renderer builds ArdaRenderGraph passes.
6. `ArdaGI` traces and resolves the image.
7. Arda's existing swap-chain abstraction presents it.

No external resource registry entries are required for a wholly native scene,
although the same interface can be used for application-owned streaming
resources.

## Unreal shared-device path

![Unreal shared-device interop](Unreal-Interop.svg)

### Device ownership

Unreal owns the D3D12 device and queues. Ardashir must not call its normal
process-global device creation path inside Unreal.

`ArdaBackend` needs a host-provided context:

```cpp
struct FArdaSharedDeviceDesc
{
    nvrhi::DeviceHandle mDevice;
    EArdaBackendType mBackend;
    FArdaQueueCapabilities mQueues;
    EArdaDeviceOwnership mOwnership;
};
```

The concrete Unreal layer constructs or receives an NVRHI wrapper using
Unreal's `ID3D12Device` and command queues. The host outlives every Arda wrapper
and in-flight submission.

The long-term backend API should become instance-oriented rather than relying
only on process-global initialization, but standalone initialization remains
available.

### External resource registry

`FArdaUnrealResourceImporter` implements
`IArdaExternalResourceRegistry`.

For buffers and textures it:

1. obtains the native `ID3D12Resource`;
2. creates an accurate NVRHI descriptor;
3. wraps the resource without allocating or copying it;
4. caches by stable Unreal identity, native pointer, and allocation revision;
5. returns the current wrapper plus a borrowed lifetime token.

Each Arda frame imports the wrappers through the existing
`RegisterExternalBuffer` and `RegisterExternalTexture` graph APIs.

### Resource state and synchronization

Sharing a device does not order independent command lists.

Every host-borrowed binding declares:

- initial resource state;
- final resource state;
- producing queue;
- consuming queue;
- host completion token;
- Arda completion token;
- subresource or byte range when relevant.

`IArdaGpuSyncHost` provides:

- host-to-Arda waits before first consumption;
- Arda-to-host completion values after submission;
- deferred release after the relevant queue completion.

`FARDGExecutionResult::mLastSubmittedInstances` is the existing Arda-side
submission token precedent. The Unreal adapter maps these instances to UE RHI
fence ordering.

### Unreal scene extraction

`FArdaUnrealSceneExtractor` maps Unreal renderer state into update batches:

- proxy/component identity to stable Arda handles;
- static and dynamic geometry to buffer views;
- instance transforms to inline values or foreign GPU layouts;
- materials to supported binding strategies;
- analytic lights and emissives to light records;
- removals and resource recreation to revisions;
- camera cuts and scene changes to history invalidation.

All UE-version-specific offsets and GPU Scene assumptions stay in the adapter.
They are checked against the expected Unreal version and layout hash.

### Material adaptation

The Unreal adapter initially supports:

1. translation of a documented material subset to `CanonicalClosure`;
2. explicit fallback material for unsupported graphs.

Later it may support:

- direct reads from compatible Unreal material buffers;
- Unreal-generated ray-tracing hit shaders;
- Substrate closure translation.

The adapter must never claim that arbitrary Unreal material graphs can be
consumed as simple parameter buffers.

## Rendering-call interoperability

Unreal RHI commands must not be captured inside `FArdaSceneSnapshot`.

If the host must contribute work, it implements:

```cpp
class IArdaExternalWorkProvider
{
public:
    virtual void DeclareWork(
        EArdaFramePhase Phase,
        IArdaRenderGraphBridge& Bridge,
        const FArdaSceneSnapshot& Scene) = 0;
};
```

Suggested phases:

- `BeforeSceneUpdate`;
- `AfterSceneUpdate`;
- `BeforeTrace`;
- `AfterTrace`;
- `BeforePresent`.

The bridge accepts resource and access declarations plus an integration-owned
record callback. Arda remains responsible for overall ordering and graph
execution.

The initial pure ray-traced Unreal path should not import Unreal rendering
calls. It should import scene resources and changes, then let Arda own the
entire image-generation pipeline.

## ReSTIR GI-specific scene requirements

The scene/GPU scene contract must provide:

- stable instance and primitive IDs across frames;
- current and previous transforms;
- material and opacity identity;
- analytic and emissive light revisions;
- environment revision;
- TLAS revision;
- camera-cut and history validity flags;
- previous dense-index mappings when compaction changes;
- motion classification;
- residency changes.

`ArdaGI` derives its own:

- initial sample reservoirs;
- temporal reservoirs;
- spatial reservoirs;
- visibility cache;
- radiance/normal/depth history;
- disocclusion and rejection masks.

Any change that invalidates sample identity is represented by an explicit
history invalidation reason, not a single undifferentiated dirty flag.

## Validation and failure policy

Validation occurs before graph construction:

- every handle generation is current;
- every required resource resolves;
- every foreign wrapper matches its allocation revision;
- every required semantic exists in the selected layout;
- resource states and queue ownership are known;
- BLAS inputs are resident and compatible;
- material binding mode is supported;
- retained lifetime covers the submitted GPU work.

Failure modes are explicit:

- reject frame;
- omit primitive with diagnostic;
- use fallback material;
- choose an explicitly configured conversion pass.

Silent repacking is not a fallback.

## Testing plan

### `ArdaScene` unit tests

- stale handles are rejected;
- slot reuse increments generation;
- transactions produce deterministic change sets;
- snapshots remain immutable during later updates;
- removals do not alias live handles;
- previous transforms and camera cuts invalidate history correctly;
- dense remapping is deterministic.

### Standalone integration tests

- a native triangle scene builds BLAS/TLAS and traces without Unreal;
- static geometry reuses BLAS;
- transform-only change updates TLAS;
- material-only change does not rebuild BLAS;
- resize and camera cut invalidate the correct histories;
- standalone swap-chain presentation remains functional.

### Interop contract tests

- mock borrowed resources are never destroyed by Arda;
- allocation revision replaces cached wrappers;
- content revision avoids unnecessary wrapper recreation;
- lifetime tokens survive simulated GPU completion;
- missing foreign semantics fail rather than trigger a copy;
- state and queue handoffs are balanced.

### GPU scene and RDG tests

- external resources deduplicate on import;
- dirty uploads cover only changed Arda-owned ranges;
- foreign layouts create no copy pass;
- AS resources participate in culling and barriers;
- BLAS refit/rebuild choices are deterministic;
- TLAS retains all referenced BLAS through completion;
- descriptor slots retire after frames in flight.

### Unreal smoke tests

- the NVRHI wrapper and Unreal use the same D3D12 device;
- an Unreal-owned texture and vertex/index buffers are consumed without copy;
- resource recreation updates wrappers safely;
- one static mesh, instance, material, analytic light, and camera render through
  Arda;
- GPU captures show correct UE-to-Arda and Arda-to-UE synchronization;
- disabling the Unreal plugin has no effect on standalone builds.

## Delivery phases

### Phase 1: engine-neutral scene core

Define and test:

- generational handles;
- resource views and data layouts;
- geometry, instance, material, light, and view records;
- update transactions;
- immutable snapshots and change sets.

Exit criterion: two independent producers can create equivalent snapshots
without NVRHI or Unreal dependencies.

### Phase 2: standalone GPU scene

Define and test:

- Ardashir-owned resource registry;
- bindless registry;
- GPU scene tables;
- BLAS/TLAS manager;
- snapshot-to-RDG update planning.

Exit criterion: a standalone ray-traced scene renders through ArdaBackend and
ArdaRenderGraph.

### Phase 3: first-class RDG ray tracing

Add:

- acceleration-structure resources;
- AS transitions and validation;
- RT bindings;
- RT dispatch support;
- descriptor-table declarations.

Exit criterion: no production RT pass relies on undeclared side-band resources.

### Phase 4: ReSTIR GI

Implement the algorithm only after the scene contract is stable:

- primary visibility;
- candidate generation;
- temporal reuse;
- spatial reuse;
- final shading;
- denoising and presentation.

Exit criterion: stable standalone temporal rendering across geometry, material,
light, camera, and resize changes.

### Phase 5: Unreal D3D12 bridge

Add:

- shared-device host;
- resource importer;
- synchronization bridge;
- scene extractor;
- canonical material subset;
- custom renderer frame entry.

Exit criterion: static Unreal geometry renders through Arda with no geometry
buffer copy and with validated queue/resource-state ownership.

### Phase 6: advanced Unreal paths

Add incrementally:

- direct Unreal GPU Scene layouts;
- GPU-driven TLAS instance generation;
- dynamic/skinned geometry;
- richer material translation or compatible hit shaders;
- optional external AS import;
- Niagara, hair, landscape, and specialized geometry adapters.

### Phase 7: Vulkan interop

Implement the same device, resource, semaphore, and ownership contracts for
Vulkan after the D3D12 model is proven.

## Final dependency rules

The intended dependency direction is:

```text
ArdaScene
    ^
    |
ArdaInterop      ArdaBackend      ArdaRenderGraph
    ^                 ^                 ^
    |                 |                 |
    +----------- ArdaSceneGPU ----------+
                      ^
                      |
                    ArdaGI

Integrations/Unreal/ArdaUnreal
    implements ArdaInterop and feeds ArdaScene;
    it depends on the renderer stack, never the reverse.
```

The standalone executable uses the same stack without
`Integrations/Unreal/ArdaUnreal`.

## Related existing foundations

- `Source/ArdaBackend/Public/ArdaDevice.h`: current device and queue context.
- `Source/ArdaRenderGraph/Public/ArdaRenderGraphBuilder.h`: external
  buffer/texture import and execution results.
- `Source/ArdaRenderGraph/Public/ArdaRenderGraphResources.h`: current
  external/transient resource ownership model.
- `Source/ArdaRenderGraph/Public/ArdaRenderGraphPass.h`: validated physical
  access during pass recording.
- `Docs/NVRHI/07-Ray-Tracing.md`: NVRHI acceleration structure and RT pipeline
  model.
- `Docs/NVRHI/09-Backends-and-Interop.md`: native resource wrapping and backend
  interoperability.

