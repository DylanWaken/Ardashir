# ArdaRenderGraph: from declarations to GPU work

ArdaRenderGraph is Ardashir's deferred dependency graph for
[NVRHI](https://github.com/NVIDIA-RTX/NVRHI). Instead of allocating every
temporary texture immediately and hand-writing every barrier, renderer code
first describes *intent*: this pass writes a buffer, that pass samples a
texture, and the final pass renders to the swap chain. The graph can then reason
about the whole frame before submitting anything.

Our recurring example is a deliberately small procedural-terrain pipeline:

```text
[Upload terrain settings] -> [Generate noise heightmap] -> [Erode heightmap]
                                      |                         |
                                      v                         v
                         [Debug heightmap/minimap]      [Triangulate terrain]
                                  (culled)                      |
                                               TerrainVertices + TerrainIndices
                                                               |
                                                               v
                                                  [Render terrain] -> [Overlay]
                                                               |
                                                          BackBuffer
```

From those declarations, ArdaRenderGraph:

1. discovers data dependencies;
2. removes the intentionally unused terrain-debug/minimap pass, because it has
   no observable output;
3. chooses graphics, asynchronous-compute, or copy pipelines;
4. derives resource lifetimes and NVRHI state transitions;
5. creates or reuses physical resources;
6. records command lists and inserts cross-queue waits; and
7. submits the surviving work in registration order.

This terrain graph is a pedagogical example used throughout the series; the
repository does not currently ship terrain shaders or a terrain demo. Chapter
12 preserves the actual triangle renderer as a separate, source-backed example.

The implementation is compact enough to read. This series therefore teaches
each concept and then links it to the code that implements it. The public API
starts in
[`ArdaRenderGraph.h`](../../Source/ArdaRenderGraph/Public/ArdaRenderGraph.h);
the build, compile, and execute paths live under
[`Source/ArdaRenderGraph`](../../Source/ArdaRenderGraph).

![ArdaRenderGraph architecture from declarations to NVRHI submission](assets/architecture.svg)

## The two-level mental model

During graph construction, `FARDGTextureRef` and `FARDGBufferRef` point to
*logical records*. They contain descriptors, names, ownership flags, and
dependency history. They are not necessarily backed by an
`nvrhi::ITexture` or `nvrhi::IBuffer` yet.

```text
Build and compile                         Execute
-----------------                         -------
logical Texture T0  --------------------> physical nvrhi::ITexture
logical Buffer  B0  --------------------> physical nvrhi::IBuffer
pass + state declarations --------------> barriers + nvrhi::ICommandList
producer/sync edges --------------------> ordering + queue waits
```

That separation is the central idea. It lets compilation cull dead work and
calculate lifetimes before execution commits GPU memory. Imported resources are
the exception: their logical records already point at caller-owned NVRHI
handles.

## One frame in seven steps

1. Construct one `FARDGBuilder` with a device and queue capabilities.
2. Import caller-owned resources and create graph-owned logical resources.
3. Put resource accesses in `ARDG_*` parameter structs.
4. Register passes in dependency order. **Registration order is the
   topological order**; compilation does not reorder arbitrary passes.
5. Make results observable by writing an imported resource, extracting a
   graph-created resource, or marking an intentional side effect `NeverCull`.
6. Call `Execute()`. It compiles automatically, materializes resources, records
   command lists, and submits them.
7. Discard the builder and create another for the next graph submission.

`Execute()` completes CPU submission, not GPU execution. Presentation,
readback waits, and frame-level synchronization still belong to the
application.

## Series map

The first four chapters establish the vocabulary used by every source
walkthrough:

1. [Getting started](01-Getting-Started.md) — build the three-stage terrain core
   and follow it from logical declarations to an imported back-buffer write.
2. [Core concepts](02-Core-Concepts.md) — logical and physical resources,
   handles and registries, pass records, frozen parameters, the blackboard,
   sentinels, and the one-shot lifecycle.
3. [Resources and parameters](03-Resources-and-Parameters.md) — descriptors,
   views, every parameter macro family, imports, extraction, uniform buffers,
   states, subresources, and lifetimes.
4. [Passes and dependencies](04-Passes-and-Dependencies.md) — RAW, WAR, and WAW
   hazards, producer versus synchronization edges, forward registration,
   manual dependencies, and backward culling.

The remaining chapters trace those ideas through the implementation:

5. [Compilation](05-Compilation.md)
6. [Execution and queues](06-Execution-and-Queues.md)
7. [Debugging and practices](07-Debugging-and-Practices.md)
8. [Build and edge walkthrough](08-Build-and-Edge-Walkthrough.md)
9. [Compiler source walkthrough](09-Compiler-Source-Walkthrough.md)
10. [Allocation and materialization](10-Allocation-and-Materialization.md)
11. [Executor source walkthrough](11-Executor-Source-Walkthrough.md)
12. [End-to-end examples](12-End-to-End-Examples.md)
13. [Recipes and reference](13-Recipes-and-Reference.md)

## Public entry point

ArdaRenderGraph requires C++17. Include its aggregate header and link its CMake
target:

```cpp
#include "ArdaRenderGraph.h"

using namespace arda::render_graph;
```

```cmake
target_link_libraries(MyTarget PRIVATE Ardashir::ArdaRenderGraph)
```

The target links NVRHI publicly. Device creation, shader compilation, binding
layouts, pipelines, swap-chain acquisition, and presentation are intentionally
outside the graph. See the
[NVRHI programming guide](https://github.com/NVIDIA-RTX/NVRHI/blob/main/doc/ProgrammingGuide.md)
for those systems.

## What to trust when prose and code differ

The repository source is authoritative. Useful starting points are:

- public types and flags:
  [`ArdaRenderGraphDefinitions.h`](../../Source/ArdaRenderGraph/Public/ArdaRenderGraphDefinitions.h);
- the builder API:
  [`ArdaRenderGraphBuilder.h`](../../Source/ArdaRenderGraph/Public/ArdaRenderGraphBuilder.h);
- resources and views:
  [`ArdaRenderGraphResources.h`](../../Source/ArdaRenderGraph/Public/ArdaRenderGraphResources.h);
- parameter macros:
  [`ArdaRenderGraphParameters.h`](../../Source/ArdaRenderGraph/Public/ArdaRenderGraphParameters.h);
- build-time edge discovery:
  [`ArdaRenderGraphBuilder.cpp`](../../Source/ArdaRenderGraph/Private/ArdaRenderGraphBuilder.cpp);
- compilation:
  [`ArdaRenderGraphCompiler.cpp`](../../Source/ArdaRenderGraph/Private/ArdaRenderGraphCompiler.cpp);
- execution:
  [`ArdaRenderGraphExecutor.cpp`](../../Source/ArdaRenderGraph/Private/ArdaRenderGraphExecutor.cpp);
- executable behavior checks:
  [`ArdaGraphTests.cpp`](../../Source/ArdaRenderGraph/Tests/ArdaGraphTests.cpp).

---

[Next: Getting started →](01-Getting-Started.md)
