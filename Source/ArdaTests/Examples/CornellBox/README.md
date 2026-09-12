# RDG Cornell Box path tracer

`CornellBox` is a dual-backend hardware path-tracing sample built with the persistent
`FArdaDependencyGraph` API. Registered typed nodes declare accesses and shader stages;
the compiler infers dependencies and creates compute, ray-tracing, and graphics
pipelines from their pipeline requests. Geometry buffers and unbuilt native
acceleration structures are imported before graph nodes initialize them. The setup
sequence uses registered operations.

The [node umbrella](Public/Nodes/ArdaCornellBoxNodes.h) includes independent node
classes. Each header has a matching implementation under `Private/Nodes` that
implements metadata, shader/layout preparation, pipeline requests and recording.
Every class derives from a domain specialization of `TArdaDependencyNode`; that
base owns registration, attachment and weak device-state caching. The trace node
uses separate per-instance state for its mutable shader table.
For example, [the trace node](Private/Nodes/ArdaCornellTraceNode.cpp) owns the ray
exports, layouts and shader-table cache. HLSL sources live in `Private/Nodes/Shaders`.
The renderer imports resources and attaches classes directly with
`Graph.AttachOrFind<FArdaCornellTraceNode>(Name, Parameters)`; it does not initialize
a node library. See the [shared example layout](../README.md).

The setup sequence is:

1. `GenerateCornellGeometry` creates all room, box, sphere, light, normal, index,
   and material data in a compute shader. Each sphere uses six spherified-cube
   grids at 16x16 cells per face (3,072 triangles), avoiding UV-sphere pole
   clustering while providing about 5.8x the previous sphere density.
2. `BuildCornellBLAS` builds one opaque triangle BLAS with `PreferFastTrace`.
3. When supported, `CompactCornellBLAS` compacts the static BLAS.
4. Every frame, `RebuildCornellFrameTLAS` rebuilds the scene TLAS before ray reads.
5. Each sampling update, `PathTraceCornellBox` launches one independent path
   per `(pixel, sample)` through the 3D hardware ray-dispatch grid.
6. `ReduceCornellSampleBatch` combines the parallel paths into the persistent
   HDR accumulation, then `ToneMapAndPresentCornellBox` presents the newest result.

Geometry generation and BLAS construction are separate nodes in one setup graph;
there is no CPU submission between them. With compaction, the completed BLAS size
determines the destination allocation, so a second graph compacts the static BLAS.
TLAS storage is allocated once, then a side-effecting build node executes in every
frame graph, even when sampling has finished. Replaying a cached graph records
`BuildTopLevelAccelStruct` again; this is a full build, not an update/refit. Ray
reads of the TLAS establish the producer edge automatically. Static geometry and
BLAS are retained. Resource accesses determine order even when a producer is
attached after its consumers.

One frame graph is cached per swap-chain image. `UpdateCornellFrameConstants`
uploads camera and sample inputs on every execution; the graph, shader table, and
transient allocation pool are reused. Camera motion resets accumulation counters
without rebuilding those graphs. Resizing releases cached attachment references
before resizing the swap chain. A changed sample-batch size rebuilds the affected
frame graph so its radiance buffer has the required capacity.

The frame upload is a Copy node, while TLAS rebuilding, ray dispatch, sample reduction, and
presentation each have their own node. The renderer attaches presentation before
its producers and lets `EndGraphEdit` compile the dependency order.

Ray nodes declare reads of both the TLAS and its BLAS. Acceleration-structure access
currently stays on the graphics queue, which also executes the associated compute
commands. AS build nodes reserve their queried native build scratch in the graph's
workspace accounting; TLAS builds additionally reserve instance staging storage.

The ray-generation shader iterates path segments at native recursion depth one.
It implements emissive-area-light next-event estimation, multiple importance
sampling, diffuse and GGX reflection, ideal dielectric transmission, Russian
roulette, and progressive accumulation. Geometry is marked opaque so triangle
intersection and traversal remain in fixed-function hardware.

Run from the repository root:

```powershell
python Scripts/Examples/RunCornellBox.py d3d12 build Debug --samples-per-dispatch 8 --max-samples 1024 --max-bounces 12
```

The executable accepts `--backend`, `--width`, `--height`, `--frames`, `--hidden`,
`--fullscreen`, `--samples-per-dispatch` (`--spp` is retained as an alias),
`--max-samples`, `--max-bounces`, `--exposure`, `--seed`, and `--no-compaction`.
The defaults launch eight samples per pixel concurrently and stop at 1024 samples
per pixel. The renderer caps a batch against the native ray-dispatch limit and a
256 MiB transient-radiance budget. It also bounds the worst-case path segments
per dispatch so high bounce counts remain responsive and do not trip desktop GPU
watchdogs. Use W/A/S/D and mouse look; any camera motion or resize restarts
progressive accumulation. Hold Shift to release the mouse and click to capture
it again.

Pipeline and shader-table creation occurs when a cached frame graph is first
prepared. Subsequent executions reuse them. The path tracer uses a compact payload,
recursion depth one, opaque geometry, and independent sample batches.
