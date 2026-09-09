# Deferred-frame functions and operation flows

Unreal Engine 5.8.1, commit `71fe36aac5a8df5ccd66c763ffc902b29b6a9c43`.

Companion to [the pipeline walkthrough](reference.md) and [interactive deferred frame](deferred-pipeline.html). These are selected source-checked function call sites/helper definitions. They are not an exhaustive dynamic call trace or a complete C++ call graph. Flow arrows describe key control/data prerequisites; source call order does not imply serial GPU execution. Many CPU functions declare RDG passes whose GPU work executes later. Conditional alternatives remain visible and are labeled. Proposed operation descriptions paraphrase the source responsibilities.

## Scene update & GPU Scene (scene-update)

CPU scene updates establish coherent render-thread state; GPU Scene then declares uploads for changed records. The CPU functions schedule work and manage ownership, while the uploaded tables are consumed later by GPU passes.

### Selected functions

- `call-0` **FSceneRenderer::OnRenderBegin** — Entry definition. Coordinates render-begin callbacks, scene update parameters and visibility/update prerequisites. Context: Selected stage path. Source: `Engine/Source/Runtime/Renderer/Private/SceneRendering.cpp:4198`.
- `call-1` **FScene::Update** — Call site. Applies the queued scene changes at the render boundary; this call belongs to the render-begin update path. Context: Selected stage path. Source: `Engine/Source/Runtime/Renderer/Private/SceneRendering.cpp:4378`.
- `call-2` **FGPUScene::Update** — Entry definition. Checks GPU Scene enablement, establishes dynamic-primitive offset and calls UpdateInternal. Context: Selected stage path. Source: `Engine/Source/Runtime/Renderer/Private/GPUScene.cpp:1782`.
- `call-3` **FGPUScene::UpdateInternal** — Call site. Builds changed primitive/instance/payload updates and their graph resources/tasks. Context: Selected stage path. Source: `Engine/Source/Runtime/Renderer/Private/GPUScene.cpp:1796`.
- `call-4` **FGPUScene::UploadGeneral** — Helper definition. Generic data-source adapter upload helper: organizes data transfer into the registered GPU buffers. Context: Selected stage path. Source: `Engine/Source/Runtime/Renderer/Private/GPUScene.cpp:1161`.

### Key operation flow

- `op-0` **Receive scene changes**: Collect additions, removals, transforms and material/deformation changes at a coherent frame boundary. Related functions: call-0.
- `op-1` **Apply and invalidate**: Update persistent scene records and invalidate affected cached draws and feature scene state. Related functions: call-1.
- `op-2` **Allocate & pack records**: Maintain stable identity separately from packed offsets; prepare current/previous transforms and dirty ranges. Related functions: call-2, call-3.
- `op-3` **Declare GPU uploads**: Upload/scatter the changed bytes with dependencies; keep old storage alive for in-flight work. Related functions: call-4.
- `op-4` **Publish scene bindings**: Bind shader-readable records to views and consumers only after the appropriate update prerequisites. Related functions: call-2.

Edges (control/data prerequisites):

- `op-0` → `op-1`
- `op-1` → `op-2`
- `op-2` → `op-3`
- `op-3` → `op-4`

Scene-input sections: [contract](scene-types.html#contract), [instances](scene-types.html#instances), [updates](scene-types.html#updates).

## Views, relevance & draw setup (views)

View setup is a task pipeline, not one culling loop. It combines per-view relevance with dynamic mesh gathering and pass-command construction; Nanite performs additional GPU visibility later.

### Selected functions

- `call-0` **BeginInitViews** — Call site. Starts initialization of deferred views and associated visibility tasks. Context: Selected stage path. Source: `Engine/Source/Runtime/Renderer/Private/DeferredShadingRenderer.cpp:2123`.
- `call-1` **FVisibilityTaskData::StartGatherDynamicMeshElements** — Call site. Starts the dynamic mesh gathering phase after view setup prerequisites. Context: Selected stage path. Source: `Engine/Source/Runtime/Renderer/Private/SceneVisibility.cpp:5950`.
- `call-2` **FDynamicMeshElementContext::GatherDynamicMeshElementsForPrimitive** — Helper definition. Asks each eligible primitive for view-dependent mesh elements. Context: Selected stage path. Source: `Engine/Source/Runtime/Renderer/Private/SceneVisibility.cpp:4191`.
- `call-3` **FSceneRenderer::SetupMeshPass** — Call site. Converts eligible view commands and dynamic elements into pass-specific draw work. Context: Selected stage path. Source: `Engine/Source/Runtime/Renderer/Private/SceneVisibility.cpp:4806`.

### Key operation flow

- `op-0` **Build view state**: Resolve camera matrices, view rectangle, show flags, jitter and persistent history references. Related functions: call-0.
- `op-1` **Determine relevance**: Test bounds and view visibility; classify pass relevance and select conventional LODs. Task order must preserve prerequisites. Related functions: call-0.
- `op-2` **Gather dynamic meshes**: Collect proxy mesh batches for visible dynamic primitives; cached static command data follows its reuse path. Related functions: call-1, call-2.
- `op-3` **Build pass commands**: Resolve material/vertex-factory/pass permutations, sort/batch work and prepare instance-culling bindings. Related functions: call-3.

Edges (control/data prerequisites):

- `op-0` → `op-1`
- `op-1` → `op-2`
- `op-2` → `op-3`

Scene-input sections: [views](scene-types.html#views), [instances](scene-types.html#instances), [materials](scene-types.html#materials).

## Nanite residency & packed views (streaming)

Nanite geometry is cooked beforehand. Runtime streaming processes feedback and installs pages while the renderer prepares views; synchronization is required before consumers read updated page mappings.

### Selected functions

- `call-0` **Nanite::FStreamingManager::BeginAsyncUpdate** — Call site. Starts the streaming update from the main renderer. Context: Selected stage path. Source: `Engine/Source/Runtime/Renderer/Private/DeferredShadingRenderer.cpp:2048`.
- `call-1` **Nanite::FStreamingManager::AsyncUpdate** — Helper definition. Processes the streaming update, including installation of ready pages. Context: Selected stage path. Source: `Engine/Source/Runtime/Engine/Private/Rendering/NaniteStreamingManager.cpp:2827`.
- `call-2` **Nanite::FStreamingManager::InstallReadyPages** — Call site. Installs ready pages or skips resources that are no longer valid. Context: Selected stage path. Source: `Engine/Source/Runtime/Engine/Private/Rendering/NaniteStreamingManager.cpp:2835`.
- `call-3` **Nanite::FStreamingManager::EndAsyncUpdate** — Helper definition. Completes/synchronizes streaming work and its graph-side uploads. Context: Selected stage path. Source: `Engine/Source/Runtime/Engine/Private/Rendering/NaniteStreamingManager.cpp:3044`.

### Key operation flow

- `op-0` **Read requests & budgets**: Consume earlier GPU feedback and resource changes, respecting page-pool and I/O budgets. Related functions: call-0, call-1.
- `op-1` **Install ready pages**: Upload resident geometry/hierarchy changes and repair mappings for live resources; preserve coarser resident coverage. Related functions: call-2.
- `op-2` **Complete streaming update**: Ensure page/mapping changes are ready before geometry consumers use them. Related functions: call-3.
- `op-3` **Prepare Nanite consumers**: Use packed view transforms/LOD thresholds and the resident resource state for culling; asset clustering is not repeated each frame. Related functions: call-0.

Edges (control/data prerequisites):

- `op-0` → `op-1`
- `op-1` → `op-2`
- `op-2` → `op-3`

Scene-input sections: [geometry](scene-types.html#geometry), [textures](scene-types.html#textures), [updates](scene-types.html#updates).

## Early atmosphere lighting inputs (atmosphere-luts)

Atmosphere functions declare compute passes for reusable/view-dependent lighting lookup data. Their early scheduling supplies illumination inputs before final sky/fog color is composited.

### Selected functions

- `call-0` **FSceneRenderer::RenderSkyAtmosphereLookUpTables** — Call site. Schedules atmosphere lookup work in a selected early slot; another slot is available according to async/configuration. Context: Selected stage path. Source: `Engine/Source/Runtime/Renderer/Private/DeferredShadingRenderer.cpp:2438`.
- `call-1` **FComputeShaderUtils::AddPass** — Call site. Declares the transmittance LUT compute pass; TransmittanceLut is an RDG event label, not a C++ function. Context: Selected stage path. Source: `Engine/Source/Runtime/Renderer/Private/SkyAtmosphereRendering.cpp:1536`.
- `call-2` **FComputeShaderUtils::AddPass** — Call site. Declares multiple-scattering lookup generation. Context: Selected stage path. Source: `Engine/Source/Runtime/Renderer/Private/SkyAtmosphereRendering.cpp:1559`.
- `call-3` **FComputeShaderUtils::AddPass** — Call site. Declares view-dependent sky lookup generation. Context: Selected stage path. Source: `Engine/Source/Runtime/Renderer/Private/SkyAtmosphereRendering.cpp:1842`.

### Key operation flow

- `op-0` **Resolve atmosphere inputs**: Read atmosphere parameters, light state, view state and selected scheduling mode. Related functions: call-0.
- `op-1` **Transmittance**: Integrate attenuation into a reusable lookup texture. Related functions: call-1.
- `op-2` **Multiple scattering**: Compute the higher-order scattering approximation used by later atmosphere evaluation. Related functions: call-2.
- `op-3` **View lookup / aerial data**: Generate sky-view and camera aerial-perspective resources where enabled; transition them for consumers. Related functions: call-0, call-3.

Edges (control/data prerequisites):

- `op-0` → `op-1`
- `op-1` → `op-2`
- `op-2` → `op-3`

Scene-input sections: [lighting](scene-types.html#lighting), [environment](scene-types.html#environment), [views](scene-types.html#views).

## Conventional depth prepass (depth)

The conventional prepass selects eligible depth mesh passes and declares raster work. Masking, WPO and the chosen velocity mode must agree with later material shading.

### Selected functions

- `call-0` **FDeferredShadingSceneRenderer::RenderPrePass** — Call site. Invokes the prepass for the current view range. Context: Selected stage path. Source: `Engine/Source/Runtime/Renderer/Private/DeferredShadingRenderer.cpp:2464`.
- `call-1` **FParallelMeshDrawCommandPass::BuildRenderingCommands** — Call site. Prepares command/instance-culling data for the chosen depth mesh pass. Context: Selected stage path. Source: `Engine/Source/Runtime/Renderer/Private/DepthRendering.cpp:565`.
- `call-2` **FRDGBuilder::AddDispatchPass** — Call site. Declares a raster dispatch pass; its lambda later dispatches the prepared depth commands. Context: Selected stage path. Source: `Engine/Source/Runtime/Renderer/Private/DepthRendering.cpp:567`.
- `call-3` **FParallelMeshDrawCommandPass::Dispatch** — Call site. Dispatches prepared draw-command work inside the raster pass callback. Context: Selected stage path. Source: `Engine/Source/Runtime/Renderer/Private/DepthRendering.cpp:573`.

### Key operation flow

- `op-0` **Choose early-Z coverage**: Select opaque/masked coverage and first/second-stage depth modes; configure depth/stencil. Related functions: call-0.
- `op-1` **Prepare depth draws**: Select depth-compatible shaders, stream/deformation bindings and GPU instance-culling arguments. Related functions: call-1.
- `op-2` **Declare raster work**: Record the graph's depth target/accesses and the callback that will issue rendering commands. Related functions: call-2.
- `op-3` **Execute eligible draws**: Rasterize depth with consistent masking and vertex motion, then make depth/optional velocity available. Related functions: call-3.

Edges (control/data prerequisites):

- `op-0` → `op-1`
- `op-1` → `op-2`
- `op-2` → `op-3`

Scene-input sections: [geometry](scene-types.html#geometry), [materials](scene-types.html#materials), [instances](scene-types.html#instances).

## Distance fields / ray tracing AS (trace-scene)

Software distance-field tracing and hardware triangle tracing need different scene representations. These branches are alternatives for Lumen fallback tracing, although other renderer features can independently request either representation.

### Selected functions

- `call-0` **PrepareDistanceFieldScene** — Call site. Schedules the distance-field scene preparation path when requested. Context: Selected stage path. Source: `Engine/Source/Runtime/Renderer/Private/DeferredShadingRenderer.cpp:2011`.
- `call-1` **FDistanceFieldSceneData::UpdateDistanceFieldObjectBuffers** — Call site. Updates distance-field object data for additions/removals and changed scene state. Context: Selected stage path. Source: `Engine/Source/Runtime/Renderer/Private/DistanceFieldObjectManagement.cpp:1028`.
- `call-2` **UpdateGlobalDistanceFieldVolume** — Call site. Updates per-view global distance-field coverage/clipmaps. Context: Selected stage path. Source: `Engine/Source/Runtime/Renderer/Private/DistanceFieldObjectManagement.cpp:1048`.
- `call-3` **SetupRayTracingRenderingData** — Call site. Synchronizes and prepares gathered ray-tracing scene data before a hardware-tracing consumer. Context: Selected stage path. Source: `Engine/Source/Runtime/Renderer/Private/DeferredShadingRenderer.cpp:3034`.

### Key operation flow

- `op-0` **Select trace representation**: Decide which fallback geometry the chosen lighting/reflection method consumes. Screen traces do not cover arbitrary off-screen surfaces. Related functions: call-0, call-3.
- `op-1` **Mesh / global SDF** [illustrated software path]: Update supported mesh/heightfield objects and global clipmap composition from cooked distance-field data. Related functions: call-1, call-2.
- `op-2` **Triangle acceleration scene** [illustrated hardware path]: Make deformed geometry/BLAS, instance data and TLAS ready; establish build-to-trace barriers and material lookup. Related functions: call-3.
- `op-3` **Publish trace inputs**: Bind the selected representation with matching transforms/material IDs and retain it until all ray consumers complete. Related functions: call-0, call-3.

Edges (control/data prerequisites):

- `op-0` → `op-1` — software
- `op-0` → `op-2` — hardware
- `op-1` → `op-3` — SDF ready
- `op-2` → `op-3` — AS ready

Scene-input sections: [geometry](scene-types.html#geometry), [materials](scene-types.html#materials), [fidelity](scene-types.html#fidelity).

## Nanite cull → raster → repair (nanite-visibility)

Nanite's main/post algorithm is a conditional recovery flow. Culling chooses visible cluster work; rasterization produces depth/visibility. Material evaluation is a separate stage.

### Selected functions

- `call-0` **FDeferredShadingSceneRenderer::RenderNanite** — Entry definition. Sets up Nanite rendering for deferred views and consumes raster results. Context: Selected stage path. Source: `Engine/Source/Runtime/Renderer/Private/DeferredShadingRenderer.cpp:1449`.
- `call-1` **Nanite::FRenderer::DrawGeometry** — Entry definition. Coordinates culling, raster setup, main pass and optional post-pass recovery. Context: Selected stage path. Source: `Engine/Source/Runtime/Renderer/Private/Nanite/NaniteCullRaster.cpp:6688`.
- `call-2` **AddPass_InstanceHierarchyAndClusterCull** — Call site. Declares main occlusion culling or the no-occlusion variant. Context: Selected stage path. Source: `Engine/Source/Runtime/Renderer/Private/Nanite/NaniteCullRaster.cpp:7008`.
- `call-3` **AddPass_Rasterize** — Call site. Bins/rasterizes main visible work using configured hardware/software raster paths. Context: Selected stage path. Source: `Engine/Source/Runtime/Renderer/Private/Nanite/NaniteCullRaster.cpp:7010`.
- `call-4` **BuildHZBFurthest** — Call site. Builds updated occlusion data from current depth/visibility in the non-VSM recovery branch. Context: Selected stage path. Source: `Engine/Source/Runtime/Renderer/Private/Nanite/NaniteCullRaster.cpp:7053`.
- `call-5` **AddPass_InstanceHierarchyAndClusterCull** — Call site. Re-tests rejected candidates; the subsequent AddPass_Rasterize renders recovered geometry. Context: Selected stage path. Source: `Engine/Source/Runtime/Renderer/Private/Nanite/NaniteCullRaster.cpp:7071`.

### Key operation flow

- `op-0` **Initialize raster & filter**: Prepare views, resident resources, queues, bins and per-view primitive filtering. Related functions: call-0, call-1.
- `op-1` **Main cull & raster**: Use previous HZB only when valid; retain rejected candidates. Rasterize the selected clusters. Related functions: call-2, call-3.
- `op-2` **Two-pass enabled?**: A usable previous HZB and configuration permit recovery. Otherwise the no-occlusion/main result continues directly. Related functions: call-1.
- `op-3` **Build current HZB**: Combine relevant current depth/visibility for a new occlusion test. Related functions: call-4.
- `op-4` **Post cull & raster**: Re-test previously rejected candidates against current occlusion and rasterize recovered geometry. Related functions: call-5.
- `op-5` **Export visibility / depth**: Publish winning cluster/triangle visibility, scene depth/stencil metadata and streaming feedback for consumers. Related functions: call-0, call-1.

Edges (control/data prerequisites):

- `op-0` → `op-1`
- `op-1` → `op-2`
- `op-2` → `op-3` — yes
- `op-3` → `op-4`
- `op-4` → `op-5`
- `op-2` → `op-5` — no

Scene-input sections: [geometry](scene-types.html#geometry), [instances](scene-types.html#instances), [fidelity](scene-types.html#fidelity).

## Lumen cards & Surface Cache (lumen-scene)

Lumen's Surface Cache is a persistent parameterization of supported surfaces. Page capture/resampling is budgeted; a surface can exist in the scene yet lack fresh card coverage.

### Selected functions

- `call-0` **FDeferredShadingSceneRenderer::UpdateLumenScene** — Call site. Runs the scene capture/update work selected for this frame. Context: Selected stage path. Source: `Engine/Source/Runtime/Renderer/Private/DeferredShadingRenderer.cpp:2894`.
- `call-1` **ResampleLumenCards** — Call site. Resamples card data when pages/representations change so valid information can be reused. Context: Selected stage path. Source: `Engine/Source/Runtime/Renderer/Private/Lumen/LumenSceneRendering.cpp:2635`.
- `call-2` **DilateCardPageOneTexel** — Call site. Dilates captured card-page borders before updating the destination atlases. Context: Selected stage path. Source: `Engine/Source/Runtime/Renderer/Private/Lumen/LumenSceneRendering.cpp:3097`.

### Key operation flow

- `op-0` **Select card/page updates**: Apply primitive changes, maintain card coverage and prioritize captures under the per-frame budget. Related functions: call-0.
- `op-1` **Allocate / resample**: Allocate or reuse surface pages and resample valid old data where possible. Related functions: call-1.
- `op-2` **Capture material surfaces**: Render selected geometry/materials into albedo, normal, emissive and depth capture atlases; supported Nanite geometry can accelerate capture. Related functions: call-0.
- `op-3` **Dilate and publish**: Fill page borders, update mappings/atlases and invalidate stale lighting for changed surfaces. Related functions: call-2.

Edges (control/data prerequisites):

- `op-0` → `op-1`
- `op-1` → `op-2`
- `op-2` → `op-3`

Scene-input sections: [geometry](scene-types.html#geometry), [materials](scene-types.html#materials), [fidelity](scene-types.html#fidelity).

## Light the Lumen Surface Cache (lumen-lighting)

This is cached-surface lighting, not final-gather lighting of the current GBuffer. In the pinned main frame it is called before RenderBasePass; hardware shadow/trace inputs must be ready if that mode is chosen.

### Selected functions

- `call-0` **RenderLumenSceneLighting** — Call site. Schedules lighting of the maintained Lumen surface scene. Context: Selected stage path. Source: `Engine/Source/Runtime/Renderer/Private/DeferredShadingRenderer.cpp:3041`.
- `call-1` **Lumen::BuildCardUpdateContext** — Call site. Builds separate direct/indirect card update work from budgets/history validity. Context: Selected stage path. Source: `Engine/Source/Runtime/Renderer/Private/Lumen/LumenSceneLighting.cpp:276`.
- `call-2` **RenderDirectLightingForLumenScene** — Call site. Lights selected cached surface pages using the scene's light/shadow inputs. Context: Selected stage path. Source: `Engine/Source/Runtime/Renderer/Private/Lumen/LumenSceneLighting.cpp:293`.
- `call-3` **RenderRadiosityForLumenScene** — Call site. Updates cached indirect/radiosity lighting for the selected surface pages. Context: Selected stage path. Source: `Engine/Source/Runtime/Renderer/Private/Lumen/LumenSceneLighting.cpp:300`.

### Key operation flow

- `op-0` **Prepare scene-lighting inputs**: Require valid card mappings/material captures, lighting inputs and any chosen ray-tracing resources. Related functions: call-0.
- `op-1` **Build update contexts**: Choose direct and indirect page work according to budgets and history validity. Related functions: call-1.
- `op-2` **Update direct lighting**: Evaluate light contribution/visibility on selected cached surfaces. Related functions: call-2.
- `op-3` **Update radiosity**: Reuse/trace indirect lighting on the selected cache pages and publish valid lighting atlases. Related functions: call-3.

Edges (control/data prerequisites):

- `op-0` → `op-1`
- `op-1` → `op-2`
- `op-2` → `op-3`

Scene-input sections: [lighting](scene-types.html#lighting), [environment](scene-types.html#environment), [fidelity](scene-types.html#fidelity).

## Base pass & Nanite materials (gbuffer)

Conventional mesh draws and Nanite material evaluation converge on deferred surface data. The main RenderBasePass call occurs after Lumen scene lighting; DBuffer/material/deformation inputs must be ready before their consumers.

### Selected functions

- `call-0` **FDeferredShadingSceneRenderer::RenderBasePass** — Call site. Starts base-pass rendering for the main deferred views. Context: Selected stage path. Source: `Engine/Source/Runtime/Renderer/Private/DeferredShadingRenderer.cpp:3059`.
- `call-1` **FDeferredShadingSceneRenderer::RenderBasePassInternal** — Call site. Routes view/material rendering work, including the Nanite path where enabled. Context: Selected stage path. Source: `Engine/Source/Runtime/Renderer/Private/BasePassRendering.cpp:1146`.
- `call-2` **Nanite::DispatchBasePass** — Call site. Dispatches Nanite material shading from raster results and shading commands. Context: Selected stage path. Source: `Engine/Source/Runtime/Renderer/Private/BasePassRendering.cpp:1396`.
- `call-3` **Nanite::DispatchBasePass** — Helper definition. Implementation entry for Nanite shading-bin/work dispatch and GBuffer material output. Context: Selected stage path. Source: `Engine/Source/Runtime/Renderer/Private/Nanite/NaniteShading.cpp:1670`.

### Key operation flow

- `op-0` **Bind surface inputs**: Bind depth, material textures/uniforms, deformation, lighting policy and pre-base-pass decal data. Related functions: call-0.
- `op-1` **Conventional material draws**: Evaluate eligible mesh materials and write the configured GBuffer/Substrate targets and optional velocity. Related functions: call-1.
- `op-2` **Nanite material dispatch** [illustrated nanite path]: Decode geometry visibility, generate/use shading-bin work and evaluate visible material samples. Related functions: call-2, call-3.
- `op-3` **Publish deferred surfaces**: Provide consistent depth, normals/material closures, emissive and other surface outputs for lighting consumers. Related functions: call-0.

Edges (control/data prerequisites):

- `op-0` → `op-1` — conventional
- `op-0` → `op-2` — Nanite enabled
- `op-1` → `op-3`
- `op-2` → `op-3`

Scene-input sections: [materials](scene-types.html#materials), [geometry](scene-types.html#geometry), [textures](scene-types.html#textures).

## Lumen screen-probe final gather (gi)

The screen-probe gather consumes current surface data and the maintained trace/lighting scene. It distributes samples, traces, filters and integrates them; none of those is equivalent to lighting the Surface Cache itself.

### Selected functions

- `call-0` **LumenRadianceCache::UpdateRadianceCaches** — Call site. Updates reusable world-space radiance probes requested by the gather/translucency consumers. Context: Selected stage path. Source: `Engine/Source/Runtime/Renderer/Private/Lumen/LumenScreenProbeGather.cpp:2590`.
- `call-1` **GenerateImportanceSamplingRays** — Call site. Chooses gather rays according to the enabled importance-sampling policy. Context: Selected stage path. Source: `Engine/Source/Runtime/Renderer/Private/Lumen/LumenScreenProbeGather.cpp:2624`.
- `call-2` **TraceScreenProbes** — Call site. Traces the probe rays using screen information and the configured fallback path. Context: Selected stage path. Source: `Engine/Source/Runtime/Renderer/Private/Lumen/LumenScreenProbeGather.cpp:2647`.
- `call-3` **FilterScreenProbes** — Call site. Filters probe radiance before integration. Context: Selected stage path. Source: `Engine/Source/Runtime/Renderer/Private/Lumen/LumenScreenProbeGather.cpp:2661`.
- `call-4` **InterpolateAndIntegrate** — Call site. Integrates/interpolates probe lighting onto current surfaces and rough-specular outputs. Context: Selected stage path. Source: `Engine/Source/Runtime/Renderer/Private/Lumen/LumenScreenProbeGather.cpp:2711`.
- `call-5` **RenderHardwareRayTracingScreenProbe** — Call site. Hardware fallback helper used for unresolved probe rays; cache/sky completion is still separate. Context: Selected stage path. Source: `Engine/Source/Runtime/Renderer/Private/Lumen/LumenScreenProbeTracing.cpp:797`.

### Key operation flow

- `op-0` **Place probes & update cache**: Place view-dependent probe samples and update reusable radiance-cache coverage as configured. Related functions: call-0.
- `op-1` **Choose ray samples**: Use importance sampling where enabled; generate ray work with current material/normal inputs. Related functions: call-1.
- `op-2` **Trace screen + fallback**: Attempt eligible screen hits, then selected hardware triangles or software distance fields; shade/cache-complete remaining rays. Related functions: call-2, call-5.
- `op-3` **Filter probe radiance**: Filter noisy probe samples while respecting geometric validity; optional short-range AO has separate work. Related functions: call-3.
- `op-4` **Integrate to surfaces**: Interpolate diffuse/rough-specular lighting, preserving history rejection and material closure requirements. Related functions: call-4.

Edges (control/data prerequisites):

- `op-0` → `op-1`
- `op-1` → `op-2`
- `op-2` → `op-3`
- `op-3` → `op-4`

Scene-input sections: [materials](scene-types.html#materials), [lighting](scene-types.html#lighting), [views](scene-types.html#views), [fidelity](scene-types.html#fidelity).

## Water & front-layer receiver depth (receiver-depth)

Water/front-layer receiver setup exists because shadow page demand and later reflections can need depths beyond opaque surfaces. These are early receiver representations, separate from final transparent/water color composition.

### Selected functions

- `call-0` **RenderSingleLayerWaterDepthPrepass** — Call site. Runs the water depth prepass in the configured earlier slot. Context: Selected stage path. Source: `Engine/Source/Runtime/Renderer/Private/DeferredShadingRenderer.cpp:2888`.
- `call-1` **RenderSingleLayerWaterDepthPrepass** — Call site. Alternative after-base-pass slot; these two call sites are not mandatory duplicate draws. Context: Selected stage path. Source: `Engine/Source/Runtime/Renderer/Private/DeferredShadingRenderer.cpp:3221`.
- `call-2` **RenderFrontLayerTranslucency** — Call site. Generates front-layer data for VSM page marking. Context: Selected stage path. Source: `Engine/Source/Runtime/Renderer/Private/DeferredShadingRenderer.cpp:3272`.
- `call-3` **BeginMarkVirtualShadowMapPages** — Call site. Consumes water/front-layer receiver inputs to start VSM demand generation. Context: Selected stage path. Source: `Engine/Source/Runtime/Renderer/Private/DeferredShadingRenderer.cpp:3273`.

### Key operation flow

- `op-0` **Select receiver needs**: Decide if water depth/front-layer data are required by enabled features and materials. Related functions: call-0, call-1, call-2.
- `op-1` **Water receiver depth**: Render water receiver depth in its selected prepass location, not automatically in both listed locations. Related functions: call-0, call-1.
- `op-2` **Front-layer receiver data**: Generate the relevant nearest transparent layer for page requests. Related functions: call-2.
- `op-3` **Mark shadow-page demand** [illustrated vsm path]: Feed receiver data into VSM marking; conventional shadow maps use a different allocation path. Related functions: call-3.

Edges (control/data prerequisites):

- `op-0` → `op-1`
- `op-1` → `op-2`
- `op-2` → `op-3`

Scene-input sections: [geometry](scene-types.html#geometry), [materials](scene-types.html#materials), [environment](scene-types.html#environment).

## Lumen reflection trace & resolve (reflections)

Reflection work is specular surface-ray work with tile classification, ray generation, tracing and reconstruction. It is distinct from diffuse screen probes; front-layer translucency also has a separate variant.

### Selected functions

- `call-0` **FDeferredShadingSceneRenderer::RenderLumenReflections** — Entry definition. Initializes the reflection pass and its tracing/reconstruction resources. Context: Selected stage path. Source: `Engine/Source/Runtime/Renderer/Private/Lumen/LumenReflections.cpp:1215`.
- `call-1` **FComputeShaderUtils::AddPass** — Call site. Declares ray-generation compute work using FReflectionGenerateRaysCS. Context: Selected stage path. Source: `Engine/Source/Runtime/Renderer/Private/Lumen/LumenReflections.cpp:1425`.
- `call-2` **TraceReflections** — Call site. Dispatches the selected reflection tracing path after rays/tiles are ready. Context: Selected stage path. Source: `Engine/Source/Runtime/Renderer/Private/Lumen/LumenReflections.cpp:1470`.
- `call-3` **FComputeShaderUtils::AddPass** — Call site. Declares reflection resolve using FLumenReflectionResolveCS; shader type and RDG event are not C++ entry-point names. Context: Selected stage path. Source: `Engine/Source/Runtime/Renderer/Private/Lumen/LumenReflections.cpp:1626`.

### Key operation flow

- `op-0` **Classify specular work**: Choose reflection tiles/closures and roughness/resolution policy; prepare indirect work arguments. Related functions: call-0.
- `op-1` **Generate reflection rays**: Generate directions/trace inputs from surfaces and reflection settings. Related functions: call-1.
- `op-2` **Screen + fallback trace**: Resolve eligible screen hits and trace remaining rays against the selected software/hardware representation; choose cache lookup or Hit Lighting separately. Related functions: call-2.
- `op-3` **Resolve & reconstruct**: Resolve traced samples at the target resolution and reconstruct neighboring sample contributions where configured. Related functions: call-3.
- `op-4` **Temporal / spatial filter**: Reject invalid history, accumulate/filter valid lighting, and publish specular-indirect results for composition. Related functions: call-0.

Edges (control/data prerequisites):

- `op-0` → `op-1`
- `op-1` → `op-2`
- `op-2` → `op-3`
- `op-3` → `op-4`

Scene-input sections: [materials](scene-types.html#materials), [views](scene-types.html#views), [fidelity](scene-types.html#fidelity).

## VSM / conventional shadow depth (shadows)

VSM allocates shadow storage from receiver demand and cache state. Conventional shadow maps allocate per-light views instead. They share a need for caster geometry/deformation and consistent depth/material coverage.

### Selected functions

- `call-0` **BeginMarkVirtualShadowMapPages** — Call site. Starts VSM receiver-page requests. Context: Selected stage path. Source: `Engine/Source/Runtime/Renderer/Private/DeferredShadingRenderer.cpp:3273`.
- `call-1` **FVirtualShadowMapArray::BuildPageAllocations** — Helper definition. Builds allocation/reuse work for virtual-to-physical shadow pages. Context: Selected stage path. Source: `Engine/Source/Runtime/Renderer/Private/VirtualShadowMaps/VirtualShadowMapArray.cpp:3227`.
- `call-2` **RenderShadowDepthMaps** — Call site. Schedules shadow depth work in the late slot when it was not rendered early. Context: Selected stage path. Source: `Engine/Source/Runtime/Renderer/Private/DeferredShadingRenderer.cpp:3291`.
- `call-3` **RenderVirtualShadowMaps** — Call site. Invokes the VSM rendering path from shadow-depth rendering. Context: Selected stage path. Source: `Engine/Source/Runtime/Renderer/Private/ShadowDepthRendering.cpp:1877`.

### Key operation flow

- `op-0` **Select shadow path**: Resolve shadow-enabled lights, caster/receiver policy and chosen storage method. Related functions: call-2.
- `op-1` **Request / allocate pages** [illustrated vsm path]: For VSM, mark receiver demand, reuse valid cached pages, allocate replacements and invalidate changed casters/lights. Related functions: call-0, call-1.
- `op-2` **Allocate light depth views** [illustrated conventional path]: For conventional maps, allocate per-light/cascade depth targets and views; early scheduling is conditional on dependencies. Related functions: call-2.
- `op-3` **Rasterize required depth**: Draw dirty VSM pages or conventional light views using conventional/Nanite caster representations as supported. Related functions: call-2, call-3.
- `op-4` **Publish shadow sampling**: Make depth/page mappings and projection parameters ready for light visibility evaluation. Related functions: call-2.

Edges (control/data prerequisites):

- `op-0` → `op-1` — VSM
- `op-0` → `op-2` — conventional
- `op-1` → `op-3`
- `op-2` → `op-3`
- `op-3` → `op-4`

Scene-input sections: [lighting](scene-types.html#lighting), [geometry](scene-types.html#geometry), [materials](scene-types.html#materials), [fidelity](scene-types.html#fidelity).

## Late decals, AO & velocity (decals-ao)

This scheduling window contains several conditional surface updates; it is not one immutable position for all decals or velocities. Async Lumen is dispatched only after its required inputs are ready.

### Selected functions

- `call-0` **CompositionLighting.ProcessAfterBasePass** — Call site. Earlier after-base-pass preparation used by the applicable stochastic-lighting path. Context: Selected stage path. Source: `Engine/Source/Runtime/Renderer/Private/DeferredShadingRenderer.cpp:3198`.
- `call-1` **DispatchAsyncLumenIndirectLightingWork** — Call site. Dispatches eligible indirect work using prepared surfaces/scene inputs. Context: Selected stage path. Source: `Engine/Source/Runtime/Renderer/Private/DeferredShadingRenderer.cpp:3248`.
- `call-2` **RenderVelocities** — Call site. Writes late opaque velocity when depth/base pass did not already provide the chosen mode. Context: Selected stage path. Source: `Engine/Source/Runtime/Renderer/Private/DeferredShadingRenderer.cpp:3374`.
- `call-3` **CompositionLighting.ProcessAfterBasePass** — Call site. Later after-base-pass decal/AO work for the applicable remaining path. Context: Selected stage path. Source: `Engine/Source/Runtime/Renderer/Private/DeferredShadingRenderer.cpp:3391`.

### Key operation flow

- `op-0` **Resolve surface-update policy**: DBuffer belongs before material evaluation; after-base-pass decals/AO and velocity select their configured slots. Related functions: call-0, call-3.
- `op-1` **Prepare async inputs**: Complete the surface data required by the selected stochastic/indirect consumers before they start. Related functions: call-0.
- `op-2` **Dispatch eligible Lumen** [illustrated async path]: Schedule compute/async indirect work with real dependencies; turning async on does not create missing GBuffer or trace data. Related functions: call-1.
- `op-3` **Finish remaining updates**: Perform required late velocity/decal/AO work without overwriting an incompatible temporal or material representation. Related functions: call-2, call-3.

Edges (control/data prerequisites):

- `op-0` → `op-1`
- `op-1` → `op-2`
- `op-2` → `op-3`

Scene-input sections: [materials](scene-types.html#materials), [views](scene-types.html#views), [updates](scene-types.html#updates).

## Deferred direct lighting (direct)

Deferred direct lighting evaluates supported lights against current surface data and shadow resources. The sorted light set chooses suitable paths; MegaLights and hair lighting are conditional additional paths, not automatic replacements for all conventional lighting.

### Selected functions

- `call-0` **FDeferredShadingSceneRenderer::RenderLights** — Call site. Starts conventional deferred light rendering for the sorted visible light set. Context: Selected stage path. Source: `Engine/Source/Runtime/Renderer/Private/DeferredShadingRenderer.cpp:3467`.
- `call-1` **RenderLightFunction** — Call site. Applies per-light material modulation where required in the shadowed-light path. Context: Selected stage path. Source: `Engine/Source/Runtime/Renderer/Private/LightRendering.cpp:2379`.
- `call-2` **RenderLight** — Call site. Schedules an individual applicable deferred-light draw. Context: Selected stage path. Source: `Engine/Source/Runtime/Renderer/Private/LightRendering.cpp:1735`.
- `call-3` **RenderMegaLights** — Call site. Runs the configured MegaLights path when its context is valid. Context: Selected stage path. Source: `Engine/Source/Runtime/Renderer/Private/DeferredShadingRenderer.cpp:3471`.

### Key operation flow

- `op-0` **Classify visible lights**: Read sorted light types/features and select conventional or configured specialized paths. Related functions: call-0.
- `op-1` **Prepare visibility / modulation**: Bind shadow masks/maps, channels, IES/source-shape data and light functions as required. Related functions: call-1.
- `op-2` **Evaluate direct BRDF**: Apply the selected material/closure lighting model and accumulate direct illumination into the scene outputs. Related functions: call-2.
- `op-3` **Complete specialized lights**: Run enabled MegaLights or other dedicated contributions with their own resource contracts; avoid double-counting. Related functions: call-3.

Edges (control/data prerequisites):

- `op-0` → `op-1`
- `op-1` → `op-2`
- `op-2` → `op-3`

Scene-input sections: [lighting](scene-types.html#lighting), [materials](scene-types.html#materials).

## Indirect & specular composition (composite)

The async path can overlap indirect computation with direct lights, but final composition is a join. Non-async and visualization paths use different calls/flags; the published source anchors identify the main regular Lumen composition point.

### Selected functions

- `call-0` **DispatchAsyncLumenIndirectLightingWork** — Entry definition. Prepares async indirect outputs that later composition consumes. Context: Selected stage path. Source: `Engine/Source/Runtime/Renderer/Private/IndirectLightRendering.cpp:990`.
- `call-1` **RenderDiffuseIndirectAndAmbientOcclusion** — Call site. Earlier non-regular-composite work is invoked with bCompositeRegularLumenOnly=false. Context: Selected stage path. Source: `Engine/Source/Runtime/Renderer/Private/DeferredShadingRenderer.cpp:3429`.
- `call-2` **RenderDiffuseIndirectAndAmbientOcclusion** — Call site. After RenderLights, joins regular Lumen indirect results with bCompositeRegularLumenOnly=true. Context: Selected stage path. Source: `Engine/Source/Runtime/Renderer/Private/DeferredShadingRenderer.cpp:3518`.
- `call-3` **RenderDeferredReflectionsAndSkyLighting** — Call site. Composes deferred reflection/sky contributions according to the chosen methods. Context: Selected stage path. Source: `Engine/Source/Runtime/Renderer/Private/DeferredShadingRenderer.cpp:3528`.
- `call-4` **RenderLights** — Call site. Produces the direct-light scene contribution between the earlier indirect/AO call and the later regular Lumen composite. Context: Selected stage path. Source: `Engine/Source/Runtime/Renderer/Private/DeferredShadingRenderer.cpp:3467`.

### Key operation flow

- `op-0` **Indirect work in flight** [illustrated async path]: Eligible gather/reflection work runs once its inputs are available; preserve its output handles and dependencies. Related functions: call-0.
- `op-1` **Earlier indirect / AO**: The earlier call uses bCompositeRegularLumenOnly=false. It handles applicable non-async/non-regular indirect and AO work before direct lighting; it is not the direct-light producer. Related functions: call-1.
- `op-2` **Direct-lit scene ready**: RenderLights produces the direct contribution after earlier indirect/AO processing. Scene color and relevant surface buffers form the other inputs to the join. Related functions: call-4.
- `op-3` **Join & composite indirect**: Wait through graph dependencies for required indirect outputs before reading/combining them; serial paths still need equivalent data contracts. Related functions: call-2.
- `op-4` **Reflection / sky blend**: Combine the chosen specular/environment terms and any required material-specific composition without double-counting. Related functions: call-3.

Edges (control/data prerequisites):

- `op-0` → `op-3` — indirect ready
- `op-1` → `op-2` — earlier work
- `op-2` → `op-3` — scene color ready
- `op-3` → `op-4`

Scene-input sections: [lighting](scene-types.html#lighting), [materials](scene-types.html#materials), [fidelity](scene-types.html#fidelity).

## Sky, fog, clouds & water (atmosphere)

Final atmosphere/media/water composition consumes earlier lighting/depth data. Underwater and cloud paths alter scheduling, so the flow summarizes major dependencies rather than claiming one order for every material and camera state.

### Selected functions

- `call-0` **RenderSkyAtmosphere** — Call site. Draws/composes sky and aerial-perspective contributions for the applicable mode. Context: Selected stage path. Source: `Engine/Source/Runtime/Renderer/Private/DeferredShadingRenderer.cpp:3784`.
- `call-1` **RenderFog** — Call site. Composes configured fog contribution into scene color. Context: Selected stage path. Source: `Engine/Source/Runtime/Renderer/Private/DeferredShadingRenderer.cpp:3822`.
- `call-2` **RenderTranslucency** — Call site. Renders the underwater translucency subset before water where this branch is active. Context: Selected stage path. Source: `Engine/Source/Runtime/Renderer/Private/DeferredShadingRenderer.cpp:3876`.
- `call-3` **RenderSingleLayerWater** — Call site. Runs water surface lighting/refraction/composition with prepared depth and scene-without-water inputs. Context: Selected stage path. Source: `Engine/Source/Runtime/Renderer/Private/DeferredShadingRenderer.cpp:3880`.
- `call-4` **RenderVolumetricCloud** — Call site. Declares cloud rendering in the main late path. Other modes use earlier asynchronous/volumetric-target work; cloud rendering and composition depend on the selected target mode. Context: Selected stage path. Source: `Engine/Source/Runtime/Renderer/Private/DeferredShadingRenderer.cpp:3851`.

### Key operation flow

- `op-0` **Prepare media inputs**: Use lit scene color/depth, atmosphere LUTs, cloud/fog data and camera-underwater state. Related functions: call-0, call-1.
- `op-1` **Compose sky / fog / clouds**: Apply the configured atmosphere and participating-media contributions with appropriate depth and ordering. The linked late cloud call is one selected path; earlier async/volumetric-target cloud work can supply inputs instead. Related functions: call-0, call-1, call-4.
- `op-2` **Underwater subset**: Where needed, render translucent objects that must be visible through water before the water composite. Related functions: call-2.
- `op-3` **Shade / compose water**: Combine water BRDF, absorption/scattering, reflection and refraction using its dedicated inputs; ordinary translucency follows its own policy. Related functions: call-3.

Edges (control/data prerequisites):

- `op-0` → `op-1`
- `op-1` → `op-2`
- `op-2` → `op-3`

Scene-input sections: [environment](scene-types.html#environment), [lighting](scene-types.html#lighting), [geometry](scene-types.html#geometry).

## Translucency & scene-color effects (translucency)

Translucency is classified into rendering/composition groups. It can use forward-style surface/volume lighting, sorting/OIT and separate render targets rather than opaque deferred material storage.

### Selected functions

- `call-0` **RenderFrontLayerTranslucency** — Call site. Builds a front-layer representation for lighting/reflections after direct lights in this main path. Context: Selected stage path. Source: `Engine/Source/Runtime/Renderer/Private/DeferredShadingRenderer.cpp:3496`.
- `call-1` **FDeferredShadingSceneRenderer::RenderTranslucency** — Call site. Renders the selected ordinary translucency view set. Context: Selected stage path. Source: `Engine/Source/Runtime/Renderer/Private/DeferredShadingRenderer.cpp:4011`.
- `call-2` **RenderTranslucencyViewInner** — Helper occurrence. Inner helper used by translucent view rendering; actual pass group/blend/OIT policy determines its work. Context: Selected stage path. Source: `Engine/Source/Runtime/Renderer/Private/TranslucentRendering.cpp:1817`.
- `call-3` **RenderVelocities** — Call site. Provides late translucent velocity when the selected early-velocity path did not do so. Context: Selected stage path. Source: `Engine/Source/Runtime/Renderer/Private/DeferredShadingRenderer.cpp:4032`.

### Key operation flow

- `op-0` **Classify transparent groups**: Choose standard/after-DOF/other pass policies, sorting/OIT requirements and front-layer support. Related functions: call-0, call-1.
- `op-1` **Bind lighting & scene inputs**: Bind scene color/depth, refraction, supported GI/reflections and translucency lighting volumes/forward lights. Related functions: call-1.
- `op-2` **Render transparent layers**: Draw the selected groups to scene color or separate targets with correct blend, depth and ordering. Related functions: call-2.
- `op-3` **Velocity & later composition**: Write velocity where required and hand separate-translucency resources to their post-processing composition points. Related functions: call-3, call-1.

Edges (control/data prerequisites):

- `op-0` → `op-1`
- `op-1` → `op-2`
- `op-2` → `op-3`

Scene-input sections: [materials](scene-types.html#materials), [environment](scene-types.html#environment), [views](scene-types.html#views).

## Temporal reconstruction & post (post)

Post-processing is a feature-selected pass sequence over the rendered scene. Temporal upscalers consume real motion/history inputs; exposure and tone mapping define the relationship between scene radiance and final output pixels.

### Selected functions

- `call-0` **AddPostProcessingPasses** — Call site. Builds the selected post-processing pipeline per main view. Context: Selected stage path. Source: `Engine/Source/Runtime/Renderer/Private/DeferredShadingRenderer.cpp:4311`.
- `call-1` **AddMotionBlurPass** — Call site. Runs motion blur where enabled in the selected sequence. Context: Selected stage path. Source: `Engine/Source/Runtime/Renderer/Private/PostProcess/PostProcessing.cpp:1269`.
- `call-2` **AddBloomSetupPass** — Call site. Prepares bloom input/downsampling for the configured bloom path. Context: Selected stage path. Source: `Engine/Source/Runtime/Renderer/Private/PostProcess/PostProcessing.cpp:1555`.
- `call-3` **AddTonemapPass** — Call site. Applies configured tone/output transformation in the main post-processing sequence. Context: Selected stage path. Source: `Engine/Source/Runtime/Renderer/Private/PostProcess/PostProcessing.cpp:1629`.
- `call-4` **DiaphragmDOF::AddPasses** — Call site. Adds enabled diaphragm depth of field before the main temporal upscaler. Separate translucency composition depends on DOF/TSR/material settings. Context: Selected stage path. Source: `Engine/Source/Runtime/Renderer/Private/PostProcess/PostProcessing.cpp:976`.
- `call-5` **AddMainTemporalSuperResolutionPasses** — Call site. TSR branch of the main temporal reconstruction selection. Context: Selected stage path. Source: `Engine/Source/Runtime/Renderer/Private/PostProcess/PostProcessing.cpp:1156`.
- `call-6` **AddGen4MainTemporalAAPasses** — Call site. Alternative TAA branch of the same temporal selection. Context: Selected stage path. Source: `Engine/Source/Runtime/Renderer/Private/PostProcess/PostProcessing.cpp:1163`.
- `call-7` **AddThirdPartyTemporalUpscalerPasses** — Call site. Third-party temporal upscaler alternative; it uses the selected interface and compatible previous history. Context: Selected stage path. Source: `Engine/Source/Runtime/Renderer/Private/PostProcess/PostProcessing.cpp:1170`.

### Key operation flow

- `op-0` **Assemble view inputs**: Bind current color/depth/velocity, output rectangle, jitter, exposure and appropriate previous histories. Related functions: call-0.
- `op-1` **Pre-upscale DOF / materials**: Apply enabled before-DOF material work and diaphragm DOF. Separate translucency can compose here or in TSR/later stages according to settings; this is not one universal transparency composition point. Related functions: call-4.
- `op-2` **Temporal reconstruction**: Select one of TSR, TAA or the third-party temporal upscaler (or bypass when disabled); reject disocclusion/cut/invalid history and produce the chosen resolution. Related functions: call-5, call-6, call-7.
- `op-3` **Later image effects**: Apply enabled motion blur and bloom at the illustrated main-sequence points after temporal reconstruction; plugins and visualization modes can alter the selected sequence. Related functions: call-1, call-2.
- `op-4` **Tone / output conversion**: Apply exposure/tone/color/output transfer consistently; retain required history and resolve to the requested output target. Related functions: call-3.

Edges (control/data prerequisites):

- `op-0` → `op-1`
- `op-1` → `op-2`
- `op-2` → `op-3`
- `op-3` → `op-4`

Scene-input sections: [views](scene-types.html#views), [textures](scene-types.html#textures).

## RDG execution, history & output (execute)

Earlier renderer functions mostly declare graph work. FRDGBuilder::Execute resolves/executes that graph and records RHI commands. GPU completion and presentation remain separate lifetime events; the outer scene render builder invokes graph execution.

### Selected functions

- `call-0` **FRDGBuilder::Execute** — Call site. The outer scene render builder executes the graph after renderer pass construction. Context: Selected stage path. Source: `Engine/Source/Runtime/Renderer/Private/SceneRenderBuilder.cpp:772`.
- `call-1` **FRDGBuilder::Execute** — Entry definition. Waits for setup as needed, resolves resource/graph work and executes the compiled pass schedule. Context: Selected stage path. Source: `Engine/Source/Runtime/RenderCore/Private/RenderGraphBuilder.cpp:1766`.
- `call-2` **FRDGBuilder::CollectPassBarriers** — Helper definition. Collects transition/queue barrier work from declared pass resource accesses. Context: Selected stage path. Source: `Engine/Source/Runtime/RenderCore/Private/RenderGraphBuilder.cpp:3872`.
- `call-3` **FRDGBuilder::ExecutePass** — Helper definition. Runs a pass prologue, its callback and epilogue against the command list. Context: Selected stage path. Source: `Engine/Source/Runtime/RenderCore/Private/RenderGraphBuilder.cpp:3511`.

### Key operation flow

- `op-0` **Finish graph construction**: Complete queued setup and define graph outputs/extractions; unused work may be culled under graph rules. Related functions: call-0, call-1.
- `op-1` **Resolve resource schedule**: Determine lifetimes, allocations/aliasing, external accesses and producer/consumer synchronization. Related functions: call-1, call-2.
- `op-2` **Execute pass callbacks**: Emit prologue barriers, draw/dispatch callbacks and epilogue transitions on the appropriate command lists. Related functions: call-3.
- `op-3` **Retain outputs & retire safely**: Extract/import persistent backing resources for history. Submission, GPU fences and presentation govern external lifetime; Execute returning is not proof the GPU finished. Related functions: call-0, call-1.

Edges (control/data prerequisites):

- `op-0` → `op-1`
- `op-1` → `op-2`
- `op-2` → `op-3`

Scene-input sections: [textures](scene-types.html#textures), [updates](scene-types.html#updates), [fidelity](scene-types.html#fidelity).
