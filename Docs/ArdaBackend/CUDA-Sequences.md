# CUDA sequences

Use `FArdaCudaSequence` to compose compute operands into one ordered CUDA batch.
Use registered CUDA nodes in `FArdaDependencyGraph` for compiler-owned batching. Each operand
selects one compiled kernel or one external operation, such as a cuBLAS or cuDNN
call that may itself enqueue several kernels. The operand's binding and selection
contract remains unchanged. See [External CUDA calls](CUDA-External-Calls.md) for
typed library adapters and lifetime requirements.

For graphics textures processed through linear CUDA pointers, see
[CUDA texture buffers](CUDA-Texture-Buffers.md). Copy once before the sequence and
once after it, keeping intermediate operations in CUDA buffers.

## Direct recording and submission

Include `Compute/ArdaCudaSequence.h`. For example, using the test/sample
`FArdaAddOperand` and three CUDA-shared buffers of `Count * sizeof(uint32_t)` bytes:

```cpp
FArdaAddOperand Operand(Device);
FArdaCudaSequence Sequence(Device);
FArdaAddParameters P;
P.mCount = Count;
P.mInput.mBuffer = Input;
P.mOutput.mBuffer = Scratch;
P.mBias = 7;
if (auto Status = Sequence.Add(Operand, P); !Status)
{
    return Status;
}
P.mInput.mBuffer = Scratch;
P.mOutput.mBuffer = Output;
P.mBias = 11;
if (auto Status = Sequence.Add(Operand, P); !Status)
{
    return Status;
}

// Commands is an open, caller-owned graphics command list on Device.
// Its earlier commands may produce Input; later commands may consume Output.
return Sequence.DispatchDeferred(Commands);
```

Each `Add` calls the operand's `PrepareDispatch` and freezes its values and resource
views. `AddPlan` accepts an already prepared single-operation plan and copies it.
Different operand types, schemas, views, compiled kernels, and external calls may
be mixed. Compiled kernels may have different launch geometries; external libraries
choose their own kernel launches.
The sequence does not require the original operands or host parameter objects to
remain alive. Scratch buffers accessed through the RHI must be supplied as declared
CUDA-shared resources. External adapters receive the framework stream and resolved
typed parameters during native recording, and may own private library state.

Alternatively, `Sequence.Dispatch()` creates and submits one command list and returns
`FArdaCudaSequenceSubmission`, containing the device, queue, completion instance,
`mOperationCount`, and `mKernelCount`. `GetOperationCount()` includes external calls;
`GetKernelCount()` counts only explicit compiled-kernel entries. Neither kernel
count includes launches hidden inside an external library. A successful return is
asynchronous. Observe completion before host
access or reuse from another queue. The immutable sequence may be dispatched again;
each recording creates fresh native submission state. A recorded command list itself
is still single-use until reset. Do not append steps concurrently with dispatch.

An empty sequence, or one containing only explicit operand `NoWork` plans, succeeds
without a CUDA launch. Immediate submission returns instance zero. A failed `Add`
or `AddPlan` is sticky and prevents later submission of a partial algorithm.

## Persistent dependency graph

Include `ArdaRenderGraph.h`. Register an operand once under a device-specific node
name, then attach typed nodes inside an edit transaction. ArdaInductor derives
resource dependencies and coalesces consecutive eligible CUDA nodes into one batch.

```cpp
FArdaDependencyGraph Graph(Device);
if (auto S = RegisterArdaCudaOperandNode("app.add", Operand); !S) return S;
if (auto S = Graph.BeginGraphEdit(); !S) return S;
// Input is an imported resource; Middle and Output are separate logical buffer values.
TArdaDependencyCudaParameters<FArdaAddParameters> P;
P.mCount = Count;
P.mInput.mResource = Input;
P.mOutput.mResource = Middle;
P.mBias = 7;
if (auto N = Graph.AttachOrFind("add seven", "app.add", P); !N) return N.mStatus;
P.mInput.mResource = Middle;
P.mOutput.mResource = Output;
P.mBias = 11;
if (auto N = Graph.AttachOrFind("add eleven", "app.add", P); !N) return N.mStatus;
if (auto S = Graph.MarkOutput(Output); !S) return S;
if (auto S = Graph.EndGraphEdit(); !S) return S;
return Graph.Execute().mStatus;
```

`Input`, `Middle`, and `Output` are `FArdaDependencyResourceHandle` values created
or imported in the same edit transaction. Their buffers require CUDA sharing.
The graph owns immutable parameter copies and registered definitions. Each logical
value has one producer; attachment order does not define execution order.
Unregister device-bound definitions when their library lifetime ends; existing
graphs retain their definitions until retired.

`FArdaInductorOptions::mCudaGraphMode` selects capture policy. Each compiled batch
has a retained native graph cache per frame slot, and `GetCudaGraphStats()` reports
capture/replay counters. Compiled-kernel and external library operands use the same
node registration API. D3D12 CiG stays on the graphics queue with ordered capture
and submission. See [ArdaInductor](../ArdaRDG/ArdaInductor.md) for edit transactions,
resource lifetime, async submission, and memory planning.

## Native execution and validation

The sequence uses `IArdaRHICommandList::DispatchCudaSequence`. This validates each
single-operation descriptor, merges identical resource views, combines access modes,
and remaps each operation's patch indices into the shared binding table. Native
recording validates compiled-kernel limits and patches all arguments. On a graph
cache miss or an uncaptured launch, it prepares every external call before capture
or the first enqueue. `PrepareDispatch` only
freezes a host plan; an external adapter's `PrepareCall` runs later with the provider's
CUDA context current and physical addresses resolved.

- D3D12 ordinary CUDA: one stream, one shared fence, and one pair of handoffs for
  the entire sequence.
- Vulkan ordinary CUDA and Vulkan CiG: one stream, one Ready/Done semaphore pair,
  and ownership release/acquire for the sequence's resource union.
- D3D12 CiG: one capture enclosing all sequence operations, with graphics UAV barriers
  around the capture. External calls must explicitly admit CiG capture. There are no
  graphics barriers between sequence operations.

All operations execute in order on that stream. External adapters must bind that
stream and satisfy its ordering contract. Shared allocations remain imported;
batching does not copy arrays or convert their contents. Independent calls to the
direct `Dispatch`/`DispatchDeferred` methods are not automatically combined.

The framework rejects invalid descriptors, resource views, compiled limits, and
external-call preparation failures before enqueuing the first sequence operation.
A driver, kernel, or library error after execution begins
can still leave partial work; failure is not transactional and must not be retried
over the same outputs. The existing failed-submission cleanup and completion rules
apply. Operations must declare every accessed RHI resource; compiled kernels must
also provide correct geometry. Validation does not prove memory safety or library
algorithm compatibility.

Provider interface version **12**, including queue-qualified timestamp capabilities,
requires rebuilding external providers and consumers.
`IArdaProviderCommandList::DispatchCuda` accepts a nonempty ordered operation batch
against one binding table. Each entry contains exactly one compiled entry or external
call. It must process all operations in one batch or report
`Unsupported`; it must not execute only the first entry. Public `DispatchCuda` and
single-operation operand validation still reject multiple entries in one dispatch.

## Retained CUDA Graphs

Each `FArdaCudaSequence` creates a `FArdaCudaGraphCache` in `Prefer` mode unless
you provide one. Repeated dispatch of that sequence reuses its cache. To prepare
fresh host plans while retaining captured variants, keep the cache separately:

```cpp
auto Cache = eastl::make_shared<FArdaCudaGraphCache>(
    EArdaCudaGraphMode::Require, 4); // At most four cached argument variants.

// Each frame: construct the sequence with current parameters and resource views.
FArdaCudaSequence Sequence(Device, EArdaRHIQueueType::Graphics, Cache);
if (auto S = Sequence.Add(FirstOperand, FirstParameters); !S) return S;
if (auto S = Sequence.Add(SecondOperand, SecondParameters); !S) return S;
auto Submitted = Sequence.Dispatch();
if (!Submitted) return Submitted.mStatus;
auto Stats = Cache->GetStats();
```

- `Disabled` always invokes ordinary operation launchers.
- `Prefer` captures eligible batches, with an ordinary batch fallback if capture
  is unavailable, an adapter has not opted in, or capture/instantiation fails.
- `Require` reports failure when graph capture cannot be used. Use it to verify
  that a workload exercises native replay rather than silently falling back.

An error returned by an operation itself during capture is propagated in all
modes after ending capture. It does not retry a potentially mutated library
object. Prefer-mode fallback applies to graph capture support/driver failures.

The native cache compares complete resolved parameter bytes, ordered kernel and
external factory identities, launch dimensions/shared memory, external capture
revisions, context, and bound resource identities. This avoids hash collisions.
A matching variant replays without calling external `PrepareCall` or operation
`Enqueue` again. A new variant captures before any algorithm work executes. Up to
four variants are retained by default, so A → B → A can reuse A; when full, the
least recently recorded variant is evicted. `GetMaximumCachedVariants()` reports
the positive constructor limit. `Reset()` evicts all variants while retaining
cumulative counters. Creating new operand/factory identities each frame defeats
reuse; keep those definitions stable.

Each executable retains its parameter snapshots, compiled entry modules, all
declared resources (including scratch), prepared external objects, CUDA context
and capture stream. A command list retains the executable it recorded, so cache
eviction or reset cannot release objects used by in-flight work. Driver-owned
graph allocation overhead is not reported as graph-managed VRAM; the variant
limit bounds entry count, not total GPU bytes. Distinct bindings retained by each
variant can also keep otherwise unused application resources alive. Use a separate
cache for each logical batch and frame slot; overlap still requires normal queue
and resource synchronization.

For ordinary CUDA and Vulkan CiG, each launch queues its current graphics wait,
then `cuGraphLaunch`, then its current graphics signal. Those synchronization
operations are never captured. D3D12 first captures and instantiates the CUDA
Graph, then records its launch inside a fresh CiG command list with the existing
UAV barriers. Graphics command lists remain single-use and retain CiG's submission
order requirement. This removes repeated operation enqueue/preparation overhead;
it does not remove resource ownership handoffs or accelerate kernel arithmetic.

NVIDIA's [CiG stream contract](https://docs.nvidia.com/cuda/archive/13.3.0/cuda-driver-api/group__CUDA__STREAM.html)
permits graph launches in CiG capture while excluding nested graph capture,
host callbacks, stream synchronization, stream memory operations and asynchronous
allocation/free. Therefore graph construction precedes CiG capture. General
[CUDA capture restrictions](https://docs.nvidia.com/cuda/cuda-programming-guide/04-special-topics/cuda-graphs.html)
also prohibit synchronizing an active capture and require ending invalidated
captures. Arda always ends a capture before falling back or returning an error.

An external adapter opts in by overriding
`SupportsGraphCapture(const FArdaCudaCapabilities&) const noexcept`; the default
is `false`. This promise is separate from D3D12 CiG support. The adapter must use
the supplied stream only and retain every GPU-visible library object in its
prepared call. It must avoid host callbacks, allocations, hidden synchronization
and hidden mutable configuration. When configuration affects captured work,
represent it in parameter bytes or change `GetGraphCaptureRevision()` before
the next recording. Native graph validation admits only kernel, copy, memset,
empty and child-graph nodes; synchronization/allocation/host nodes are rejected.
Library copies are allowed only when their source/destination lifetime is retained
by declared bindings or prepared state. Graph replay does not rerun CPU-side
adapter effects. See [External CUDA calls](CUDA-External-Calls.md).

`FArdaCudaGraphStats` reports successful captures, reuse hits, replay launches,
rebuilds, evictions, current variant count, fallbacks, capture failures and the last
fallback reason. `mReplayCount` excludes each executable's first launch;
`mCacheHitCount` counts reuse during recording. CiG launches are recorded before
GPU submission, so these are API activity counters, not proof of GPU completion.
Use normal submission completion plus output validation for that proof.

## Verification

`ArdaCudaTests.cpp` includes native tests for shared-stream identity, all-step
prevalidation, ping-pong buffers, view offsets and guards, parameter snapshots,
resource lifetime, repeated immediate submission, NoWork, sticky errors, and RDG
dependency/culling/failure behavior. Tests run in D3D12/Vulkan ordinary and CiG modes
with native validation. `ArdaComputeOperandTests.cpp` also checks batch admission
without a CUDA SDK and verifies that the single-kernel restriction remains intact.

The retained-graph tests run A → B → A with changed scalar values and buffer
addresses, compare numerical outputs, check capture/replay counts, and force LRU
eviction at capacity two. External-call tests cover explicit opt-in, `Prefer`
fallback, `Require` rejection, disabled capture, revision invalidation and prepared
state lifetime. The persistent ArdaInductor CUDA test completes five numerical
frames with alternate-frame timing, reusing both timed and untimed capture variants
and checking capture/replay counts and numerical outputs.
