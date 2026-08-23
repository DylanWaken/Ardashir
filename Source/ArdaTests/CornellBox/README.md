# RDG Cornell Box path tracer

`CornellBox` is a dual-backend hardware path-tracing sample. Every GPU workload is
an RDG pass and every GPU resource is an RDG resource node (created, extracted, or
imported). The setup sequence is:

1. `GenerateCornellGeometry` creates all room, box, sphere, light, normal, index,
   and material data in a compute shader. Each sphere uses six spherified-cube
   grids at 16x16 cells per face (3,072 triangles), avoiding UV-sphere pole
   clustering while providing about 5.8x the previous sphere density.
2. `BuildCornellBLAS` builds one opaque triangle BLAS with `PreferFastTrace`.
3. When supported, `CompactCornellBLAS` compacts the static BLAS.
4. `BuildCornellTLAS` creates the scene TLAS.
5. Each sampling update, `PathTraceCornellBox` launches one independent path
   per `(pixel, sample)` through the 3D hardware ray-dispatch grid.
6. `ReduceCornellSampleBatch` combines the parallel paths into the persistent
   HDR accumulation, then `ToneMapAndPresentCornellBox` presents the newest result.

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

The implementation follows NVIDIA's RTX guidance to keep ray-tracing pipeline
creation off the frame path, minimize payload and recursion depth, mark opaque
geometry as opaque, and accumulate independent sample batches. Advanced vendor-specific
features such as SER, RTXDI, and NRD are intentionally not pretended through the
portable RHI; they can be integrated when those reusable backend capabilities are
added.
