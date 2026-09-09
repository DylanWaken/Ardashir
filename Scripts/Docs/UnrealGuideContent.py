"""Authored explanations for the source-pinned Unreal rendering atlas."""

GROUPS = [
    ("world", "01", "World, components & assets", "Game-thread objects describe intent; render-thread proxies and GPU allocations have different lifetimes."),
    ("scene", "02", "Render-thread scene", "Persistent scene registration, proxies, primitive/light records and scene extensions."),
    ("views", "03", "Views, visibility & submission", "Per-view state turns persistent scene data into eligible draws and GPU culling work."),
    ("gpu-scene", "04", "GPU Scene & instance data", "Shader-readable scene tables, stable identifiers, instance transforms and upload machinery."),
    ("materials", "05", "Materials, meshes & render resources", "Compiled material permutations, vertex factories and render-thread resources bridge assets to RHI objects."),
    ("nanite", "06", "Nanite representation", "Cluster/page geometry, streaming, packed views, visibility rasterization and material shading commands."),
    ("lumen", "07", "Lumen representation", "Cards, surface-cache pages, cached lighting, screen probes and persistent radiance/history state."),
    ("shadows", "08", "Shadows & ray representations", "VSM depth pages, mesh/global distance fields, BLAS/TLAS and ray tracing commands are distinct scene representations."),
    ("rdg", "09", "Render Dependency Graph", "Logical frame resources, pass dependencies, lifetime tracking and extraction into persistent storage."),
    ("rhi", "10", "RHI & native GPU objects", "Portable resource/command contracts and platform implementations. These C++ wrappers are usually CPU objects that own or reference GPU state."),
]

# name | group | role | implementation responsibility. Inheritance comes from the source inventory.
TYPE_ROWS = r"""
UWorld|world|Owns the gameplay world and its link to the renderer's scene interface. A world is not itself a GPU allocation.|Keep simulation ownership separate from the render-thread scene; marshal registration, transforms and removal across the thread boundary.
AActor|world|Groups components and gameplay behavior in a world. Renderable geometry normally reaches the renderer through primitive components rather than the actor itself.|Model transforms, component attachment and actor lifetime without making GPU passes dereference mutable actor state.
UActorComponent|world|Base for actor-owned behavior, including components with no spatial or rendering representation.|Implement component registration and destruction; only renderable subclasses need proxies.
USceneComponent|world|Adds a transform and attachment hierarchy to an actor component. Spatial presence alone does not imply a drawable primitive.|Propagate attachment/transform changes and generate render updates only for descendants that need them.
UPrimitiveComponent|world|Represents a scene primitive with bounds, visibility and rendering settings; creates the render-thread primitive proxy.|Snapshot render-relevant state, allocate a stable scene identity, enqueue additions/updates/removal and defer destruction until consumers finish.
UMeshComponent|world|Adds material assignment and mesh-oriented behavior to primitive components.|Track material overrides independently from shared mesh asset buffers; invalidate cached draw/shader state when assignments change.
UStaticMeshComponent|world|Places a static-mesh asset in the world with per-component transform, material and rendering state.|Share asset render data between placements; create an appropriate ordinary or Nanite proxy according to support and settings.
UInstancedStaticMeshComponent|world|Represents many placements of one static mesh using instance data rather than one component per placement.|Maintain instance transforms, custom data, bounds and update ranges; upload changed instances to shader-readable scene storage.
UHierarchicalInstancedStaticMeshComponent|world|Adds hierarchical organization to instanced static meshes for efficient visibility and instance management.|Keep instance indices, bounds hierarchy and GPU instance data consistent through inserts/removals.
USkinnedMeshComponent|world|Base for mesh components whose vertices are transformed by a skinning/deformation system.|Produce current/previous deformation state, conservative bounds and matching raster/ray-tracing representations.
USkeletalMeshComponent|world|Connects skeletal animation and a skeletal mesh to skinned rendering.|Update bone/deformation buffers and velocities; ray tracing geometry may require separate BLAS updates after deformation.
UStaticMesh|world|Asset containing the static mesh's source/settings and runtime render data, including Nanite data when built.|Cook mesh LODs, vertex/index streams, material sections, bounds and optional Nanite/distance-field/card representations.
USkeletalMesh|world|Asset holding skeletal mesh geometry, skeleton-related data and render LOD resources.|Build skinning streams, sections and LODs; distinguish immutable asset data from per-component animation state.
UMaterialInterface|world|Common material-facing interface used by base materials and instances to expose rendering properties and parameters.|Resolve material identity and parameter inheritance to a render-thread material proxy.
UMaterial|world|Authored material graph and shading configuration from which target-platform shader permutations are compiled.|Translate graph operations into shader code, derive shader keys and compile compatible material/vertex-factory/pass permutations.
UMaterialInstance|world|Material variant that inherits a parent material and supplies parameter/static-permutation overrides.|Share compiled permutations where possible and update uniform expressions when instance parameters change.
UTexture|world|Asset-level texture interface; its rendering resource is managed separately.|Stream/upload texture data through render resources and preserve mip residency and sampler/view contracts.
ULightComponent|world|Base for light components that transfer light properties to the rendering scene.|Snapshot intensity, shape, channels and shadow policy into a light proxy and invalidate dependent caches on change.
UDirectionalLightComponent|world|Models a directional light with parallel illumination and directional shadow settings.|Build view-dependent shadow clipmaps/cascades and evaluate its BRDF contribution without treating it as a finite local-light sphere.
UPointLightComponent|world|Models an omnidirectional finite local light.|Cull affected receivers, evaluate attenuation and provide the selected shadow representation.
USpotLightComponent|world|Extends a point light with a cone-shaped emission region.|Add cone culling and angular attenuation to the local-light and shadow setup.
URectLightComponent|world|Models a rectangular area light.|Represent emitter shape and orientation; use the applicable area-light approximation or sampled lighting path.
USkyLightComponent|world|Describes environment illumination/capture settings rather than an ordinary local light volume.|Manage sky capture/convolution and make environment radiance available to indirect and reflection systems.
UDecalComponent|world|Places a deferred decal with transform, material and projection bounds.|Classify decal blend requirements; schedule DBuffer decals before material evaluation or compatible later decals before lighting.
FSceneInterface|scene|Engine-facing interface through which world/components communicate with a rendering scene.|Define scene registration and update operations without exposing private renderer storage to gameplay code.
FScene|scene|Persistent renderer scene implementing FSceneInterface. Owns or coordinates primitive/light records, GPU Scene and feature-specific scene state.|Keep additions/removals, spatial indexing, cached draws and feature caches coherent. FScene is CPU renderer state, not the GPU Scene buffer.
FPrimitiveSceneProxy|scene|Render-thread representation of a primitive's immutable or explicitly updated drawing state. It supplies view relevance and mesh descriptions.|Avoid unsynchronized access to the live component; retain needed render data and generate static/dynamic mesh batches.
FStaticMeshSceneProxy|scene|Ordinary static-mesh primitive proxy that exposes mesh sections/LODs and their materials to renderer passes.|Select usable render LODs and produce mesh batches with correct material, vertex factory and primitive identity.
FSkeletalMeshSceneProxy|scene|Primitive proxy for skinned mesh rendering, connected to skeletal mesh render objects/resources.|Ensure mesh batches see the correct deformed vertex streams and current/previous frame data.
Nanite::FSceneProxy|scene|Nanite-specific primitive proxy that connects a component's scene identity and materials to Nanite resources/pipelines.|Register Nanite raster/shading bins and GPU Scene instances; provide non-Nanite representations when required by another feature.
Nanite::FSceneProxyBase|scene|Shared Nanite proxy base derived from FPrimitiveSceneProxy, bridging ordinary primitive contracts to Nanite-specific proxies.|Keep common primitive visibility/material/scene registration behavior available while specializing geometry and raster/shading resources.
FPrimitiveSceneInfo|scene|Renderer-owned registration record tying a primitive proxy to scene indices, cached mesh data and renderer bookkeeping.|Track stable identifiers and compact indices separately; invalidate cached commands and release records safely when a primitive leaves the scene.
FLightSceneProxy|scene|Render-thread snapshot of a light's shape, color, attenuation and shadow-related properties.|Provide light evaluation constants and bounds without reading mutable light components during rendering.
FLightSceneInfo|scene|Renderer light registration and bookkeeping record associated with a light proxy.|Manage light identity, interactions and inclusion in sorted lighting/shadow work.
FSkyLightSceneProxy|scene|Render-thread sky-light state and environment data reference.|Manage capture results and irradiance/reflection inputs with frame-safe lifetime.
FDeferredDecalProxy|scene|Render-thread decal representation with material and projection state.|Cull/sort decals and route them into the appropriate depth/material/lighting stages.
ISceneExtension|scene|Extension point for persistent scene systems beyond the monolithic scene record.|Give systems such as shadow caches explicit create/update/destroy integration instead of hiding ownership in frame passes.
FSceneViewFamily|views|Groups views rendered together with shared show flags, output and family-level settings.|Choose a rendering path and coordinate stereo/multiview output and feature policy.
FSceneView|views|Public per-view description including transforms, rect and rendering settings.|Compute consistent current/previous view matrices, clipping conventions and screen-to-world reconstruction.
FViewInfo|views|Renderer-specific extension of FSceneView carrying visibility results, uniform parameters and per-frame feature data.|Allocate frame-local view resources and connect persistent view history only when it is valid.
FSceneViewStateInterface|views|Public interface to persistent view state that can survive successive frames.|Separate view lifetime and history ownership from an individual frame's FViewInfo.
FSceneViewState|views|Renderer implementation of persistent view state; also a render resource. Holds histories used by occlusion, Lumen and temporal reconstruction.|Invalidate/reallocate history on camera cuts, view-state changes or incompatible configuration; extract RDG results for later frames.
FSceneRenderer|views|Base renderer coordinating scene/view-family rendering, common resources and feature work.|Construct the frame graph from a stable scene snapshot and chosen per-view pipeline state.
FDeferredShadingSceneRenderer|views|Desktop deferred renderer that orchestrates visibility, depth, Nanite, material evaluation, shadows, lighting and post processing.|Implement resource dependencies and conditional branches rather than assuming its C++ call order is a serial GPU timeline.
FMobileSceneRenderer|views|Renderer implementation for the mobile feature/platform path.|Treat it as a different rendering path, not a child stage of the desktop deferred pipeline shown here.
FMeshBatch|views|Pass-independent mesh description supplied by a proxy: material proxy, vertex factory, primitive settings and one or more elements.|Keep it as an input description; compile it through a mesh pass processor before command submission.
FMeshBatchElement|views|One draw element's geometry range, instance data and related draw parameters within a mesh batch.|Resolve index/vertex ranges and primitive/instance bindings for the chosen pass.
FMeshPassProcessor|views|Converts eligible mesh batches into pass-specific draw commands and selects material shaders/render state.|Filter by pass relevance and material policy, resolve shader permutations and emit compatible pipeline/binding commands.
FMeshDrawCommand|views|Prepared draw state containing shader bindings, pipeline-related state and draw arguments for submission/caching.|Cache only stable state, match dynamic-instancing compatibility and retain referenced resources through GPU completion.
FVisibleMeshDrawCommand|views|Per-view/pass wrapper selecting a prepared draw command and its visibility/sort information.|Build sorted visible command lists without rebuilding every cached mesh command.
FParallelMeshDrawCommandPass|views|Coordinates setup and parallel submission of mesh draw commands for a pass.|Join command-setup tasks, perform instance-culling setup and record work into compatible RHI command lists.
FInstanceCullingManager|views|Frame-level coordination for instance-culling resources and contexts across passes/views.|Share scene/view input tables and dispatch GPU work that compacts eligible instances and prepares indirect draws.
FInstanceCullingContext|views|Per-pass instance-culling configuration and draw-command work description.|Produce instance-ID streams and indirect argument buffers; declare compute-to-indirect-read dependencies.
FGPUScene|gpu-scene|CPU manager for GPU-resident primitive, instance, payload and light-related scene buffers and their updates.|Allocate IDs/ranges, upload dirty data, retire removed entries and expose the right buffers to culling/material/shadow shaders.
FPrimitiveSceneShaderData|gpu-scene|Packed shader-facing representation of one primitive's rendering data.|Define matching C++/shader layout for transforms, bounds and primitive properties; never upload the FPrimitiveSceneInfo object itself.
FInstanceSceneShaderData|gpu-scene|Shader-facing per-instance representation used with primitive-level data.|Encode instance transforms/identifiers according to the shader ABI; distinguish instance records from primitive records.
FInstanceSceneDataBuffers|gpu-scene|CPU-side instance-data buffers used by instance scene proxies/update paths.|Manage transforms, bounds and optional instance payload consistently before upload to GPU Scene.
FGPUSceneResourceParameters|gpu-scene|Shader parameter structure exposing GPU Scene buffers and related access data.|Bind declared SRVs/UAVs through the graph/scene uniform system, not through hidden global raw pointers.
FSceneUniformBuffer|gpu-scene|Extensible scene uniform-buffer wrapper that composes registered scene data for shaders.|Register feature members, update their values and publish a consistent scene uniform buffer for frame consumers.
FMaterialRenderProxy|materials|Render-thread material interface used by draws to resolve material resources and uniform expressions.|Keep dynamic parameter values separate from shader-map identity and retain fallback materials for unavailable permutations.
FMaterial|materials|Rendering-facing material description used to query shader maps, properties and compilation state.|Expose shading properties and retrieve compatible compiled permutations for the target platform and feature level.
FMaterialResource|materials|Material rendering resource that connects an authored material to compiled shader-map data.|Manage shader-map lifetime and provide the rendering material state consumed by mesh processors.
FMaterialShaderMap|materials|Collection of compiled shader permutations associated with a material and its rendering configuration.|Cache by material/platform/permutation identity, including vertex-factory/pass variants; avoid recompiling per instance.
FShader|materials|Base compiled shader wrapper and metadata used to access shader code/bindings.|Keep compiled code, reflection/parameter metadata and target-specific RHI shader objects consistent.
FGlobalShader|materials|Shader type for work independent of an individual material, common in compute/full-screen renderer passes.|Compile feature/permutation variants and bind explicit graph parameters for the selected shader.
FMaterialShader|materials|Shader base whose compilation/bindings depend on material state.|Combine material uniform data with pass-specific inputs and choose a valid material permutation.
FMeshMaterialShader|materials|Material shader specialization that also depends on mesh/vertex-factory data.|Match the material shader to the vertex factory and mesh pass; handle primitive and instance inputs correctly.
FVertexFactory|materials|Abstraction describing how a mesh's vertex data is fetched by shader permutations.|Define vertex declarations/streams or manual-fetch interfaces; it is not itself the geometry asset or a GPU Scene table.
FLocalVertexFactory|materials|Vertex factory for conventional local/static mesh vertex streams.|Bind position/tangent/UV/color streams and their material-shader interface.
FRenderResource|materials|Render-thread-managed resource lifecycle abstraction. It can own several RHI objects or no single GPU allocation.|Implement initialization/release ordering and retain dependencies; distinguish render resource lifetime from UObject and FRHIResource lifetimes.
FVertexBuffer|materials|Render resource wrapper for a vertex buffer RHI allocation.|Upload/create the buffer and expose vertex-fetch-compatible bindings.
FIndexBuffer|materials|Render resource wrapper for index-buffer storage.|Track element width and topology/range constraints when preparing indexed draws.
FTextureResource|materials|Render-resource representation of a texture asset with RHI texture/sampler state.|Upload/stream mips and maintain valid texture views and sampler bindings.
FStaticMeshRenderData|materials|Runtime static-mesh render data, including LOD resources and optional feature representations.|Build/cook shared render buffers and arrange feature-specific data without conflating asset geometry with per-instance transforms.
FStaticMeshLODResources|materials|Geometry buffers and sections for one conventional static-mesh LOD.|Maintain section/material mapping, index ranges and vertex-factory input buffers.
FSkeletalMeshRenderData|materials|Shared rendering data for skeletal-mesh LODs.|Provide immutable LOD streams while per-object animation supplies changing deformation inputs.
FSkeletalMeshLODRenderData|materials|One skeletal mesh render LOD's vertex/index/section data.|Support bone influences and render sections with compatible skinning vertex factories and ray geometry updates.
Nanite::FResources|nanite|Cooked/runtime Nanite asset representation containing page/cluster hierarchy and streaming metadata.|Build a hierarchical cluster representation offline, package resident/streamed pages and expose root resources at runtime.
Nanite::FStreamingManager|nanite|Render resource managing Nanite page residency, requests and streaming updates.|Process feedback, prioritize requests, upload pages, apply fixups and keep hierarchy traversal safe when detail pages are missing.
Nanite::FPackedView|nanite|GPU-oriented view record for Nanite culling/rasterization, including transforms, bounds and LOD/culling parameters.|Pack camera/shadow/capture views consistently with shader layouts and the target raster extent.
Nanite::FRasterContext|nanite|Nanite raster target/configuration context used to create visibility/depth output.|Allocate/clear compatible raster resources and select supported hardware/software raster paths.
Nanite::FRasterResults|nanite|Outputs of Nanite visibility rasterization, including resources needed by depth export, material shading and visualization.|Carry visibility/cluster/bin information into later passes; a visibility buffer is not a finished GBuffer or lit scene color.
FNaniteShadingCommands|nanite|Prepared material-shading commands for Nanite mesh passes.|Build shading bins/work from visible material assignments, then evaluate material attributes into the scene's shading representation.
FNaniteRasterPipelines|nanite|Registry of Nanite raster pipeline variants used by mesh passes.|Separate raster material needs such as masks/deformation from later material shading and cache compatible pipeline variants.
FNaniteShadingPipelines|nanite|Registry of Nanite shading pipeline/material variants.|Associate visible material work with compiled shading pipelines and dispatch state.
FLumenSceneData|lumen|Persistent Lumen scene representation: primitive groups, mesh cards, surface-cache allocation and lighting-related data.|Track scene changes and residency, budget captures/lighting updates and retain feature state across frames.
FLumenPrimitiveGroup|lumen|Grouping of primitives/instances used to manage Lumen representation and surface-cache work.|Choose capture/representation granularity, track bounds and invalidate affected cards when geometry changes.
FLumenMeshCards|lumen|Mesh-level collection/placement of cards used to parameterize surfaces for Lumen.|Transform asset card descriptions to the scene and connect them to primitive groups and card allocation.
FLumenCard|lumen|Oriented surface parameterization used to capture/material-cache a portion of geometry.|Track card transforms, extents and allocated mip/page state; cards approximate surface coverage, not a second triangle mesh for primary visibility.
FLumenSurfaceCacheAllocator|lumen|Allocates physical atlas space for Lumen surface-cache pages.|Handle page residency, reuse and eviction; prevent stale mappings and budget memory/capture work.
FScreenProbeGatherParameters|lumen|Shader parameter bundle for Lumen screen-probe placement, tracing and gather work.|Bind screen depth/normals, probe atlases, trace results and integration data explicitly through RDG.
FRadianceCacheState|lumen|Persistent world-space radiance-cache state associated with Lumen view history.|Manage probe allocation, clipmap placement, update budgets and temporal reuse. This is distinct from the surface cache of material/lighting on cards.
FVirtualShadowMapArray|shadows|Frame-level virtual shadow map resources and rendering operations for lights.|Mark requested virtual pages, allocate/cache physical depth pages, rasterize missing/invalid pages and project shadows onto receivers.
FVirtualShadowMapArrayCacheManager|shadows|Persistent scene extension managing VSM cache state across frames.|Track light/primitive changes and invalidate affected pages; retain extracted resources and reuse only valid depth pages.
FVirtualShadowMapClipmap|shadows|Directional-light clipmap organization providing virtual shadow coverage around views.|Map world-space coverage across levels and update mappings as the camera/light changes.
FProjectedShadowInfo|shadows|Renderer description/state for a projected shadow and its subject/receiver work.|Build shadow views, collect casters and route conventional or applicable virtual-shadow work.
FDistanceFieldSceneData|shadows|Renderer scene bookkeeping for mesh distance-field representations and their GPU data.|Upload object bounds/transforms and compose/update the global distance field when the selected features require it.
FRayTracingGeometry|shadows|Render-resource wrapper for ray-tracing geometry, normally backed by a bottom-level acceleration structure.|Create/build/update BLAS from geometry streams and synchronize deformation/upload before tracing consumers.
FRayTracingScene|shadows|Renderer coordinator for ray-tracing instances and top-level acceleration-structure state.|Collect visible/eligible instances, upload instance descriptors, build TLAS and retain geometry references; it is separate from FScene and GPU Scene.
FRayTracingInstance|shadows|Ray-tracing instance description connecting geometry, transforms, masks and material bindings.|Choose the geometry representation for each instance and preserve hit-group/material mapping.
FRayTracingMeshCommand|shadows|Prepared material/shader binding information for ray-tracing mesh sections.|Build consistent hit-shader or material-binding data alongside the acceleration structures.
FRDGBuilder|rdg|Builds and executes a Render Dependency Graph from passes and declared resource accesses.|Track reads/writes, cull unused work, plan lifetimes/barriers, schedule supported async work and execute through RHI command lists.
FRDGPass|rdg|Internal graph pass representation with parameters, dependencies and execution work.|Represent pass resource accesses and execution flags; graph order is constrained by dependencies and observable outputs.
FRDGResource|rdg|Base graph resource bookkeeping and identity.|Track lifetime and accesses within a graph; a graph handle is not a general persistent GPU ownership token.
FRDGViewableResource|rdg|RDG resource base for buffers/textures supporting views and access tracking.|Connect graph identity to allocated or imported RHI storage and per-view usage.
FRDGTexture|rdg|Logical texture in an RDG graph, transient or imported from persistent storage.|Describe format/extent/usage and declare subresource accesses; extract/import explicitly when results must survive a frame.
FRDGBuffer|rdg|Logical buffer in an RDG graph.|Declare structured/raw/indirect use and compute/graphics access transitions; choose transient or persistent storage intentionally.
FRDGTextureSRV|rdg|Shader-resource view of an RDG texture.|Expose read access to the intended mip/slice/format while retaining graph dependency tracking.
FRDGTextureUAV|rdg|Unordered-access view of an RDG texture.|Declare shader write/read-write accesses and required ordering before later sampling or attachment use.
FRDGBufferSRV|rdg|Read view of an RDG buffer.|Match buffer format/stride and declare shader reads in pass parameters.
FRDGBufferUAV|rdg|Unordered-access view of an RDG buffer.|Track compute writes/atomics and transitions to draw-indirect or shader-read consumers.
FRDGUniformBuffer|rdg|Graph-managed uniform-buffer object whose contents may reference other graph resources.|Preserve nested resource dependencies when binding structured pass/scene parameters.
FRDGPooledBuffer|rdg|Reference-counted backing buffer that can persist outside one graph.|Retain extracted buffers and register them with a later graph; distinguish backing lifetime from FRDGBuffer handle lifetime.
IPooledRenderTarget|rdg|Interface to pooled texture storage that can outlive frame-local graph handles.|Retain persistent history/output textures, manage descriptors and import them explicitly into RDG.
FSceneTextures|rdg|Bundle of the active scene's depth, color, GBuffer and other view-family texture resources.|Allocate compatible scene textures and refresh shader parameter bindings as depth, velocity and lighting outputs become available.
FRHIResource|rhi|Reference-counted base for portable RHI resource objects.|Implement resource lifetime and destruction after GPU use; CPU reference lifetime alone is not a substitute for submission retention.
FRHIViewableResource|rhi|RHI base for storage that can be referenced by shader/resource views.|Track storage identity and access requirements separately from view descriptors.
FRHIBuffer|rhi|Portable buffer allocation contract.|Create vertex/index/structured/raw/indirect-capable storage with valid size, usage and access transitions.
FRHITexture|rhi|Portable texture allocation contract.|Describe dimensions, format, mip/array/sample layout and support the views/transitions needed by renderer passes.
FRHIUniformBuffer|rhi|RHI uniform/constant-buffer object with a declared layout and resource bindings.|Keep CPU/shader layout and bound resource lifetimes consistent.
FRHIShaderResourceView|rhi|Portable shader read view of compatible storage.|Create typed/raw/structured or texture subresource views and bind them to shaders.
FRHIUnorderedAccessView|rhi|Portable shader unordered-access view.|Support storage writes/atomics and order them against subsequent uses.
FRHIShader|rhi|Base RHI shader object for compiled platform shader code.|Create native shader representations and match parameter/resource bindings to compiled code.
FRHIVertexShader|rhi|RHI vertex-stage shader object.|Consume vertex-factory data and produce rasterization inputs with the correct pipeline interface.
FRHIPixelShader|rhi|RHI fragment/pixel-stage shader object.|Evaluate pass/material outputs and match attachment formats and render state.
FRHIComputeShader|rhi|RHI compute-stage shader object.|Bind compute resources and launch direct/indirect thread groups with explicit dependencies.
FRHIGraphicsPipelineState|rhi|Prepared graphics pipeline state object.|Combine shaders, vertex declarations, fixed-function state and attachment compatibility; cache by all relevant state.
FRHIComputePipelineState|rhi|Prepared compute pipeline state object.|Create/cache the native compute pipeline associated with shader and binding requirements.
FRHIRayTracingGeometry|rhi|Portable bottom-level ray-tracing acceleration-structure resource contract.|Translate geometry descriptions into native BLAS storage/build commands and barriers.
FRHIRayTracingScene|rhi|Portable top-level ray-tracing acceleration-structure resource contract.|Build native TLAS from instance descriptions and expose it to supported tracing shader/inline-query paths.
FRHIRayTracingPipelineState|rhi|RHI pipeline object for ray-generation/miss/hit shader execution.|Create shader collections/pipelines and compatible binding tables when using pipeline-based ray tracing.
FRHISamplerState|rhi|Portable texture sampling state.|Specify filtering, addressing and related sampling policy separately from texture allocation/view identity.
FRHIRasterizerState|rhi|Portable rasterization state.|Set culling, fill and depth-bias behavior consistent with the target pass.
FRHIDepthStencilState|rhi|Portable depth/stencil test and write state.|Specify comparison, write masks and stencil operations; keep resource states and attachment access compatible.
FRHIBlendState|rhi|Portable color blending/write-mask state.|Choose blend equations and masks per render target, especially for lighting and translucency composition.
FRHIVertexDeclaration|rhi|Portable vertex-input element/stream layout.|Match attribute formats, offsets and stream slots to the vertex factory and shader input.
FRHIGPUFence|rhi|GPU completion marker queried/waited from appropriate CPU workflows.|Use for readback/recycling and completion tracking; resource barriers solve a different ordering problem.
FRHIViewport|rhi|RHI presentation target interface associated with a viewport/swap chain.|Manage back-buffer acquisition, resize and presentation outside the deferred lighting algorithm itself.
FRHICommandListBase|rhi|Base command-list recording and execution infrastructure.|Record resource/command work with thread-safe ownership and proper submission lifetime.
FRHIComputeCommandList|rhi|Command-list interface for compute-capable operations.|Record dispatches, transitions and compute-compatible commands; availability of a separate hardware queue is a platform decision.
FRHICommandList|rhi|Graphics-capable command list extending compute command functionality.|Record render passes/draws and synchronize them with compute/copy work.
FRHICommandListImmediate|rhi|Immediate command-list specialization coordinating renderer/RHI execution.|Flush/submit at defined boundaries; immediate API naming does not mean each call has completed on the GPU.
FDynamicRHI|rhi|Platform RHI implementation interface and resource/device entry points.|Translate portable resource and capability contracts to the selected graphics backend.
IRHICommandContext|rhi|Platform command-context interface implementing graphics/compute recording operations.|Emit native commands and maintain native command state behind the portable RHI interface.
FD3D12DynamicRHI|rhi|Direct3D 12 implementation of the dynamic RHI.|Own D3D12 devices/queues and implement portable RHI creation/submission contracts.
FVulkanDynamicRHI|rhi|Vulkan implementation of the dynamic RHI.|Map RHI operations onto Vulkan devices, queues, synchronization and resources.
FMetalDynamicRHI|rhi|Metal implementation of the dynamic RHI.|Map RHI capabilities/resources/commands to Metal objects and execution constraints.
"""

INVARIANTS = [
    ("Three scene representations", "FScene is persistent CPU renderer state; FGPUScene manages shader-readable scene tables; FRayTracingScene prepares instances/TLAS. None is an alias for either of the others. Nanite visibility, Lumen cards and VSM pages are additional purpose-specific representations."),
    ("Three lifetimes", "UObjects/components belong to gameplay/asset lifetime, proxies/render resources to rendering lifetime, and RHI allocations to retained GPU work. FRDG handles belong to their graph; persistent histories use extraction and later import. A C++ inheritance edge does not prove ownership, thread affinity or GPU residency."),
    ("Nanite is geometry visibility", "Nanite culls/rasterizes a hierarchical geometry representation and produces visibility/depth data. Material evaluation still writes a GBuffer or the configured Substrate representation, and deferred lighting still needs it. Nanite does not replace all mesh paths, materials, shadows or Lumen."),
    ("Two Lumen caches", "The Surface Cache parameterizes scene surfaces with cards and stores material/cached lighting atlases. The Radiance Cache stores world-space probe radiance for reuse. Screen probes are view-dependent gather samples. These are three distinct structures with different update and history rules."),
    ("Tracing and lighting are separate choices", "Screen traces can use only visible screen information. Remaining rays use the selected software distance-field or hardware triangle/AS representation. Surface-cache lookup versus hit lighting chooses how a hit is shaded; enabling hardware tracing does not mean every hit evaluates full material lighting."),
    ("Representation mismatch", "Nanite primary visibility can be more detailed than the geometry used by hardware ray tracing. Fallback meshes can create self-intersection/mismatch artifacts; screen traces may hide some discrepancies. Native Nanite ray tracing is a separate mode/coverage choice, not an unconditional property of enabling Nanite."),
    ("Temporal correctness", "Previous HZB, surface/radiance caches, probe/reflection histories and TSR history are separate inputs. Camera cuts, disocclusion, moved geometry/lights, resolution changes and residency changes require the appropriate rejection or invalidation. Keeping stale history is not equivalent to a valid cache hit."),
    ("Schedule versus dependency", "The timeline is a source-anchored dependency explanation, not a GPU capture or duration chart. CPU setup tasks and RDG passes can overlap. Async Lumen work must wait for its GBuffer/scene/AS inputs and must finish before consumers composite it; async compute does not guarantee a speedup."),
    ("Reimplementation scope", "The operation lists are an engineering decomposition inferred from source responsibilities. They are not a claim that Ardashir already implements Unreal/Nanite/Lumen, nor a complete binary-compatible port. Start with conventional deferred rendering and correctness tests, then add virtualized geometry, lighting caches and scheduling optimizations."),
]

SOURCES = {
    "deferred": "Renderer/Private/DeferredShadingRenderer.cpp",
    "scene": "Renderer/Private/SceneRendering.cpp",
    "gpu": "Renderer/Private/GPUScene.cpp",
    "visibility": "Renderer/Private/SceneVisibility.cpp",
    "depth": "Renderer/Private/DepthRendering.cpp",
    "nanite": "Renderer/Private/Nanite/NaniteCullRaster.cpp",
    "naniteMaterials": "Renderer/Private/Nanite/NaniteShading.cpp",
    "base": "Renderer/Private/BasePassRendering.cpp",
    "lumenScene": "Renderer/Private/Lumen/LumenSceneRendering.cpp",
    "lumenLighting": "Renderer/Private/Lumen/LumenSceneLighting.cpp",
    "lumenGather": "Renderer/Private/Lumen/LumenScreenProbeGather.cpp",
    "lumenTrace": "Renderer/Private/Lumen/LumenScreenProbeTracing.cpp",
    "lumenReflections": "Renderer/Private/Lumen/LumenReflections.cpp",
    "indirect": "Renderer/Private/IndirectLightRendering.cpp",
    "shadows": "Renderer/Private/ShadowDepthRendering.cpp",
    "vsm": "Renderer/Private/VirtualShadowMaps/VirtualShadowMapArray.cpp",
    "lights": "Renderer/Private/LightRendering.cpp",
    "ray": "Renderer/Private/RayTracing/RayTracingScene.cpp",
    "post": "Renderer/Private/PostProcess/PostProcessing.cpp",
    "rdg": "RenderCore/Private/RenderGraphBuilder.cpp",
    "builder": "Renderer/Private/SceneRenderBuilder.cpp",
}


def stage(id, title, lane, column, summary, inputs, outputs, steps, operations, validation, sources, *, requires=None, depends=None, types=None, caveat="", async_capable=False):
    return dict(id=id, title=title, lane=lane, column=column, summary=summary, inputs=inputs, outputs=outputs,
        steps=steps, operations=operations, validation=validation, sourceQueries=sources, requires=requires or [],
        depends=depends or [], types=types or [], caveat=caveat, asyncCapable=async_capable)


STAGES = [
    stage("scene-update", "Scene update & GPU Scene", "setup", 0,
        "Turn queued world changes into stable render-thread records and shader-readable primitive/instance tables.",
        ["Component/proxy additions, removals and changes", "Persistent FScene and previous-frame state"],
        ["Updated primitive/light records and GPU Scene tables", "Resource lifetime and upload dependencies"],
        ["Snapshot game-thread state into proxies; render passes consume the render-thread representation.", "Apply scene changes, update bounds and light interactions, and invalidate affected cached mesh commands.", "Allocate primitive/instance IDs and upload changed shader data. Keep previous transforms for motion vectors.", "Retain old resources until queued CPU and GPU consumers no longer reference them."],
        ["Stable handles with generation counters and deferred reclamation", "Add/update/remove queues plus dirty-range tracking", "Structured-buffer allocation, scatter uploads and upload barriers", "Current/previous transform storage and scene-uniform binding"],
        ["Repeatedly add/remove/move instances while frames are in flight; detect stale IDs.", "Compare shader-read transforms with CPU reference data; check velocity after teleport and ordinary motion."],
        [("scene", "IVisibilityTaskData* FSceneRenderer::OnRenderBegin"), ("gpu", "void FGPUScene::Update(")],
        types=["FScene", "FPrimitiveSceneProxy", "FPrimitiveSceneInfo", "FGPUScene"]),
    stage("views", "Views, relevance & draw setup", "setup", 1,
        "Build per-view visibility and submission work while retaining persistent temporal state separately.",
        ["Scene records, bounds and view family", "Camera matrices, show flags and feature/platform settings"],
        ["FViewInfo visibility/relevance", "Mesh draw commands and instance-culling work", "Allocated scene textures"],
        ["Initialize current/previous view transforms, jitter, frustum and view-state references.", "Cull primitives and classify material/pass relevance; select conventional mesh LODs where applicable.", "Reuse valid cached FMeshDrawCommands or build dynamic commands through mesh-pass processors.", "Prepare instance culling and pass resource bindings. Nanite has additional GPU hierarchy/cluster visibility later."],
        ["View/frustum construction and bounds tests", "Visibility bitsets, relevance classification and LOD selection", "Material/vertex-factory/pass shader and PSO lookup", "Draw sorting/batching, indirect arguments and instance culling", "Depth, GBuffer, scene-color and velocity texture allocation"],
        ["Test two views with different visibility and independent histories.", "Compare cached and rebuilt draws after material changes; verify hidden primitives do not submit work."],
        [("deferred", "BeginInitViews("), ("visibility", "ComputeViewVisibility")],
        depends=["scene-update"], types=["FSceneViewFamily", "FViewInfo", "FSceneViewState", "FMeshBatch", "FMeshDrawCommand", "FInstanceCullingManager"]),
    stage("streaming", "Nanite residency & packed views", "geometry", 2,
        "Make virtualized geometry pages and per-view culling parameters available before Nanite work.",
        ["Cooked Nanite hierarchy/cluster pages", "Prior streaming requests and camera/instance data"],
        ["Resident page mappings and hierarchy buffers", "Packed views and raster/shading pipeline state"],
        ["Cooked resources describe clustered geometry and a traversable hierarchy; runtime streaming does not build the asset from scratch each frame.", "Process feedback, upload requested pages and maintain valid resident geometry/coarser fallbacks.", "Pack view matrices, rectangles and LOD/culling parameters, including previous occlusion data when valid.", "Resolve programmable raster/material state needed by the visible scene."],
        ["Offline mesh clustering, hierarchy construction, encoding and error metrics", "Page pool, virtual-to-physical mappings, request queues and bounded streaming uploads", "GPU hierarchy/page tables and packed-view buffers", "Residency-safe refinement and streaming feedback readback"],
        ["Force a tiny page budget and rapid camera travel; verify missing detail degrades without holes.", "Invalidate old occlusion data after camera cuts and verify newly visible geometry appears."],
        [("deferred", "GStreamingManager.BeginAsyncUpdate"), ("nanite", "FRenderer::DrawGeometry")],
        requires=["nanite"], depends=["views"], types=["Nanite::FResources", "Nanite::FStreamingManager", "Nanite::FPackedView"]),
    stage("depth", "Conventional depth prepass", "geometry", 3,
        "Rasterize eligible non-Nanite geometry into scene depth; exact coverage depends on the early-Z mode.",
        ["Visible depth-pass mesh commands", "Scene depth target, material masking and current/previous transforms"],
        ["Early scene depth/stencil", "Optional early velocity"],
        ["Clear/configure depth and stencil using the renderer's depth convention.", "Draw the configured opaque/masked subset with depth-compatible material permutations.", "Generate velocity here only when the selected mode requires it; other configurations write it in the base pass or a later pass.", "Make resolved depth available to consumers when required."],
        ["Depth-only PSOs and alpha-test/WPO-compatible vertex/material evaluation", "Indirect indexed draws with instance data", "Depth/stencil transitions, clears and resolves", "Consistent current/previous clip-space motion-vector encoding"],
        ["Check masked cutouts and displaced geometry agree between depth and base passes.", "Test early-Z/velocity modes; avoid missing or double-written motion vectors."],
        [("deferred", "RenderPrePass(GraphBuilder, InViews", 2464), ("deferred", "DDM_AllOpaqueNoVelocity", 2100)],
        depends=["views"], types=["FMeshPassProcessor", "FSceneTextures"], caveat="The prepass is configurable. DBuffer, Nanite, shadows and other features can impose depth requirements; this box is not a promise that every primitive writes depth here."),
    stage("nanite-visibility", "Nanite cull → raster → repair", "geometry", 4,
        "Traverse visible geometry, rasterize cluster/triangle visibility, then recover geometry incorrectly rejected by previous-frame occlusion.",
        ["Resident Nanite pages, GPU Scene and packed views", "Previous HZB when valid; current conventional depth"],
        ["Nanite visibility buffer and depth", "Visible cluster lists, raster results and streaming requests"],
        ["Cull instances and hierarchy nodes, choose appropriate cluster detail, and queue visible clusters.", "The main occlusion pass uses the previous HZB when two-pass occlusion is enabled. Rejected candidates remain eligible for a post pass.", "Raster bins dispatch hardware rasterization and software compute rasterization according to work/material/platform policy; small triangles benefit from the software path.", "Build a current HZB after main visibility, re-test previously occluded candidates and rasterize recovered geometry in the post pass.", "Export Nanite depth/stencil and metadata for shared scene consumers. The visibility buffer identifies winning geometry; it is not the final shaded GBuffer."],
        ["GPU work queues, hierarchy traversal, bounds/frustum/occlusion tests and LOD error selection", "Work compaction, counters, indirect dispatch/draw arguments and overflow handling", "Hardware and compute raster paths with a consistent depth/visibility winner rule", "HZB reduction, main/post candidate lists and recovery rasterization", "Depth export, visibility decoding and material/raster-bin metadata"],
        ["Move a large occluder away and rotate the camera: newly exposed clusters must recover in the same frame.", "Compare hardware/software raster overlap and edge depth; test masked/WPO bins and queue overflow."],
        [("nanite", "CULLING_PASS_OCCLUSION_MAIN", 7008), ("nanite", "CULLING_PASS_OCCLUSION_POST", 7071), ("deferred", "void FDeferredShadingSceneRenderer::RenderNanite")],
        requires=["nanite"], depends=["streaming", "depth"], types=["Nanite::FRasterContext", "Nanite::FRasterResults"],
        caveat="Two-pass occlusion is conditional and disabled when a usable previous HZB is absent. Nanite geometry support and programmable raster features vary by material, platform and configuration."),
    stage("lumen-scene", "Lumen cards & Surface Cache", "caches", 4,
        "Maintain a persistent surface parameterization and capture material properties for the surfaces selected by the update budget.",
        ["Scene primitive changes, Lumen card descriptions and camera priorities", "Surface-cache atlas allocations and capture geometry"],
        ["Updated cards/page mappings", "Albedo, normal, emissive and depth material atlases"],
        ["Update Lumen primitive groups and mesh-card representations as geometry enters, moves or leaves the maintained scene.", "Prioritize visible/important surface pages and allocate or recycle atlas space within budgets.", "Capture surface material attributes into card pages; Nanite accelerates supported captures but is not the entire Lumen representation.", "Update borders/mappings and invalidate stale cached lighting for changed surfaces. Coverage and freshness are independent correctness concerns."],
        ["Offline card generation or an equivalent surface parameterization", "Scene membership, spatial prioritization and per-frame update budgeting", "Virtual page allocation and atlas management", "Material/depth capture, border dilation and mapping-table updates", "Invalidation for geometry/material/residency changes"],
        ["Visualize uncached surfaces and card coverage in concave/interior geometry.", "Move geometry and change emissive/material values; check updates converge without stale illumination."],
        [("deferred", "UpdateLumenScene("), ("lumenScene", "DilateCardPageOneTexel")],
        requires=["lumen"], depends=["scene-update", "views"], types=["FLumenSceneData", "FLumenMeshCards", "FLumenCard", "FLumenSurfaceCacheAllocator"],
        caveat="The timeline groups work by dependency; Lumen scene setup begins earlier in Render and can overlap geometry preparation. Not every surface is recaptured every frame."),
    stage("trace-scene", "Distance fields / ray tracing AS", "caches", 3,
        "Prepare the non-screen geometry representation used by Lumen tracing and other ray-based effects.",
        ["Renderable geometry, transforms, deformation and feature settings", "Cooked distance fields or ray-tracing geometry buffers"],
        ["Software: mesh/global distance-field resources", "Hardware: BLAS/TLAS and instance/material data"],
        ["Software tracing uses mesh distance fields where enabled, then broader global-distance-field coverage for eligible remaining work.", "Hardware tracing builds/refits bottom-level geometry AS and a top-level instance AS; shader/material data must agree with the geometry representation.", "Update dynamic geometry and instance transforms, and establish build-to-trace synchronization.", "Nanite primary visibility and the hardware RT representation can differ. Fallback geometry or separately enabled native Nanite ray tracing affects correspondence."],
        ["Software path: mesh SDF generation, spatial object lists, GDF clipmaps/composition and updates", "Hardware path: geometry descriptors, BLAS build/refit, TLAS instances/build and scratch allocation", "Ray instance/material lookup tables and geometry lifetime tracking", "Build/upload barriers and representation-specific invalidation"],
        ["Compare raster and traced silhouettes, especially Nanite fallback meshes and deformed objects.", "Check thin surfaces, moving instances, clipmap transitions and off-screen occluders."],
        [("deferred", "PrepareDistanceFieldScene("), ("deferred", "SetupRayTracingRenderingData(", 3034), ("lumenTrace", "RenderHardwareRayTracingScreenProbe")],
        depends=["scene-update", "views"], types=["FDistanceFieldSceneData", "FRayTracingGeometry", "FRayTracingScene", "FRHIRayTracingScene"],
        caveat="This node combines alternative representations. Hardware tracing is not required for Lumen; screen tracing does not eliminate the need for a valid fallback scene. AS setup may run later when its first consumer allows it."),
    stage("lumen-lighting", "Light the Lumen Surface Cache", "caches", 5,
        "Update direct lighting and radiosity on cached surface pages before their radiance is sampled by later gathers.",
        ["Lumen cards and material atlases", "Light data, tracing resources and existing cached lighting"],
        ["Updated direct/indirect cached surface lighting", "Lighting update state and atlas data"],
        ["Choose pages for lighting updates using separate direct-light and radiosity budgets.", "Evaluate direct illumination/visibility for selected Lumen scene surfaces.", "Update radiosity using available scene/cache information and previous state, then make cached radiance available to rays.", "In this 5.8.1 deferred entry point, RenderLumenSceneLighting is called before RenderBasePass. This is separate from the later screen-space final gather."],
        ["Light lists, lighting-page selection and indirect work arguments", "Surface-space direct-light evaluation and visibility traces", "Radiosity transport, cache sampling and temporal update state", "Atlas read/write dependencies, invalidation and convergence diagnostics"],
        ["Toggle a light in an enclosed room; track direct and bounced-light convergence separately.", "Check update budgets and invalidation avoid permanent stale lighting."],
        [("deferred", "RenderLumenSceneLighting("), ("lumenLighting", "RenderDirectLightingForLumenScene"), ("lumenLighting", "RenderRadiosityForLumenScene")],
        requires=["lumen"], depends=["lumen-scene", "trace-scene", "atmosphere-luts"], types=["FLumenSceneData"], async_capable=True),
    stage("gbuffer", "Base pass & Nanite materials", "geometry", 6,
        "Evaluate visible surface materials into deferred shading inputs shared by conventional meshes and Nanite.",
        ["Depth/visibility, mesh commands and material shaders", "GPU Scene, textures, DBuffer decals where enabled"],
        ["GBuffer or configured Substrate data", "Initial scene color/emissive and optional velocity", "Resolved depth and material classification"],
        ["Apply pre-base-pass DBuffer decal inputs when configured; these modify material evaluation rather than being a separate light source.", "Conventional mesh base-pass draws evaluate material and vertex-factory permutations against scene depth.", "Nanite decodes visibility, bins pixels/tiles by shading work and evaluates the corresponding materials. Nanite's geometry visibility does not bypass material shading.", "Write normals, material parameters and the selected shading representation; refresh scene-texture bindings for later consumers."],
        ["Deferred surface schema and material compiler/permutation cache", "Depth-tested conventional material draws", "Nanite visibility decode, shading-bin count/reserve/scatter and indirect material dispatch", "GBuffer/Substrate writes, emissive accumulation and velocity routing", "DBuffer inputs, stencil/material classification and resource transitions"],
        ["Render the same material with conventional and Nanite geometry and compare surface outputs.", "Verify normal spaces, roughness conventions, masked edges, decals and motion vectors before debugging lighting."],
        [("deferred", "RenderBasePass(*this, GraphBuilder, Views, SceneTextures", 3059), ("naniteMaterials", "ShadingBinBuildCS"), ("base", "void FDeferredShadingSceneRenderer::RenderBasePass(")],
        depends=["depth", "nanite-visibility"], types=["FMaterial", "FVertexFactory", "FNaniteShadingCommands", "FSceneTextures"],
        caveat="Substrate changes material storage/evaluation details. The stage shows the shared contract, not one universal fixed GBuffer layout. The Nanite dependency is conditional; conventional geometry remains when Nanite is off."),
    stage("gi", "Lumen screen-probe final gather", "lighting", 7,
        "Estimate indirect diffuse and rough-specular lighting for visible surfaces using screen probes, ray tracing and reusable radiance.",
        ["Valid scene depth/GBuffer and view data", "Lumen scene lighting, Radiance Cache, trace scene and prior histories"],
        ["Filtered/integrated indirect lighting textures", "Probe/radiance-cache histories and confidence information"],
        ["Place screen probes on a regular grid and add adaptive samples where depth/normal variation demands more coverage.", "Mark/update world-space Radiance Cache probes and generate importance-sampled probe rays.", "Trace screen-space rays first when enabled; compact unresolved work. Off-screen or invalid screen hits require fallback tracing.", "Select hardware triangle tracing or software mesh-SDF/heightfield tracing followed by global-SDF coverage as configured. Hardware mode still has a cache/sky completion phase; it does not automatically run software GDF ray marching.", "Resolve hit radiance from the selected lighting path/cache, filter probe results, interpolate/integrate for pixels and validate/reproject temporal history."],
        ["Screen/adaptive probe placement, tile classification and sampling sequences", "Radiance-cache probe allocation, update scheduling and interpolation", "Screen-depth traversal, hit validation and unresolved-ray compaction", "SDF sphere tracing or hardware ray dispatch with synchronized AS", "Radiance lookup, importance sampling, spatial/temporal filtering and per-pixel integration"],
        ["Reveal an off-screen bright source and verify fallback tracing still contributes.", "Test camera cuts, disocclusion, thin walls and roughness variation; visualize probe coverage and history rejection."],
        [("lumenGather", "GenerateImportanceSamplingRays"), ("lumenGather", "TraceScreenProbes"), ("lumenGather", "InterpolateAndIntegrate", 2711), ("lumenTrace", "TraceGlobalSDFAndApplyRadianceCache")],
        requires=["lumen"], depends=["gbuffer", "lumen-lighting", "trace-scene"], types=["FScreenProbeGatherParameters", "FRadianceCacheState"], async_capable=True,
        caveat="Screen Probe Gather is the main illustrated final-gather path; experimental/alternative modes and quality settings can change the pass set. Async dispatch happens only after required inputs are valid."),
    stage("reflections", "Lumen reflection trace & resolve", "lighting", 8,
        "Trace view-dependent specular rays and reconstruct stable reflections for eligible materials.",
        ["Depth, normals, roughness and view direction", "Screen history, trace scene, Surface Cache and reflection history"],
        ["Resolved/denoised reflection radiance", "Updated reflection history"],
        ["Classify eligible pixels/tiles by material and roughness; allocate compact trace work.", "Use screen traces where allowed and trace unresolved rays in the configured software or hardware representation.", "Surface-cache mode reads cached hit lighting. Hit Lighting evaluates lighting at ray hits where that mode is supported/enabled; hardware tracing alone does not imply Hit Lighting.", "Resolve samples to pixels, reject invalid history, temporally accumulate and spatially filter before specular composition."],
        ["Reflection tile classification, ray generation and indirect dispatch arguments", "Screen traces, hit validation, fallback tracing and ray/material lookup", "Surface-cache radiance sampling or hit-lighting shaders and visibility", "Sample resolve, temporal reprojection/rejection, variance and spatial filtering"],
        ["Use a mirror reflecting an off-screen object and compare tracing/lighting modes.", "Change camera, roughness, dynamic lighting and resolution; reject stale reflections without persistent trails."],
        [("lumenReflections", "FDeferredShadingSceneRenderer::RenderLumenReflections("), ("lumenReflections", "FLumenReflectionResolveCS")],
        requires=["lumen"], depends=["gbuffer", "trace-scene", "lumen-lighting"], types=["FSceneViewState", "FRayTracingScene"], async_capable=True,
        caveat="Diffuse GI and reflection methods are independently selectable in Unreal. The explorer's Lumen switch represents both enabled together; it does not model every mixed-method combination."),
    stage("shadows", "VSM / conventional shadow depth", "geometry", 8,
        "Produce light-space visibility, with virtual shadow maps allocating depth only for requested pages.",
        ["Visible receiver depth, lights and caster geometry", "VSM page tables, cache state and invalidation"],
        ["Resident shadow-depth pages or conventional shadow maps", "Shadow sampling/page metadata"],
        ["For VSM, mark receiver-requested pages from scene depth and relevant water/front-layer translucency inputs.", "Allocate/reuse physical pages, invalidate pages affected by moving geometry/lights and prepare clipmap/local-light views.", "Cull/rasterize Nanite and conventional shadow casters into requested dirty pages.", "Make depth/page tables available for shadow projection and light evaluation. Conventional shadow maps use a different allocation/view scheme."],
        ["Light-space projection and directional clipmap/local-light views", "Virtual page marking, physical allocation, caching and invalidation", "Shadow caster culling, Nanite shadow views and conventional depth draws", "Depth atlas writes, page-table lookup, filtering and visibility projection"],
        ["Move a caster and verify old cached shadows disappear while new pages update.", "Test clipmap boundaries, physical-page exhaustion, water and translucent receivers."],
        [("deferred", "BeginMarkVirtualShadowMapPages"), ("deferred", "RenderShadowDepthMaps(", 3291), ("vsm", "void FVirtualShadowMapArray::BuildPageAllocations(")],
        depends=["gbuffer", "receiver-depth", "nanite-visibility"], types=["FVirtualShadowMapArray", "FVirtualShadowMapArrayCacheManager", "FVirtualShadowMapClipmap", "FProjectedShadowInfo"],
        caveat="The earlier conventional-shadow scheduling option is explicitly unsupported with VSM in this path. Do not move VSM page marking ahead of its receiver-depth dependencies. VSM and Nanite are complementary, separately controlled systems."),
    stage("decals-ao", "Late decals, AO & velocity", "geometry", 9,
        "Finish the deferred surface/depth inputs needed by lighting, accounting for feature-dependent scheduling.",
        ["Base-pass GBuffer/depth/stencil", "Decal lists, velocity policy and AO settings"],
        ["Lighting-ready scene textures", "AO and motion vectors for relevant consumers"],
        ["Process deferred decals and configured ambient-occlusion work after the base pass; preserve lighting-channel data before stencil reuse where needed.", "Finish depth resolves and opaque velocity if the selected mode did not produce it earlier.", "Refresh scene texture uniform parameters after writes.", "Stochastic lighting can move before-lighting decals earlier so classification and async Lumen read valid material data. AO/lightmap/DFAO interactions depend on the selected lighting path."],
        ["Projected decal volume draws and blending rules", "Screen-space AO generation/filtering when enabled", "Depth resolves, lighting-channel extraction and stencil ownership", "Late velocity draws and read-after-write dependency declaration"],
        ["Check decals alter the values consumed by both direct and indirect lighting.", "Test moving opaque geometry and verify AO does not incorrectly double-darken indirect lighting."],
        [("deferred", "CompositionLighting.ProcessAfterBasePass", 3198), ("deferred", "CompositionLighting.ProcessAfterBasePass", 3391), ("deferred", "DispatchAsyncLumenIndirectLightingWork")],
        depends=["gbuffer"], types=["FDeferredDecalProxy", "FSceneTextures"], caveat="This is a grouping of conditional finishing work, not a barrier that every Lumen dispatch must follow. In particular, 5.8.1 can dispatch async Lumen before the regular late composition call after preparing the inputs it needs."),
    stage("direct", "Deferred direct lighting", "lighting", 10,
        "Evaluate lights against visible material data and apply their shadow visibility into HDR scene color.",
        ["Lighting-ready GBuffer/depth", "Light data, shadows and scene color"],
        ["Direct-lit HDR scene color", "Auxiliary translucency-lighting data where enabled"],
        ["Build/use light lists and select suitable deferred light evaluation paths.", "Read material/shading-model data, reconstruct surface position and evaluate the light BRDF.", "Apply shadow visibility, attenuation and light functions and accumulate radiance.", "Optional MegaLights/stochastic lighting changes light sampling and shadow requests; it is a separate feature, not a synonym for Lumen."],
        ["Light upload, classification and light-volume/tile/cluster work", "Position reconstruction, BRDF evaluation and light attenuation", "Shadow-map/VSM sampling or configured ray-traced shadow evaluation", "HDR accumulation, light functions and optional translucency-volume injection"],
        ["Compare a single unshadowed light with an analytic BRDF reference.", "Test shadowed light overlap, energy scaling, lighting channels and exposure independence."],
        [("deferred", "RenderLights("), ("lights", "FDeferredShadingSceneRenderer::RenderLights"), ("deferred", "RenderMegaLights(")],
        depends=["shadows", "decals-ao"], types=["FLightSceneProxy", "FLightSceneInfo"]),
    stage("composite", "Indirect & specular composition", "lighting", 11,
        "Join indirect work with direct lighting, then complete deferred reflections/sky and material-specific composition.",
        ["Direct-lit HDR scene color", "Completed diffuse/rough-specular GI and reflection textures", "GBuffer, sky/reflection inputs and material classification"],
        ["Combined opaque HDR lighting", "Material-specific post-lighting results"],
        ["Non-async indirect work may be launched/composited through the earlier RenderDiffuseIndirectAndAmbientOcclusion call.", "For regular async Lumen, the renderer performs a dedicated composite after RenderLights so direct lighting can overlap the gather.", "Join async work through RDG resource dependencies before reading its outputs; this is a real producer/consumer requirement.", "Compose deferred reflections and sky lighting, then run enabled subsurface/Substrate-specific opaque work without counting the same contribution twice."],
        ["Async-to-graphics synchronization and declared resource dependencies", "Diffuse/rough-specular integration and AO/material weighting", "Specular reflection/sky composition with method-dependent selection", "Subsurface profiles/filtering and enabled material-specific composition"],
        ["Compare async on/off images under identical settings and inspect queue waits.", "Isolate direct, diffuse indirect and specular terms to find double counting or missing energy."],
        [("deferred", "/* bCompositeRegularLumenOnly = */ true"), ("deferred", "RenderDeferredReflectionsAndSkyLighting("), ("indirect", "DispatchAsyncLumenIndirectLightingWork")],
        depends=["direct", "gi", "reflections"], types=["FRDGBuilder", "FSceneTextures"],
        caveat="Horizontal position describes an explanatory scheduling window, not milliseconds. Async capability depends on platform, mode and resource hazards; it can be slower when the GPU is already saturated."),
    stage("atmosphere", "Sky, fog, clouds & water", "output", 12,
        "Compose participating media and water with opaque lighting using feature-specific depth and ordering rules.",
        ["Opaque HDR scene color/depth and lights", "Atmosphere LUTs, fog/cloud/water resources and histories"],
        ["Atmospheric/water scene color", "Depth/transmittance inputs for later translucency"],
        ["Generate sky-atmosphere LUTs earlier according to the selected scheduling mode; they can be required before Lumen scene lighting.", "Evaluate sky atmosphere, height/volumetric fog and cloud rendering/composition when enabled.", "Single-layer water uses dedicated depth/refraction/lighting work; its depth prepass can contribute to earlier VSM requests.", "Underwater translucency and cloud composition have special ordering paths; the ordinary opaque-to-translucent sequence is not universal."],
        ["Atmosphere transmittance/multiple-scattering LUTs and sky evaluation", "Froxel volume allocation, light injection, scattering integration and temporal filtering", "Cloud ray marching, low-resolution reconstruction and composition", "Water depth, refraction, absorption/scattering and reflection composition"],
        ["Test camera transitions above/below water and fogged opaque silhouettes.", "Check transmittance and compositing order with clouds and translucent objects."],
        [("deferred", "RenderSkyAtmosphere(", 3784), ("deferred", "RenderFog(", 3822), ("deferred", "RenderSingleLayerWater(", 3880)],
        depends=["composite"], types=["FSceneTextures"], caveat="This box summarizes optional systems. LUT generation and some async cloud work occur earlier than final composition; rendering without these features skips the corresponding work."),
    stage("translucency", "Translucency & scene-color effects", "output", 13,
        "Render transparent surfaces with separate shading/compositing rules and pass placement.",
        ["Opaque/media scene color, scene depth and lights", "Translucent mesh batches/materials and optional front-layer resources"],
        ["Composited color or separate translucency targets", "Translucent velocity/depth where supported"],
        ["Classify translucent draws into configured pass groups and sort where the blend model requires it.", "Shade using the selected translucency lighting method; transparent materials do not simply write the opaque GBuffer.", "Use front-layer reflection/depth paths where supported, and composite separate translucency at the appropriate post-processing point.", "Respect depth, refraction, fog and velocity policies; material coverage differs from opaque Lumen/Nanite paths."],
        ["Translucent pass classification, draw sorting and blending", "Translucency lighting-volume or forward surface-lighting evaluation", "Scene-color copy/refraction and separate target allocation", "Depth-aware composition and supported translucent motion vectors"],
        ["Test overlapping glass, fog, refractive surfaces and fast motion.", "Check before/after-DOF placement and front-layer reflection limitations."],
        [("deferred", "RenderTranslucency(", 4011), ("deferred", "RenderFrontLayerTranslucency(", 3496)],
        depends=["atmosphere"], types=["FMeshBatch", "FMaterial"], caveat="Main opaque Nanite/Lumen behavior should not be generalized to every translucent material. Some translucency is rendered earlier (for example underwater/front-layer setup) or composited later."),
    stage("post", "Temporal reconstruction & post", "output", 14,
        "Convert HDR scene radiance into a stable, display-ready image while managing exposure and temporal histories.",
        ["HDR scene color, depth, velocity and translucency resources", "Previous temporal/exposure history, jitter and output size"],
        ["Upscaled/anti-aliased, tone-mapped output", "New view histories and exposure state"],
        ["Run enabled depth-of-field and pre-upscale material/translucency work.", "Select TSR, TAA or a configured third-party temporal upscaler, or a non-temporal AA path. Reproject with motion/depth and reject invalid history.", "Apply enabled motion blur and construct exposure/bloom inputs; precise pass choices vary with settings and extensions.", "Tone map/color grade for the output encoding, then apply configured after-tonemap effects, spatial AA or output scaling.", "Extract valid history resources and mark resets for camera cuts, resolution/method changes and unsupported reuse."],
        ["Temporal jitter, reprojection, disocclusion detection, history clamping and reconstruction", "Dynamic-resolution input/output rect handling and history allocation", "DOF/motion-blur filters and translucency integration", "Exposure measurement/adaptation, bloom pyramid and tone/color transform", "Output encoding plus history extraction/reset policy"],
        ["Test camera cuts, disocclusion, dynamic resolution and thin moving geometry.", "Verify linear HDR lighting is not accidentally tone-mapped twice; test SDR/HDR output conventions."],
        [("deferred", "AddPostProcessingPasses("), ("post", "UpscalerPassInputs"), ("post", "AddTonemapPass(")],
        depends=["translucency"], types=["FSceneViewState", "FRDGTexture"], caveat="TSR reconstructs the image from current/previous inputs; it does not generate Nanite geometry or Lumen GI. The post chain is conditional and extensible, not one fixed list for every view."),
    stage("execute", "RDG execution, history & output", "output", 15,
        "Compile and execute the declared frame graph, retain requested history and hand completed output to the viewport path.",
        ["Pass/resource declarations from the whole frame", "External targets, extracted histories and queue capabilities"],
        ["Submitted GPU commands and render target output", "Retained histories, feedback/readbacks and resource retirement"],
        ["Render functions mostly declare RDG passes. Track reads/writes and dependencies throughout all earlier stages, not only at this final box.", "Compile the graph: cull unused work, determine lifetimes, allocate transient resources and produce transitions/queue synchronization.", "SceneRenderBuilder executes the graph. RHI command recording/submission carries actual GPU work to the backend.", "Extract histories and feedback for future frames; imported/extracted resources have lifetimes beyond an individual graph.", "Viewport/swapchain code handles presentation outside FDeferredShadingSceneRenderer::Render. Completion/fences govern reuse and destruction."],
        ["Typed resources/views, pass parameters, access tracking and dependency DAG", "Pass culling, lifetime analysis, transient allocation/aliasing and barrier generation", "Graphics/compute queue submission, cross-queue waits and GPU fences", "External-resource import/export, history extraction and readback staging", "Swapchain output transitions, presentation and frames-in-flight retirement"],
        ["Poison/reuse transient memory to expose undeclared dependencies.", "Compare serialized and async schedules; check graph resources are not retained as dangling FRDG pointers.", "Resize/recreate the viewport while frames are in flight and validate fence-controlled resource retirement."],
        [("builder", "GraphBuilder.Execute();"), ("rdg", "FRDGBuilder::Execute")],
        depends=["post"], types=["FRDGBuilder", "FRDGPass", "FRHICommandListImmediate", "FRHIGPUFence"], caveat="The last column indicates submission/output responsibility, not that earlier boxes were already executing on the GPU as the CPU declared them. RDG logical objects and native GPU allocations are different lifetime layers."),
    stage("atmosphere-luts", "Early atmosphere lighting inputs", "caches", 2,
        "Prepare enabled atmosphere lookup resources before lighting systems that consume the sky environment.",
        ["Atmosphere parameters, lights and view data", "Cloud-shadow data when used by the selected LUT schedule"],
        ["Atmosphere lookup resources available to Lumen scene lighting and later sky composition"],
        ["Select the before-prepass or before-base-pass LUT scheduling mode.", "Declare transmittance/scattering/sky lookup work and any cloud-shadow prerequisite selected by that path.", "Publish/commit the pending atmosphere resources before Lumen scene consumers need them.", "Later sky/fog/cloud composition consumes related resources after opaque lighting; this early node does not composite the final sky image."],
        ["Atmosphere parameter upload and lookup texture allocation", "Transmittance/scattering lookup generation and selected cloud-shadow inputs", "Pending-resource publication and graph read/write dependencies"],
        ["Switch the supported LUT scheduling mode and compare lighting results.", "Change the sun/atmosphere parameters and verify Lumen scene lighting does not use stale lookup state."],
        [("deferred", "RenderSkyAtmosphereLookUpTables(", 2438), ("deferred", "RenderSkyAtmosphereLookUpTables(", 2849)],
        depends=["views"], types=["FSceneTextures", "FRDGTexture"], caveat="Optional atmosphere work; absent atmosphere features use the appropriate defaults. This column is an early scheduling window, not a claim both alternative LUT calls execute every frame."),
    stage("receiver-depth", "Water & front-layer receiver depth", "geometry", 7,
        "Provide the extra receiver information needed before virtual shadow-map page requests.",
        ["Opaque scene depth, water/front-layer materials and view data"],
        ["Single-layer-water depth when enabled", "Front-layer translucency receiver data for VSM marking"],
        ["Render the selected single-layer-water depth prepass. Its before/after-base-pass location is configurable; the late option must still finish before VSM requests.", "Prepare eligible front-layer translucency receiver data with the VSM-page-marking path.", "Pass these depth/receiver resources into virtual-page marking alongside opaque scene depth.", "Final water shading and front-layer/translucent composition happen later; receiver setup is not final transparent color."],
        ["Water depth material permutations and target/resolve handling", "Front-layer depth/receiver classification and resources", "Pass dependencies from receiver writes to VSM page marking"],
        ["Place water and glass receivers where opaque depth alone would not request the right shadow pages.", "Compare water depth-prepass locations and verify final shadow coverage stays correct."],
        [("deferred", "RenderSingleLayerWaterDepthPrepass(", 2888), ("deferred", "RenderSingleLayerWaterDepthPrepass(", 3221), ("deferred", "RenderFrontLayerTranslucency(", 3272)],
        requires=["vsm"], depends=["gbuffer"], types=["FSceneTextures"], caveat="This node shows the latest required receiver-preparation window for VSM. A before-base-pass water prepass can produce its portion earlier. Each resource is conditional on the relevant material/feature being enabled."),
]

ROADMAP = [
    ("1. Correct conventional frame", "Implement resource/view/PSO/command contracts, uploads, frame retirement and a dependency graph. Add scene registration, views, depth, GBuffer, one direct light, conventional shadows, tone mapping and presentation.", "A fixed test scene matches a CPU/analytic reference; resize and add/remove operations survive multiple frames in flight."),
    ("2. GPU-driven scene", "Add structured primitive/instance tables, stable handles, cached mesh commands, GPU culling, indirect work and current/previous transform history.", "CPU and GPU culling agree; material changes invalidate commands; moving instances have correct velocities."),
    ("3. Virtualized geometry", "Build an offline cluster/page pipeline, streaming residency, hierarchy traversal, hardware/software visibility raster, HZB recovery and material shading bins. Preserve the conventional path for unsupported content.", "Streaming pressure and rapid disocclusion never produce persistent holes; visibility and material outputs agree with a reference mesh."),
    ("4. Cached dynamic indirect light", "Implement a trace scene (SDF or AS), card/surface-cache parameterization, capture and lighting updates; add screen/radiance probes, fallback tracing and diffuse/specular resolve with temporal rejection.", "Off-screen illumination works; changed lights/materials converge; camera cuts and disocclusion do not retain stale lighting."),
    ("5. Scalable shadows and composition", "Add VSM request/allocation/invalidation, full light classification, reflection composition, material-specific effects, atmosphere/water and translucent pass policies.", "Cache invalidation and depth dependencies hold across moving lights/casters and transparent/media scenes."),
    ("6. Reconstruction and overlap", "Add robust TSR-like reconstruction or integrate an upscaler, then tune update budgets, async compute, transient aliasing and streaming from captures.", "Async and serial images agree within algorithmic tolerance; measured critical-path time improves without residency or temporal regressions."),
]

FAQ = [
    ("What is the ownership path from a static mesh to a draw?", "AActor owns components; UStaticMeshComponent references UStaticMesh asset data and creates a suitable FPrimitiveSceneProxy (ordinary FStaticMeshSceneProxy or supported Nanite proxy). FScene registers a FPrimitiveSceneInfo, and FGPUScene exposes shader-readable primitive/instance data. Conventional passes consume FMeshBatch through FMeshPassProcessor into FMeshDrawCommand; Nanite consumes its cluster/page representation and raster/shading pipelines. These arrows describe association/data flow, not C++ inheritance."),
    ("Which objects actually live on the GPU?", "UObjects, scene proxies, FScene, FRDGBuilder and FRHIResource-derived wrappers are CPU-side management objects. GPU buffers/textures/AS hold their uploaded or generated data. FRenderResource manages render-resource initialization/release; RHI wraps backend resources; RDG describes logical use and frame lifetimes. Do not upload arbitrary C++ proxy object memory as a shader ABI."),
    ("How do RDG resources survive a frame?", "Import existing external resources into a graph and queue extraction when an output must persist. Store the extracted backing resource in view/scene state and register it in the next graph. A raw FRDGTexture/FRDGBuffer pointer is scoped to its graph and is not a persistent GPU allocation handle."),
    ("Are Nanite, VSM and Lumen one mandatory pipeline?", "No. Nanite virtualizes geometry, VSM virtualizes shadow-depth storage, and Lumen estimates indirect diffuse/specular lighting. They share scene/depth/geometry data and can accelerate one another, but have distinct representations and feature switches. Conventional geometry still participates in this deferred frame."),
    ("Why can the timeline place Lumen before and after the base pass?", "Lumen scene/card updates and cached surface lighting maintain a persistent scene representation and are scheduled before the main base pass in this checkout. Screen-probe final gather and reflection work need visible-surface inputs afterward. Regular async Lumen composition occurs after direct lighting to permit overlap. This does not put all Lumen work in one serial slot."),
    ("Is the type inventory exhaustive?", "It is a reproducible lexical index of the declared module/header scope, including explicit class/struct definitions and supported shader-parameter macros. It is not a compiler AST: aliases, many generated macros, plugins, editor/developer types, forward declarations and template instantiations are excluded. Inactive preprocessor alternatives remain and duplicate base-name matches are shown as ambiguous. The guided hierarchy provides authored semantic explanations for key types; remaining entries expose source/inheritance/surface hints and module context, not invented class contracts."),
    ("What does this say about existing Ardashir capabilities?", "The operation lists and roadmap describe work needed for a renderer with similar responsibilities. They do not assert that current Ardashir modules already implement those features. Exact shaders, binary layouts, platform support, quality settings and production performance require further engineering and source study."),
]
