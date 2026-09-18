# Unreal RHI comparison and defect ledger

Reviewed 2026-09-17 against the local Unreal Engine **5.8.1** source, commit
`71fe36aac5a8df5ccd66c763ffc902b29b6a9c43`. Unreal RHI, RHICore, D3D12RHI and
VulkanRHI were unmodified in that checkout. Arda was reviewed in the current
working tree, including the preceding backend/graph simplifications.

This is the current defect and feature-disposition ledger. The
[earlier parity plan](Unreal-RHI-Parity-Plan.md) remains the implementation history;
its broad support labels do not establish complete invalid-input or lifetime
coverage. Local source was available, so the comparison did not rely on web
summaries or assumptions about a different Unreal release.

**Implementation scope:** the accepted defects and features marked **implement**
are now implemented. Deferred features retain their original rationale below.
Arda graph still uses registered operands, automatic pipeline assignment,
dependency analysis and scheduling; this work does not replace that model with
Unreal RDG's authoring model. Shared validation and native feature facts protect
the existing graph and direct-RHI paths.

## Confirmed defects and changes in this pass

The rows below describe the original failure sequences. A fix is considered
verified only by the validation results recorded later in this document.

| ID | Severity | Original trigger and consequence | Change / disposition |
|---|---|---|---|
| RHI-01 | P1 | D3D12 `Open`/`Reset` reset the same command allocator while a submitted list could still be executing. Retaining the allocator prevents destruction, but does not make resetting its memory safe. | Rotate native command-list/allocator storage when the previous fence is pending; preserve completed-storage reuse. |
| RHI-02 | P1 | Vulkan `Open` re-began a previously submitted command buffer and retained stale recording state. `Reset` already knew how to retain old recording generations. | Route reopen through the existing reset/generation path. |
| RHI-03 | P1 | Recommitting a shader table replaced storage whose address was embedded in recorded or submitted ray dispatches. Retaining the mutable table wrapper did not retain its old contents. A failed rebuild could also invalidate the published table. | Retain immutable bound storage/record generations and publish successful rebuilds transactionally on both providers. |
| RHI-04 | P2 | Vulkan queue-signal destruction waited and destroyed its fence in the same try block. A failed wait, including device loss, skipped fence destruction. | Destroy after proven completion or reported device loss. OPEN-10 now transfers recoverable failures to deferred ownership; they are not permission to destroy a pending fence. |
| RHI-05 | P2 | Vulkan sparse mapping/commit temporary fences and newly allocated memory were raw handles; native failures bypassed cleanup, and an ignored sparse-bind error could wait indefinitely on an unsignaled fence. | Check sparse-bind results and clean temporary allocations on pre-submit rejection or proven retirement. OPEN-10 now retains accepted operations until retirement proof. |
| RHI-06 | P1 | Vulkan concurrent submissions could reserve timeline values before acquiring the queue lock, then submit higher values before lower ones. | Allocate values inside the queue submission critical section. |
| RHI-07 | P1 | A command list held a raw facade-device pointer. Dropping the last device reference or shutting down the backend when it released that last reference left `GetDevice` and recording helpers with a dangling pointer. | The command list strongly retains its facade device; its native recording and retained resources are destroyed before that reference is released. |
| RHI-08 | P1 | An in-range Indirect buffer with offset 1/2/3, or a non-DWORD draw stride, passed the facade and reached native recording. | Reject misaligned direct/indexed/compute/ray indirect argument offsets and draw strides before recording. |
| RHI-09 | P2 | Ray dimensions such as `(2^31, 2^31, 4)` wrapped the 64-bit product to zero and bypassed invocation limits. | Check multiplication overflow before applying the device limit. |
| RHI-10 | P1 | D3D12 initialization could fail after creating a queue but before its fence; destructor `WaitForIdle` then attempted to signal a null fence. | Skip uninitialized queue/fence pairs during teardown. Native fence-creation failure injection verifies cleanup and successful retry. |
| RHI-11 | P1 | D3D12 `WaitForIdle` reserved/signaled retirement fence values outside the execution critical section, allowing a concurrent submission to enqueue a newer value first. | Use the same execution mutex for idle reservation/signaling and command submission. Dedicated concurrent idle/submission tests verify increasing receipts and successful retirement. |

Unreal supplies the relevant lifetime model through deferred deletion and
fence-qualified payload retirement: [RHI resource retirement](D:/UnrealEngine/Engine/Source/Runtime/RHI/Private/RHIResources.cpp:46),
[D3D12 allocator retirement](D:/UnrealEngine/Engine/Source/Runtime/D3D12RHI/Private/D3D12Submission.cpp:1266),
and [Vulkan deferred deletion](D:/UnrealEngine/Engine/Source/Runtime/VulkanRHI/Private/VulkanMemory.cpp:4701).
Arda's retained native submission objects can provide the same safety property;
there is no need to duplicate Unreal's complete global deletion architecture.
Unreal also explicitly checks [indirect alignment and dispatch limits][u-indirect].

## Accepted findings and implemented corrections

IDs are preserved from the review so existing references remain useful. The
original `OPEN` prefix is an identifier, not the current disposition. All rows
below are implemented; validation scope and remaining limits follow the inventory.

| ID | Original failure or gap | Implemented correction and regression evidence |
|---|---|---|
| OPEN-01 | Direct work could reach the driver without a compatible open list, pipeline or dispatch limits. | One facade gate checks recording generation, queue, pipeline kind, group limits and index binding. Invalid void calls latch errors; Close and submission reject them until Open/Reset. Zero work validates state then records nothing. Native tests exercise wrong queues, absent/stale pipelines, exact/over-limit dispatch, reset and failed submission. |
| OPEN-02 | Descriptor shapes and native resource/format limits had no complete shared admission contract. | Shared dimension, enum, cube, mip, sample, CPU access, usage and initial-state checks feed creation, import, memory queries and graph validation. Immutable device limits and QueryFormatSupport provide native facts. Zero means an unreported limit, not zero capacity. CPU boundary tests and native capability probes cover accepted/rejected shapes, formats and range limits. |
| OPEN-03 | Graphics binding lacked central range, viewport and attachment validation. | Validate vertex/index usage, slots, alignment and ranges; viewport/scissor counts and bounds; framebuffer formats/samples and read-only depth. Normalize required bindings into declared layout order, reusing one omitted push-only set per layout/recording instead of allocating per bind. Native setter failure invalidates the recording; facade rejection preserves its prior state. Dedicated framebuffer views honor mip/layer/aspect/extent or return Unsupported when the native path cannot honor them. Vulkan resumes rendering after an intervening transfer/barrier. Native negative tests, descriptor interception and selected-subresource pixel readback cover these paths. |
| OPEN-04 | No structured native GPU-failure evidence. | CaptureDiagnosticSnapshot returns owned, bounded adapter/driver, queue/submission, native error, submitted marker and optional DRED/Vulkan device-fault data without submitting or waiting. Injected DRED/device-fault probes validate copying, bounds and unavailable facilities. No automatic device recreation is implied. |
| OPEN-05 | Short runs and source inspection left lifetime evidence incomplete. | Added repeated allocate/record/submit/reopen/trim cycles with resource/descriptor/allocator baselines, D3D12 partial initialization and concurrent idle tests, native wait/bind/signal failure injection and eventual production reclamation checks. Cached memory is trimmed before asserting a leak. This is bounded evidence, not exhaustive failure-path certification. |
| OPEN-06 | Append-only sparse heap history pinned replaced/unmapped heaps. | A shared range-ownership helper tracks only live mappings; accepted operations separately retain previous, replacement and intermediate owners until completion. Failed pre-submit operations roll back. Tests cover partial overlap, replacement, unmap, same-batch intermediate heaps and cross-queue accepted generations. Vulkan rejects overlapping ranges in a single native bind batch. |
| OPEN-07 | Vulkan multi-draw and nonzero first-instance support did not reflect enabled features. | Owned devices enable supported core facts; adopted devices report actual enabled facts. Multi-draw falls back to legal single draws when needed. mbIndirectFirstInstance explicitly governs GPU-written nonzero first-instance values. Adopted-device raster readback exercises fallback output and resumed rendering. |
| OPEN-08 | Vulkan mesh-only support was misreported as mesh plus amplification/task. | Added a mesh-only tier with semantic requirement matching. Amplification pipelines require the full tier; mesh-only pipelines remain usable. CPU tier tests and adopted-device capability/pipeline tests cover the distinction. |
| OPEN-09 | Raw facade address reuse could admit stale resources after reinitialization. | Resource ownership uses the retained per-device lifetime-tracker identity, avoiding both address-reuse admission and resource/device reference cycles. A same-provider reinitialization regression checks stale-resource rejection. |
| OPEN-10 | Recoverable Vulkan waits could leave accepted fences/backing unreclaimed or prematurely freed. | Accepted sparse operations and queue fences transfer to deferred owners. GC/idle retry retirement only after proof; accepted sparse generations preserve both sides of a remap. D3D12 failed retirement signals follow the same ownership rule. If final shutdown cannot prove retirement, providers enter process-retained quarantine and later initialization/GC retries bounded checks. Deterministic pending-work tests verify retention and eventual cleanup after the injected fault is released. |
| OPEN-11 | Same-size/growing prefix commit could discard contents; UINT64_MAX rounding could wrap to decommit. | Clamp before checked rounding and change only the tail of the committed prefix. GPU readbacks preserve known bytes through same-size, growth, shrink and extreme requests. Vulkan spatial-image and opaque-prefix modes require full Commit(0) unbind before switching; native byte offsets cannot safely identify spatial tiles. Explicit mip-tail ranges and buffer mappings remain interoperable. |

Implementation and tests: [facade][a-direct], [descriptor admission][a-descriptors],
[native D3D12][a-d3d-indirect], [native Vulkan][a-vk-direct],
[command validation tests](../../Source/ArdaInfra/ArdaBackend/Tests/ArdaCommandValidationTests.cpp),
[D3D12 lifetime tests](../../Source/ArdaInfra/ArdaBackend/Tests/ArdaD3D12LifetimeAuditTests.cpp),
[Vulkan lifetime tests](../../Source/ArdaInfra/ArdaBackend/Tests/ArdaVulkanLifetimeAuditTests.cpp),
and [sparse range tests](../../Source/ArdaInfra/ArdaBackend/Tests/ArdaSparseMappingTests.cpp).

## Memory ownership assessment

The allocator uses weak backreferences from leases to allocator state and from
provider allocation callbacks to the allocator/provider. Active placed resources
retain their heap lease; cached placed objects retain the native heap. This avoids
an obvious allocator/provider ownership cycle and keeps heaps alive while GPU work
uses them. Retirement destroys native objects outside the allocator mutex.
See [allocator ownership](D:/Projects/Ardashader/Source/ArdaInfra/ArdaBackend/Private/RHI/Memory/ArdaGpuAllocatorPrivate.cpp)
and the [submission-lease regression](D:/Projects/Ardashader/Source/ArdaInfra/ArdaBackend/Tests/ArdaGpuAllocatorTests.cpp).
This is a source assessment backed by targeted tests, not a claim that all failure
paths or driver allocations are leak-free.

## Feature inventory and disposition


“Present” means a public/native path and relevant authored evidence exist; it does not mean all hardware supports it or all invalid input is covered. “Partial” names the specific missing semantics. “Missing” means no equivalent portable authoring contract was found, not a claim about the GPU hardware. Implement/defer decisions are for Arda's desktop RT/ML scope, not blind Unreal parity.

| # | Category | Arda status and evidence | Unreal comparison | Recommendation and Arda-specific reason |
|---|---|---|---|---|
| 1 | Structured feature admission | **Present.** Independent ray, descriptor, queue, residency and arithmetic facts; `Evaluate` aggregates missing abilities. [Arda][a-capabilities] | Structured globals distinguish RT and shader-bundle abilities. [UE][u-globals] | Keep. Prefer exact requirements to a coarse “supports RT” flag; add native limits through the same immutable device facts. |
| 2 | Texture/buffer creation validation | **Present for the supported descriptor contract.** Shared shape/usage/initial-state admission plus immutable native limits. [Arda][a-descriptors] | Device-sized shape/sample/reserved restrictions. [UE][u-texture-validation] | **Implemented OPEN-02.** Creation, import, memory queries and graph resource validation share the contract. |
| 3 | Per-format support and compute limits | **Present.** QueryFormatSupport reports texture/buffer/attachment/storage/filter/sample support; device limits include dispatch groups and binding ranges. [Arda][a-capabilities] | Typed UAV format queries and dispatch limits. [UE format][u-format], [limits][u-indirect] | **Implemented OPEN-02.** Unreported limit fields remain zero. D3D12 storage-buffer capacity depends on element count/stride and does not publish a universal byte-range limit. |
| 4 | Buffer/texture copies and MSAA resolve | **Present, qualified.** Central texture-copy/resolve region policy; raw and pitched transfers. CPU tests cover unsupported planes, pitches, signed-row limit and bounds. [Arda][a-copies], [tests][a-copy-tests] | Copy validation checks block/region compatibility. [UE][u-copy] | Keep central helpers; extend negative cases rather than introduce a parallel copy API. Conversion/reallocation features can be deferred. |
| 5 | Readback, upload, mapped staging | **Present.** Async callback readback, pitched staging map/unmap, graph completion publication. [Arda][a-readback] | GPU-fence-qualified staging map and copy-to-staging. [UE][u-staging] | Keep. Further streaming/persistent-map APIs are optional until profiling shows overhead. Existing completion/failure semantics matter more. |
| 6 | Mip/slice/depth-stencil plane states | **Present.** Separate state cells and strict view-range validation; depth/stencil independent-state regression. [Arda][a-subresources], [test][a-plane-test] | Resource/view identity includes subresources for tracker assertions. [UE][u-validation-common] | Keep. Do not collapse to a single texture-state slot while simplifying. |
| 7 | Explicit/split/discard transitions | **Present for the documented state contract.** Explicit before/after and begin/end flags; facade verifies before-state, updates final state after success. [Arda][a-transitions] | Transition identities enforce begin/end pipeline accounting. [UE][u-transitions] | Keep explicit before/after and split accounting; extend negative/replay coverage alongside OPEN-01. No second transition abstraction is needed. |
| 8 | UAV ordering and heap aliasing | **Present.** Per-resource UAV barrier control, alias barriers, graph hazard ordering. [Arda][a-barriers], [graph][a-hazards] | Explicit UAV-overlap scopes and transient allocation overlap tracking. [UE UAV][u-uav], [allocator][u-transient] | Keep graph ordering authoritative. A public scoped UAV-overlap API is **deferred** until callers need deliberate independent accesses outside the graph. |
| 9 | Graphics/compute/copy queues and GPU waits | **Present with command-storage and timeline corrections in this pass.** Queue family/timestamp metadata, per-submission tokens and queue waits. [Arda][a-queues] | Pipeline-aware transitions and queue/pipeline execution contexts. [UE][u-transitions] | Keep the corrected reset/retirement/timeline paths and their regression coverage. Do not mistake existing API support for complete concurrency correctness. |
| 10 | Direct draw/dispatch validation | **Present.** Recording/queue/pipeline/limit gate, sticky void errors, deliberate zero-work behavior. [Arda][a-direct] | Pipeline, resource and work validation. [UE][u-work-validation] | **Implemented OPEN-01.** Native failure cannot leave a silently reusable partial recording. |
| 11 | Indirect draw/compute/ray work | **Present.** Argument range/usage/alignment and pipeline gates; enabled-feature-based Vulkan multi-draw fallback and first-instance contract. [Arda][a-indirect] | Alignment and GPU-count multi-draw. [UE validation][u-indirect], [count variant][u-count-draw] | **Implemented OPEN-07.** **Defer** GPU-count/device-generated commands until an actual workload needs them. GPU-written arguments remain the caller's responsibility. |
| 12 | Vertex/geometry/tessellation/mesh pipelines | **Present, gated.** Vulkan mesh-only and mesh-plus-task facts are distinct, including adopted devices. [Arda][a-pipelines], [tests][a-cap-tests] | Graphics shader frequencies and mesh/bundle support. [UE][u-globals] | **Implemented OPEN-08.** Retain existing stages; raster authoring remains narrower as described below. |
| 13 | Fixed-function raster, depth/stencil and blend | **Partial.** No stencil front/back operations/masks/reference, blend operations/color write masks, or depth-bias controls; depth test/write and blend factors exist. [Arda][a-fixed] | Full depth/stencil, blend-op/mask and raster depth-bias descriptors. [UE][u-fixed] | **Defer** broad raster expansion for RT/ML scope. Add color write masks/depth bias/stencil only when tooling/compositing/raster shadows need them; descriptor hashing/equality/PSO keys must evolve together. |
| 14 | Graphics binding/framebuffer compatibility | **Present for supported views.** Central range/viewport/PSO validation; native views honor selected subresources. [Arda][a-graphics-bind] | Stream/render-pass/PSO validation. [UE][u-stream], [render pass][u-renderpass] | **Implemented OPEN-03.** Unsupported native view shapes/aspects/reinterpretations fail explicitly instead of silently binding another view. |
| 15 | Render-pass load/store/resolve/subpasses | **Missing explicit authoring contract.** Framebuffer attachments and clears exist; callers cannot specify full load/store/subpass behavior. [Arda][a-framebuffer] | Explicit color/depth/stencil load/store actions and subpass hints. [UE][u-load-store] | **Defer** until tile-based/mobile targets or measurable desktop pass merging needs appear; current desktop scope explicitly excludes mobile. Keep clear/resolve behavior documented. |
| 16 | Fixed descriptor sets and typed SRV/UAV views | **Present.** Resource/ownership/range validation and typed view retention; GPU range test exists. [Arda][a-binding-schema], [tests][a-boundary-tests] | Shader resources validated against declarations and access tracking. [UE][u-work-validation], [views][u-texture-validation] | Keep. Extend negative tests for native limit edge cases instead of recreating Unreal's parameter binding layer. |
| 17 | Bindless, variable arrays, update-after-bind | **Present, capability-gated.** Independent flags/capacities and table updates; pending descriptor versions retained. [Arda][a-bindless], [write][a-write-table] | Bindless shader parameters/resource collections. [UE][u-shader-parameters] | Keep actual layout distinctions. Complete in-flight mutation/reset regressions; hardware unavailability remains a legitimate skip, not proof of execution. |
| 18 | Uniform updates and dynamic offsets | **Partial.** Uniform buffers, buffer ranges and volatile storage exist; no portable bind-time uniform dynamic-offset command was found. [Arda][a-uniform], [binding schema][a-binding-schema] | Uniform update and dynamic-offset commands. [UE update][u-uniform], [dynamic][u-dynamic-offset] | **Defer** dynamic offsets until binding-set churn is measured. Existing per-frame graph bindings and volatile buffers cover current semantics. |
| 19 | Push/root constants | **Present.** Capacity/alignment validation latches void-call errors; multiple block/register-space tests. [Arda][a-direct], [tests][a-boundary-tests] | Root constants plus batched parameter application. [UE][u-shader-parameters] | Keep. Reuse its error-latching model for direct work validation. |
| 20 | Resource collections and texture references | **Present.** Retained resource collections, descriptor-table backing and mutable texture references. [Arda collections][a-collections], [references][a-texture-reference] | Explicit collection updates and texture-reference updates. [UE][u-collections], [references][u-texture-reference] | Keep; ensure in-flight snapshots/lifetimes remain part of update contracts, checked by targeted generation/lifetime regressions. |
| 21 | BLAS/TLAS build, update and compaction | **Present, gated.** Memory sizing, build/update, GPU instance-buffer path with CPU count, compact queries/copies and lifecycle tests. [Arda][a-as], [tests][a-cap-tests] | Batched AS building and separately bound scene storage. [UE][u-as] | Keep qualified scope. **Defer** true GPU-generated build counts/batched platform policies until needed; do not call the CPU-count instance-buffer path fully indirect building. |
| 22 | RT pipelines, local arguments and persistent SBT | **Present with generation retention repaired in this audit.** Explicit record updates/commit, export/hit groups, local records, direct/indirect rays. [Arda][a-sbt] | Separate shader-table creation and validation-before-dispatch. [UE creation][u-sbt], [validation][u-sbt-validation] | **Validate and keep** immutable generation retention added in this audit. Preserve existing local-argument breadth rather than removing it for LOC. |
| 23 | Work graphs and shader bundles | **Present with deliberate limits.** D3D12 CPU-input work graphs, portable prepared shader bundles; unsupported providers return `Unsupported`. [Arda][a-workgraph] | Compute/graphics bundle dispatch and multiple work-graph tiers. [UE][u-bundles], [tiers][u-globals] | Keep platform gating; **defer** GPU-input work graphs/native device-generated-command parity until a measured workload needs it. |
| 24 | Sampler feedback and opacity micromaps | **Present, platform-specific.** Dedicated APIs and real output/lifecycle probes; feedback D3D12 and micromaps Vulkan. [Arda][a-special-rt], [tests][a-cap-tests] | Unreal's broader texture feedback/residency and RT ecosystem is not a portable guarantee of identical hardware features. [UE reserved/RT facts][u-globals] | Keep honest per-provider gating. No fake generic “native support” emulation is needed. |
| 25 | VRS and conservative rasterization | **Missing authorable native feature; placeholders only.** Capability bits default false; no raster parameter/VRS binding API, and matrix intentionally fails if true. [Arda caps][a-capabilities], [test fail][a-contract-probe] | Shading-rate command and fuller raster state. [UE VRS][u-vrs], [raster][u-fixed] | **Defer**, keep false. RT/ML workloads do not justify adding controls and qualification workloads merely because hardware supports them. |
| 26 | Timers, events, GPU fences; occlusion | **Partial.** Event/timer/fence APIs and per-queue timer reuse/wrap tests exist; no occlusion/predication API. `mbQueries` explicitly means these three families. [Arda][a-queries], [tests][a-cap-tests] | Render query type includes occlusion. [UE][u-occlusion] | Keep timing/fences. **Defer** occlusion/predication to a demonstrated visibility workload; GPU compute culling may be more useful for this renderer. Do not describe `mbQueries` as all query types. |
| 27 | Explicit heaps and transient memory planning | **Present.** Placed/virtual/committed resources, memory queries, graph memory plan and alias activations. [Arda][a-heaps], [graph][a-graph-runtime] | Transient allocations track overlap/fences and heap/page strategies. [UE][u-transient] | Keep and repair retirement/reset defects. **Defer** Unreal's numerous platform allocator strategies; Arda needs demonstrable budget/fragmentation improvements, not naming parity. |
| 28 | Sparse residency and budget telemetry | **Present with shape/device qualifications.** Tile maps, preserving prefix commit, range ownership and deferred retirement. [Arda][a-sparse] | Reserved-resource restrictions and globals. [UE validator][u-texture-validation], [globals][u-globals] | **Implemented OPEN-06/10/11.** **Defer** extra sparse shapes/alias mappings until streaming requires them. Vulkan spatial-image/opaque-prefix mode switches require full unbind. |
| 29 | Shader libraries and pipeline caches | **Present.** Retained library entry points and persistent native cache path; execution/cache probes. [Arda][a-libraries], [tests][a-cap-tests] | Shader-library/pipeline state cache infrastructure. [UE][u-pso-cache] | Keep. Unreal's engine-level precache policy is **unnecessary** to port wholesale; build policy around Arda's actual shader/pipeline inventory. |
| 30 | Graph access validation and immutable node semantics | **Present.** Declared access, parameter snapshots, hazard validation, transactional edits and undeclared resource rejection. [Arda hazards][a-hazards], [context][a-graph-runtime] | RDG resource/parameter/pass validation. [UE][u-rdg] | Keep precise ranges and immutable snapshots. Add regression coverage where native lifetime changes touch replay; no justification for deleting validations to reduce code size. |
| 31 | Numeric shader capability / subgroup admission | **Present but intentionally small.** FP16, packed INT8, subgroup width interval and native buffer addresses are independently reported. [Arda][a-numeric] | A larger global shader/platform capability set. [UE][u-globals] | Keep exact facts; **defer** cooperative matrices/FP64/atomic breadth until a module needs them and a native known-result test exists. |
| 32 | Desktop presentation, multi-GPU and platform policy | **Partial by scope.** Desktop custom present exists and has tests; no multi-GPU mask, mobile/Metal/console layer, full HDR/frame-pacing policy. [Arda tests][a-cap-tests] | GPU masks are embedded in buffer/texture descriptors; rich engine viewport policy. [UE masks][u-mgpu] | **Unnecessary now** for multi-GPU/platform parity; **defer** HDR/frame-pacing to a product display requirement. Current desktop restriction is explicit and reasonable. |
| 33 | Device failure and GPU crash diagnostics | **Present, optional native detail.** Owned bounded snapshots include DRED or enabled Vulkan device-fault data with submitted-marker fallback. | [UE DRED](D:/UnrealEngine/Engine/Source/Runtime/D3D12RHI/Private/D3D12Util.cpp:224), [UE Vulkan faults](D:/UnrealEngine/Engine/Source/Runtime/VulkanRHI/Private/VulkanUtil.cpp:525). | **Implemented OPEN-04.** **Defer** automatic recovery; application resource recreation needs a separate contract. Missing native fault data does not establish device health. |
| 34 | Leak attribution and memory snapshots | **Partial by deliberate scope.** Resource/descriptor/pending counts, allocator/budget telemetry and repeatable baseline/failure tests; no full allocation-level GPU census. | [UE memory stats](D:/UnrealEngine/Engine/Source/Runtime/RHI/Public/DynamicRHI.h:536) and deferred deletion. | **Implemented OPEN-05.** Deeper allocation snapshots remain deferred until these counters cannot locate growth; a wholesale profiler port is unnecessary. |
| 35 | Query pools, RT scratch pools and allocator strategy | **Partial policy parity, safe inspected ownership.** Arda uses per-timer-query native heaps and per-build retained RT scratch; Unreal has reusable pools and caller-supplied scratch options. | [UE query pools](D:/UnrealEngine/Engine/Source/Runtime/D3D12RHI/Private/D3D12Device.cpp:727), [UE scratch fallback](D:/UnrealEngine/Engine/Source/Runtime/D3D12RHI/Private/D3D12RayTracing.cpp:4937). | **Defer** pooling/caller-scratch expansion until profiles show allocation or memory-pressure cost. Inspected timer/scratch paths retain their native allocations through submission completion; a pool is a performance policy, not a missing rendering feature. |

## Validation and remaining coverage

Validation completed on 2026-09-17 on NVIDIA RTX PRO 6000 Blackwell Workstation
Edition, driver 610.62. GPU validation was enabled in both configurations.

| Configuration | Build scope | Cases | Passed | Skipped | Failed |
|---|---|---:|---:|---:|---:|
| Debug, D3D12 + Vulkan, CUDA off | Full project, examples and public-header compilation | 945 | 799 | 146 | 0 |
| Release, D3D12 + CUDA, Vulkan off | ArdaBackendTests, ArdaRHITests and ArdaGraphTests | 727 | 615 | 112 | 0 |

CUDA used toolkit 13.4.59. The Release selection contains every discovered case
from those three rebuilt executables; stale/unbuilt example binaries were not
part of that selection. Explicitly disabled providers are skipped or omitted by
fixture configuration; unexpected initialization failures remain failures when
the provider is enabled. Vulkan CUDA and a separate validation-off configuration
were not exercised in this pass.

The recorded skips have the following causes; zero reported failures does not
mean that every path was exercised:

| Skip reason | Debug | CUDA Release |
|---|---:|---:|
| CUDA disabled / launch support unavailable in the CUDA-off build | 126 | 0 |
| Vulkan backend disabled in the Release build | 0 | 98 |
| Backend capability unreported, unsupported or inapplicable | 18 | 10 |
| Symlink fixture setup reported an existing destination | 2 | 2 |
| D3D12 CUDA graphics-queue surface probe returned CUDA_ERROR_UNKNOWN | 0 | 2 |

The symlink setup errors and two CUDA surface-probe errors are unresolved
validation gaps, not demonstrated hardware limitations. The CUDA skips are
`SurfaceConversionAndMipLifetime/2` and `SequenceMixesBufferAndSurfaceSchemas/2`
in `Native/FArdaCudaGpu`; their ordinary D3D12 CUDA-context counterparts passed.
The recorded probe error alone does not distinguish a driver issue from an
implementation defect. The two shader-directory symlink fixtures reported
already-existing destinations rather than missing symlink privileges.

The full Debug figures describe the tested working tree, including independent
terrain-example edits that are not part of the backend/graph commit. Both builds
also used the existing local EASTL CMake/warning adjustments, which remain outside
this commit; these runs are not an isolated clean-checkout certification.

These are complete final runs after the implementation and fixture corrections,
not an aggregate of earlier failed runs. The remaining source-header edits only
clarify documentation. The Debug run includes all 11 D3D12 lifetime audit cases,
Vulkan deferred-owner/mode-switch tests, native command/binding admission,
selected mip/layer pixel readback, sparse prefix byte preservation, and the
8,200-bind descriptor-reuse regression. Both configurations exercise graph
planning, pipeline inference, scheduling, replay and completion tests.

- Formatting: **44 changed C++ source/header files**, zero formatting findings.
- Naming: **1,223 declarations in 185 backend/provider/graph files**, zero violations.
- `git diff --check`: passed.
- Public API synchronization and recipe generation checks: passed. Backend API:
  **3,046 symbols, 36/36 headers**; graph API: **635 symbols, 13/13 headers**.
  Recipe coverage: **35 resource kinds and 90 capability predicates**.
- Documentation validation: zero errors. It checks generated inventories,
  links/fragments, SVG/XML, glossary and chapter contracts.
- Browser smoke checks: localhost under `/Docs/ArdaBackend/`, desktop 1280px,
  tablet 768px and mobile 390px reflow without horizontal page overflow;
  diagrams loaded and diagnostic snapshot API search returned canonical entries.
  This was not an exhaustive assistive-technology or offline-browser audit.
- A fresh isolated Docs-only reader explained recording recovery, sparse data
  preservation, ownership/counters, fault snapshots and graph scheduling from
  guide-to-canonical links. Earlier reader findings were corrected in source
  contracts/generation and graph-order prose. The final sampled reading passed;
  its minor Boolean-selector/void-return wording findings were also clarified.

Evidence: [Debug build](../../build/rhi-implementation-final-build.log),
[Debug tests](../../build/rhi-implementation-final-tests.log),
[Debug XML](../../build/rhi-implementation-final-results.xml),
[CUDA build](../../build/rhi-implementation-cuda-build.log),
[CUDA tests](../../build/rhi-implementation-cuda-tests.log),
[CUDA XML](../../build/pixel-sort-parameter-validation/rhi-implementation-cuda-results.xml),
[documentation checks](../../build/rhi-implementation-doc-validation.log),
[formatting](../../build/rhi-implementation-format.log), and
[naming](../../build/rhi-implementation-naming.log).
Build artifacts are local evidence and are not required for published guides.


The tests combine native GPU execution with controlled API interception. Injected
device-loss reports follow actual completion; no physical adapter removal, real
GPU hang, every-driver certification or exhaustive allocation-failure campaign
was attempted. Native capability skips are not passing execution evidence.
Snapshot availability and graceful retirement under injected faults do not imply
automatic device recovery.

If a driver permanently refuses completion/device-loss proof, exceptional
quarantine retains its provider and pending owners until process exit. This is
an explicit safety policy, not a claim that arbitrary driver failures reclaim
all memory. Normal completion and released injected faults have reclamation tests.

The feature inventory's deferred/unnecessary items remain outside this accepted
implementation scope. Arda graph's operand registry, automatic pipeline selection,
scheduling, replay and access validation remain authoritative.

[a-capabilities]: D:/Projects/Ardashader/Source/ArdaInfra/ArdaBackend/Public/RHI/Config/ArdaRHICapabilities.h
[a-descriptors]: ../../Source/ArdaInfra/ArdaBackend/Private/RHI/Resources/
[a-format-info]: ../../Source/ArdaInfra/ArdaBackend/Public/RHI/Resources/ArdaRHIFormat.h
[a-copies]: ../../Source/ArdaInfra/ArdaBackend/Private/RHI/Scheduling/ArdaRHIResourceCopies.cpp
[a-copy-tests]: D:/Projects/Ardashader/Source/ArdaInfra/ArdaBackend/Tests/ArdaTextureBufferTests.cpp
[a-readback]: D:/Projects/Ardashader/Source/ArdaInfra/ArdaBackend/Public/RHI/Device/ArdaRHIDevice.h
[a-subresources]: D:/Projects/Ardashader/Source/ArdaInfra/ArdaBackend/Public/RHI/Resources/ArdaRHISubresources.h
[a-plane-test]: D:/Projects/Ardashader/Source/ArdaInfra/ArdaRenderGraph/Tests/ArdaInductorCommandProgramTests.cpp
[a-transitions]: ../../Source/ArdaInfra/ArdaBackend/Private/RHI/Scheduling/ArdaRHICommandStates.cpp
[a-barriers]: ../../Source/ArdaInfra/ArdaBackend/Public/RHI/Scheduling/ArdaRHICommandList.h
[a-hazards]: D:/Projects/Ardashader/Source/ArdaInfra/ArdaRenderGraph/Private/ArdaDependencyHazards.cpp
[a-queues]: D:/Projects/Ardashader/Source/ArdaInfra/ArdaBackend/Public/RHI/Device/ArdaRHIDevice.h
[a-direct]: ../../Source/ArdaInfra/ArdaBackend/Private/RHI/Pipelines/ArdaRHICommandPipelines.cpp
[a-indirect]: ../../Source/ArdaInfra/ArdaBackend/Private/RHI/Pipelines/ArdaRHICommandPipelines.cpp
[a-d3d-indirect]: D:/Projects/Ardashader/Source/ArdaInfra/ArdaBackendImpls/D3D12/ArdaD3D12Backend.cpp
[a-vk-indirect]: D:/Projects/Ardashader/Source/ArdaInfra/ArdaBackendImpls/Vulkan/ArdaVulkanBackend.cpp
[a-vk-direct]: D:/Projects/Ardashader/Source/ArdaInfra/ArdaBackendImpls/Vulkan/ArdaVulkanBackend.cpp
[a-pipelines]: ../../Source/ArdaInfra/ArdaBackend/Private/RHI/Pipelines/
[a-fixed]: ../../Source/ArdaInfra/ArdaBackend/Public/RHI/Pipelines/ArdaRHIFixedFunctionStates.h
[a-graphics-bind]: ../../Source/ArdaInfra/ArdaBackend/Private/RHI/Pipelines/ArdaRHICommandPipelines.cpp
[a-framebuffer]: ../../Source/ArdaInfra/ArdaBackend/Public/RHI/Resources/ArdaRHIFramebuffer.h
[a-binding-schema]: ../../Source/ArdaInfra/ArdaBackend/Public/RHI/Shaders/ArdaRHIBindingLayout.h
[a-bindless]: ../../Source/ArdaInfra/ArdaBackend/Public/RHI/Shaders/ArdaRHIDescriptorTable.h
[a-write-table]: ../../Source/ArdaInfra/ArdaBackend/Private/RHI/Shaders/ArdaRHIShaderCreation.cpp
[a-uniform]: D:/Projects/Ardashader/Source/ArdaInfra/ArdaBackend/Public/RHI/Device/ArdaRHIDevice.h
[a-collections]: ../../Source/ArdaInfra/ArdaBackend/Private/RHI/Shaders/ArdaRHIShaderCreation.cpp
[a-texture-reference]: D:/Projects/Ardashader/Source/ArdaInfra/ArdaBackend/Public/RHI/Device/ArdaRHIDevice.h
[a-as]: D:/Projects/Ardashader/Source/ArdaInfra/ArdaBackend/Public/RHI/Device/ArdaRHIDevice.h
[a-sbt]: ../../Source/ArdaInfra/ArdaBackend/Private/RHI/Shaders/ArdaRHIShaderCreation.cpp
[a-workgraph]: D:/Projects/Ardashader/Source/ArdaInfra/ArdaBackend/Public/RHI/Device/ArdaRHIDevice.h
[a-special-rt]: D:/Projects/Ardashader/Source/ArdaInfra/ArdaBackend/Public/RHI/Device/ArdaRHIDevice.h
[a-queries]: D:/Projects/Ardashader/Source/ArdaInfra/ArdaBackend/Public/RHI/Device/ArdaRHIDevice.h
[a-heaps]: D:/Projects/Ardashader/Source/ArdaInfra/ArdaBackend/Public/RHI/Device/ArdaRHIDevice.h
[a-graph-runtime]: D:/Projects/Ardashader/Source/ArdaInfra/ArdaRenderGraph/Private/ArdaInductorRuntime.cpp
[a-sparse]: D:/Projects/Ardashader/Source/ArdaInfra/ArdaBackend/Public/RHI/Device/ArdaRHIDevice.h
[a-libraries]: D:/Projects/Ardashader/Source/ArdaInfra/ArdaBackend/Public/RHI/Device/ArdaRHIDevice.h
[a-numeric]: D:/Projects/Ardashader/Source/ArdaInfra/ArdaBackend/Public/RHI/Config/ArdaRHICapabilities.h
[a-boundary-tests]: D:/Projects/Ardashader/Source/ArdaInfra/ArdaBackend/Tests/ArdaBindingBoundaryTests.cpp
[a-cap-tests]: D:/Projects/Ardashader/Source/ArdaInfra/ArdaBackend/Tests/ArdaExtendedRHIParityTests.cpp
[a-contract-probe]: D:/Projects/Ardashader/Source/ArdaInfra/ArdaBackend/Tests/ArdaExtendedRHIParityTests.cpp
[u-indirect]: D:/UnrealEngine/Engine/Source/Runtime/RHI/Public/RHIValidation.h:48
[u-work-validation]: D:/UnrealEngine/Engine/Source/Runtime/RHI/Private/RHIValidation.cpp:1413
[u-globals]: D:/UnrealEngine/Engine/Source/Runtime/RHI/Public/RHIGlobals.h:424
[u-format]: D:/UnrealEngine/Engine/Source/Runtime/RHI/Public/RHI.h:133
[u-texture-validation]: D:/UnrealEngine/Engine/Source/Runtime/RHI/Private/RHI.cpp:1510
[u-copy]: D:/UnrealEngine/Engine/Source/Runtime/RHI/Public/RHIValidationUtils.h:15
[u-staging]: D:/UnrealEngine/Engine/Source/Runtime/RHI/Public/DynamicRHI.h:591
[u-validation-common]: D:/UnrealEngine/Engine/Source/Runtime/RHI/Public/RHIValidationCommon.h:258
[u-transitions]: D:/UnrealEngine/Engine/Source/Runtime/RHI/Public/RHITransition.h:510
[u-uav]: D:/UnrealEngine/Engine/Source/Runtime/RHI/Public/RHIValidationContext.h:257
[u-transient]: D:/UnrealEngine/Engine/Source/Runtime/RHICore/Public/RHICoreTransientResourceAllocator.h:316
[u-count-draw]: D:/UnrealEngine/Engine/Source/Runtime/RHI/Public/RHIValidationContext.h:971
[u-fixed]: D:/UnrealEngine/Engine/Source/Runtime/RHI/Public/RHI.h:271
[u-stream]: D:/UnrealEngine/Engine/Source/Runtime/RHI/Public/RHIValidationContext.h:761
[u-renderpass]: D:/UnrealEngine/Engine/Source/Runtime/RHI/Public/RHIValidationContext.h:1150
[u-load-store]: D:/UnrealEngine/Engine/Source/Runtime/RHI/Public/RHIResources.h:4098
[u-shader-parameters]: D:/UnrealEngine/Engine/Source/Runtime/RHI/Public/RHIContext.h:313
[u-uniform]: D:/UnrealEngine/Engine/Source/Runtime/RHI/Public/DynamicRHI.h:414
[u-dynamic-offset]: D:/UnrealEngine/Engine/Source/Runtime/RHI/Public/RHIContext.h:364
[u-collections]: D:/UnrealEngine/Engine/Source/Runtime/RHI/Public/DynamicRHI.h:509
[u-texture-reference]: D:/UnrealEngine/Engine/Source/Runtime/RHI/Public/DynamicRHI.h:434
[u-as]: D:/UnrealEngine/Engine/Source/Runtime/RHI/Public/RHIContext.h:474
[u-sbt]: D:/UnrealEngine/Engine/Source/Runtime/RHI/Public/DynamicRHI.h:970
[u-sbt-validation]: D:/UnrealEngine/Engine/Source/Runtime/RHI/Private/RHIValidation.cpp:548
[u-bundles]: D:/UnrealEngine/Engine/Source/Runtime/RHI/Public/RHIContext.h:318
[u-vrs]: D:/UnrealEngine/Engine/Source/Runtime/RHI/Public/RHIValidationContext.h:1016
[u-occlusion]: D:/UnrealEngine/Engine/Source/Runtime/RHI/Public/RHIDefinitions.h:255
[u-pso-cache]: D:/UnrealEngine/Engine/Source/Runtime/RHI/Public/PipelineFileCache.h:1
[u-rdg]: D:/UnrealEngine/Engine/Source/Runtime/RenderCore/Private/RenderGraphValidation.cpp:218
[u-mgpu]: D:/UnrealEngine/Engine/Source/Runtime/RHI/Public/RHIResources.h:1382
