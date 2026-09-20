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
such as `TArdaComputeDependencyNode`. The base exposes `FArdaParameters` and implements
registration and invocation. The
renderer includes the selected node headers (or an include-only umbrella) and
records operations through an edit scope:

```cpp
FArdaGraphEdit Edit(Graph);
TerrainGenerate(Edit, Settings, RawHeightmap);
TerrainErode(Edit, RawHeightmap, Heightmap);
if (auto S = Edit.Commit(); !S) return S;
if (auto S = Graph.Compile(); !S) return S;
```

The surrounding renderer obtains logical resources through `AllocateBuffer`,
`AllocateTexture`, `BindBuffer` and `BindTexture` in the same edit, then updates
dynamic frame inputs and executes or submits the retained graph. There is
no node-library object or renderer-side registration/initialization call. Each node
exports one globally unique callable beside its class declaration with
`ARDA_DECLARE_GRAPH_NODE(FArdaTerrainGenerateNode, TerrainGenerate);`, paired with
one `ARDA_DEFINE_GRAPH_NODE` in its implementation. The same callable accepts
parameter structs and initializer callbacks with an optional diagnostic name.

Each node implements `GetMetadata`, `GetCanonicalKey`, `Describe`, and `Record`
(or `PrepareCuda` for CUDA). The base checks these hooks at compile time and
registers a device-independent definition on first attachment. Nodes with setup
declare a private implementation of nested `FArdaState` and supply `Prepare(Device)`
for their shaders, layouts, operand and pipeline configuration. Nodes needing
mutable runtime state can declare `FArdaInstanceState` and supply `CreateInstance`;
the graph prepares shader bindings. Stateless defaults cover upload/copy nodes.
Optional `Validate` checks inputs before setup.

Each invocation records a new operation through the edit facade and automatically
registers its definition. Diagnostic names are optional and must be unique in a
construction. Equivalent ordered constructions reuse a cached persistent revision. The core base weakly caches successful immutable
state by node type/device; private bound parameters strongly retain it for
execution. Live graphs share device state and own separate mutable instances.
Retiring all instances releases the state. Failed setup is not cached and can be
retried. State selection is fixed by node type/device; parameter-dependent setup
belongs in `CreateInstance`, with GPU memory accounted for in `Describe`.

`Edit.Commit()` queues the closed edit. `Compile()` infers dependencies,
pipeline patterns and CUDA batches. The first submission creates or reuses native PSOs. Graph replay does not rerun node setup or registry lookup.
The graph/executor prepares binding sets and the path tracer's shader table per
compiled frame slot. Upload/readback nodes can attach without initializing unrelated shaders.

Camera/window control, resource sizing, graph composition, presentation, and CPU
verification belong in the renderer. Shader classes, binding metadata, kernel
selection, command recording, and fixed pipeline settings belong in each node's
implementation file.

Executable startup selects compiler policy through the shared
`ConfigureArdaExampleShaderCompiler` helper in `../Public/ArdaExampleShaders.h`.
These source-distributed samples allow missing/outdated shader compilation in
both Debug and Release. Terrain and Cornell Box still honor `--shader-mode load-only`,
which requires cooked artifacts and never compiles them. This explicit sample policy
does not change the backend's Release default of disabling automatic compilation.

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

The renderer invokes consumers before producers, closes with `Edit.Commit()`,
and calls `Compile()`. ArdaInductor resolves the edges and pipelines then; it does not infer dependencies
from attachment order. Frame input changes reuse the plan. `ARDGExample --verify`
enables first-frame geometry and gradient readback checks; after verification it
records a fresh construction that omits those readbacks. Ordinary viewer runs omit those diagnostics.
`RunARDGExample.py` forwards `--verify`, which is independent of `--validation`.

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
completion it records a new construction containing only the draw against the
now-initialized geometry imports. Subsequent
frames reuse draw-only graphs. Pixel Sort already exposes upload, noise compute,
CUDA sort, presentation, and each optional readback as individual nodes; its radix
kernel is one operator rather than an entire graphics/CUDA program.

See the [registration recipes](../../../Docs/ArdaRDG/quick-guide.html#node-registration) for native compute, indexed raster, fullscreen graphics, ray tracing and CUDA nodes, including an authored CUDA class. They show optional library discovery before devices exist and automatic registration during operation invocation.

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
registration and invocation. Add `ARDA_DECLARE_GRAPH_NODE(NodeClass, InvocationName);`
after the class and the matching `ARDA_DEFINE_GRAPH_NODE` once in its `.cpp`;
the renderer calls `InvocationName(Edit, Parameters)` or passes positional fields.
Raw graph mutation is private. See [registering a node](../../../Docs/ArdaRDG/Registering-Nodes.md)
for the complete header and implementation contract.
An optional `Node::Register()` lets a library enumerate definitions before any
device exists. Describe every resource access
and pipeline contribution. The canonical key must cover frozen resources and
settings; mutable frame inputs are sampled during recording on the render thread.
Keep those inputs alive and avoid changing them during recording. Nodes declare
outputs in DeclareResources and map shader metadata to logical arguments in
Describe. The graph allocates the outputs and prepares binding sets, bindless
tables, shader parameters and ray tables per compiled frame slot. Recording uses
the execution context's prepared state; node callbacks do not create binding sets.
Renderers compose typed outputs from `Operation.GetBuffer` / `GetTexture`. See the
[self-contained node recipes](../../../Docs/ArdaRDG/node-recipes.html).

Frame graphs retain swap-chain textures. Release those graphs before resizing the
swap chain. Triangle, terrain, and Cornell use synchronous `Execute`; Pixel Sort
uses `Submit` for ordinary animation and waits on its frame ticket only when CPU
verification or capture needs completion. Recycling an occupied graph frame slot
can still wait. Every example reuses a graph while its frozen resource and
configuration identity remains valid.

See the [ArdaInductor guide](../../../Docs/ArdaRDG/ArdaInductor.md) for the graph
contract and the [compilation chapter](../../../Docs/ArdaRDG/compilation.html) for
scheduling and pipeline inference.

## GPU validation build policy

ARDGExample, CornellBox, RHITest and PixelSort run without native GPU validation
by default. Pass `--validation` to request it. PixelSort `--verify` independently
runs its CPU/GPU comparisons; ARDGExample `--verify` checks terrain geometry and
gradients. Building an example does not depend on validation-layer
provisioning. Existing GPU CTest cases cover ordinary runs; separate `Validation`
cases pass `--validation` explicitly in ON builds.

All examples honor `-DARDASHIR_ENABLE_GPU_VALIDATION=OFF`, including builds with
unit tests disabled, and reject `--validation` in those builds. The default is
`ON` for both Debug and Release. OFF disables native debug-layer requests and
provisioning while preserving result verification and assertions. Use separate
build directories to retain both versions. The Python launchers preserve the
configured CMake option. Dedicated GPU test executables retain their strict
validation policy.
