# Modular graph examples

Terrain (`ARDGExample`), Cornell Box, and Pixel Sort use the persistent dependency
graph. The triangle sample in `../RHITest` follows the same layout:

```text
Public/Nodes/       Individual node classes and their logical parameters
Private/Nodes/      Shader registration, layouts, node descriptions, and recording
Private/Nodes/Shaders/  HLSL sources used by the node implementations
```

Pixel Sort also keeps its shared CUDA operand schema in `Public/Nodes` and its
host variant selector and compiled `.cu` kernels in `Private/Nodes`. CMake deploys
its shader assets beneath `Nodes/Shaders` beside the executable.

Each operation has its own header and implementation file. Its class derives from
`TArdaDependencyNode<Derived, Parameters, Kind>` through a domain specialization
such as `TArdaComputeDependencyNode`. The base exposes `FParameters` and implements
registration and attachment. The
renderer includes the selected node headers (or an include-only umbrella) and
calls the graph's typed attachment API inside an edit:

```cpp
auto Generate = Graph.AttachOrFind<FArdaTerrainGenerateNode>(
    "Generate", {Settings, RawHeightmap});
if (!Generate) return Generate.mStatus;
auto Erode = Graph.AttachOrFind<FArdaTerrainErodeNode>(
    "Erode", {RawHeightmap, Heightmap});
if (!Erode) return Erode.mStatus;
```

The surrounding renderer creates/imports those logical resources, ends the edit,
updates dynamic frame inputs and executes or submits the retained graph. There is
no node-library object or renderer-side registration/initialization call.

Each node implements `GetMetadata`, `GetCanonicalKey`, `Describe`, and `Record`
(or `PrepareCuda` for CUDA). The base checks these hooks at compile time and
registers a device-independent definition on first attachment. Nodes with setup
declare a private implementation of nested `FState` and supply `Prepare(Device)`
for their shaders, layouts, operand and pipeline configuration. Nodes with mutable
binding caches declare `FInstanceState` and supply `CreateInstance`. Stateless
defaults cover upload/copy nodes. Optional `Validate` checks inputs before setup.

The graph rejects attachment outside an edit, then resolves definition/name/key
identity before preparing any new instance. Identical reattachment skips setup;
conflicting semantics fail. The core base weakly caches successful immutable
state by node type/device; private bound parameters strongly retain it for
execution. Live graphs share device state and own separate mutable instances.
Retiring all instances releases the state. Failed setup is not cached and can be
retried. State selection is fixed by node type/device; parameter-dependent setup
belongs in `CreateInstance`, with GPU memory accounted for in `Describe`.

ArdaInductor creates/caches actual PSOs at `EndGraphEdit`, infers dependencies and
batches CUDA work. Graph replay does not rerun node setup or registry lookup.
Per-instance binding sets and the path tracer's shader table are also internal to
their nodes. Upload/readback nodes can attach without initializing unrelated shaders.

Camera/window control, resource sizing, graph composition, presentation, and CPU
verification belong in the renderer. Shader classes, binding metadata, kernel
selection, command recording, and fixed pipeline settings belong in each node's
implementation file.

Registration is per operation, not per renderer or whole application. Terrain
uses seven distinct parameter types for settings upload, camera upload,
generation, erosion, triangulation, drawing, and overlay composition. The settings
copy and two optional geometry readbacks use separate built-in nodes. Each compute
or graphics callback records one dispatch or draw (including that draw's attachment
clears); none hides the next stage's dispatch or an inter-stage barrier.

The terrain data flow is:

```text
settings upload -> settings copy -> generate -> raw heightmap -> erode
erode -> eroded heightmap -> triangulate -> vertices/indices -> draw
camera upload ------------------------------------------------> draw
eroded heightmap ----------------------------------------------> draw
draw -> scene color -> overlay -> swap-chain color
vertices -> vertex readback     indices -> index readback
```

Each logical output has one writer. Erosion reads the raw heightmap and writes
a separate eroded value; the overlay samples scene color and writes the back
buffer. This gives ArdaInductor visibility into intermediate lifetimes, barriers,
and scheduling. The extra intermediate textures are graph-owned transients and
participate in its memory plan; splitting operations does not promise zero extra
storage. Each operation's canonical key contains only its own resources/settings
so it can be attached independently. Device setup for these fixed operations is
determined by the node class and graph device, not exposed in renderer parameters.

The renderer attaches consumers before producers and calls `EndGraphEdit` once.
ArdaInductor resolves the edges and pipelines then; it does not infer dependencies
from attachment order. Frame input changes reuse the plan. First-frame readback
nodes are removed through an edit after numerical validation.

Cornell assembles geometry generation and the BLAS build in one setup graph.
Compaction needs the completed BLAS size on the CPU, so an optional second setup
graph contains the compact-BLAS node. Every frame graph contains a TLAS rebuild,
including frames after the sample limit is reached. TLAS storage is retained;
the build callback records a full build on every execution, while static BLAS and
geometry are reused. Ray nodes read the TLAS written by that build, establishing
the dependency automatically. Frame upload, ray dispatch, sample reduction, and
presentation are separate operations; presentation can be attached first.
Triangle's initial
frame graph contains independent vertex/index uploads feeding its draw. After
completion it removes the uploads and reattaches the draw against initialized
imports, because producer removal also removes dependent consumers. Subsequent
frames reuse draw-only graphs. Pixel Sort already exposes upload, noise compute,
CUDA sort, presentation, and each optional readback as individual nodes; its radix
kernel is one operator rather than an entire graphics/CUDA program.

See the [registration recipes](../../../Docs/ArdaRDG/quick-guide.html#node-registration) for native compute, indexed raster, fullscreen graphics, ray tracing and CUDA nodes, including an authored CUDA class. They show optional library discovery before devices exist and automatic registration during typed attachment.

Start with these implementations:

- [Terrain nodes](ARDGExample/Public/Nodes/ArdaTerrainNodes.h) and
  [graph composition](ARDGExample/Private/ArdaTerrainRenderer.cpp).
- [Cornell nodes](CornellBox/Public/Nodes/ArdaCornellBoxNodes.h) and
  [graph composition](CornellBox/Private/ArdaCornellBoxRenderer.cpp).
- [Pixel Sort nodes](PixelSort/Public/Nodes/ArdaPixelSortNodes.h) and
  [graph composition](PixelSort/PixelSortRenderer.cpp).
- [Triangle nodes](../RHITest/Public/Nodes/ArdaTriangleNodes.h) and
  [graph composition](../RHITest/Private/ArdaTriangleRenderer.cpp).

To add an operation, put its base-derived class and typed parameters in an individual
`Public/Nodes/Arda...Node.h` file. Its matching `Private/Nodes/Arda...Node.cpp` implements
metadata, preparation, description and recording alongside its shaders. Inherit
registration and attachment; the renderer only calls `Graph.AttachOrFind<Node>`.
An optional `Node::Register()` lets a library enumerate definitions before any
device exists. Describe every resource access
and pipeline contribution. The canonical key must cover frozen resources and
settings; mutable frame inputs are sampled during recording on the render thread.
Keep those inputs alive and avoid changing them during recording. Binding caches
are private node state and refresh if a compiled resource's native identity changes.

Frame graphs retain swap-chain textures. Release those graphs before resizing the
swap chain. Triangle, terrain, and Cornell use synchronous `Execute`; Pixel Sort
uses `Submit` for ordinary animation and waits on its frame ticket only when CPU
verification or capture needs completion. Recycling an occupied graph frame slot
can still wait. Every example reuses a graph while its frozen resource and
configuration identity remains valid.

See the [ArdaInductor guide](../../../Docs/ArdaRDG/ArdaInductor.md) for the graph
contract and the [compilation chapter](../../../Docs/ArdaRDG/compilation.html) for
scheduling and pipeline inference.
