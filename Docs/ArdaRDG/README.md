# Dependency graph documentation

For application authoring, start with the [resource-variable programming model](../../Source/ArdaInfra/ArdaDependencyGraph/README.md#program-with-resource-variables). `FArdaGraphEdit` records operations such as `AllocateTexture`, `BindTexture`, `Clear` and `CopyTo`; every reusable node also publishes a named global callable such as `CopyBuffer(Edit, Source, Destination)`. `Edit.Commit()` checks authoring errors and queues the edit, `Graph.Compile()` selects or compiles a persistent revision, and `Graph.Execute()` runs it. The [compiled node recipes](Examples/AssembleNodes.cpp) demonstrate named callables, automatic registration, output retention and readback.

To add an operation, follow [registering a graph node](Registering-Nodes.md): declare its class and `ARDA_DECLARE_GRAPH_NODE(NodeClass, InvocationName)` in the header, define `ARDA_DEFINE_GRAPH_NODE(NodeClass, InvocationName)` once in its implementation, and implement metadata, canonical keys, resource declarations and recording. The guide distinguishes global C++ callable names, runtime registry identities and per-operation diagnostic names, and covers discovery, linking and failure handling.

Each frame can record its desired operations afresh on the same graph. Ordered edit hashes form a construction hash; matching constructions reuse the cached graph and compilation results. Successful compilation clears the edit queue and locks edits until `Execute()` or `Submit()` is attempted. The next ordinary edit starts empty, so omitted operations need no explicit removal. The graph retains up to four revisions by default and can reuse A after an A/B/A sequence. Use `Compile(true)` to force compilation, `InvalidatePersistentCache()` to invalidate reuse, and `GetRevisionStats()` to inspect cache activity. See [revision lifetime and identity](../../Source/ArdaInfra/ArdaDependencyGraph/README.md#frame-construction-and-persistent-revisions) for imported resources, retained pointees and error recovery.

The same `CopyTo(Edit, Source, Destination)` name selects the registered operation from its typed arguments:

| Source and destination | Registered node |
| --- | --- |
| Buffer to buffer | `FArdaMemoryCopyBufferNode` |
| Texture to texture | `FArdaMemoryCopyTextureNode` |
| Buffer to texture | `FArdaGraphBufferToTextureNode` |
| Texture to buffer | `FArdaGraphTextureToBufferNode` |
| Acceleration structure to acceleration structure | `FArdaMemoryCopyAccelerationStructureNode` |

Mixed buffer/texture copies accept a texture slice and buffer layout. An omitted layout or zero row pitch infers a portable aligned pitch for the selected region, preserving the byte offset; the buffer must have room for that pitched footprint. `Upload` and `Readback` handle ordinary packed CPU byte vectors.

Start with the [persistent dependency graph and ArdaInductor guide](ArdaInductor.md) for edits, registered typed nodes, scheduling, CUDA sequences and memory planning. The [pipeline guide](ArdaInductor-Pipelines.md) explains stage contributions, named slots, ambiguity, native PSO creation and reuse.

The [architecture chapter](architecture.html#modules) separates graph resources, node definitions and default nodes from inductor configuration, rules, compiler orchestration and runtime. The [compilation and scheduling chapter](compilation.html) explains ordered, configurable preprocess and compile rule instances derived from `FArdaGraphRule`, their graph/result transformation contracts, built-in alternatives, a [custom ordering policy](compilation.html#custom-order), memory/cost models and search algorithms. `Compile()` publishes or reuses a logical schedule and pending memory plan. The first `Submit()` materializes native resources, pipelines and per-frame execution state; later frames reuse them.

The [overview](index.html), [getting started example](getting-started.html), [renderer guide](quick-guide.html) and [execution guide](execution.html) all use `FArdaDependencyGraph`. [GPU telemetry and background scheduling](ArdaInductor.md#gpu-telemetry-and-background-scheduling) covers optional per-node timing, EMA/history, nonblocking collection and schedule adoption after frame retirement. Both timing and automatic tuning are disabled by default. Timing observes any selected order; automatic and manual schedule optimization require every selected rule to opt in, preserving stable/custom policies by default. The [API reference](api-reference.html) inventories the current public graph headers. `ArdaDependencyGraph.h` is an umbrella for the persistent APIs.

Regenerate and validate the inventory using the commands in [the backend documentation maintenance guide](../ArdaBackend/README.md#keeping-the-docs-synchronized).
