# ArdaInductor pipeline inference

Persistent dependency-graph nodes describe shader contributions and request pipeline slots.
`EndGraphEdit()` infers each live request and creates or reuses its native PSO before `Execute()` records commands.
Recording callbacks receive the resolved PSO through `FArdaDependencyExecutionContext`; they still supply resource bindings and draw/dispatch arguments.
The public contracts are [ArdaInductorPipeline.h](../../Source/ArdaRenderGraph/Public/ArdaInductorPipeline.h) and [ArdaDependencyGraph.h](../../Source/ArdaRenderGraph/Public/ArdaDependencyGraph.h).

## Describe contributions and slots

A registered `TArdaDependencyNodeDefinition<Parameters>` returns `FArdaDependencyNodeDesc` from `mDescribe`.
Add any number of `FArdaInductorPipelineContribution` values to `mPipelineStages` and requests to `mPipelines`.
Each contribution retains a shader; `IArdaRHIShader::GetStage()` supplies its stage and `GetPersistentCacheHash()` supplies its content identity.
Each request selects `mKind`, optional `mGroup`, and a consumer-local `mSlot`.
An empty slot becomes `"default"`; duplicate slot names on one node fail compilation.
The graph assigns `mTerminalNodeId`, so ordinary node descriptions leave it unset.

For example, this description assumes typed parameters contain an output resource, compute shader, and compatible global layouts:

```cpp
Definition.mDescribe = [](const FComputeParameters& P)
{
    FArdaDependencyNodeDesc Desc;
    Desc.mAccesses.push_back({P.mOutput,
        EArdaDependencyAccess::Write, EArdaRHIResourceState::UnorderedAccess});
    FArdaInductorPipelineContribution Stage;
    Stage.mShader = P.mShader;
    Stage.mBindingLayouts = P.mLayouts;
    Desc.mPipelineStages.push_back(eastl::move(Stage));
    FArdaInductorPipelineRequest Request;
    Request.mKind = EArdaPipelineStateKind::Compute;
    Request.mSlot = "default";
    Desc.mPipelines.push_back(eastl::move(Request));
    return Desc;
};
```

The definition also needs its normal canonical parameter-key visitor and recording callback.
Its key must cover every behavior-affecting parameter, including shader contents, layout semantics, resource versions, and dispatch dimensions.
Returning a PSO request does not declare resource accesses: describe those separately in `mAccesses`.

## Ancestry, intermediate nodes, and ambiguity

Inference searches the requesting node and its dependencies, including explicit `AddDependency(Producer, Consumer)` edges.
Each branch stops before an upstream consumer that requests the same pipeline family: that consumer owns a completed pipeline, so its stages/settings belong to its own draw or dispatch.
Thus consecutive compute consumers using different shaders get independent PSOs without manual group tags; the terminal's own stages and unfinished stage ancestry remain eligible.
Consumers of other pipeline families remain traversable. A shared stage provider can feed several consumers through independent dependency edges that bypass any completed same-family consumer.
The compiler projects request families into `FArdaInductorPipelineNode::mPipelineBoundaries`; direct callers of `InferArdaInductorPipeline()` must supply that projection themselves.
Intermediate nodes may have no shader contributions; a vertex stage can reach a draw through several such nodes.
Attachment order and node numbering do not choose the winning shader.
Missing dependency IDs and cycles are checked throughout the terminal's ancestry, including beyond pipeline boundaries; stage compatibility and shader identities are checked only inside its bounded inference region.

For a node that only contributes stages, set `mbPipelineStageOnly = true` and connect it to its consumers.
It requires no recording callback and records no separate GPU commands; its live descendants use the inferred PSO.
Stage-only descriptions must leave `mAccesses`, `mColorTargets`, and `mDepthTarget` empty; put resource work in executable nodes.
An unconsumed declaration has no reason to keep an otherwise dead branch alive.

The grouping rules are explicit:

- An empty contribution `mGroup` is shared by every selected group.
- An empty request `mGroup` accepts at most one named group within its bounded inference region.
- Multiple reachable named groups require the request to select one.
- Distinct shaders for the same stage/export in the selected group are ambiguous; repeating the same stage/content is accepted.
- Contributions belonging to other pipeline families are ignored on data ancestry; pixel shaders apply to both traditional graphics and mesh pipelines.
- Global layouts merge in canonical register-space and semantic order. Equal layouts are deduplicated. Distinct layouts in one space are accepted when their stage visibility is disjoint, matching the RHI graphics binding model; differing layouts with overlapping visibility are rejected. Use compatible shared settings when stages overlap.

For example, two ancestors contributing `VS_A` in group `"terrain"` and `VS_B` in group `"water"` can share an ungrouped pixel shader.
A request with no group fails; `Request.mGroup = "terrain"` selects `VS_A` plus that shared pixel shader.
Giving both vertex contributions an empty group still fails because they conflict within the selected pipeline.
A node may instead request two slots, `"terrain"` and `"water"`, each with its corresponding group.

## The five pipeline families

The resolver maps all five `EArdaPipelineStateKind` values to the existing RHI pipeline cache:

- `Compute`: exactly one compute shader; result member `mCompute`.
- `Graphics`: vertex shader with optional pixel/geometry stages. Tessellation requires paired hull/domain shaders, `PatchList` topology, and valid patch-control-point settings; result `mGraphics`.
- `Meshlet`: mesh shader with optional amplification and pixel shaders; result `mMeshlet`.
- `RayTracing`: named ray-generation, miss, and callable exports plus named hit groups; at least one ray-generation shader is required; result `mRayTracing`.
- `WorkGraph`: work-graph shaders plus program name, entry point, and nonzero maximum input records; result `mWorkGraph`.

These are inference capabilities. Native creation still checks the selected device/backend's support and returns its ordinary failure status when a feature is unavailable.
The resolver does not translate shader languages or provide a software substitute for unsupported native pipeline features.

Ray-generation, miss, and callable contributions set `mExportName`.
Closest-hit, any-hit, and intersection contributions set a common `mHitGroupName` to assemble one group; an intersection shader makes it procedural.
Use `mLocalBindingLayout` for a ray export or hit group, and `mBindingLayouts` for global layouts.
Conflicting local layouts or shader exports fail; general export names and hit-group names must be distinct.
Null shaders and empty configured hit groups are rejected.

Work-graph program settings can be provided by the consumer:

```cpp
auto Settings = eastl::make_shared<FArdaInductorPipelineConfiguration>();
Settings->mKind = EArdaPipelineStateKind::WorkGraph;
Settings->mWorkGraph.mDesc.mProgramName = "MyProgram";
Settings->mWorkGraph.mDesc.mEntryPoint = "MyEntry";
Settings->mWorkGraph.mDesc.mMaxInputRecords = 32;
Request.mKind = EArdaPipelineStateKind::WorkGraph;
Request.mConfiguration = Settings;
```

## Fixed settings and ownership

`FArdaInductorPipelineConfiguration` holds the five existing pipeline initializers; only the member selected by `mKind` participates.
Attach it to a contribution or request through `mConfiguration` to supply raster/input-layout settings, ray limits, or work-graph program settings.
It may also seed shader fields and global layouts. Later contributions merge with those fields and cannot silently replace conflicting shaders.
When several settings anchors reach one request, their fixed/program settings must agree; inference does not choose the closest anchor.

Finish building a configuration before attaching the node. The graph snapshots description configuration values when attaching, while retaining immutable RHI shader/layout objects.
Descriptor vectors and strings are owned by that snapshot; changing a caller's configuration alias must not rewrite already-attached node semantics.
The same rule is the author's responsibility for arbitrary nested pointers in custom typed parameters: copying a `shared_ptr<const T>` does not freeze another mutable alias to `T`.
Recording callbacks must use the described, immutable behavior. Replace changed semantics inside a graph edit and give them an updated canonical key.
Readback destinations or diagnostic sinks can remain mutable outputs; they must not silently change the declared pipeline or resource behavior.

## Framebuffers, keys, and cache lifetime

Graphics and mesh consumers declare `mColorTargets` and/or `mDepthTarget` plus matching texture accesses.
The runtime materializes their framebuffer and passes it to the cache during pipeline resolution.
Empty `mColorFormats` and an `Unknown` depth format are completed from its attachments.
Set `mSampleCount = 0` to derive samples; its default value of one remains an explicit requirement.
Explicit formats or sample counts that disagree with the framebuffer fail.
Without a framebuffer, the current cache requires nonempty explicit color formats and a nonzero sample count.

[`FArdaInductorPipelinePattern::mStableKey`](api-reference.html#api-arda-fardainductorpipelinepattern-mstablekey-cee78de4) identifies normalized shader and pipeline semantics before framebuffer completion.
It includes pipeline family, shader stages/content hashes, global/local layouts, input layout, fixed state, formats/samples, and ray/work-graph settings.
It excludes node IDs, graph instance labels, slot/group names, debug names, and caller-supplied persistent-cache labels.
Layout spaces, ray exports/hit groups, and work-graph shaders are canonically ordered, so equivalent ancestry traversal orders produce the same key.
The resolved result's key also includes the completed framebuffer formats and samples.
These semantic keys are separate from native object addresses and the existing cache's internal descriptor keys.

[`ResolveArdaInductorPipeline()`](api-reference.html#api-arda-resolveardainductorpipeline-eff1546a) verifies the pattern key, then calls the appropriate [`FArdaPipelineStateCache::GetOrCreate*`](../ArdaBackend/api-reference.html#api-arda-backend-fardapipelinestatecache-182d380c) method.
The in-memory cache compares complete RHI pipeline descriptors, including shader, binding-layout and input-layout object references; it does not look up PSOs by the ArdaInductor stable key.
Two separately created shaders or layouts with equivalent contents can therefore produce the same stable pattern key while occupying different in-memory PSO cache entries.
Repeated requests that share the same RHI references and matching settings reuse the cached PSO while its entry remains resident.
Native persistent caches receive a separate content-derived key during PSO creation; that metadata does not merge the in-memory entries.
The graph retains its device-bound cache and compiled typed pipeline references across frames.
Repeated `Execute()` records fresh commands using those resolved references; it does not infer or create the PSO again.
Use `Graph.GetPipelineCacheStats()` to inspect entries, misses, hits, and creation failures for this cache.
The lower-level device descriptor-cache counters measure a different layer.

## Use the resolved pipeline when recording

During node recording, call `Context.GetPipeline("default")` or another declared slot name. Class hooks receive public parameters and state separately; lower-level definition callbacks receive their prepared execution parameters.
The returned `FArdaInductorResolvedPipeline` has the corresponding typed reference, such as `mCompute` or `mGraphics`; an unknown slot returns null.
Resolve declared resources with `GetBuffer()`/`GetTexture()`, build binding sets compatible with the resolved pipeline's layouts, and bind the appropriate RHI state.
For graphics/mesh state, use `Context.GetFramebuffer()` for the compiled attachments.
Then issue the normal draw, dispatch, ray, or work-graph command through `GetCommands()`.
For compute specifically, check `SetComputeState()`'s status, call the void `Dispatch()` method, then return `FArdaRHIStatus{}`.

See [ArdaInductorGpuTests.cpp](../../Source/ArdaRenderGraph/Tests/ArdaInductorGpuTests.cpp) for a complete typed compute node, bindings, consumer-before-producer attachment, and three-frame numerical readback.
[ArdaInductorPipelineTests.cpp](../../Source/ArdaRenderGraph/Tests/ArdaInductorPipelineTests.cpp) covers intermediate ancestry, groups, layout conflicts, tessellation pairs, ray exports, work-graph identity, and invalid configurations without a GPU.
The implementation is in [ArdaInductorPipeline.cpp](../../Source/ArdaRenderGraph/Private/ArdaInductorPipeline.cpp); [ArdaInductorRuntime.cpp](../../Source/ArdaRenderGraph/Private/ArdaInductorRuntime.cpp) connects inference to compilation and execution.

## Native API references

The native model behind these settings is described by Microsoft's [D3D12 pipeline-state overview](https://learn.microsoft.com/en-us/windows/win32/direct3d12/managing-graphics-pipeline-state-in-direct3d-12) and Khronos's [VkGraphicsPipelineCreateInfo specification](https://docs.vulkan.org/refpages/latest/refpages/source/VkGraphicsPipelineCreateInfo.html).
Ray exports, hit groups, and local-root associations follow the concepts in the [DirectX Raytracing specification](https://microsoft.github.io/DirectX-Specs/d3d/Raytracing.html).
Microsoft's [work-graph mesh-node description](https://devblogs.microsoft.com/directx/d3d12-mesh-nodes-in-work-graphs/) explains the native program/state-object model; the currently exposed Arda work-graph fields remain the implementation contract above.
