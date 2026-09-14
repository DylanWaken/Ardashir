# Automatic resources, bindings and shader tables

The [node recipes](node-recipes.html) are the tutorial and the [centralized API reference](api-reference.html) defines the public contract. Node implementations declare logical arguments; ArdaInductor prepares physical execution state after compilation has selected resources and pipelines.

## Materialization and lifetime

1. The common node base calls DeclareResources on a parameter copy. Named outputs either declare graph-owned buffers/textures or validate supplied handles. Output resolution precedes the canonical identity check. Failed attachment rolls back provisional resources.
2. Describe supplies shader metadata, logical resources, push-constant bytes, optional bindless entries and ray records. Metadata-derived accesses participate in dependency resolution alongside explicit copy, attachment and external-library accesses.
3. The compiler validates producer coverage, schedules work and assigns physical allocations. Shader metadata produces fixed layouts; bindless declarations produce bounded bindless layouts. Generated layouts are interned by semantic description.
4. Pipeline inference merges layouts and shader contributions, associates explicit local ray layouts with their exports, and creates or reuses native PSOs.
5. Each compiled frame slot receives its own fixed binding sets, descriptor tables, relocated push constants and committed shader tables. Resource arguments point at that frame's selected physical storage.
6. Execution context state helpers bind those prepared objects and bytes. Repeated graph execution does not recreate them. Adaptive schedule adoption retains compatible prepared objects; a graph edit rebuilds changed semantics after retiring earlier work.

The graph owns physical output storage. Shader metadata must outlive the graph (normally static shader metadata); node state retains shader objects. Descriptors and shader tables are immutable while compiled work uses them. Dynamic resource contents require synchronized uploads/imports. New dimensions, resource identities, table membership or frozen scalar values require an edit.

The synchronous readback node waits for a requested CPU result. Automatic binding setup adds no separate per-execution GPU wait, but graph frame reuse, readback, native ownership transfers and existing execution policy still impose their normal synchronization.

## Range-aware resource edges

Each declared descriptor resolves to a logical resource plus its native view range. Buffer intervals overlap when max(beginA, beginB) is less than min(endA, endB). Texture ranges overlap when their mip, slice and plane intervals all intersect. Acceleration structures remain whole-resource accesses.

Different nodes may write the same resource. The compiler applies two rules separately to each overlapping region. With one writer, readers depend on that producer regardless of attachment order. With multiple distinct writer nodes, write/write, write/read and read/write hazards follow original successful attachment order. Reads consume the preceding writer's contents; read/read accesses need no ordering edge. Disjoint regions keep independent producers, and readers spanning regions depend on the appropriate producer of each.

Graph-owned reads require complete initialized coverage. A read before the first of multiple writes fails; imported storage may supply initial contents. ReadWrite consumes the incoming value before updating it, so an owned region needs a preceding producer. A single ReadWrite node cannot initialize its own input. Repeated writes use the same logical handle and physical graph resource within a frame slot, with no implicit output allocation or saved old version. Separate handles are needed when older contents must remain available after an overwrite. See the [write-read-write example](resources.html#worked-example).

Removal follows data dependencies: removing a writer removes readers of the contents it produced and recursively their affected consumers. It does not remove a later pure overwrite or its readers solely because they access the same region; a ReadWrite update does depend on its incoming producer. Ordering-only hazards do not cause removal cascades. A resource's output marker survives while another producer remains. Recompilation reevaluates the remaining writer count per region, so removing a writer can restore the single-producer rule.

Buffer access boundaries and texture subresource cells define the regions used for overlap analysis. Costs depend on declared ranges and their intersections, not on buffer bytes or texture texels being inspected. The [dependency compilation chapter](compilation.html#dependencies) describes dependency construction. The graph does not inspect GPU control flow or arbitrary texture pixel coordinates.

## Bindless parameters

Declare every potentially selected entry, including GPU-selected indices. Different indices are not proof of independent storage. A descriptor that views an entire resource conservatively declares that entire range. Sparse entries are supported, but the shader must never index an unpopulated slot.

Descriptor-index relocations patch a uint32 field in a named frozen shader value. They must reference a populated table entry with the matching register and descriptor type. Default indices are table-relative; absolute heap indices require direct heap access with exactly one descriptor bank. Invalid indices, capacity, duplicate entries, missing shader members and invalid byte offsets fail compilation.

Tables are node-local, so independent nodes can use the same layout while declaring separate resources or ranges. Native table allocation and individual entry writes happen during compilation per frame slot; large table construction is not a zero-cost operation. Replay retains the resulting objects.

## Shader tables

Without explicit records, a ray slot selects its unique ray-generation export and every miss, hit-group and callable export in deterministic pipeline order. More than one ray-generation export requires an explicit selection.

Explicit record indices are dense and zero-based within each record category. The graph converts them into the native flat record array. Unknown exports, wrong categories, duplicate coordinates, holes, conflicting local schemas and missing required local arguments fail compilation. Local binding metadata generates an export-associated local layout and per-frame local bindings. Local resources and geometry references contribute ordinary graph accesses.

Fixed local descriptor layouts work on D3D12. The current Vulkan backend requires direct-heap local descriptors and reports Unsupported for this fixed-local-metadata path. Automatically generated global-binding ray tables work on both backends. Raw local argument bytes remain author-defined and must match the shader ABI.

## Scheduling and native constraints

Disjoint same-state UAV buffer accesses omit unnecessary UAV ordering barriers. The executor tracks all outstanding accesses since the last barrier, so an intervening disjoint access cannot hide an older overlapping write.

Buffer state and queue ownership are still tracked for the whole native buffer. Queue changes and incompatible states can therefore serialize disjoint ranges. Separate allocations allow more cross-queue freedom. Texture ownership/state uses native subresources. Explicit edges, memory alias proofs, budget-driven ordering and the single CUDA stream can impose additional order. Independence allows overlap; it does not guarantee simultaneous occupancy.

See [scheduling algorithms](compilation.html), [pipeline inference](ArdaInductor-Pipelines.md) and [GPU conformance tests](../../Source/ArdaInfra/ArdaRenderGraph/Tests/ArdaInductorGpuTests.cpp) for the implementation contracts and numerical examples.
