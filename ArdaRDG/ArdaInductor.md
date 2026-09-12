# Persistent compute and graphics graphs with ArdaInductor

`FArdaDependencyGraph` is the persistent graph API. It owns typed node instances, logical resource versions, a compiled schedule, a reusable physical resource pool, and a pipeline cache. `EndGraphEdit()` invokes **ArdaInductor**; `Execute()` submits the compiled work repeatedly. Node attachment order has no execution meaning.

The public entry points are [ArdaDependencyGraph.h](../../Source/ArdaRenderGraph/Public/ArdaDependencyGraph.h), [ArdaDependencyNode.h](../../Source/ArdaRenderGraph/Public/ArdaDependencyNode.h), [ArdaDependencyGraphNodes.h](../../Source/ArdaRenderGraph/Public/ArdaDependencyGraphNodes.h), and [ArdaDependencyGraphCuda.h](../../Source/ArdaRenderGraph/Public/ArdaDependencyGraphCuda.h). The [pipeline guide](ArdaInductor-Pipelines.md) covers automatic graphics, meshlet, compute, ray-tracing, and work-graph PSOs. `ArdaRenderGraph.h` is the umbrella for the persistent APIs; execution options and frame reports are declared in `ArdaDependencyGraphExecution.h`.

## Separation of responsibilities

`Ardashir::ArdaGraph` is a separate library with no graphics or CUDA dependency. Its [TArdaDirectedGraph](../../Source/ArdaGraph/Public/ArdaGraph.h) accepts arbitrary node and edge payloads. It stores dense live node/edge lists, per-node incoming/outgoing adjacency lists, neighbor hash maps, and an endpoint-pair edge index. Node/edge lookup and duplicate-edge lookup take expected constant time; node removal visits incident edges. Deterministic Kahn ordering and iterative cycle detection avoid depending on hash iteration. Generational handles reject deleted, foreign, and abandoned-edit identities. Snapshots share an identity lineage while copying graph contents.

`Ardashir::ArdaRenderGraph` adds typed resource semantics and execution. Its global `FArdaNodeRegistry::Get()` stores immutable library definitions under unique names and synchronizes registration/lookup. Built-ins are installed automatically: `arda.upload`, `arda.copy-buffer`, `arda.clear-buffer`, `arda.readback`, and `arda.sync`. Applications register their own graphics, compute, copy, or CUDA definitions in the same registry. A definition is shared; a graph owns each instance's parameters and name. `Unregister()` removes future library lookup while existing instances retain their definition. Unregister device-bound CUDA definitions when retiring their device so the global library does not unnecessarily retain an old CUDA context.

ArdaInductor resolves semantic edges, culls unused work, infers PSOs, selects a deterministic schedule, groups CUDA operations, assigns queues, and plans storage. A successful edit compiles the native execution program, which retains physical frame pools, barrier plans and resolved pipelines. Optional background scheduling can replace that program while reusing its allocations; ordinary replay does not recreate transient allocations each frame.

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
compares the public parameter key before preparation. Matching instances return
immediately; conflicting semantics fail before setup. A new instance runs optional
validation, obtains immutable device state, creates private instance state and
describes its accesses. Public input snapshots and prepared execution parameters
have distinct schemas: the registry's optional
[preparation adapter](api-reference.html#api-arda-fardadependencynodedefinition-mprepare-5cdfebb9)
retains the prepared result for description and execution. Class nodes hide this
wrapper inside the base. Dynamic libraries can still use lower-level callback
definitions with the same attachment lifecycle; named callers pass the public
parameters, never the private wrapper.

All examples use individual node classes in `Public/Nodes` and matching `.cpp`
files in `Private/Nodes`. The umbrella headers only include classes. There is no
renderer-owned node library to initialize. Terrain's generate node owns its
shader/parameter metadata, layout, compute configuration and callback; attaching
an upload does not prepare the generate or draw shaders. A node declaring `FState`
implements `Prepare(Device)` and returns retained immutable state. A node declaring
`FInstanceState` implements `CreateInstance(Device, Parameters, State)` and returns
its private mutable cache. Both state types can be forward-declared in the public
node header and defined in its implementation file. Stateless defaults cover
nodes needing neither setup nor mutable bindings. The base weakly caches device
state by concrete node type and device. Bound parameters hold strong references,
so live graphs reuse setup and retiring them releases the state and device.
Per-instance caches handle binding sets and shader tables without sharing mutable
objects between nodes. Update these caches when physical resource identities change.

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
[worked custom-node recipe](quick-guide.html#node-authoring) for a complete operation.

See the [registration recipes](quick-guide.html#node-registration) for native compute, indexed raster, fullscreen graphics, ray tracing and CUDA nodes, including an authored CUDA class. They show optional library discovery before devices exist and automatic registration during typed attachment.

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

The base lowers class nodes to `TArdaDependencyNodeDefinition<Parameters, PreparedParameters>`. Callback adapters can use this directly; the second type defaults to the first when no preparation is needed. `mCanonicalKey` consumes public `Parameters` and must encode all behavior-affecting values and resource identities without padding bytes. The optional `mPrepare` adapter receives retained public parameters and returns retained `PreparedParameters`. `mDescribe` consumes `PreparedParameters` to derive accesses, pipelines, cost, workspace and side effects. Native definition callbacks use `mRecord(Context, PreparedParameters)`; CUDA definition callbacks use `mPrepareCuda(Context, PreparedParameters, Sequence)`. These are the lower-level definition callbacks. Class hooks still receive public `FParameters` and state references separately because the base unwraps its private prepared execution parameters.

Parameters are copied into retained const storage at attachment. Value containers are snapshots. Pipeline configuration objects are copied separately, even if the author retains a mutable pointer. Arbitrary nested pointers in custom parameter structs remain the author's responsibility: semantic input pointees must stay immutable for the instance's lifetime and their relevant contents must participate in its canonical key. Output sinks such as the built-in readback vector are intentionally mutable. Do not mutate them while execution uses them. An application can define an explicitly dynamic upload source when its contents do not affect the compiled description. Terrain updates a retained camera/time object before synchronous execution; concurrent submissions require separate snapshots or synchronization.

Each logical resource version has **one producing node**. Readers depend on that producer regardless of which node was attached first. A sequence `input -> sort pass 1 -> sort pass 2` declares separate input/intermediate/output handles. The compiler may map dead compatible intermediate versions to the same physical allocation. Multiple producers for one handle are rejected rather than ordered by attachment. One producer can declare multiple buffer ranges; their merged coverage must cover owned-resource reads. Texture reads must be covered by a declared producer range. External inputs can be read without a producer; a single `ReadWrite` node can update external storage in place. Multiple in-place updates should be one registered operation or use separate versioned outputs.

Resource access declarations are the complete hazard contract. Declare every buffer/texture and its RHI state/range, including framebuffer targets. Native callbacks resolve only declared resources through the execution context. Hidden resource access inside an external library cannot be inferred; declare its inputs, outputs and retained workspace. Non-resource side effects require explicit edges and `mbSideEffect` when observable. `arda.sync` is an empty named join: connect its predecessors and successors with `AddDependency()`. Pure joins and stage declarations generate contracted dependency edges without empty native command lists.

All mutation is restricted to `BeginGraphEdit()` / `EndGraphEdit()`. `AttachOrFind(instanceName, definitionName, parameters)` returns an existing instance only when its definition and canonical key match. A name collision with different semantics is an error; remove the old instance and attach its replacement within the edit. Resource dependencies are resolved at compilation. `FindNode()` and `GetTopology()` provide lookup and inspection.

`RemoveNode()` removes that node and recursively removes consumers of the resource versions it wrote. Manual ordering edges alone do not cause removal of their successor. Removed output versions lose their output marker. Removing a node leaves logical resource declarations available for replacement producers. `CancelGraphEdit()` restores the previous graph, handles, resource descriptions, and options. Failed compilation keeps the edit open for correction or cancellation, and execution is unavailable while editing. Begin-edit waits for outstanding work before replacing compiled storage.

The graph is externally synchronized: edit and execution calls on one instance must not run concurrently. Registry synchronization does not make graph mutation thread-safe.

To change input data without recompiling, update the contents of already imported device storage after its prior use completes, then restore the graph's entry-state contract. Changing node parameter values, shapes, or resource identities requires an edit. Retained addresses do not make transient values into cross-frame inputs; use external storage for input state carried between executions.

## Automatic CUDA batches

Existing compiled-kernel and external-call operands can be registered with `RegisterArdaCudaOperandNode(name, sharedOperand)`. Use `TArdaDependencyCudaParameters<Operand::FParameters>` to replace native buffer/surface bindings with logical graph handles. The helper derives accesses from the operand schema and resolves native bindings at execution. The default canonical visitor handles scalar and enum values; aggregate values need a custom definition with an explicit canonical serializer. Device-bound definitions need unique registry names for different devices.

The helper does not infer GPU allocations hidden inside an operand or external library. Use a custom definition whose `mDescribe` supplies `mWorkspaceBytes` when such retained storage must participate in the graph budget.

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

Before allocation, D3D12 and Vulkan report native requirements through descriptor-only RHI queries. Candidate plans allocate no graph VRAM. The accepted plan is materialized once and actual sizes are verified. Edit-time recompilation retires the previous transient pool before committing its replacement, while persistent allocations remain retained across edits. A failed materialization can require rebuilding the prior plan during cancellation. Out-of-memory and backend errors propagate as `FArdaRHIStatus`; repository APIs use status results rather than C++ exceptions.

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

The design follows the separation of definition, compilation and repeated execution described by [CUDA Graphs](https://docs.nvidia.com/cuda/cuda-programming-guide/04-special-topics/cuda-graphs.html). Its reuse rules also follow the central requirement that reuse needs GPU ordering, not just disjoint positions in a CPU list. ArdaInductor implements its own graphics-compatible object pool; NVIDIA graph memory nodes additionally support driver-managed virtual-memory aliasing.

[PyTorch CUDAGraph Trees](https://docs.pytorch.org/docs/2.9/torch.compiler_cudagraph_trees.html) illustrate why live outputs and stable addresses must remain valid across repeated execution and shared memory pools. Arda retains those invariants through frame-local pools and a bounded CUDA executable variant cache. Graph topology changes remain explicit edit transactions; Python control-flow tracing is deferred. [ONNX Runtime device tensors](https://onnxruntime.ai/docs/performance/device-tensor.html) motivate retaining device-local intermediates and overlapping transfers with computation.

[TensorRT's performance guide](https://docs.nvidia.com/deeplearning/tensorrt/latest/performance/optimization.html) describes the tradeoff between concurrent execution and memory reuse. Arda exposes that tradeoff through efficiency and memory objectives, explicit allocation diagnostics, and allocator-added waits. These references guide the architecture; performance improvements still need measurement on each application's graph.

## Compiler implementation map

The [compilation chapter](compilation.html) explains the algorithms in execution order. These links resolve to source files in a repository checkout or repository browser:

- [Indexed graph](../../Source/ArdaGraph/Public/ArdaGraph.h): adjacency and endpoint maps, Kahn ordering, reachability and iterative cycle witnesses.
- [Semantic compiler and scheduler](../../Source/ArdaRenderGraph/Private/ArdaInductor.cpp): dependency coverage, liveness, greedy seeds, adjacent swaps, iterative topological enumeration, queue assignment and cost comparison.
- [Native memory planner](../../Source/ArdaRenderGraph/Private/ArdaInductorMemory.cpp): requirement queries, minimum-growth heap placement, compatible committed slots, alias dependencies and budget accounting.
- [Pipeline resolver](../../Source/ArdaRenderGraph/Private/ArdaInductorPipeline.cpp): bounded ancestry, group/layout/stage merging, canonical byte serialization and stable pattern keys.
- [Adaptive search](../../Source/ArdaRenderGraph/Private/ArdaInductorAdaptiveSchedule.cpp): persistent insertion neighborhoods, critical-path candidates and fixed-pool lifetime checks.
- [Telemetry and adoption](../../Source/ArdaRenderGraph/Private/ArdaInductorTiming.cpp): per-node EMA, snapshot cost normalization, background requests and retirement-gated swaps.
- [Native preparation and frame slots](../../Source/ArdaRenderGraph/Private/ArdaInductorRuntime.cpp): CUDA coalescing, declaration contraction, image alias helpers, frame reuse and tickets.
- [Command program](../../Source/ArdaRenderGraph/Private/ArdaInductorCommandProgram.cpp) and [executor](../../Source/ArdaRenderGraph/Private/ArdaInductorCommandExecutor.cpp): physical identity/state histories, recording levels, ownership transitions and per-queue token reduction.

## Executable examples and tests

The [triangle renderer](../../Source/ArdaTests/RHITest/Private/ArdaTriangleRenderer.cpp) caches a graph per swap-chain image with an inferred graphics PSO. The [terrain renderer](../../Source/ArdaTests/Examples/ARDGExample/Private/ArdaTerrainRenderer.cpp) uses a distinct typed operation for each upload, compute dispatch, draw and overlay, plus separate copy/readback nodes. Raw/eroded heightmaps and pre-overlay color are explicit transient values. Consumers are attached first and ArdaInductor derives the execution order. First-frame numerical validation removes its readback nodes after completion; ordinary frames change only dynamic CPU uniform inputs. Each graph is first compiled after its swap-chain image is acquired, so the entry-state snapshot reflects the acquired image. Both release cached backbuffer imports before resize.


- [Generic graph tests](../../Source/ArdaGraph/Tests/ArdaDirectedGraphTests.cpp): indexed topology, cycle witnesses, removal and snapshot identities.
- [Compiler tests](../../Source/ArdaRenderGraph/Tests/ArdaInductorTests.cpp): attachment-order independence, range coverage, node removal, CUDA batching and queue choices.
- [Memory planner tests](../../Source/ArdaRenderGraph/Tests/ArdaInductorMemoryTests.cpp): native sizes, lifetime proofs, alias ordering and cap rejection.
- [Pipeline inference tests](../../Source/ArdaRenderGraph/Tests/ArdaInductorPipelineTests.cpp): intermediate ancestry, ambiguous groups, stable PSO identity and all pipeline families.
- [Native pipeline execution](../../Source/ArdaRenderGraph/Tests/ArdaInductorGpuTests.cpp): numerical graphics-compute outputs, repeated execution and PSO reuse on D3D12/Vulkan.
- [CUDA execution](../../Source/ArdaBackend/Tests/ArdaCudaTests.cpp): `PersistentInductorCompilesReorderedCudaNodesAndReusesFrameStorage` attaches two dependent kernels in reverse order, runs five frames with alternate-frame per-node timing, verifies numerical readback, executable reuse and alias savings, then rejects an insufficient cap and executes the restored graph.
- [Queue execution](../../Source/ArdaRenderGraph/Tests/ArdaInductorQueueGpuTests.cpp): checks compiled and actual graphics/compute/copy assignments, delayed joins, and numerical results across frames on both native backends.
- [Edit recovery](../../Source/ArdaRenderGraph/Tests/ArdaInductorLifecycleTests.cpp) and [readback completion](../../Source/ArdaRenderGraph/Tests/ArdaInductorCompletionTests.cpp): injected allocation failures, retryable cancellation, partially submitted readbacks, and deterministic host result publication.
