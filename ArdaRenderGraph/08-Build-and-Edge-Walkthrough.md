# 8. Build and edge walkthrough

[← Debugging and recommended practices](07-Debugging-and-Practices.md) ·
[Documentation home](README.md) ·
[Next: Compiler source walkthrough →](09-Compiler-Source-Walkthrough.md)

Compilation is not the first time ArdaRenderGraph learns about resources. Most
dependency discovery happens immediately inside `AddPass`. By compile time,
each pass already owns its incoming producer edges, each resource already knows
its last writer and readers since that write, and each pass already contains
the raw state declarations that barrier compilation will lower.

This chapter expands the build section of the
[complete call-chain diagram](assets/arda-render-graph-call-chain.svg).

![ArdaRenderGraph build-stage call chain](assets/arda-render-graph-call-chain.svg#ardg-build-view)

## Reading the build-stage excerpt

The four columns separate API calls from implementation and stored state:

1. **Public API** starts the builder, creates/imports logical resources,
   freezes parameter data, registers passes, and optionally adds manual
   dependencies or extractions.
2. **Builder internals** enforce the `IsBuilding()` lifecycle, append
   arena-backed registry entries, create `FARDGLambdaPass`, and enter setup.
3. **Setup and metadata walk** enumerates every parameter leaf and routes it to
   `AddTexture`, `AddBuffer`, `AddView`, `AddUniformBuffer`, or raster-binding
   handling.
4. **Persistent logical state** receives raw access declarations, producer and
   synchronization-producer edges, resource last-writer/readers history, and
   first/last build-time uses.

The expanded write-state diamond is the dependency core. Every access follows
the current last writer. A write additionally waits for all readers of the old
version, clears that reader epoch, and becomes the new last writer. The bottom
box is the hard stage boundary: `Compile()` sets `mbCompiling`, so all
`IsBuilding()`-guarded mutation closes before compiler code runs.

The relevant code is split between the public
[`AddPass` template](../../Source/ArdaRenderGraph/Public/ArdaRenderGraphBuilder.h#L364-L422),
the private
[`AddPassInternal`](../../Source/ArdaRenderGraph/Private/ArdaRenderGraphBuilder.cpp#L1687-L1724),
and the anonymous private
[`FARDGSetupContext`](../../Source/ArdaRenderGraph/Private/ArdaRenderGraphBuilder.cpp#L225-L616).

## Actual behavior versus future direction

- **Actual now:** dependency history is updated synchronously, in pass
  registration order, from generated parameter metadata.
- **Future direction:** finer-grained dependency history or deferred parallel
  graph construction would need a different data model. Neither exists now.

## 1. The typed `AddPass` front end

Consider:

```cpp
FGenerateParameters Parameters;
Parameters.mSettings = TerrainSettings;
Parameters.mHeightmap = HeightmapUAV;

FARDGPassHandle Generate = Graph.AddPass(
    "GenerateNoiseHeightmap",
    &Parameters,
    EARDGPassFlags::Compute | EARDGPassFlags::AsyncCompute,
    [](FARDGPassExecutionContext& Context,
       const FGenerateParameters& Frozen)
    {
        // Record commands using Frozen declarations.
    });
```

The typed overload does four things before entering private builder code:

1. rejects a null parameter-struct pointer;
2. freezes the parameter object into graph-arena storage if necessary;
3. wraps the user's callable in a type-erased
   `FARDGPassExecuteFunction`; and
4. passes the frozen pointer and `ParameterType::GetStaticMetadata()` to
   `AddPassInternal`.

The exact template is
[`FARDGBuilder::AddPass`](../../Source/ArdaRenderGraph/Public/ArdaRenderGraphBuilder.h#L373-L400).

### What “freeze” means

`FreezeParameters` checks whether the pointer already names storage allocated
by this builder. If so, it reuses that object. Otherwise it copy-constructs a
new object in the builder's arena. The pass lambda later sees this frozen copy,
not a caller's mutable stack object. See
[`AllocateParameters`](../../Source/ArdaRenderGraph/Public/ArdaRenderGraphBuilder.h#L185-L209)
and
[`FreezeParameters`](../../Source/ArdaRenderGraph/Public/ArdaRenderGraphBuilder.h#L554-L562).

Freezing is shallow with respect to pointer members. Graph resource references
inside the struct still point to logical records owned by the builder; setup
validates those identities.

The parameterless `AddPass` overload supplies no metadata, so it creates no
automatic resource edges. A parameterless side-effect pass therefore needs
`NeverCull`, a manual dependency into live work, or another explicit observable
mechanism.

## 2. `AddPassInternal` creates the pass before visiting metadata

`AddPassInternal` first verifies the builder is still in its build phase,
requires a name and executable body, and validates flag combinations. It then
appends an `FARDGLambdaPass` to the pass registry.

Appending first is important: setup needs the new stable pass handle while it
updates resource histories. Handles are dense append-only indices, so every
previously recorded writer or reader has a smaller handle.

If metadata exists, `AddPassInternal` constructs a temporary setup context with
references to the pass, texture, buffer, view, and uniform-buffer registries,
then calls:

```cpp
Setup.Visit(*Metadata, Parameters);
```

The implementation is
[`AddPassInternal`](../../Source/ArdaRenderGraph/Private/ArdaRenderGraphBuilder.cpp#L1059-L1096).
The prologue was already appended when `FARDGBuilder::FImpl` was constructed,
so the first user pass follows P0; see
[`FImpl::FImpl`](../../Source/ArdaRenderGraph/Private/ArdaRenderGraphBuilderInternal.h#L13-L25).

## 3. Metadata turns C++ layout into an ordered stream

The `ARDG_*` macros generate one `FARDGParameterMember` per declared member.
Each record stores:

- semantic type;
- source name;
- offset, size, and alignment;
- element count and stride;
- default NVRHI state; and
- nested metadata when the member is an inline parameter struct.

The metadata definitions are in
[`ArdaRenderGraphParameters.h`](../../Source/ArdaRenderGraph/Public/ArdaRenderGraphParameters.h#L17-L113),
and the macro-generated record construction begins at
[`ARDG_INTERNAL_PARAMETER`](../../Source/ArdaRenderGraph/Public/ArdaRenderGraphParameters.h#L292-L394).

`FARDGParameterMetadata::Enumerate` walks members in declaration order and array
elements in increasing index order. Inline nested structs recurse immediately,
building paths such as `mLayers[1].mInput`. With the default
`bIncludeNestedContainers = false`, setup receives nested leaves, not separate
container callbacks. See
[`EnumerateInternal`](../../Source/ArdaRenderGraph/Public/ArdaRenderGraphParameters.h#L208-L283).

This ordering makes edge and state collection deterministic.

## 4. `FARDGSetupContext::Visit` dispatches each leaf

`Visit` asks metadata to enumerate and switches on every leaf's
`EARDGParameterType`. The behavior is:

| Parameter kind | Setup action |
| --- | --- |
| ordinary value | none |
| inline nested struct | none at switch level; metadata already recursed |
| texture | `AddTexture` with the macro's default state |
| buffer | `AddBuffer` with the macro's default state |
| texture SRV/UAV | validate and record the view, then add its parent texture and subresources |
| buffer SRV/UAV | validate and record the view, then add its parent buffer and byte range |
| texture access | add its runtime texture, state, and subresources |
| buffer access | add its runtime buffer, state, and range |
| uniform buffer | record it once and recursively visit its contents |
| render-target slots | record attachment signature and add color/depth writes |

The full switch is
[`FARDGSetupContext::Visit`](../../Source/ArdaRenderGraph/Private/ArdaRenderGraphBuilder.cpp#L258-L430).

### Default states supplied by macros

**Actual now:**

- `ARDG_TEXTURE`, `ARDG_BUFFER`, and SRV members use
  `ShaderResource`;
- texture and buffer UAV members use `UnorderedAccess`;
- direct `*_ACCESS` members carry a runtime state and range;
- color render targets use `RenderTarget`;
- depth-stencil binding uses `DepthWrite`; and
- a uniform-buffer member identifies a logical uniform buffer, while resources
  nested in that uniform buffer's contents are visited with their own metadata.

Macro definitions are at
[`ArdaRenderGraphParameters.h` lines 454–621](../../Source/ArdaRenderGraph/Public/ArdaRenderGraphParameters.h#L454-L621).

### Null and foreign references

Null resource, view, and uniform-buffer members are skipped. They express no
access and produce no state or edge.

A non-null direct texture or buffer must resolve to the identical pointer in
this builder's registry. A non-null view or uniform buffer is checked the same
way against its registry. A record from another graph fails immediately. The
ownership checks are in
[`AddTexture`](../../Source/ArdaRenderGraph/Private/ArdaRenderGraphBuilder.cpp#L144-L186),
[`AddBuffer`](../../Source/ArdaRenderGraph/Private/ArdaRenderGraphBuilder.cpp#L188-L229),
and the view cases in
[`Visit`](../../Source/ArdaRenderGraph/Private/ArdaRenderGraphBuilder.cpp#L283-L365).

`Unknown` is not a valid pass access state. Direct access members must be filled
with an explicit state before registration.

## 5. Views contribute both identity and parent access

For a texture SRV, setup:

1. checks the view belongs to this graph;
2. appends its view handle to `pass.mViews` if absent;
3. resolves the parent texture handle from the view descriptor; and
4. calls `AddTexture` with the SRV default state and view subresources.

UAV and buffer views follow the same shape. `AddView` deduplicates the view
handle, but raw texture/buffer state entries are intentionally not deduplicated.
If two fields mention the same resource, both declarations remain available for
later per-pass state merging and validation.

This separation also supports execution-time access checks: the pass records
the exact logical view and records the parent resource state requirement.

## 6. Uniform buffers recursively expose hidden resources

`AddUniformBuffer` first validates ownership. A per-setup
`mVisitedUniformBuffers` set then ensures each logical uniform buffer is
recorded and traversed only once for this pass. If the record has metadata and
contents, setup recursively calls `Visit` on those contents.

The algorithm is
[`AddUniformBuffer`](../../Source/ArdaRenderGraph/Private/ArdaRenderGraphBuilder.cpp#L231-L256).

This matters when a constant-buffer object contains a logical texture or view:
that indirect resource still contributes pass state and dependencies. The
visited set prevents repeated references—and potential recursive metadata
graphs—from repeatedly walking the same logical uniform buffer.

The test
[`SetupTraversesViewsAndNestedUniformBufferMetadata`](../../Source/ArdaRenderGraph/Tests/ArdaGraphTests.cpp#L612-L669)
checks that a direct SRV, direct UAV, and texture nested in a uniform buffer all
become texture state records and preserve the producer chain.

## 7. Raster slots also build a compatibility signature

For each non-null color slot, setup writes:

- the logical texture handle into `mRasterBindings.mColor[slot]`;
- the declared subresource set into the matching signature slot; and
- a `RenderTarget` write through `AddTexture`.

A non-null depth-stencil binding similarly records its handle/subresources and
adds a `DepthWrite` access. This code is at
[`Visit` lines 391–426](../../Source/ArdaRenderGraph/Private/ArdaRenderGraphBuilder.cpp#L391-L426).

The signature is not used to build dependencies. Dependencies come from the
same `AddTexture` write path as every other texture access. The signature is
saved for compiler raster grouping.

## 8. Exact last-writer/readers mutation

Every `FARDGViewableResource` stores:

```text
lastWriter: pass handle or invalid
readers: unique pass handles since lastWriter
```

The actual fields and mutators are in
[`FARDGViewableResource`](../../Source/ArdaRenderGraph/Public/ArdaRenderGraphResources.h#L120-L198).
They belong to the whole logical texture or buffer. Texture subresources and
buffer byte ranges are not part of this dependency-history key.

Both `AddTexture` and `AddBuffer` implement the same state machine. In faithful
pseudocode:

```text
function addAccess(resource, state, selectedRange, currentPass):
    if resource is null:
        return
    require resource belongs to this builder
    require state != Unknown

    isWrite = state contains any write-state bit
    currentPass.rawStates.push(resource, selectedRange, state, isWrite)
    resource.markUsed(currentPass)

    # Every access follows the most recent writer.
    if resource.lastWriter is valid and resource.lastWriter != currentPass:
        currentPass.addUniqueProducer(resource.lastWriter)

    if isWrite:
        # A write must not overtake reads of the previous version.
        for reader in resource.readers:
            if reader != currentPass:
                currentPass.addUniqueSynchronizationProducer(reader)

        resource.readers.clear()
        resource.lastWriter = currentPass
    else:
        resource.addUniqueReader(currentPass)
```

Compare this directly with
[`AddTexture`](../../Source/ArdaRenderGraph/Private/ArdaRenderGraphBuilder.cpp#L144-L186)
and
[`AddBuffer`](../../Source/ArdaRenderGraph/Private/ArdaRenderGraphBuilder.cpp#L188-L229).

`FARDGPass::AddProducer` and the synchronization variant ignore invalid handles
and deduplicate existing edges:
[`FARDGPass` edge methods](../../Source/ArdaRenderGraph/Public/ArdaRenderGraphPass.h#L351-L423).
The setup-context wrapper additionally suppresses a self producer.

### Why there are two edge kinds

A producer edge means “this pass consumes a version made by that writer.” It
therefore carries:

- execution ordering;
- queue synchronization when queues differ; and
- backward liveness during culling.

A synchronization edge means “this later writer must not race an earlier
reader of the old version.” It carries ordering and cross-queue synchronization
only if both endpoints survive. It does not claim the reader contributes data
to the new write, so culling does not follow it.

## 9. Walk the recurring example one pass at a time

This is the build order in the runnable
[`ArdaTerrainRenderer.cpp`](../../Source/ArdaTests/ARDGExample/Private/ArdaTerrainRenderer.cpp),
including its imported persistent upload buffer.

Start with:

```text
TerrainSettingsUpload: lastWriter=P0,      readers=[]  # external import
TerrainSettings: lastWriter=invalid, readers=[]
Heightmap:       lastWriter=invalid, readers=[]
TerrainVertices: lastWriter=invalid, readers=[]
TerrainIndices:  lastWriter=invalid, readers=[]
BackBuffer:      lastWriter=P0,      readers=[]  # external import
```

External registration sets the prologue as last producer in
[`RegisterExternalTexture/Buffer`](../../Source/ArdaRenderGraph/Private/ArdaRenderGraphBuilder.cpp#L881-L968).

### P1 `UploadTerrainSettings`: read upload, write settings

The imported source read follows P0; the graph-created destination has no
previous writer:

```text
P1.producers = [P0]
P1.syncProducers = []
TerrainSettingsUpload.readers = [P1]
TerrainSettings.lastWriter = P1
TerrainSettings.readers = []
```

### P2 `GenerateNoiseHeightmap`: read settings, write `Heightmap`

The settings read depends on P1 and joins the readers of that version. The
heightmap write has no predecessor:

```text
P2.producers = [P1]
TerrainSettings.readers = [P2]
Heightmap.lastWriter = P2
```

### P3 `DebugHeightmap`: read `Heightmap`

```text
P3.producers = [P2]
Heightmap.readers = [P3]
```

This graphics terrain-debug/minimap pass has no observable output. Nothing
makes P3 a culling root.

### P4 `ErodeHeightmap`: UAV-rewrite `Heightmap`

Setup first adds the current last writer P2 as a producer. Because this is a
write, it also turns every prior reader into a synchronization producer, clears
the reader epoch, and installs P4:

```text
P4.producers = [P2]
P4.syncProducers = [P3]
Heightmap.lastWriter = P4
Heightmap.readers = []
```

P3's producer relation to P2 and synchronization relation to P4 are both true,
but P3 still has no live output.

### P5 `TriangulateTerrain`: read `Heightmap`, write terrain buffers

The heightmap read adds P4. Both UAV buffer writes start new histories and have
no previous writers:

```text
P5.producers = [P4]
Heightmap.readers = [P5]
TerrainVertices.lastWriter = P5
TerrainIndices.lastWriter = P5
```

### P6 `RenderTerrain`: read mesh buffers, write `BackBuffer`

The two buffer reads both depend on P5 and deduplicate to one producer. The
imported back-buffer write also adds its current last writer P0:

```text
P6.producers = [P5, P0]  # insertion order; compiler later sorts
TerrainVertices.readers = [P6]
TerrainIndices.readers = [P6]
BackBuffer.lastWriter = P6
```

### P7 `TerrainOverlay`: rewrite `BackBuffer`

```text
P7.producers = [P6]
BackBuffer.lastWriter = P7
```

Compilation appends P8 `GraphEpilogue`, which receives P7 as producer. Because
`TerrainSettingsUpload` is external and was only read, P8 also receives P1 as
a synchronization producer for graph-exit ordering. Backward culling reaches
P7, P6, P5, P4, P2, and P1 through producer edges. It does not follow P4's
synchronization edge to P3, so P3 is removed. The live registration order
`[P0, P1, P2, P4, P5, P6, P7, P8]` is the execution order; compilation does
not topologically reorder it.

## 10. Multiple declarations in one pass

The mutation order follows metadata order, but self-edge suppression keeps
same-pass combinations valid.

### Read then write in one pass

The read adds the pass to `readers`. The later write:

- adds the previous external/earlier writer as a producer again, deduplicated;
- skips itself while converting readers to synchronization edges;
- clears readers; and
- makes the current pass last writer.

### Write then read in one pass

The write installs the current pass as last writer. The later read attempts to
add that writer as producer, but the setup wrapper suppresses self-dependency,
then records the pass as a reader of its own resulting version.

Pre-compile validation separately decides whether the combined declared states
are legal and whether same-pass writes satisfy same-pass initialization. Build
setup records first; it is not the final legality authority.

## 11. Whole-resource history is conservative

**Actual now:** both of these serialize:

- a write to `Heightmap` mip 0 after a read of mip 1;
- a write to bytes `[512, 768)` after a read of bytes `[0, 256)`.

That is because `mLastProducer` and `mReaders` live on
`FARDGViewableResource`, not on texture subresource or buffer-range records.
Selected ranges are preserved in the pass state for validation and barrier
lowering, but not for edge history.

Texture initialization and transitions later become per mip/slice. Buffer
initialization, dependency history, and transitions remain whole-buffer. These
different granularities are intentional facts of the current implementation,
not interchangeable claims.

**Future direction:** versioning each texture subresource and buffer interval
could expose more parallelism, at the cost of interval splitting, more edges,
and more complex extraction/final-state semantics.

## 12. Manual dependencies use the producer channel

`AddDependency(Producer, Consumer)` appends a normal producer edge to the
consumer. Both handles must already exist, differ, and point forward in
registration order. Therefore manual dependencies affect culling as well as
ordering.

See
[`FARDGBuilder::AddDependency`](../../Source/ArdaRenderGraph/Private/ArdaRenderGraphBuilder.cpp#L1098-L1117)
and
[`CompilerUsesManualDependenciesAsCullingEdges`](../../Source/ArdaRenderGraph/Tests/ArdaGraphTests.cpp#L671-L693).

There is no public “manual synchronization-only” API.

## 13. Determinism, invariants, and cost

Let:

- `M` be enumerated metadata leaves, including recursively visited uniform
  contents;
- `A` be resource accesses among those leaves;
- `R` be readers in the current epoch of a resource; and
- `D` be the number of existing edges on a pass.

Metadata enumeration itself is `O(M)`. A read performs ownership checks, appends
one state, and linearly deduplicates the reader and producer lists. A write also
walks all `R` readers and linearly deduplicates synchronization edges.
Therefore the common case is close to `O(M + total readers closed by writes)`,
but the vector-based uniqueness checks make worst-case construction
superlinear—up to quadratic in heavily repeated declarations/edges.

The build phase preserves these invariants for valid public use:

- resource histories refer only to already appended passes;
- automatic edges point from lower handles to higher handles;
- incoming pass edges are unique;
- resource reader epochs contain unique pass handles;
- every raw state entry identifies the exact range declared by metadata; and
- views/uniform buffers recorded on a pass belong to the same builder.

The compiler later sorts incoming edges before constructing reverse consumers,
so result ordering does not depend on which resource field first introduced a
duplicate edge.

## Source-backed tests to read next

- [`ParameterEnumerationRecursesThroughNestedStructsAndArrays`](../../Source/ArdaRenderGraph/Tests/ArdaGraphTests.cpp#L348-L397)
  verifies declaration-order nested enumeration.
- [`BuilderDeduplicatesExternalImportsAndRootsExternalWrites`](../../Source/ArdaRenderGraph/Tests/ArdaGraphTests.cpp#L485-L535)
  demonstrates prologue history plus read-before-write synchronization.
- [`SetupTraversesViewsAndNestedUniformBufferMetadata`](../../Source/ArdaRenderGraph/Tests/ArdaGraphTests.cpp#L612-L669)
  covers view parents and uniform recursion.
- [`CompilerUsesWholeBufferStatesAndExecutionOrderLifetimes`](../../Source/ArdaRenderGraph/Tests/ArdaGraphTests.cpp#L1175-L1233)
  demonstrates whole-buffer behavior even for disjoint declared ranges.

At this point the graph has raw declarations and incoming edges, but no
epilogue, liveness, lifetimes, lowered transitions, or queue waits. Those are
the compiler's job.

---

[← Debugging and recommended practices](07-Debugging-and-Practices.md) ·
[Documentation home](README.md) ·
[Next: Compiler source walkthrough →](09-Compiler-Source-Walkthrough.md)
