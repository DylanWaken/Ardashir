# ArdaRenderGraph: from declarations to GPU work

ArdaRenderGraph is Ardashir's deferred dependency graph for
[NVRHI](https://github.com/NVIDIA-RTX/NVRHI). Instead of allocating every
temporary texture immediately and hand-writing every barrier, renderer code
first describes *intent*: this pass writes a buffer, that pass samples a
texture, and the final pass renders to the swap chain. The graph can then reason
about the whole frame before submitting anything.

Our recurring example is a deliberately small procedural-terrain pipeline:

![Procedural terrain dependency graph with the unused debug branch culled](assets/intro-readme.svg#terrain-dag)

From those declarations, ArdaRenderGraph:

1. discovers data dependencies;
2. removes the intentionally unused terrain-debug/minimap pass, because it has
   no observable output;
3. chooses graphics, asynchronous-compute, or copy pipelines;
4. derives resource lifetimes and NVRHI state transitions;
5. creates or reuses physical resources;
6. records command lists and inserts cross-queue waits; and
7. submits the surviving work in registration order.

This is also the repository's runnable terrain example. Its canonical sources
are the
[`FArdaTerrainRenderer` implementation](../../Source/ArdaTests/ARDGExample/Private/ArdaTerrainRenderer.cpp),
the
[`ArdaTerrain.hlsl` shaders](../../Source/ArdaTests/ARDGExample/Shaders/ArdaTerrain.hlsl),
and the
[`ARDGExample` entry point](../../Source/ArdaTests/ARDGExample/ArdaARDGExampleMain.cpp).
The `ARDGExample.D3D12` and `ARDGExample.Vulkan` CTest smoke tests run one
hidden frame on available backends.

The implementation is compact enough to read. This series therefore teaches
each concept and then links it to the code that implements it. The public API
starts in
[`ArdaRenderGraph.h`](../../Source/ArdaRenderGraph/Public/ArdaRenderGraph.h);
the build, compile, and execute paths live under
[`Source/ArdaRenderGraph`](../../Source/ArdaRenderGraph).

![ArdaRenderGraph architecture from declarations to NVRHI submission](assets/architecture.svg)

## Complete build, compile, and execute call chain

The following source-ordered diagram is the implementation map for the three
stage walkthroughs. Read it from top to bottom. Cyan arrows are function calls,
green arrows identify state produced for the next layer, and dashed amber
arrows identify validation or GPU queue waits.

![Complete ArdaRenderGraph build, compile, and execute call chain](assets/arda-render-graph-call-chain.svg)

The stage chapters use exact excerpts from this master diagram:

- [build and edge discovery](08-Build-and-Edge-Walkthrough.md);
- [compiler lowering](09-Compiler-Source-Walkthrough.md); and
- [materialization, recording, and submission](11-Executor-Source-Walkthrough.md).

## The two-level mental model

During graph construction, `FARDGTextureRef` and `FARDGBufferRef` point to
*logical records*. They contain descriptors, names, ownership flags, and
dependency history. They are not necessarily backed by an
`nvrhi::ITexture` or `nvrhi::IBuffer` yet.

![Bridge from logical graph declarations to physical NVRHI resources and work](assets/intro-readme.svg#logical-physical-bridge)

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

1. [Getting started](01-Getting-Started.md) — build and run the full P0–P8
   terrain example, then follow it from declarations to submission.
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
