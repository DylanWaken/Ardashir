# 9. Compiler source walkthrough

[← Build and edge walkthrough](08-Build-and-Edge-Walkthrough.md) ·
[Documentation home](README.md) ·
[Next: Allocation and materialization →](10-Allocation-and-Materialization.md)

This chapter reads
[`FARDGCompiler::Compile`](../../Source/ArdaRenderGraph/Private/ArdaRenderGraphCompiler.cpp#L808-L888)
in call order. The goal is not merely to list functions, but to state what each
one consumes, what invariant it establishes, and what its algorithm costs.

The compiler is deliberately small: one public static entry point and private
free functions in an anonymous namespace. Its output is deterministic metadata;
the executor is responsible for physical allocation, command recording, and
submission.

## Current behavior and proposed behavior

- **Actual now** describes checked-in code and tests.
- **Future direction** is architectural commentary only. It is not implemented
  and should not be inferred from type names such as “raster group” or “fork.”

## Notation for complexity

The estimates below use:

- `P`: all passes after epilogue creation;
- `L`: live passes in `mExecutionOrder`;
- `T`, `B`: logical texture and buffer counts;
- `A`: raw texture plus buffer access records;
- `E`: producer plus synchronization edges;
- `Edata`: producer edges only;
- `S`: total texture cells, `sum(mipLevels * arraySize)`;
- `Q`: texture cells covered after expanding declared access ranges;
- `H`: for each `(pass, touched texture)` pair, the full cell count of that
  texture, with `Qlive` and `Hlive` denoting live-pass subsets; and
- `K`: emitted texture plus buffer transitions.

NVRHI's maximum color-attachment count is treated as a constant. Hash-set
operations are average `O(1)` unless stated otherwise.

These definitions matter because the current code sometimes allocates an outer
vector of size `T` or `B` for every pass, and sometimes scans all subresources
of a texture once that texture is touched. A loose “linear in passes” claim
would hide that cost.

## Entry: timer, implementation access, and idempotence

```text
start "ARDG Compile" scope timer
Graph = *Builder.mImpl
if Graph.mbCompiled:
    return Graph.mCompileResult
```

The first compile owns the mutation of compiler products. Later direct calls
return the same object without rerunning validation or stages. The public
builder wrapper also guards lifecycle state and sets `mbCompiling` around this
call:
[`FARDGBuilder::Compile`](../../Source/ArdaRenderGraph/Private/ArdaRenderGraphBuilder.cpp#L1119-L1134).

**Invariant on entry:** graph construction is finished for this compile
attempt; the prologue, user passes, logical resources, raw access states,
last-writer/readers histories, and incoming dependency edges already exist.

**Cost:** `O(1)` before the first real stage; cached compilation is `O(1)`.

## 1. `ValidateBeforeCompile(Graph)`

The first call is
[`FARDGValidation::ValidateBeforeCompile`](../../Source/ArdaRenderGraph/Private/ArdaRenderGraphValidation.cpp#L365-L478).
It validates all registered passes, including work that culling may later
remove.

### 1.1 Ownership, flags, and state legality

The resource loops check known flags, external/transient exclusion, consistency
between external flags and physical backing, and legal initial/final states.
The pass loop checks known pass flags and each raw access:

- handle exists;
- state is non-`Unknown` and legal;
- state belongs to texture or buffer domain;
- range resolves in bounds;
- descriptor permits UAV/attachment use; and
- explicit copy/async flags do not claim an unsupported state.

State masks and legality helpers are at
[`ArdaRenderGraphValidation.cpp` lines 15–158](../../Source/ArdaRenderGraph/Private/ArdaRenderGraphValidation.cpp#L15-L158).
Access checks are at
[`lines 160–225](../../Source/ArdaRenderGraph/Private/ArdaRenderGraphValidation.cpp#L160-L225).

### 1.2 Extraction record integrity

Texture and buffer extraction arrays are checked with sets for duplicate
logical resources and duplicate output addresses. Setup APIs already reject
many bad calls; compile validation protects the stored representation as a
second boundary.

### 1.3 Produced-before-read replay

[`ValidateProducedBeforeRead`](../../Source/ArdaRenderGraph/Private/ArdaRenderGraphValidation.cpp#L227-L362)
creates:

- a produced bit for every texture cell, initialized from `IsExternal()`; and
- one produced bit per whole buffer, also initialized from `IsExternal()`.

For each non-sentinel pass it first gathers writes by texture cell and by buffer
identity. It then validates reads against either previous production or a write
in the same pass. Finally it commits this pass's writes to the global produced
sets.

The two-phase per-pass design makes read/write in one pass legal without making
a read of another texture mip legal.

### Validation invariant

After this stage, every stored declaration is structurally meaningful, and
registration order provides an initialization path for every graph-created
read. This does not prove shader behavior, independently captured physical
handles, or that a callback really performs its declared write.

### Validation complexity

Resource/pass structural validation is `O(T + B + P + A)`. Production replay
initializes `O(S + B)` state. For every pass it creates an outer texture vector
of size `T`; it also expands declared texture ranges and scans the allocated
write bitmap for each texture touched by that pass. A more faithful bound is:

```text
O(S + B + P*T + Q + H + A)
```

Extraction checks add expected `O(number of extractions)` time and memory.
Peak production-state memory is `O(S + B)`, plus per-pass texture write maps and
buffer-write set.

**Future direction:** interval/range-aware buffer production would increase
precision but replace the one-bit buffer model.

## 2. Reject producer-less graph-created extraction

The next two loops inspect textures and buffers:
[`Compile` lines 819–838](../../Source/ArdaRenderGraph/Private/ArdaRenderGraphCompiler.cpp#L819-L838).

```text
for each resource:
    if extracted and not external and lastProducer is invalid:
        fail
```

This check uses build-time whole-resource `mLastProducer`. It is separate from
read validation because an unconsumed extracted output may never be read inside
the graph, but it must still have storage contents to export.

**Invariant:** every extracted graph-created resource has at least one writer.

**Cost:** `O(T + B)`.

## 3. Append and connect `GraphEpilogue`

Compilation appends a sentinel named `GraphEpilogue` and stores its handle in
the compile result:
[`Compile` lines 840–874](../../Source/ArdaRenderGraph/Private/ArdaRenderGraphCompiler.cpp#L840-L874).

Then for every external or extracted resource:

```text
if lastProducer is valid:
    epilogue.addUniqueProducer(lastProducer)

for reader in readersSinceLastWrite:
    epilogue.addUniqueSynchronizationProducer(reader)
```

The producer edge makes the latest externally observable write a culling root.
Synchronization readers are ordered before graph exit if they survive, but do
not become live merely because the resource is external.

The epilogue is appended last, so all of its predecessors are earlier handles.
Its transition arrays are populated later by `CompileBarriers`.

### Epilogue invariant

There is now one final sentinel after all user passes. Every external/extracted
last writer contributes a data/liveness path to it, and every trailing reader
contributes an ordering-only path.

### Epilogue complexity

The loops are `O(T + B + trailing readers)`, excluding vector uniqueness cost.
`FARDGPass::AddProducer` and `AddSynchronizationProducer` linearly scan their
existing vectors, so a graph with many distinct output writers/readers can make
this stage quadratic in the epilogue's degree.

## 4. `AssignPipelines(Graph)`

[`AssignPipelines`](../../Source/ArdaRenderGraph/Private/ArdaRenderGraphCompiler.cpp#L149-L174)
scans all passes and writes exactly one `EARDGPipeline`:

```text
pipeline = Graphics

if not immediate and not sentinel and
   flags contain Copy and copy queue exists and all states are copy-only:
    pipeline = Copy
else if not immediate and not sentinel and
        flags contain AsyncCompute and compute queue exists and
        all states are async-compatible:
    pipeline = AsyncCompute

pass.pipeline = pipeline
```

Copy wins if malformed/custom flags somehow contain both requests, although
normal flag validation forbids incompatible combinations. `AsyncCompute`
eligibility also implies `Compute` because pass-flag validation requires that
pair.

`IsCopyCompatible` accepts only nonzero subsets of `CopySource | CopyDest`.
`IsAsyncComputeCompatible` rejects graphics-only bits and requires
`NonPixelShaderResource` whenever a pixel-resource bit is present. The helper
code is
[`lines 83–147](../../Source/ArdaRenderGraph/Private/ArdaRenderGraphCompiler.cpp#L83-L147).

### Queue-selection invariant

Every pass, including soon-to-be-culled passes, has a deterministic pipeline.
Sentinels and immediate mode are graphics. Queue assignment does not alter
dependencies or state declarations.

### Queue-selection complexity

`O(P + A)`: each pass and each raw state record is inspected at most once in
compatibility tests.

**Future direction:** there is no timing estimate or scheduling search. A
future queue optimizer must preserve capability/state legality and deterministic
fallback.

## 5. `BuildConsumerEdges(Graph)`

Build setup stored only incoming edges. This stage creates reverse adjacency:
[`BuildConsumerEdges`](../../Source/ArdaRenderGraph/Private/ArdaRenderGraphCompiler.cpp#L176-L220).

### Algorithm

1. clear every pass's `mConsumers` and `mSynchronizationConsumers`;
2. for each consumer, sort `mProducers`;
3. validate every producer exists and has a strictly lower handle;
4. add the consumer uniquely to that producer;
5. repeat for synchronization producers.

The strict handle check is:

```text
producer exists
producerHandle < consumerHandle
producerHandle != consumerHandle
```

### No topological sort, and how cycles are prevented

**Actual now:** there is no Kahn sort, depth-first topological sort, strongly
connected-component pass, or general cycle detector.

Cycle prevention is structural:

- automatic dependencies are added from historical writers/readers to the
  currently appended pass;
- imported resources begin at the already-existing prologue;
- `AddDependency` requires the producer to be registered no later than—and,
  after the distinctness check, strictly before—the consumer; and
- the epilogue is appended last.

`BuildConsumerEdges` verifies that representation. If every edge strictly
increases the handle, a directed cycle is impossible. Registration order is
therefore already a topological order.

This is also why the compiler cannot reorder work for overlap: it never
constructs a new topological schedule. It only filters registration order after
culling.

**Future direction:** allowing backward edge declarations or pass reordering
would require a real cycle check and deterministic topological scheduling.

### Consumer-edge invariant

For every valid incoming producer or synchronization edge, the matching reverse
outgoing edge exists exactly once. Incoming lists are sorted.

### Consumer-edge complexity

Conceptually, sorting costs `O(sum(d_in log d_in))`, bounded by
`O(E log E)`, and mirroring costs `O(E)`. The current `AddConsumer` methods
linearly deduplicate outgoing vectors, adding a worst-case
`O(sum(d_out^2))` term for high-fan-out producers. Memory is `O(E)` for reverse
edges.

## 6. `CullPasses(Graph)`

[`CullPasses`](../../Source/ArdaRenderGraph/Private/ArdaRenderGraphCompiler.cpp#L222-L278)
performs reverse reachability on data/liveness edges.

### Normal-mode algorithm

```text
mark every non-sentinel pass culled
worklist = [epilogue] + every NeverCull pass

while worklist not empty:
    pass = pop
    if pass is already live and is not a sentinel:
        continue
    mark pass live
    for producer in pass.producers:       # not synchronization producers
        if producer is culled:
            push producer

executionOrder = all live registry entries, scanned in handle order
```

Synchronization edges are excluded because a read-before-later-write hazard
does not make the read semantically necessary to produce the later version.

### Immediate-mode algorithm

Immediate mode clears execution order, marks every pass live, and appends all
registry handles directly. It is a diagnostic correctness mode, not a normal
optimization path.

### Culling invariant

Every producer ancestor of epilogue or `NeverCull` roots is live. Every other
non-sentinel pass is culled. `mExecutionOrder` is a stable subsequence of
registration order and includes both sentinels.

No scheduling occurs here. Independent live passes keep their original relative
order.

### Culling complexity

`O(P + Edata)` time and `O(P)` worklist space. Synchronization edges do not
participate.

## 7. `RebuildLiveResourceIntervals(Graph)`

Build-time `MarkUsed` data includes culled passes. This stage discards it:
[`RebuildLiveResourceIntervals`](../../Source/ArdaRenderGraph/Private/ArdaRenderGraphCompiler.cpp#L280-L319).

```text
reset first/last use on all textures and buffers
for pass in executionOrder:
    mark every declared texture and buffer used by pass.handle
for each external or extracted resource:
    mark epilogue used
```

`MarkUsed` compares pass handles, which is valid because execution order is a
registration-ordered subsequence. It stores the smallest and largest live
handle, not execution indices yet.

An external/extracted resource is marked at epilogue even if its last live user
is earlier, because graph-exit state/ownership work extends its logical
lifetime. A resource with no live use becomes epilogue-used only if this final
loop marks it; in normal construction an external/extracted resource with no
live writer/read can therefore still acquire epilogue use. Barrier lowering
will see it as used and may consider final state, though equal initial/final
state emits no ordinary transition.

### Live-use invariant

Resource first/last-use handles now describe live declarations plus graph-exit
participation, not the pre-cull build.

### Live-use complexity

`O(T + B + live raw accesses)`.

## 8. `CompileResourceLifetimes(Graph)`

[`CompileResourceLifetimes`](../../Source/ArdaRenderGraph/Private/ArdaRenderGraphCompiler.cpp#L321-L384)
converts handles into execution coordinates.

### Algorithm

1. allocate `ExecutionIndices` with one slot per pass, initialized invalid;
2. map every live pass handle to its index in `mExecutionOrder`;
3. scan textures in registry order, then buffers in registry order;
4. for each resource with valid first use, emit an inclusive interval;
5. mark transient eligibility only when the resource has `Transient` and is
   neither external nor extracted;
6. in extended-lifetime or immediate mode, overwrite every interval with
   `[0, L - 1]`.

### Lifetime invariant

Every emitted interval uses execution-order indices, is inclusive, and belongs
to a live logical resource. Result ordering is deterministic: textures first in
texture registry order, then buffers in buffer registry order.

The lifetime is whole-resource. Separate mips do not receive separate physical
intervals.

### Lifetime complexity

`O(P + T + B + number of emitted lifetimes)` time and `O(P)` temporary memory.

## 9. `CompileBarriers(Graph)`

This is the largest compiler stage:
[`CompileBarriers`](../../Source/ArdaRenderGraph/Private/ArdaRenderGraphCompiler.cpp#L386-L634).

### 9.1 Initialize state trackers

For each texture, allocate `mipLevels * arraySize` states. For each buffer,
allocate one state. Replace unknown initial state with `Common`; preserve a
known imported state.

Then clear transition arrays on every pass, including culled passes and
sentinels. This makes the stage deterministic if internal code were ever
re-entered before `mbCompiled` is set.

### 9.2 Skip sentinels during ordinary pass lowering

The compiler walks `mExecutionOrder`. It skips sentinel pass declarations;
prologue has none, and epilogue transitions are generated separately at the end.

### 9.3 Build per-pass required texture states

For each raw texture access:

1. normalize combined shader-resource state for async compute;
2. lazily allocate a required-state array for that logical texture;
3. resolve the declared subresource set;
4. merge the state into every selected cell.

`MergePassState` accepts:

- unknown + required → required;
- equal + equal → equal;
- two different read-only states → bitwise OR; and
- any conflicting combination involving a write → fatal check.

Although pre-compile validation already rejects write/read bit mixtures in one
individual declaration, this merge catches conflicts spread across several
members in the same pass.

### 9.4 Emit one texture transition per required cell

The compiler scans every cell of each touched texture. For non-unknown required
cells it emits:

```text
texture
single-mip, single-slice subresource set
stateBefore = current[cell]
stateAfter = required[cell]
uavBarrier = before == after and after contains UAV
forceBarrier = conservative mode and before == after and
               not uavBarrier and after != Common
```

It then sets `current[cell] = required[cell]`.

Equal ordinary states still produce records. This is intentional pass-local
state metadata. Equal UAV states additionally request memory ordering.

### 9.5 Merge and emit whole-buffer transitions

For buffers, the compiler allocates one `RequiredBuffers[B]` array for the
pass. Every raw access to a buffer merges into one slot; `mRange` does not
partition state. A single transition is emitted for each required buffer, with
the same UAV/forced rules.

This means accesses to two disjoint byte ranges can conflict or require a UAV
barrier because the state machine is whole-buffer.

### 9.6 Emit graph-exit transitions on epilogue

For every used external or extracted texture, the compiler compares every cell
with the resource's one final state. It emits a transition when states differ
or when equal UAV state needs ordering. Buffers receive the equivalent
whole-resource check.

Ordinary equal final states are omitted. Epilogue transitions do not update the
local `Current` arrays afterward, but no later lowering consumes those arrays;
transition validation replays the emitted result independently.

### State granularity invariant

**Actual now:**

- texture state and transition continuity are per mip and array slice;
- texture dependency history and physical lifetime are whole logical texture;
- buffer dependency history, production, state, transitions, and lifetime are
  whole logical buffer; and
- a declared buffer range is validated and retained, but does not partition
  dependency or state tracking.

These statements are easy to conflate. The source models each layer
differently.

### Barrier complexity

Initialization costs `O(S + B + P)`. For live passes, outer required arrays cost
`O(L*(T + B))`; declared texture ranges contribute `Qlive`; scanning every cell
of each touched texture contributes `Hlive`; raw buffer/texture declarations add
`O(A_live)`. Final texture checks add the cell counts of used external/extracted
textures.

A representative bound is:

```text
O(S + B + P + L*(T+B) + Qlive + Hlive + A_live + finalTextureCells)
```

Temporary state is `O(S + B + T + B)` at peak, excluding emitted transitions.
Output storage is `O(K)`.

**Future direction:** sparse maps could avoid `L*T`/`L*B` outer scans, and
range-aware buffer tracking could reduce false hazards, but both would alter
complexity and determinism trade-offs.

## 10. `ValidateTransitions(Graph)`

Immediately after lowering, the compiler calls
[`FARDGValidation::ValidateTransitions`](../../Source/ArdaRenderGraph/Private/ArdaRenderGraphValidation.cpp#L480-L648).
This is an independent replay, not a continuation of the lowering trackers.

### Algorithm

1. recreate per-texture-cell and per-buffer current state from graph entry;
2. walk execution order;
3. for every transition, require replayed current state equals
   `mStateBefore`, then assign `mStateAfter`;
4. for every declared access, require:
   - exact equality for writes; or
   - `(current & required) == required` for reads;
5. after all passes, require every used external/extracted resource reaches its
   final state.

Read containment matters because per-pass merging may combine compatible
read-only bits. Write states are intentionally exact.

### Transition-validation invariant

Every compiled transition sequence is continuous, satisfies the declarations
on the selected pipeline, and fulfills graph-exit contracts.

### Transition-validation complexity

`O(S + B + K + Q_live + finalTextureCells + live buffer accesses)` time and
`O(S + B)` replay memory.

## 11. `CompileAsyncMetadata(Graph)`

[`CompileAsyncMetadata`](../../Source/ArdaRenderGraph/Private/ArdaRenderGraphCompiler.cpp#L673-L771)
computes diagnostics for each live async-compute pass.

### Backward fork search

```text
fork = prologue
worklist = producers + synchronizationProducers
visited = {}

while worklist:
    predecessor = pop
    skip if visited or culled
    if predecessor.pipeline == Graphics:
        fork = max(fork, predecessor.handle)
    else:
        push both predecessor edge kinds
```

Traversal stops through a path at its first graphics node. The greatest handle
among reached graphics boundary nodes is retained.

### Forward join search

The mirror walk starts from consumers plus synchronization consumers:

```text
join = epilogue
...
if successor.pipeline == Graphics:
    join = min(join, successor.handle)
else:
    push both successor edge kinds
```

### Async metadata invariant

Every live async pass has valid fallback boundaries: prologue if no graphics
predecessor is reached, epilogue if no graphics successor is reached.
Non-async passes have invalid fork/join handles reset by this stage.

**Actual now:** these fields are diagnostics. Explicit
`mQueueDependencies` drive submission synchronization.

### Async metadata complexity

Each async pass launches two graph traversals with its own visited sets. If
`C` live async passes exist, worst-case time is `O(C*(P + E))` and per-search
temporary memory is `O(P)`. The implementation does not memoize shared
ancestry.

**Future direction:** region formation or cached reachability could reduce
repeated traversal and make fork/join scheduling inputs rather than diagnostics.

## 12. `CompileQueueDependencies(Graph)`

[`CompileQueueDependencies`](../../Source/ArdaRenderGraph/Private/ArdaRenderGraphCompiler.cpp#L636-L671)
lowers graph edges into cross-pipeline records.

For each live consumer:

1. copy data producers;
2. append synchronization producers;
3. sort and deduplicate the combined list;
4. skip culled producers;
5. skip edges whose endpoints use the same pipeline;
6. emit producer/consumer handles and both pipelines.

Result order is deterministic: consumer execution order, then sorted producer
handle. Producer and synchronization edges collapse to one queue dependency if
both name the same pair.

Queue dependencies may involve sentinels. For example, an extracted resource
last written on async compute can generate async → graphics-epilogue
synchronization for its final transition/submission boundary.

### Queue-dependency invariant

Every live cross-pipeline graph edge has one explicit compile record; no
same-pipeline edge has one.

### Queue-dependency complexity

Copying and scanning is `O(E_live)`. Per-consumer sorting costs
`O(sum(d log d))`, bounded by `O(E_live log E_live)`. Output memory is at most
`O(E_live)`.

## 13. `CompileRasterGroups(Graph)`

The final helper is
[`CompileRasterGroups`](../../Source/ArdaRenderGraph/Private/ArdaRenderGraphCompiler.cpp#L773-L805).

It walks execution order and resets each pass's group to `UINT32_MAX`. A pass is
groupable only if it is graphics `Raster` without `SkipRenderPass`.
Non-groupable work clears `PreviousRaster`, breaking adjacency.

For a groupable pass:

- start a new group when there is no previous raster or attachment signatures
  differ;
- otherwise copy the previous raster's group.

The signature comparison includes all logical color/depth handles and exact
subresource sets:
[`FARDGRasterBindingSignature`](../../Source/ArdaRenderGraph/Public/ArdaRenderGraphPass.h#L100-L135).

### Raster-group invariant

Group indices are dense from zero, and a group is one maximal consecutive run
of compatible live graphics raster passes.

### Raster-group complexity

`O(L)` with a fixed NVRHI attachment-slot count, `O(1)` extra memory.

**Actual now:** groups are compile metadata only.

**Future direction:** command-list/render-pass merging would need executor and
backend work plus load/store and attachment-lifetime rules.

## 14. Commit the result

Only after every stage succeeds:

```text
Graph.mbCompiled = true
return Graph.mCompileResult
```

The result structure is
[`FARDGCompileResult`](../../Source/ArdaRenderGraph/Public/ArdaRenderGraphBuilder.h#L142-L162).
Detailed products remain in each
[`FARDGPassState`](../../Source/ArdaRenderGraph/Public/ArdaRenderGraphPass.h#L137-L190).

### Final compiler invariants

- graph topology is immutable through normal builder APIs;
- execution order contains exactly live passes in registration order;
- every edge points forward and has reverse adjacency;
- lifetimes use inclusive execution indices;
- texture transitions are per mip/slice;
- buffer transitions are whole-resource;
- transition replay satisfies all live declarations and final states;
- every async pass has fork/join diagnostics;
- every cross-pipeline live edge has a queue dependency; and
- raster groups describe only consecutive compatible live raster passes.

## End-to-end cost profile

Ignoring vector-deduplication pathologies, the compiler is mostly linear in
passes, declarations, edges, and expanded texture cells. The notable
nonlinear/dense terms in the actual implementation are:

- `P*T` work in produced-before-read validation;
- `L*(T+B)` outer arrays/scans during barrier lowering;
- full texture-cell scans represented by `H` and `Hlive`;
- per-consumer edge sorting;
- vector-based unique insertion, which can become quadratic at high degree;
  and
- two graph traversals per live async-compute pass.

For ordinary frame graphs with small per-pass resource sets, these constants
may dominate less than command recording. For very large generated graphs,
measure these specific terms rather than treating `Compile()` as abstract
`O(P+E)`.

## Source-backed test map

All current render-graph tests are in
[`ArdaGraphTests.cpp`](../../Source/ArdaRenderGraph/Tests/ArdaGraphTests.cpp).
The compiler-focused cases are:

- [`CompilerTracksProducersCullsDeadPassesAndPreservesSentinels`](../../Source/ArdaRenderGraph/Tests/ArdaGraphTests.cpp#L537-L610):
  epilogue/prologue, culling, execution-visible uses;
- [`CompilerUsesManualDependenciesAsCullingEdges`](../../Source/ArdaRenderGraph/Tests/ArdaGraphTests.cpp#L671-L693):
  manual producer-edge liveness;
- [`CompilerAssignsQueueFallbackAndAsyncForkJoinMetadata`](../../Source/ArdaRenderGraph/Tests/ArdaGraphTests.cpp#L695-L799):
  capabilities, fallback, async normalization, fork/join;
- [`CompilerGroupsCompatibleConsecutiveRasterPasses`](../../Source/ArdaRenderGraph/Tests/ArdaGraphTests.cpp#L801-L842):
  attachment-signature grouping;
- [`CompilerRejectsReadBeforeProduce`](../../Source/ArdaRenderGraph/Tests/ArdaGraphTests.cpp#L844-L863):
  graph-created initialization;
- [`CompilerRejectsIllegalQueueStatesAndSubresourceReads`](../../Source/ArdaRenderGraph/Tests/ArdaGraphTests.cpp#L938-L993):
  explicit queue-state rejection and per-mip production;
- [`DebugModesExposeConservativeBarriersAndExtendedLifetimes`](../../Source/ArdaRenderGraph/Tests/ArdaGraphTests.cpp#L995-L1060):
  immediate mode, forced equal-state barrier, extended intervals;
- [`GraphDumpIsDeterministicAndContainsCompilerProducts`](../../Source/ArdaRenderGraph/Tests/ArdaGraphTests.cpp#L1062-L1084):
  stable repeated dump;
- [`CompilerLowersTextureSubresourcesUavAndFinalTransitions`](../../Source/ArdaRenderGraph/Tests/ArdaGraphTests.cpp#L1112-L1173):
  per-mip state, repeated UAV ordering, epilogue state;
- [`CompilerUsesWholeBufferStatesAndExecutionOrderLifetimes`](../../Source/ArdaRenderGraph/Tests/ArdaGraphTests.cpp#L1175-L1233):
  whole-buffer state despite disjoint ranges and inclusive live interval;
- [`CompilerLowersCrossQueueDependencies`](../../Source/ArdaRenderGraph/Tests/ArdaGraphTests.cpp#L1235-L1285):
  graphics-to-async dependency output; and
- [`RecordsIndependentPassesAndSubmitsCrossQueueWaits`](../../Source/ArdaRenderGraph/Tests/ArdaGraphTests.cpp#L1542-L1639):
  integration coverage showing compiler queue records become executor waits.

The suite validates representative behavior, not every combinatorial path.
Notably, transition-discontinuity checks primarily defend compiler invariants;
public APIs do not expose a normal way to inject arbitrary compiled transition
records for a direct negative test.

## What is not in this source file

`ArdaRenderGraphCompiler.cpp` does not:

- materialize logical resources;
- pool or alias physical memory;
- turn transition records into NVRHI calls;
- record pass lambdas;
- form CPU recording levels;
- signal/wait queue instances;
- submit command lists; or
- publish extracted handles.

Those are executor responsibilities. Keeping that boundary in mind prevents
compiler metadata such as `mAsyncFork` or `mRasterGroup` from being mistaken
for execution that has not happened.

---

[← Build and edge walkthrough](08-Build-and-Edge-Walkthrough.md) ·
[Documentation home](README.md) ·
[Next: Allocation and materialization →](10-Allocation-and-Materialization.md)
