# Persistent compute and graphics graphs with ArdaInductor

`FArdaDependencyGraph` is the persistent graph API. It owns typed node instances, logical resources, a compiled schedule, reusable physical storage, and a pipeline cache. `EndGraphEdit()` invokes **ArdaInductor**; `Execute()` submits the compiled work repeatedly. Regions with one producer are independent of attachment order. Regions with multiple writer nodes use original successful attachment order to determine which contents each read observes.

The public entry points are [ArdaDependencyGraph.h](../../Source/ArdaInfra/ArdaRenderGraph/Public/ArdaDependencyGraph.h), [ArdaDependencyNode.h](../../Source/ArdaInfra/ArdaRenderGraph/Public/ArdaDependencyNode.h), [ArdaDependencyGraphNodes.h](../../Source/ArdaInfra/ArdaRenderGraph/Public/ArdaDependencyGraphNodes.h), and [ArdaDependencyGraphCuda.h](../../Source/ArdaInfra/ArdaRenderGraph/Public/ArdaDependencyGraphCuda.h). The [pipeline guide](ArdaInductor-Pipelines.md) covers automatic graphics, meshlet, compute, ray-tracing, and work-graph PSOs. `ArdaRenderGraph.h` is the umbrella for the persistent APIs; execution options and frame reports are declared in `ArdaDependencyGraphExecution.h`.

## Separation of responsibilities

`Ardashir::ArdaGraph` is a separate library with no graphics or CUDA dependency. Its [TArdaDirectedGraph](../../Source/ArdaInfra/ArdaGraph/Public/ArdaGraph.h) accepts arbitrary node and edge payloads. It stores dense live node/edge lists, per-node incoming/outgoing adjacency lists, neighbor hash maps, and an endpoint-pair edge index. Node/edge lookup and duplicate-edge lookup take expected constant time; node removal visits incident edges. Deterministic Kahn ordering and iterative cycle detection avoid depending on hash iteration. Generational handles reject deleted, foreign, and abandoned-edit identities. Snapshots share an identity lineage while copying graph contents.

`Ardashir::ArdaRenderGraph` adds typed resource semantics and execution. Its global `FArdaNodeRegistry::Get()` stores immutable library definitions under unique names and synchronizes registration/lookup. Built-ins are installed automatically: `arda.upload`, `arda.copy-buffer`, `arda.clear-buffer`, `arda.readback`, and `arda.sync`. Applications register their own graphics, compute, copy, or CUDA definitions in the same registry. A definition is shared; a graph owns each instance's parameters and name. `Unregister()` removes future library lookup while existing instances retain their definition. Class registrations retain no device. Device-bound CUDA operands are attachment parameters retained by graph instances until retirement.

ArdaInductor resolves semantic edges, culls unused work, infers PSOs, selects a deterministic schedule, groups CUDA operations, assigns queues, and supplies resource lifetimes to the backend memory planner. A successful edit compiles the native execution program, which retains physical frame leases, barrier plans and resolved pipelines. The [device-wide GPU allocator](../ArdaBackend/Gpu-Allocator.md) reuses compatible storage across graph instances and direct RHI callers after their active references and submissions retire. Optional background scheduling can replace a program while retaining its allocations; ordinary replay does not recreate transient allocations each frame.

Pipeline inference stops each ancestry branch before an upstream consumer requesting the same pipeline family. Consecutive compute nodes with different shaders therefore receive independent PSOs without manual group tags. Unfinished stage declarations still propagate through intermediate nodes; a shared stage provider reaches multiple consumers through independent dependency edges. Conflicting contributions inside one inference region require explicit groups or compatible settings, as described in the [pipeline guide](ArdaInductor-Pipelines.md#ancestry-intermediate-nodes-and-ambiguity).

## Self-contained node attachment

The typed [Graph.AttachOrFind&lt;Node&gt;](api-reference.html#api-arda-fardadependencygraph-attachorfind-ea35df5c) overload lets a renderer
compose operations without registering shaders or building binding layouts.
A node derives from the [master CRTP base](api-reference.html#api-arda-tardadependencynode-35213481),
usually through its graphics, compute, copy, CUDA or synchronization specialization.
CRTP passes the concrete class as the base template's first argument, so required
hooks are checked at compilation without adding virtual calls. The common
[root](api-reference.html#api-arda-fardadependencynodebase-2e1c7f13) provides the inheritance
identity; the templated base implements registration, attachment and state lifetime.
Each node supplies static metadata, a canonical key, a resource/pipeline description
and its execution hook. Native nodes implement `Record`; CUDA nodes implement
`PrepareCuda` to append operations to the supplied sequence without submitting it.
Synchronization nodes may omit execution. The public
[contract trait](api-reference.html#api-arda-tardadependencynodecontract-a4cadef5)
also lets libraries check a class without registering it.

Typed attachment checks the edit transaction, then uses the base's shared path.
The [named-definition overload](api-reference.html#api-arda-fardadependencygraph-attachorfind-5ccb9fb8)
looks up the same registered class and validates its public parameter type. Explicit
requirements run first, followed by logical output declaration and canonical-key
comparison. Matching instances skip preparation; conflicting semantics fail before
setup. A new instance validates its public parameters, obtains immutable device
state, creates private instance state and describes its accesses. The base retains
these objects in private execution storage. All authors use the class hooks;
there is no callback-definition registration or separately authored prepared schema.

All examples use individual node classes in `Public/Nodes` and matching `.cpp`
files in `Private/Nodes`. The umbrella headers only include classes. There is no
renderer-owned node library to initialize. Terrain's generate node owns its
shader/parameter metadata, layout, compute configuration and callback; attaching
an upload does not prepare the generate or draw shaders. A node declaring `FArdaState`
implements `Prepare(Device)` and returns retained immutable state. A node declaring
`FArdaInstanceState` implements `CreateInstance(Device, Parameters, State)` and returns
its private mutable cache. Both state types can be forward-declared in the public
node header and defined in its implementation file. Stateless defaults cover
nodes needing neither setup nor private runtime state. The base weakly caches device
state by concrete node type and device. Bound parameters hold strong references,
so live graphs reuse setup and retiring them releases the state and device.
The compiler prepares binding sets, bindless tables, relocated shader parameters and ray shader tables per compiled frame slot after physical allocation. Recording hooks use context state helpers; node instance caches do not manage these objects. See [automatic bindings](ArdaInductor-Bindings.md) and [complete node recipes](node-recipes.html).

Device setup must be determined by node type/device. Parameter-dependent setup
belongs in `CreateInstance`; declare any retained GPU workspace in `Describe`.
Do not hide dependencies in prepared state. A library can call the base's
[Register](api-reference.html#api-arda-tardadependencynode-register-296cf60a)
to populate the singleton before devices or graphs exist. Registration is
idempotent for the same class/version/domain and rejects another implementation
using that name. Metadata remains constant for the class. Unregistering removes
lookup but leaves existing graph instances intact; later registration installs
a new definition identity. Shared device state remains immutable, and graph edits
and submission retain their single-owner CPU contract. The renderer can update
explicit dynamic inputs between recordings; nested semantic pointees are not
deep-copied by parameter snapshots.

First attachment can compile/load shaders and allocate layouts on the CPU/RHI
side; this work is outside ordinary graph replay. ArdaInductor creates PSOs during
`EndGraphEdit`, then reuses the compiled execution program. Typed attachment adds
no GPU submission or wait; node preparation hooks must follow that rule too.
Preparation failure leaves the edit open and does not attach that node. Returning
success with null state is rejected. The base retains only successful live setup,
so repair and retry are possible. Cancelling an edit releases new bound parameters
and restores removed ones. Removing a committed node releases its state after
the edit commits and outstanding owners retire; other live graphs can retain it.
Process-wide registrations store callbacks without retaining a device. See the
[worked custom-node recipe](node-recipes.html#compute) for a complete operation.

See the [registration recipes](node-recipes.html) for native compute, indexed raster, fullscreen graphics, ray tracing and CUDA nodes, including an authored CUDA class. They show optional library discovery before devices exist and automatic registration during typed attachment.

## Node hardware and environment admission

The common node base provides an overridable static [GetRequirements](api-reference.html#api-arda-tardadependencynode-getrequirements-defdae36) hook returning [FArdaDependencyNodeRequirements](api-reference.html#api-arda-fardadependencynoderequirements-7513e792). Select portable features, minimum limits, CUDA support and named read-only environment predicates from immutable public parameters. The default imposes no explicit requirements. The hook runs before argument validation, output declaration, preparation and instance setup, so it must safely handle unvalidated inputs and must not allocate, submit, wait or mutate external state.

Typed class attachment and lookup by registered class name share this check. Explicit requirements are evaluated on every attachment, including an existing-name lookup. New descriptions add recognized pipeline, bindless and resource-state requirements before graph binding setup and insertion. The descriptor's requirement field is graph-populated output; authors use the hook. Unsupported reports identify the node and aggregate missing abilities. Failure preserves the open edit, inserts no node and rolls back that attachment's provisional outputs; a failed reattachment preserves the existing instance.

An empty explicit set permits device-free semantic analysis when other hooks also work without a device; inferred requirements remain recorded without device enforcement. Nonempty explicit requirements are never assumed supported without a device. Dedicated compute/copy families are not inferred from the node domain, preserving normal scheduling fallback. Admission does not supply missing resource effects or prove every native format, binary, pipeline or runtime constraint.

Read [the feature admission chapter](feature-requirements.html) for compiled hook examples, parameter-dependent requirements, unknown-limit semantics and failure recovery. The [source-backed feature modularity audit](Feature-Modularity-Audit.md) distinguishes missing reusable operations from missing graph resource models and missing backend implementations.

## Minimal persistent graph

This example uses a real RHI device and an existing command implementation. Check every returned status in application code, as shown here. The readback node is deliberately attached first.

```cpp
#include "ArdaDependencyGraphNodes.h"

arda::FArdaRHIStatus RunPersistentClear(arda::FArdaRHIDeviceRef Device)
{
    using namespace arda;
    FArdaDependencyGraph Graph(Device);
    if (auto S = Graph.BeginGraphEdit(); !S) return S;

    FArdaRHIBufferDesc Desc;
    Desc.mByteSize = 256;
    Desc.mUsage = EArdaRHIBufferUsage::UnorderedAccess;
    auto Output = Graph.CreateBuffer("output", Desc);
    if (!Output) return Output.mStatus;

    auto Bytes = eastl::make_shared<eastl::vector<uint8_t>>();
    auto Read = Graph.AttachOrFind<FArdaGraphReadbackNode>("read",
        FArdaGraphReadbackParameters{Output.mValue, Bytes});
    if (!Read) return Read.mStatus;
    auto Clear = Graph.AttachOrFind<FArdaGraphClearNode>("clear",
        FArdaGraphClearParameters{Output.mValue, 7});
    if (!Clear) return Clear.mStatus;
    if (auto S = Graph.EndGraphEdit(); !S) return S;

    for (unsigned Frame = 0; Frame != 3; ++Frame)
    {
        auto Run = Graph.Execute();
        if (!Run.mStatus) return Run.mStatus;
        // Bytes contains 64 uint32_t values, each equal to 7.
    }
    return {};
}
```

Readback is observable, so it retains the clear producer automatically. A GPU output with no readback must be marked with `MarkOutput()` or declared external/persistent; otherwise a producer with no observable consumers can be culled. `mExecutionOrder`, `mQueues`, `mCudaBatches`, `mMemoryDependencies`, and memory counters in `GetCompileResult()` expose the resulting plan. A graph without a device supports semantic compilation and estimated memory diagnostics, but cannot execute or enforce a native VRAM cap.

`arda.readback` uses `Context.ReadbackBuffer()` to record an asynchronous transfer. The graph submits all native work before waiting for readback completion and publishing the host vectors. This avoids a per-copy CPU wait that would delay later independent graphics submissions. Readback errors propagate through `Execute().mStatus`; failed frames clear their registered readback destinations, and unsubmitted copies are cancelled rather than waited indefinitely. `Execute()` waits for the entire graph frame, including graphs without readbacks. Use `Submit()` and tickets when the CPU should continue before completion.

A failed recording, submission, or readback completion invalidates the lowered execution plan. Repair the cause and complete a successful graph edit/recompilation before retrying `Execute()`; GPU work accepted before the failure is not rolled back.

## Parameters, resource versions, and edits

All nodes use the same class contract, including built-ins and CUDA operand adapters. The base lowers its statically checked hooks directly into framework-owned `FArdaDependencyNodeExecutable` storage. Registry lookup exposes only a retained const view; executable records cannot be constructed, copied or registered by application code. Named attachment looks up this same class registration and checks the public parameter type. Authors never provide a second prepared parameter schema: the base retains public parameters, device state and instance state privately.

Use `FArdaDependencyKeyBuilder` to encode explicit semantic fields. `Resource` includes graph identity, index and generation; `BufferRange` and `TextureRange` include every range dimension. Scalar values use their declared width in little-endian order; strings and byte spans carry lengths. Encode every behavior-affecting value and immutable pointee meaning. Do not serialize padded structs or use a digest alone as node identity.

Parameters are copied into retained const storage at attachment. Value containers are snapshots. Pipeline configuration objects are copied separately, even if the author retains a mutable pointer. Arbitrary nested pointers in custom parameter structs remain the author's responsibility: semantic input pointees must stay immutable for the instance's lifetime and their relevant contents must participate in its canonical key. Output sinks such as the built-in readback vector are intentionally mutable. Do not mutate them while execution uses them. An application can define an explicitly dynamic upload source when its contents do not affect the compiled description. Terrain updates a retained camera/time object before synchronous execution; concurrent submissions require separate snapshots or synchronization.

Ordering is resolved separately for each buffer byte region or texture mip/slice/plane region. Where exactly one node writes, every reader follows that producer, even if attached first. Where two or more distinct nodes write, overlapping write/write, write/read and read/write hazards follow original successful attachment order. A read consumes the preceding writer's contents and finishes before a following overwrite. Disjoint regions remain independent; a read spanning regions depends on the appropriate producer of each. Bindless entries contribute their resolved views to this same analysis.

For example, attach `Write1`, `Read1`, `Write2`, `Read2` on the same range of one handle. The compiler preserves that chain: the reads observe the first and second writes respectively. These operations reuse one physical graph resource within the frame slot; the second write creates no implicit resource, version, copy or allocation. Declare separate outputs if old contents must survive an overwrite. The planner may alias such separate outputs only when native ordering proves their lifetimes do not overlap. See the [resource ordering example](resources.html#worked-example).

Graph-owned reads require initialized coverage across their entire range. On a region with repeated writes, an owned read before the first writer fails; imported storage may supply its initial contents. `ReadWrite` consumes incoming contents and produces an update, so its own write cannot initialize its input. A previous writer can initialize an owned region before a later `ReadWrite` node. `mbPersistent` retains storage but does not waive this coverage rule. Explicit dependencies can add ordering, but contradicting an inferred hazard fails compilation with a cycle.

Resource access declarations are the complete hazard contract. Declare every buffer/texture and its RHI state/range, including framebuffer targets. Native callbacks resolve only declared resources through the execution context. Hidden resource access inside an external library cannot be inferred; declare its inputs, outputs and retained workspace. Non-resource side effects require explicit edges and `mbSideEffect` when observable. `arda.sync` is an empty named join: connect its predecessors and successors with `AddDependency()`. Pure joins and stage declarations generate contracted dependency edges without empty native command lists.

All mutation is restricted to `BeginGraphEdit()` / `EndGraphEdit()`. `AttachOrFind(instanceName, definitionName, parameters)` returns an existing instance only when its definition and canonical key match. Finding that instance preserves its original attachment order. A name collision with different semantics is an error; remove the old instance and attach its replacement within the edit. A replacement is appended after the surviving nodes, even if it reuses a name or internal handle slot. Resource dependencies are resolved at compilation. `FindNode()` and `GetTopology()` provide lookup and inspection.

`RemoveNode()` removes that node and recursively removes readers that consume its produced contents, followed by their affected consumers. A later independent overwrite does not consume the earlier value, while `ReadWrite` does. Ordering-only hazards and manual edges do not by themselves cause removal of their successors. An output marker is cleared only when no surviving producer remains. Logical resource declarations remain available for replacement producers. Recompilation resolves the remaining accesses again: removing a writer can return a region to the single-producer rule. `CancelGraphEdit()` restores the previous graph, handles, attachment order, resource descriptions, and options. Failed compilation keeps the edit open for correction or cancellation, and execution is unavailable while editing. Begin-edit waits for outstanding work before replacing compiled storage.

The graph is externally synchronized: edit and execution calls on one instance must not run concurrently. Registry synchronization does not make graph mutation thread-safe.

To change input data without recompiling, update the contents of already imported device storage after its prior use completes, then restore the graph's entry-state contract. Changing node parameter values, shapes, or resource identities requires an edit. Retained addresses do not make transient values into cross-frame inputs; use external storage for input state carried between executions.

## Automatic CUDA batches

Compiled-kernel and external-call operand nodes derive from `TArdaCudaOperandNode<Derived, Operand>`, which specializes the common node base. A derived class supplies `GetMetadata`; inherited hooks derive requirements, accesses and native arguments from the operand schema. Its public parameters contain `mOperation` (the retained device-bound operand) and `mArguments` (the logical-resource schema). Attach with `Graph.AttachOrFind<MyCudaNode>("instance", {Operand, Arguments})`. The operand's process-local identity participates in the key, so different device-bound operations cannot silently reuse one instance. The default visitor handles scalar and enum values; aggregate values require overriding the class key hook with explicit semantic encoding. See the [CUDA sequence guide](../ArdaBackend/CUDA-Sequences.md#persistent-dependency-graph) for a complete adapter declaration.

The helper does not infer GPU allocations hidden inside an operand or external library. Derive a custom node class whose `Describe` hook supplies `mWorkspaceBytes` when such retained storage must participate in the graph budget.

The compiler gives ready CUDA nodes an affinity preference. Consecutive CUDA nodes become a single `FArdaCudaSequence`: one graphics-to-CUDA acquisition, ordered calls on the framework stream, and one CUDA-to-graphics release. A real dependency through graphics splits the batch. CUDA grouping preserves each operand's implementation, including registered cuBLAS/cuDNN adapters; it does not merge kernel machine code. Copying textures through shared pitched buffers remains an explicitly declared operation when the interop path requires it. See [CUDA sequences](../ArdaBackend/CUDA-Sequences.md), [external calls](../ArdaBackend/CUDA-External-Calls.md), and [texture buffers](../ArdaBackend/CUDA-Texture-Buffers.md).

Each compiled CUDA batch owns a retained CUDA Graph cache per frame slot. `mCudaGraphMode` selects `Disabled`, `Prefer` (default), or `Require`. Qualified batches capture and instantiate once, then replay a `CUgraphExec`; graphics handoff waits/signals remain outside the retained graph so every frame uses fresh fence values. CiG records the graph launch into a fresh native command list. `GetCudaGraphStats()` reports capture, replay, fallback and variant counters. External library calls must explicitly opt into capture and provide a revision when hidden replay-affecting state changes. The bounded variant cache retains resource addresses, modules and prepared call state; revisiting an unchanged parameter variant reuses its executable. Python bindings and a Python tracer remain deferred. CUDA nodes still use one framework stream within a batch.

## Scheduling and memory model

The [complete compilation and scheduling chapter](compilation.html) derives every implemented algorithm: dependency construction and culling, both greedy seeds, bounded adjacent swaps, iterative topological enumeration, queue and CUDA assignment, allocation packing, EMA-driven insertion search and native submission. It includes equations, a worked overlap example, source links and the limits of the model.

For a node with relative cost `c(v)`, the critical-path rank is `r(v) = c(v) + max(r(successor))`. A ready-node list schedule uses this rank, CUDA affinity, and stable instance-name tie breaking. A second candidate prioritizes bytes becoming dead minus bytes newly required. Candidate evaluation computes queue finish times as `finish(v) = max(queueAvailable, finish(all predecessors)) + c(v)`, including allocator-added dependencies. Efficiency mode minimizes the predicted makespan plus CUDA handoff and image-alias helper costs, with allocation bytes breaking ties. Memory mode minimizes allocation bytes first and predicted cost second. A hard cap filters infeasible candidates under either objective. The critical-path ranking is related to the upward-rank idea in [HEFT](https://disco.ethz.ch/courses/fs14/seminar/paper/Jochen/4.pdf); measured durations can replace relative node hints.

Copy nodes use an enabled, supported copy queue with GPU waits. Compute nodes move to asynchronous compute when a forward-reachable eligible compute group has enough members and independent graphics cost before its earliest direct noneligible successor. The group can branch and need not be contiguous; current automatic scheduling keeps CUDA nodes on Graphics. The defaults are four eligible compute IR nodes and four relative cost units of overlap opportunity, configurable through `mMinimumAsyncChain` and `mMinimumAsyncSlack`. Queue support and GPU-wait capabilities are checked. Explicit resource and memory-order edges produce native waits; unrelated queue work can overlap. A node's kind must accurately describe commands legal on that queue.

Optional [GPU telemetry and background scheduling](#gpu-telemetry-and-background-scheduling) replace eligible cost hints with measured node durations. Edit-time search can change the memory plan; background search preserves existing allocations and alias dependencies.

For a candidate ordering, a transient value is live from its first use through its last consumer. Outputs and persistent values are excluded from reuse. Native buffer heaps suballocate aligned byte ranges, intersect compatible memory-type masks, and alias disjoint lifetimes even when buffer sizes or usages differ. Texture storage uses separate heaps to avoid buffer/image granularity conflicts. CUDA interop, persistent, CPU-visible, MSAA and render/depth-target storage use committed allocations; compatible committed objects can still be pooled. Efficiency mode requires an existing happens-before proof between lifetimes. Memory mode or a hard cap may add edges from all previous users to the next first use, making cross-queue reuse safe even when that reduces overlap. Aliasing barriers execute before ordinary transitions when placed storage becomes active.

Compatible placed images can share byte ranges across different formats. Every overlapping image receives an activation pass with an alias barrier and an undefined-content transition, plus a retirement pass after all of its users. Retirement returns it to the graphics queue and Common state before another image becomes active; inactive images are excluded from frame-end transitions. This prevents an epilogue from touching memory already reassigned to a different image. The extra helper submissions participate in dependencies and the efficiency cost model (`mAliasSubmissionCost`).

Used tiled/sparse buffers or textures and buffers with `mMaxVersions > 1` are rejected by this planner because their storage cannot be accounted by its current model.

```cpp
FArdaInductorOptions Options;
Options.mObjective = EArdaInductorObjective::Memory;
Options.mMaxVramBytes = 512ull * 1024 * 1024; // zero disables the hard cap
Options.mFramesInFlight = 3;
Options.mSearchMode = EArdaInductorSearchMode::Bounded;
// SetOptions is legal only inside an edit.
auto Status = Graph.SetOptions(Options);
```

The cap applies to the graph's accounted native resource pool: `shared imports + persistent allocations + retained adapter workspaces + framesInFlight * transient pool`. Imported resources count even without an active consumer. Their complete retained allocation identities and capacities are counted once, including an entire parent heap retained by a small placed resource. Unknown raw-native imports require explicit allocation metadata for a hard cap. `mAllocatedBytes` reports the total; `mPeakLiveBytes` reports logical liveness across the configured slots and `mAliasedBytes` reports storage saved by reuse.

`mWorkspaceBytes` declares retained adapter-owned allocations. `mTransientWorkspaceBytes` instead requests a real graph-owned scratch buffer: retrieve it with `Context.GetWorkspaceBuffer()`, use it only within that node's work, and let the planner alias it with other nonoverlapping scratch/resources. Culled scratch consumes no storage; retained adapter memory still counts. RHI driver/PSO/CUDA-graph internals, hidden library allocations, unrelated process allocations, and provider staging caches are outside this resource-budget contract. An adapter that allocates its own GPU storage must declare it; the graph is not a process-wide residency manager.

Before allocation, D3D12 and Vulkan report native requirements through descriptor-only RHI queries. Candidate plans allocate no graph VRAM. The accepted plan acquires storage through the backend allocator and actual sizes are verified. Edit-time recompilation retires the previous transient leases before acquiring replacements, while persistent allocations remain retained across edits. Compatible idle allocations can satisfy those replacements without native allocation calls. The graph budget still counts each leased allocation in full; idle cached allocations and other graphs are governed separately by device allocator policy. A failed materialization can require rebuilding the prior plan during cancellation. Out-of-memory and backend errors propagate as `FArdaRHIStatus`; repository APIs use status results rather than C++ exceptions.

`mSearchMode` offers `Greedy`, `Bounded` (default), and `Exhaustive`. All modes evaluate both a critical-path/CUDA-affinity seed and a memory-release seed; Greedy stops there. Bounded mode improves the incumbent through legal adjacent swaps and explores other topological orders, up to `mMaxSearchStates` (default 10000), even when a feasible greedy result already exists. Iterative backtracking has no recursive depth limit. Exhaustive mode ignores the state cap and evaluates every legal order using the admitted allocator and cost model; its factorial worst case makes it appropriate for small graphs or offline compilation. `mbSearchComplete`, `mbSearchExhausted`, `mSearchStatesExamined`, and `mSchedulesExamined` distinguish a complete search from a bounded result. A feasible result remains usable after exhaustion; failure to find a feasible plan within the limit is distinct from completed-search infeasibility. No over-budget plan executes. Only complete enumeration establishes an optimum among the plans evaluated by the implemented allocator and queue heuristics. Greedy and an incomplete bounded search provide feasible incumbents without that guarantee; no mode proves the best possible memory layout or measured hardware execution. Peak-memory DAG scheduling is strongly NP-complete even for restricted graph families; see [New Tools for Peak Memory Scheduling](https://arxiv.org/abs/2312.13526).

`mFramesInFlight` selects independently allocated transient frame slots (default one). `Submit()` records and submits a frame and returns a retained ticket; it waits only when recycling an occupied slot. `Wait(ticket)` waits for that frame's native submission tokens and readback callbacks, then publishes its outputs. `IsComplete(ticket)` polls without blocking. Tickets remain valid after slot reuse and graph edits. `Execute()` is the synchronous `Submit`/`Wait` convenience operation. Completed CPU readback destinations are owned per frame; use distinct destinations when multiple results must coexist. GPU timestamps and CUDA executable caches are also owned per slot.

Shared imports, persistent resources and retained graph outputs conservatively order successive frames with GPU waits, including ownership transitions. Fully transient frame pools can overlap. Begin-edit and destruction retire only this graph's submissions, without a device-wide idle. External storage must enter on the graphics queue. During compilation, ArdaInductor snapshots each used imported texture's current uniform facade/native state with a read-only state query; it does not use the original creation descriptor as the texture entry state. Initialize the texture, or acquire its swap-chain image, before compilation: `Unknown`, `Discard` and nonuniform texture entry states are not admitted. The graph restores the captured state and graphics ownership at each frame boundary. Callers must synchronize external writes and restore that same entry contract before every subsequent submission. Imported buffers retain the descriptor-initial-state contract (`Unknown` is treated as `Common`).

```cpp
auto First = Graph.Submit();
auto Second = Graph.Submit();
if (!First || !Second) { /* handle status, including partial-submission tickets */ }
auto Completed = Graph.Wait(First.mValue);
auto Ready = Graph.IsComplete(Second.mValue);
auto SecondCompleted = Graph.Wait(Second.mValue);
```

## GPU telemetry and background scheduling

GPU timing and adaptive scheduling are independently disabled by default; automatic scheduling requires timing. Configure them inside an edit, alongside the graph's other options. The values below show the telemetry and tuning defaults after enabling both features:

```cpp
FArdaInductorOptions Options;
Options.mbEnableGpuTiming = true;
Options.mGpuTimingEmaAlpha = 0.2;
Options.mGpuTimingSampleInterval = 1;       // every submitted frame
Options.mGpuTimingHistoryCapacity = 4096;  // raw node samples across the graph
Options.mbEnableAdaptiveScheduling = true;
Options.mAdaptiveSchedulingInterval = 30;  // minimum submitted frames between jobs
Options.mAdaptiveSchedulingMinSamples = 8;
Options.mAdaptiveSchedulingSearchBudget = 128;
Options.mAdaptiveSchedulingMinImprovement = 0.02;
if (auto S = Graph.BeginGraphEdit(); !S) return S;
if (auto S = Graph.SetOptions(Options); !S) return S;
if (auto S = Graph.EndGraphEdit(); !S) return S;
```

Graphics, compute and copy nodes receive GPU timestamps on queues whose `FArdaRHIQueueCapabilities::SupportsTimestamps()` returns true. D3D12 copy timing requires native copy-queue timestamp support and its dedicated query-heap type. Vulkan respects each queue family's timestamp counter width; transfer-only families also require enabled host query reset, because they cannot record query-pool resets. Arda enables supported host reset on devices it creates. Adopted Vulkan devices must declare `vulkan.enabled-feature=hostQueryReset` when their host enabled that feature. Host reset occurs only when the prior query generation has completed, as required by [Vulkan query reset](https://docs.vulkan.org/refpages/latest/refpages/source/vkResetQueryPool.html). Unsupported queues omit samples; instrumentation does not move work to another queue or serialize queues merely to measure them. Stage-only declarations and pure synchronization nodes have no GPU command interval to time.

CUDA nodes receive individual event regions within their existing sequence, preserving batching and the single stream ordering contract. Their reported durations exclude the surrounding graphics/CUDA handoff and CPU recording/submission. One semantic CUDA node can contain multiple kernels or library calls, so its region is a node duration, not necessarily a single-kernel duration. CUDA regions report their actual compiled graphics or compute submission queue in `mQueue`; identify CUDA work through the node definition. Timestamp/event instrumentation and CPU polling have nonzero overhead. Increase `mGpuTimingSampleInterval` to sample fewer frames when needed.

`GetTimingProfile()` returns `FArdaInductorTimingSample` entries identified by complete node handles. `mGpuSeconds` is an exponential moving average in collection order: the first sample initializes it, and subsequent samples use `EMA += alpha * (sample - EMA)`. Alpha must be in `(0, 1]`. Frame receipts can be collected out of submission order; every consumed execution still contributes once to the EMA, count, minimum, maximum and raw history. The last duration, frame sequence and queue describe the highest collected frame sequence, so a late receipt does not replace those fields. These are completed GPU intervals, not CPU wall-clock frame time. `GetTimingHistory()` retains the most recently collected raw `FArdaInductorNodeTiming` entries within its capacity and returns them ordered by frame sequence and node identity. The default capacity of 4096 counts node samples, not frames; zero retains EMA statistics without raw history.

`CollectTelemetry()` polls completed submissions and available native timestamps/events, then advances scheduling. `Submit()` also advances collection and tuning. Collection adds no GPU wait. Incomplete samples remain pending, and a frame slot skips new instrumentation while its previous sample is unresolved. Unsupported or failed query setup and collection drop affected samples while GPU work continues. An error ending an already-started native query fails recording when the command list cannot safely be submitted; the original provider error is preserved. The profile/history getters collect available samples but do not adopt a schedule. `Execute()` remains synchronous `Submit()` followed by `Wait()`; enabling telemetry does not change that contract or remove `Submit()`'s existing frame-slot recycling wait.

```cpp
Graph.CollectTelemetry(); // poll while the application continues other work
auto Profile = Graph.GetTimingProfile();
auto History = Graph.GetTimingHistory();
auto Tuning = Graph.GetAdaptiveSchedulingStats();
// Tuning.mbPending covers search/preparation and a completed plan awaiting retirement.
```

Automatic tuning starts at most one job at a time, no more frequently than `mAdaptiveSchedulingInterval` submitted frames. By default a measured node needs eight samples and a positive finite EMA before its timing replaces a cost hint; nodes without eligible measurements keep their declared hints. A worker normalizes eligible durations into hint units, explores at most 128 schedule candidates per iteration, and prepares an improved native program. It does not run speculative GPU work or invoke graph execution callbacks to benchmark candidates. Search uses the current physical allocations and preserves existing alias dependencies; it cannot recover overlap prohibited by those dependencies or choose a different memory allocation plan.

A candidate must improve modeled cost by at least two percent by default. This threshold is an estimate from the current cost model, not a promise of measured speedup or global optimality. The bounded search can miss better schedules, and instrumentation, CPU search and native program preparation all have costs. Compare observed results over repeated frames and disable adaptive scheduling when its overhead is not justified.

The application keeps submitting the current program while the worker searches. A prepared program is adopted only when all existing frame slots have naturally completed and their readbacks can retire. Collection never waits for GPU completion to force adoption; a continuously saturated graph may defer the swap. GPU allocations are reused, preserving addresses and the resource-budget contract. Explicit edits and graph destruction cancel and join CPU preparation before replacing or releasing storage, so a worker cannot retain an old VRAM pool through reallocation. Driver query/program/graph internals remain outside the declared resource budget.

`GetAdaptiveSchedulingStats()` exposes completed iterations, accepted schedules, candidates examined, pending state, last baseline/candidate costs and last status. `mAcceptedSchedules` increases when a replacement is adopted; its compile revision also changes even without a semantic edit. A pending job may already have finished CPU preparation and simply be awaiting frame retirement. A successful `OptimizeFromTimingProfile()` call requests this same nonblocking process outside an edit; it does not wait for outstanding GPU work or immediately publish a new compile result. Manual requests require at least one eligible positive per-node sample and work with automatic scheduling disabled. Inspect stats and call `CollectTelemetry()` or continue submitting to observe completion and adoption.

`ClearTimingProfile()` discards EMA and raw samples and invalidates an optimization based on the old sample epoch; it does not wait for GPU work. A successful edit also clears samples. Changed canonical parameters or registered definition identities invalidate old cost overrides. Stable full node handles prevent same-name replacement nodes from inheriting another node's timing.

## Relationship to ML graph runtimes

The design follows the separation of definition, compilation and repeated execution described by [CUDA Graphs](https://docs.nvidia.com/cuda/cuda-programming-guide/04-special-topics/cuda-graphs.html). Its reuse rules also follow the central requirement that reuse needs GPU ordering, not just disjoint positions in a CPU list. ArdaInductor uses the backend's graphics-compatible memory planner and device-wide object cache; NVIDIA graph memory nodes additionally support driver-managed virtual-memory aliasing.

[PyTorch CUDAGraph Trees](https://docs.pytorch.org/docs/2.9/torch.compiler_cudagraph_trees.html) illustrate why live outputs and stable addresses must remain valid across repeated execution and shared memory pools. Arda retains those invariants through frame-local pools and a bounded CUDA executable variant cache. Graph topology changes remain explicit edit transactions; Python control-flow tracing is deferred. [ONNX Runtime device tensors](https://onnxruntime.ai/docs/performance/device-tensor.html) motivate retaining device-local intermediates and overlapping transfers with computation.

[TensorRT's performance guide](https://docs.nvidia.com/deeplearning/tensorrt/latest/performance/optimization.html) describes the tradeoff between concurrent execution and memory reuse. Arda exposes that tradeoff through efficiency and memory objectives, explicit allocation diagnostics, and allocator-added waits. These references guide the architecture; performance improvements still need measurement on each application's graph.

## Compiler implementation map

The [compilation chapter](compilation.html) explains the algorithms in execution order. These links resolve to source files in a repository checkout or repository browser:

- [Indexed graph](../../Source/ArdaInfra/ArdaGraph/Public/ArdaGraph.h): adjacency and endpoint maps, Kahn ordering, reachability and iterative cycle witnesses.
- [Resource dependency resolver](../../Source/ArdaInfra/ArdaRenderGraph/Private/ArdaDependencyHazards.cpp): per-region producer coverage, attachment-ordered hazards and value dependencies used by compilation and removal.
- [Semantic compiler and scheduler](../../Source/ArdaInfra/ArdaRenderGraph/Private/ArdaInductor.cpp): declaration validation, liveness, greedy seeds, adjacent swaps, iterative topological enumeration, queue assignment and cost comparison.
- [Native memory planner](../../Source/ArdaInfra/ArdaBackend/Private/Allocator/ArdaMemoryPlanner.cpp): requirement queries, minimum-growth heap placement, compatible committed slots, alias dependencies and budget accounting.
- [Pipeline resolver](../../Source/ArdaInfra/ArdaRenderGraph/Private/ArdaInductorPipeline.cpp): bounded ancestry, group/layout/stage merging, canonical byte serialization and stable pattern keys.
- [Adaptive search](../../Source/ArdaInfra/ArdaRenderGraph/Private/ArdaInductorAdaptiveSchedule.cpp): persistent insertion neighborhoods, critical-path candidates and fixed-pool lifetime checks.
- [Telemetry and adoption](../../Source/ArdaInfra/ArdaRenderGraph/Private/ArdaInductorTiming.cpp): per-node EMA, snapshot cost normalization, background requests and retirement-gated swaps.
- [Native preparation and frame slots](../../Source/ArdaInfra/ArdaRenderGraph/Private/ArdaInductorRuntime.cpp): CUDA coalescing, declaration contraction, image alias helpers, frame reuse and tickets.
- [Command program](../../Source/ArdaInfra/ArdaRenderGraph/Private/ArdaInductorCommandProgram.cpp) and [executor](../../Source/ArdaInfra/ArdaRenderGraph/Private/ArdaInductorCommandExecutor.cpp): physical identity/state histories, recording levels, ownership transitions and per-queue token reduction.

## Executable examples and tests

The [triangle renderer](../../Source/ArdaTests/RHITest/Private/ArdaTriangleRenderer.cpp) caches a graph per swap-chain image with an inferred graphics PSO. The [terrain renderer](../../Source/ArdaTests/Examples/ARDGExample/Private/ArdaTerrainRenderer.cpp) uses a distinct typed operation for each upload, compute dispatch, draw and overlay, plus separate copy/readback nodes. Raw/eroded heightmaps and pre-overlay color are explicit transient values. Consumers are attached first and ArdaInductor derives the execution order. ARDGExample --verify enables first-frame geometry and gradient checks, then removes its readback nodes after completion; ordinary viewer runs omit those diagnostics. Verification is independent of --validation. Ordinary frames change only dynamic CPU uniform inputs. Each graph is first compiled after its swap-chain image is acquired, so the entry-state snapshot reflects the acquired image. Both release cached backbuffer imports before resize.


- [Generic graph tests](../../Source/ArdaInfra/ArdaGraph/Tests/ArdaDirectedGraphTests.cpp): indexed topology, cycle witnesses, removal and snapshot identities.
- [Compiler tests](../../Source/ArdaInfra/ArdaRenderGraph/Tests/ArdaInductorTests.cpp): attachment-order independence, range coverage, node removal, CUDA batching and queue choices.
- [Memory planner tests](../../Source/ArdaInfra/ArdaBackend/Tests/ArdaMemoryPlannerTests.cpp): native sizes, lifetime proofs, alias ordering and cap rejection.
- [Pipeline inference tests](../../Source/ArdaInfra/ArdaRenderGraph/Tests/ArdaInductorPipelineTests.cpp): intermediate ancestry, ambiguous groups, stable PSO identity and all pipeline families.
- [Native pipeline execution](../../Source/ArdaInfra/ArdaRenderGraph/Tests/ArdaInductorGpuTests.cpp): numerical graphics-compute outputs, repeated execution and PSO reuse on D3D12/Vulkan.
- [CUDA execution](../../Source/ArdaInfra/ArdaBackend/Tests/ArdaCudaTests.cpp): `PersistentInductorCompilesReorderedCudaNodesAndReusesFrameStorage` attaches two dependent kernels in reverse order, runs five frames with alternate-frame per-node timing, verifies numerical readback, executable reuse and alias savings, then rejects an insufficient cap and executes the restored graph.
- [Queue execution](../../Source/ArdaInfra/ArdaRenderGraph/Tests/ArdaInductorQueueGpuTests.cpp): checks compiled and actual graphics/compute/copy assignments, delayed joins, and numerical results across frames on both native backends.
- [Edit recovery](../../Source/ArdaInfra/ArdaRenderGraph/Tests/ArdaInductorLifecycleTests.cpp) and [readback completion](../../Source/ArdaInfra/ArdaRenderGraph/Tests/ArdaInductorCompletionTests.cpp): injected allocation failures, retryable cancellation, partially submitted readbacks, and deterministic host result publication.
