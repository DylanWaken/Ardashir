"""Selected source call sites and operation flowcharts for all deferred-frame stages.

Flow edges describe key data/control prerequisites, not a complete C++ call graph.
Names under Calls are functions; shader/RDG event labels are identified separately.
"""
from UnrealGuideContent import SOURCES

FILES = dict(SOURCES, streaming="Engine/Private/Rendering/NaniteStreamingManager.cpp",
    sky="Renderer/Private/SkyAtmosphereRendering.cpp", distance="Renderer/Private/DistanceFieldObjectManagement.cpp",
    translucent="Renderer/Private/TranslucentRendering.cpp")


def call(name, file, query, role, when="Selected stage path", line=None, kind="Call site"):
    return dict(name=name, file=file, query=query, role=role, when=when, line=line, kind=kind)


def op(label, detail, calls=(), gate=""):
    return dict(label=label, detail=detail, calls=list(calls), gate=gate)


def detail(overview, calls, nodes, edges=None, sections=("contract",)):
    return dict(overview=overview, calls=calls, flow=dict(nodes=nodes,
        edges=edges if edges is not None else [(i, i+1, "") for i in range(len(nodes)-1)]), sceneSections=list(sections))


DETAILS = {
"scene-update": detail(
    "CPU scene updates establish coherent render-thread state; GPU Scene then declares uploads for changed records. The CPU functions schedule work and manage ownership, while the uploaded tables are consumed later by GPU passes.", [
        call("FSceneRenderer::OnRenderBegin", "scene", "IVisibilityTaskData* FSceneRenderer::OnRenderBegin", "Coordinates render-begin callbacks, scene update parameters and visibility/update prerequisites.", kind="Entry definition"),
        call("FScene::Update", "scene", "Scene->Update(GraphBuilder, SceneUpdateParameters)", "Applies the queued scene changes at the render boundary; this call belongs to the render-begin update path."),
        call("FGPUScene::Update", "gpu", "void FGPUScene::Update(", "Checks GPU Scene enablement, establishes dynamic-primitive offset and calls UpdateInternal.", kind="Entry definition"),
        call("FGPUScene::UpdateInternal", "gpu", "UpdateInternal(GraphBuilder, ExternalAccessQueue, UpdateTaskPrerequisites", "Builds changed primitive/instance/payload updates and their graph resources/tasks.", line=1796),
        call("FGPUScene::UploadGeneral", "gpu", "void FGPUScene::UploadGeneral(", "Generic data-source adapter upload helper: organizes data transfer into the registered GPU buffers.", kind="Helper definition"),
    ], [op("Receive scene changes", "Collect additions, removals, transforms and material/deformation changes at a coherent frame boundary.", [0]),
        op("Apply and invalidate", "Update persistent scene records and invalidate affected cached draws and feature scene state.", [1]),
        op("Allocate & pack records", "Maintain stable identity separately from packed offsets; prepare current/previous transforms and dirty ranges.", [2,3]),
        op("Declare GPU uploads", "Upload/scatter the changed bytes with dependencies; keep old storage alive for in-flight work.", [4]),
        op("Publish scene bindings", "Bind shader-readable records to views and consumers only after the appropriate update prerequisites.", [2])], sections=("contract","instances","updates")),
"views": detail(
    "View setup is a task pipeline, not one culling loop. It combines per-view relevance with dynamic mesh gathering and pass-command construction; Nanite performs additional GPU visibility later.", [
        call("BeginInitViews", "deferred", "BeginInitViews(", "Starts initialization of deferred views and associated visibility tasks.", line=2123),
        call("FVisibilityTaskData::StartGatherDynamicMeshElements", "visibility", "TaskDatas.VisibilityTaskData->StartGatherDynamicMeshElements();", "Starts the dynamic mesh gathering phase after view setup prerequisites.", line=5950),
        call("FDynamicMeshElementContext::GatherDynamicMeshElementsForPrimitive", "visibility", "void FDynamicMeshElementContext::GatherDynamicMeshElementsForPrimitive", "Asks each eligible primitive for view-dependent mesh elements.", kind="Helper definition"),
        call("FSceneRenderer::SetupMeshPass", "visibility", "SceneRenderer.SetupMeshPass(View, BasePassDepthStencilAccess", "Converts eligible view commands and dynamic elements into pass-specific draw work.", line=4806),
    ], [op("Build view state", "Resolve camera matrices, view rectangle, show flags, jitter and persistent history references.", [0]),
        op("Determine relevance", "Test bounds and view visibility; classify pass relevance and select conventional LODs. Task order must preserve prerequisites.", [0]),
        op("Gather dynamic meshes", "Collect proxy mesh batches for visible dynamic primitives; cached static command data follows its reuse path.", [1,2]),
        op("Build pass commands", "Resolve material/vertex-factory/pass permutations, sort/batch work and prepare instance-culling bindings.", [3])], sections=("views","instances","materials")),
"streaming": detail(
    "Nanite geometry is cooked beforehand. Runtime streaming processes feedback and installs pages while the renderer prepares views; synchronization is required before consumers read updated page mappings.", [
        call("Nanite::FStreamingManager::BeginAsyncUpdate", "deferred", "GStreamingManager.BeginAsyncUpdate", "Starts the streaming update from the main renderer.", line=2048),
        call("Nanite::FStreamingManager::AsyncUpdate", "streaming", "void FStreamingManager::AsyncUpdate()", "Processes the streaming update, including installation of ready pages.", kind="Helper definition"),
        call("Nanite::FStreamingManager::InstallReadyPages", "streaming", "InstallReadyPages(AsyncState.NumReadyOrSkippedPages);", "Installs ready pages or skips resources that are no longer valid.", line=2835),
        call("Nanite::FStreamingManager::EndAsyncUpdate", "streaming", "void FStreamingManager::EndAsyncUpdate(", "Completes/synchronizes streaming work and its graph-side uploads.", kind="Helper definition"),
    ], [op("Read requests & budgets", "Consume earlier GPU feedback and resource changes, respecting page-pool and I/O budgets.", [0,1]),
        op("Install ready pages", "Upload resident geometry/hierarchy changes and repair mappings for live resources; preserve coarser resident coverage.", [2]),
        op("Complete streaming update", "Ensure page/mapping changes are ready before geometry consumers use them.", [3]),
        op("Prepare Nanite consumers", "Use packed view transforms/LOD thresholds and the resident resource state for culling; asset clustering is not repeated each frame.", [0])], sections=("geometry","textures","updates")),
"atmosphere-luts": detail(
    "Atmosphere functions declare compute passes for reusable/view-dependent lighting lookup data. Their early scheduling supplies illumination inputs before final sky/fog color is composited.", [
        call("FSceneRenderer::RenderSkyAtmosphereLookUpTables", "deferred", "RenderSkyAtmosphereLookUpTables(", "Schedules atmosphere lookup work in a selected early slot; another slot is available according to async/configuration.", line=2438),
        call("FComputeShaderUtils::AddPass", "sky", 'RDG_EVENT_NAME("TransmittanceLut")', "Declares the transmittance LUT compute pass; TransmittanceLut is an RDG event label, not a C++ function.", line=1536),
        call("FComputeShaderUtils::AddPass", "sky", 'RDG_EVENT_NAME("MultiScatteringLut")', "Declares multiple-scattering lookup generation.", line=1559),
        call("FComputeShaderUtils::AddPass", "sky", 'RDG_EVENT_NAME("SkyViewLut")', "Declares view-dependent sky lookup generation.", line=1842),
    ], [op("Resolve atmosphere inputs", "Read atmosphere parameters, light state, view state and selected scheduling mode.", [0]),
        op("Transmittance", "Integrate attenuation into a reusable lookup texture.", [1]),
        op("Multiple scattering", "Compute the higher-order scattering approximation used by later atmosphere evaluation.", [2]),
        op("View lookup / aerial data", "Generate sky-view and camera aerial-perspective resources where enabled; transition them for consumers.", [0,3])], sections=("lighting","environment","views")),
"depth": detail(
    "The conventional prepass selects eligible depth mesh passes and declares raster work. Masking, WPO and the chosen velocity mode must agree with later material shading.", [
        call("FDeferredShadingSceneRenderer::RenderPrePass", "deferred", "RenderPrePass(GraphBuilder, InViews", "Invokes the prepass for the current view range.", line=2464),
        call("FParallelMeshDrawCommandPass::BuildRenderingCommands", "depth", "Pass->BuildRenderingCommands(GraphBuilder, Scene->GPUScene", "Prepares command/instance-culling data for the chosen depth mesh pass.", line=565),
        call("FRDGBuilder::AddDispatchPass", "depth", "GraphBuilder.AddDispatchPass(", "Declares a raster dispatch pass; its lambda later dispatches the prepared depth commands.", line=567),
        call("FParallelMeshDrawCommandPass::Dispatch", "depth", "Pass->Dispatch(DispatchPassBuilder", "Dispatches prepared draw-command work inside the raster pass callback.", line=573),
    ], [op("Choose early-Z coverage", "Select opaque/masked coverage and first/second-stage depth modes; configure depth/stencil.", [0]),
        op("Prepare depth draws", "Select depth-compatible shaders, stream/deformation bindings and GPU instance-culling arguments.", [1]),
        op("Declare raster work", "Record the graph's depth target/accesses and the callback that will issue rendering commands.", [2]),
        op("Execute eligible draws", "Rasterize depth with consistent masking and vertex motion, then make depth/optional velocity available.", [3])], sections=("geometry","materials","instances")),
"trace-scene": detail(
    "Software distance-field tracing and hardware triangle tracing need different scene representations. These branches are alternatives for Lumen fallback tracing, although other renderer features can independently request either representation.", [
        call("PrepareDistanceFieldScene", "deferred", "PrepareDistanceFieldScene(", "Schedules the distance-field scene preparation path when requested.", line=2011),
        call("FDistanceFieldSceneData::UpdateDistanceFieldObjectBuffers", "distance", "DistanceFieldSceneData.UpdateDistanceFieldObjectBuffers(", "Updates distance-field object data for additions/removals and changed scene state.", line=1028),
        call("UpdateGlobalDistanceFieldVolume", "distance", "UpdateGlobalDistanceFieldVolume(GraphBuilder", "Updates per-view global distance-field coverage/clipmaps.", line=1048),
        call("SetupRayTracingRenderingData", "deferred", "SetupRayTracingRenderingData(", "Synchronizes and prepares gathered ray-tracing scene data before a hardware-tracing consumer.", line=3034),
    ], [op("Select trace representation", "Decide which fallback geometry the chosen lighting/reflection method consumes. Screen traces do not cover arbitrary off-screen surfaces.", [0,3]),
        op("Mesh / global SDF", "Update supported mesh/heightfield objects and global clipmap composition from cooked distance-field data.", [1,2], "software"),
        op("Triangle acceleration scene", "Make deformed geometry/BLAS, instance data and TLAS ready; establish build-to-trace barriers and material lookup.", [3], "hardware"),
        op("Publish trace inputs", "Bind the selected representation with matching transforms/material IDs and retain it until all ray consumers complete.", [0,3])],
    edges=[(0,1,"software"),(0,2,"hardware"),(1,3,"SDF ready"),(2,3,"AS ready")], sections=("geometry","materials","fidelity")),
"nanite-visibility": detail(
    "Nanite's main/post algorithm is a conditional recovery flow. Culling chooses visible cluster work; rasterization produces depth/visibility. Material evaluation is a separate stage.", [
        call("FDeferredShadingSceneRenderer::RenderNanite", "deferred", "void FDeferredShadingSceneRenderer::RenderNanite", "Sets up Nanite rendering for deferred views and consumes raster results.", kind="Entry definition"),
        call("Nanite::FRenderer::DrawGeometry", "nanite", "FRenderer::DrawGeometry", "Coordinates culling, raster setup, main pass and optional post-pass recovery.", kind="Entry definition", line=6688),
        call("AddPass_InstanceHierarchyAndClusterCull", "nanite", "AddPass_InstanceHierarchyAndClusterCull( Configuration.bTwoPassOcclusion", "Declares main occlusion culling or the no-occlusion variant.", line=7008),
        call("AddPass_Rasterize", "nanite", "MainPassBinning = AddPass_Rasterize(", "Bins/rasterizes main visible work using configured hardware/software raster paths.", line=7010),
        call("BuildHZBFurthest", "nanite", "BuildHZBFurthest(", "Builds updated occlusion data from current depth/visibility in the non-VSM recovery branch.", line=7053),
        call("AddPass_InstanceHierarchyAndClusterCull", "nanite", "AddPass_InstanceHierarchyAndClusterCull( CULLING_PASS_OCCLUSION_POST );", "Re-tests rejected candidates; the subsequent AddPass_Rasterize renders recovered geometry.", line=7071),
    ], [op("Initialize raster & filter", "Prepare views, resident resources, queues, bins and per-view primitive filtering.", [0,1]),
        op("Main cull & raster", "Use previous HZB only when valid; retain rejected candidates. Rasterize the selected clusters.", [2,3]),
        op("Two-pass enabled?", "A usable previous HZB and configuration permit recovery. Otherwise the no-occlusion/main result continues directly.", [1]),
        op("Build current HZB", "Combine relevant current depth/visibility for a new occlusion test.", [4]),
        op("Post cull & raster", "Re-test previously rejected candidates against current occlusion and rasterize recovered geometry.", [5]),
        op("Export visibility / depth", "Publish winning cluster/triangle visibility, scene depth/stencil metadata and streaming feedback for consumers.", [0,1])],
    edges=[(0,1,""),(1,2,""),(2,3,"yes"),(3,4,""),(4,5,""),(2,5,"no")], sections=("geometry","instances","fidelity")),
"lumen-scene": detail(
    "Lumen's Surface Cache is a persistent parameterization of supported surfaces. Page capture/resampling is budgeted; a surface can exist in the scene yet lack fresh card coverage.", [
        call("FDeferredShadingSceneRenderer::UpdateLumenScene", "deferred", "UpdateLumenScene(", "Runs the scene capture/update work selected for this frame.", line=2894),
        call("ResampleLumenCards", "lumenScene", "ResampleLumenCards(", "Resamples card data when pages/representations change so valid information can be reused.", line=2635),
        call("DilateCardPageOneTexel", "lumenScene", "DilateCardPageOneTexel(", "Dilates captured card-page borders before updating the destination atlases.", line=3097),
    ], [op("Select card/page updates", "Apply primitive changes, maintain card coverage and prioritize captures under the per-frame budget.", [0]),
        op("Allocate / resample", "Allocate or reuse surface pages and resample valid old data where possible.", [1]),
        op("Capture material surfaces", "Render selected geometry/materials into albedo, normal, emissive and depth capture atlases; supported Nanite geometry can accelerate capture.", [0]),
        op("Dilate and publish", "Fill page borders, update mappings/atlases and invalidate stale lighting for changed surfaces.", [2])], sections=("geometry","materials","fidelity")),
"lumen-lighting": detail(
    "This is cached-surface lighting, not final-gather lighting of the current GBuffer. In the pinned main frame it is called before RenderBasePass; hardware shadow/trace inputs must be ready if that mode is chosen.", [
        call("RenderLumenSceneLighting", "deferred", "RenderLumenSceneLighting(", "Schedules lighting of the maintained Lumen surface scene.", line=3041),
        call("Lumen::BuildCardUpdateContext", "lumenLighting", "Lumen::BuildCardUpdateContext(", "Builds separate direct/indirect card update work from budgets/history validity.", line=276),
        call("RenderDirectLightingForLumenScene", "lumenLighting", "RenderDirectLightingForLumenScene(", "Lights selected cached surface pages using the scene's light/shadow inputs.", line=293),
        call("RenderRadiosityForLumenScene", "lumenLighting", "RenderRadiosityForLumenScene(", "Updates cached indirect/radiosity lighting for the selected surface pages.", line=300),
    ], [op("Prepare scene-lighting inputs", "Require valid card mappings/material captures, lighting inputs and any chosen ray-tracing resources.", [0]),
        op("Build update contexts", "Choose direct and indirect page work according to budgets and history validity.", [1]),
        op("Update direct lighting", "Evaluate light contribution/visibility on selected cached surfaces.", [2]),
        op("Update radiosity", "Reuse/trace indirect lighting on the selected cache pages and publish valid lighting atlases.", [3])], sections=("lighting","environment","fidelity")),
"gbuffer": detail(
    "Conventional mesh draws and Nanite material evaluation converge on deferred surface data. The main RenderBasePass call occurs after Lumen scene lighting; DBuffer/material/deformation inputs must be ready before their consumers.", [
        call("FDeferredShadingSceneRenderer::RenderBasePass", "deferred", "RenderBasePass(*this, GraphBuilder, Views, SceneTextures", "Starts base-pass rendering for the main deferred views.", line=3059),
        call("FDeferredShadingSceneRenderer::RenderBasePassInternal", "base", "RenderBasePassInternal(Renderer, GraphBuilder", "Routes view/material rendering work, including the Nanite path where enabled.", line=1146),
        call("Nanite::DispatchBasePass", "base", "Nanite::DispatchBasePass(", "Dispatches Nanite material shading from raster results and shading commands.", line=1396),
        call("Nanite::DispatchBasePass", "naniteMaterials", "void DispatchBasePass(", "Implementation entry for Nanite shading-bin/work dispatch and GBuffer material output.", line=1670, kind="Helper definition"),
    ], [op("Bind surface inputs", "Bind depth, material textures/uniforms, deformation, lighting policy and pre-base-pass decal data.", [0]),
        op("Conventional material draws", "Evaluate eligible mesh materials and write the configured GBuffer/Substrate targets and optional velocity.", [1]),
        op("Nanite material dispatch", "Decode geometry visibility, generate/use shading-bin work and evaluate visible material samples.", [2,3], "nanite"),
        op("Publish deferred surfaces", "Provide consistent depth, normals/material closures, emissive and other surface outputs for lighting consumers.", [0])],
    edges=[(0,1,"conventional"),(0,2,"Nanite enabled"),(1,3,""),(2,3,"")], sections=("materials","geometry","textures")),
"gi": detail(
    "The screen-probe gather consumes current surface data and the maintained trace/lighting scene. It distributes samples, traces, filters and integrates them; none of those is equivalent to lighting the Surface Cache itself.", [
        call("LumenRadianceCache::UpdateRadianceCaches", "lumenGather", "LumenRadianceCache::UpdateRadianceCaches(", "Updates reusable world-space radiance probes requested by the gather/translucency consumers.", line=2590),
        call("GenerateImportanceSamplingRays", "lumenGather", "GenerateImportanceSamplingRays(", "Chooses gather rays according to the enabled importance-sampling policy.", line=2624),
        call("TraceScreenProbes", "lumenGather", "TraceScreenProbes(", "Traces the probe rays using screen information and the configured fallback path.", line=2647),
        call("FilterScreenProbes", "lumenGather", "FilterScreenProbes(GraphBuilder, View", "Filters probe radiance before integration.", line=2661),
        call("InterpolateAndIntegrate", "lumenGather", "InterpolateAndIntegrate(", "Integrates/interpolates probe lighting onto current surfaces and rough-specular outputs.", line=2711),
        call("RenderHardwareRayTracingScreenProbe", "lumenTrace", "RenderHardwareRayTracingScreenProbe", "Hardware fallback helper used for unresolved probe rays; cache/sky completion is still separate.", line=797),
    ], [op("Place probes & update cache", "Place view-dependent probe samples and update reusable radiance-cache coverage as configured.", [0]),
        op("Choose ray samples", "Use importance sampling where enabled; generate ray work with current material/normal inputs.", [1]),
        op("Trace screen + fallback", "Attempt eligible screen hits, then selected hardware triangles or software distance fields; shade/cache-complete remaining rays.", [2,5]),
        op("Filter probe radiance", "Filter noisy probe samples while respecting geometric validity; optional short-range AO has separate work.", [3]),
        op("Integrate to surfaces", "Interpolate diffuse/rough-specular lighting, preserving history rejection and material closure requirements.", [4])], sections=("materials","lighting","views","fidelity")),
"receiver-depth": detail(
    "Water/front-layer receiver setup exists because shadow page demand and later reflections can need depths beyond opaque surfaces. These are early receiver representations, separate from final transparent/water color composition.", [
        call("RenderSingleLayerWaterDepthPrepass", "deferred", "RenderSingleLayerWaterDepthPrepass(", "Runs the water depth prepass in the configured earlier slot.", line=2888),
        call("RenderSingleLayerWaterDepthPrepass", "deferred", "RenderSingleLayerWaterDepthPrepass(", "Alternative after-base-pass slot; these two call sites are not mandatory duplicate draws.", line=3221),
        call("RenderFrontLayerTranslucency", "deferred", "RenderFrontLayerTranslucency(GraphBuilder, Views, SceneTextures, MegaLightsContext, true", "Generates front-layer data for VSM page marking.", line=3272),
        call("BeginMarkVirtualShadowMapPages", "deferred", "ShadowSceneRenderer.BeginMarkVirtualShadowMapPages(", "Consumes water/front-layer receiver inputs to start VSM demand generation.", line=3273),
    ], [op("Select receiver needs", "Decide if water depth/front-layer data are required by enabled features and materials.", [0,1,2]),
        op("Water receiver depth", "Render water receiver depth in its selected prepass location, not automatically in both listed locations.", [0,1]),
        op("Front-layer receiver data", "Generate the relevant nearest transparent layer for page requests.", [2]),
        op("Mark shadow-page demand", "Feed receiver data into VSM marking; conventional shadow maps use a different allocation path.", [3], "vsm")], sections=("geometry","materials","environment")),
"reflections": detail(
    "Reflection work is specular surface-ray work with tile classification, ray generation, tracing and reconstruction. It is distinct from diffuse screen probes; front-layer translucency also has a separate variant.", [
        call("FDeferredShadingSceneRenderer::RenderLumenReflections", "lumenReflections", "FDeferredShadingSceneRenderer::RenderLumenReflections(", "Initializes the reflection pass and its tracing/reconstruction resources.", line=1215, kind="Entry definition"),
        call("FComputeShaderUtils::AddPass", "lumenReflections", "FComputeShaderUtils::AddPass(", "Declares ray-generation compute work using FReflectionGenerateRaysCS.", line=1425),
        call("TraceReflections", "lumenReflections", "TraceReflections(", "Dispatches the selected reflection tracing path after rays/tiles are ready.", line=1470),
        call("FComputeShaderUtils::AddPass", "lumenReflections", "FComputeShaderUtils::AddPass(", "Declares reflection resolve using FLumenReflectionResolveCS; shader type and RDG event are not C++ entry-point names.", line=1626),
    ], [op("Classify specular work", "Choose reflection tiles/closures and roughness/resolution policy; prepare indirect work arguments.", [0]),
        op("Generate reflection rays", "Generate directions/trace inputs from surfaces and reflection settings.", [1]),
        op("Screen + fallback trace", "Resolve eligible screen hits and trace remaining rays against the selected software/hardware representation; choose cache lookup or Hit Lighting separately.", [2]),
        op("Resolve & reconstruct", "Resolve traced samples at the target resolution and reconstruct neighboring sample contributions where configured.", [3]),
        op("Temporal / spatial filter", "Reject invalid history, accumulate/filter valid lighting, and publish specular-indirect results for composition.", [0])], sections=("materials","views","fidelity")),
"shadows": detail(
    "VSM allocates shadow storage from receiver demand and cache state. Conventional shadow maps allocate per-light views instead. They share a need for caster geometry/deformation and consistent depth/material coverage.", [
        call("BeginMarkVirtualShadowMapPages", "deferred", "ShadowSceneRenderer.BeginMarkVirtualShadowMapPages(", "Starts VSM receiver-page requests.", line=3273),
        call("FVirtualShadowMapArray::BuildPageAllocations", "vsm", "void FVirtualShadowMapArray::BuildPageAllocations(", "Builds allocation/reuse work for virtual-to-physical shadow pages.", line=3227, kind="Helper definition"),
        call("RenderShadowDepthMaps", "deferred", "RenderShadowDepthMaps(", "Schedules shadow depth work in the late slot when it was not rendered early.", line=3291),
        call("RenderVirtualShadowMaps", "shadows", "ShadowSceneRenderer->RenderVirtualShadowMaps(GraphBuilder, bNaniteEnabled);", "Invokes the VSM rendering path from shadow-depth rendering.", line=1877),
    ], [op("Select shadow path", "Resolve shadow-enabled lights, caster/receiver policy and chosen storage method.", [2]),
        op("Request / allocate pages", "For VSM, mark receiver demand, reuse valid cached pages, allocate replacements and invalidate changed casters/lights.", [0,1], "vsm"),
        op("Allocate light depth views", "For conventional maps, allocate per-light/cascade depth targets and views; early scheduling is conditional on dependencies.", [2], "conventional"),
        op("Rasterize required depth", "Draw dirty VSM pages or conventional light views using conventional/Nanite caster representations as supported.", [2,3]),
        op("Publish shadow sampling", "Make depth/page mappings and projection parameters ready for light visibility evaluation.", [2])],
    edges=[(0,1,"VSM"),(0,2,"conventional"),(1,3,""),(2,3,""),(3,4,"")], sections=("lighting","geometry","materials","fidelity")),
"decals-ao": detail(
    "This scheduling window contains several conditional surface updates; it is not one immutable position for all decals or velocities. Async Lumen is dispatched only after its required inputs are ready.", [
        call("CompositionLighting.ProcessAfterBasePass", "deferred", "CompositionLighting.ProcessAfterBasePass", "Earlier after-base-pass preparation used by the applicable stochastic-lighting path.", line=3198),
        call("DispatchAsyncLumenIndirectLightingWork", "deferred", "DispatchAsyncLumenIndirectLightingWork(", "Dispatches eligible indirect work using prepared surfaces/scene inputs.", line=3248),
        call("RenderVelocities", "deferred", "RenderVelocities(GraphBuilder, Views, SceneTextures, EVelocityPass::Opaque", "Writes late opaque velocity when depth/base pass did not already provide the chosen mode.", line=3374),
        call("CompositionLighting.ProcessAfterBasePass", "deferred", "CompositionLighting.ProcessAfterBasePass", "Later after-base-pass decal/AO work for the applicable remaining path.", line=3391),
    ], [op("Resolve surface-update policy", "DBuffer belongs before material evaluation; after-base-pass decals/AO and velocity select their configured slots.", [0,3]),
        op("Prepare async inputs", "Complete the surface data required by the selected stochastic/indirect consumers before they start.", [0]),
        op("Dispatch eligible Lumen", "Schedule compute/async indirect work with real dependencies; turning async on does not create missing GBuffer or trace data.", [1], "async"),
        op("Finish remaining updates", "Perform required late velocity/decal/AO work without overwriting an incompatible temporal or material representation.", [2,3])], sections=("materials","views","updates")),
"direct": detail(
    "Deferred direct lighting evaluates supported lights against current surface data and shadow resources. The sorted light set chooses suitable paths; MegaLights and hair lighting are conditional additional paths, not automatic replacements for all conventional lighting.", [
        call("FDeferredShadingSceneRenderer::RenderLights", "deferred", "RenderLights(GraphBuilder, SceneTextures, LightingChannelsTexture, SortedLightSet);", "Starts conventional deferred light rendering for the sorted visible light set.", line=3467),
        call("RenderLightFunction", "lights", "const bool bLightFunctionRendered = RenderLightFunction(GraphBuilder", "Applies per-light material modulation where required in the shadowed-light path.", line=2379),
        call("RenderLight", "lights", "RenderLight(GraphBuilder, Scene, View, SceneTextures, LightSceneInfo, nullptr, LightingChannelsTexture,", "Schedules an individual applicable deferred-light draw.", line=1735),
        call("RenderMegaLights", "deferred", "RenderMegaLights(", "Runs the configured MegaLights path when its context is valid.", line=3471),
    ], [op("Classify visible lights", "Read sorted light types/features and select conventional or configured specialized paths.", [0]),
        op("Prepare visibility / modulation", "Bind shadow masks/maps, channels, IES/source-shape data and light functions as required.", [1]),
        op("Evaluate direct BRDF", "Apply the selected material/closure lighting model and accumulate direct illumination into the scene outputs.", [2]),
        op("Complete specialized lights", "Run enabled MegaLights or other dedicated contributions with their own resource contracts; avoid double-counting.", [3])], sections=("lighting","materials")),
"composite": detail(
    "The async path can overlap indirect computation with direct lights, but final composition is a join. Non-async and visualization paths use different calls/flags; the published source anchors identify the main regular Lumen composition point.", [
        call("DispatchAsyncLumenIndirectLightingWork", "indirect", "DispatchAsyncLumenIndirectLightingWork", "Prepares async indirect outputs that later composition consumes.", line=990, kind="Entry definition"),
        call("RenderDiffuseIndirectAndAmbientOcclusion", "deferred", "RenderDiffuseIndirectAndAmbientOcclusion(", "Earlier non-regular-composite work is invoked with bCompositeRegularLumenOnly=false.", line=3429),
        call("RenderDiffuseIndirectAndAmbientOcclusion", "deferred", "RenderDiffuseIndirectAndAmbientOcclusion(", "After RenderLights, joins regular Lumen indirect results with bCompositeRegularLumenOnly=true.", line=3518),
        call("RenderDeferredReflectionsAndSkyLighting", "deferred", "RenderDeferredReflectionsAndSkyLighting(", "Composes deferred reflection/sky contributions according to the chosen methods.", line=3528),
        call("RenderLights", "deferred", "RenderLights(GraphBuilder, SceneTextures, LightingChannelsTexture, SortedLightSet);", "Produces the direct-light scene contribution between the earlier indirect/AO call and the later regular Lumen composite.", line=3467),
    ], [op("Indirect work in flight", "Eligible gather/reflection work runs once its inputs are available; preserve its output handles and dependencies.", [0], "async"),
        op("Earlier indirect / AO", "The earlier call uses bCompositeRegularLumenOnly=false. It handles applicable non-async/non-regular indirect and AO work before direct lighting; it is not the direct-light producer.", [1]),
        op("Direct-lit scene ready", "RenderLights produces the direct contribution after earlier indirect/AO processing. Scene color and relevant surface buffers form the other inputs to the join.", [4]),
        op("Join & composite indirect", "Wait through graph dependencies for required indirect outputs before reading/combining them; serial paths still need equivalent data contracts.", [2]),
        op("Reflection / sky blend", "Combine the chosen specular/environment terms and any required material-specific composition without double-counting.", [3])],
    edges=[(0,3,"indirect ready"),(1,2,"earlier work"),(2,3,"scene color ready"),(3,4,"")], sections=("lighting","materials","fidelity")),
"atmosphere": detail(
    "Final atmosphere/media/water composition consumes earlier lighting/depth data. Underwater and cloud paths alter scheduling, so the flow summarizes major dependencies rather than claiming one order for every material and camera state.", [
        call("RenderSkyAtmosphere", "deferred", "RenderSkyAtmosphere(", "Draws/composes sky and aerial-perspective contributions for the applicable mode.", line=3784),
        call("RenderFog", "deferred", "RenderFog(", "Composes configured fog contribution into scene color.", line=3822),
        call("RenderTranslucency", "deferred", "RenderTranslucency(*this, GraphBuilder", "Renders the underwater translucency subset before water where this branch is active.", line=3876),
        call("RenderSingleLayerWater", "deferred", "RenderSingleLayerWater(", "Runs water surface lighting/refraction/composition with prepared depth and scene-without-water inputs.", line=3880),
        call("RenderVolumetricCloud", "deferred", "RenderVolumetricCloud(GraphBuilder, SceneTextures, bSkipVolumetricRenderTarget, bSkipPerPixelTracing,", "Declares cloud rendering in the main late path. Other modes use earlier asynchronous/volumetric-target work; cloud rendering and composition depend on the selected target mode.", line=3851),
    ], [op("Prepare media inputs", "Use lit scene color/depth, atmosphere LUTs, cloud/fog data and camera-underwater state.", [0,1]),
        op("Compose sky / fog / clouds", "Apply the configured atmosphere and participating-media contributions with appropriate depth and ordering. The linked late cloud call is one selected path; earlier async/volumetric-target cloud work can supply inputs instead.", [0,1,4]),
        op("Underwater subset", "Where needed, render translucent objects that must be visible through water before the water composite.", [2]),
        op("Shade / compose water", "Combine water BRDF, absorption/scattering, reflection and refraction using its dedicated inputs; ordinary translucency follows its own policy.", [3])], sections=("environment","lighting","geometry")),
"translucency": detail(
    "Translucency is classified into rendering/composition groups. It can use forward-style surface/volume lighting, sorting/OIT and separate render targets rather than opaque deferred material storage.", [
        call("RenderFrontLayerTranslucency", "deferred", "RenderFrontLayerTranslucency(GraphBuilder, Views, SceneTextures, MegaLightsContext, false", "Builds a front-layer representation for lighting/reflections after direct lights in this main path.", line=3496),
        call("FDeferredShadingSceneRenderer::RenderTranslucency", "deferred", "RenderTranslucency(*this, GraphBuilder", "Renders the selected ordinary translucency view set.", line=4011),
        call("RenderTranslucencyViewInner", "translucent", "RenderTranslucencyViewInner(", "Inner helper used by translucent view rendering; actual pass group/blend/OIT policy determines its work.", kind="Helper occurrence", line=1817),
        call("RenderVelocities", "deferred", "RenderVelocities(GraphBuilder, Views, SceneTextures, EVelocityPass::Translucent, false);", "Provides late translucent velocity when the selected early-velocity path did not do so.", line=4032),
    ], [op("Classify transparent groups", "Choose standard/after-DOF/other pass policies, sorting/OIT requirements and front-layer support.", [0,1]),
        op("Bind lighting & scene inputs", "Bind scene color/depth, refraction, supported GI/reflections and translucency lighting volumes/forward lights.", [1]),
        op("Render transparent layers", "Draw the selected groups to scene color or separate targets with correct blend, depth and ordering.", [2]),
        op("Velocity & later composition", "Write velocity where required and hand separate-translucency resources to their post-processing composition points.", [3,1])], sections=("materials","environment","views")),
"post": detail(
    "Post-processing is a feature-selected pass sequence over the rendered scene. Temporal upscalers consume real motion/history inputs; exposure and tone mapping define the relationship between scene radiance and final output pixels.", [
        call("AddPostProcessingPasses", "deferred", "AddPostProcessingPasses(", "Builds the selected post-processing pipeline per main view.", line=4311),
        call("AddMotionBlurPass", "post", "FMotionBlurOutputs PassOutputs = AddMotionBlurPass(", "Runs motion blur where enabled in the selected sequence.", line=1269),
        call("AddBloomSetupPass", "post", "AddBloomSetupPass(GraphBuilder, View, SetupPassInputs)", "Prepares bloom input/downsampling for the configured bloom path.", line=1555),
        call("AddTonemapPass", "post", "SceneColor = AddTonemapPass(GraphBuilder, View, PassInputs);", "Applies configured tone/output transformation in the main post-processing sequence.", line=1629),
        call("DiaphragmDOF::AddPasses", "post", "if (DiaphragmDOF::AddPasses(", "Adds enabled diaphragm depth of field before the main temporal upscaler. Separate translucency composition depends on DOF/TSR/material settings.", line=976),
        call("AddMainTemporalSuperResolutionPasses", "post", "Outputs = AddMainTemporalSuperResolutionPasses(", "TSR branch of the main temporal reconstruction selection.", line=1156),
        call("AddGen4MainTemporalAAPasses", "post", "Outputs = AddGen4MainTemporalAAPasses(", "Alternative TAA branch of the same temporal selection.", line=1163),
        call("AddThirdPartyTemporalUpscalerPasses", "post", "Outputs = AddThirdPartyTemporalUpscalerPasses(", "Third-party temporal upscaler alternative; it uses the selected interface and compatible previous history.", line=1170),
    ], [op("Assemble view inputs", "Bind current color/depth/velocity, output rectangle, jitter, exposure and appropriate previous histories.", [0]),
        op("Pre-upscale DOF / materials", "Apply enabled before-DOF material work and diaphragm DOF. Separate translucency can compose here or in TSR/later stages according to settings; this is not one universal transparency composition point.", [4]),
        op("Temporal reconstruction", "Select one of TSR, TAA or the third-party temporal upscaler (or bypass when disabled); reject disocclusion/cut/invalid history and produce the chosen resolution.", [5,6,7]),
        op("Later image effects", "Apply enabled motion blur and bloom at the illustrated main-sequence points after temporal reconstruction; plugins and visualization modes can alter the selected sequence.", [1,2]),
        op("Tone / output conversion", "Apply exposure/tone/color/output transfer consistently; retain required history and resolve to the requested output target.", [3])], sections=("views","textures")),
"execute": detail(
    "Earlier renderer functions mostly declare graph work. FRDGBuilder::Execute resolves/executes that graph and records RHI commands. GPU completion and presentation remain separate lifetime events; the outer scene render builder invokes graph execution.", [
        call("FRDGBuilder::Execute", "builder", "GraphBuilder.Execute();", "The outer scene render builder executes the graph after renderer pass construction.", line=772),
        call("FRDGBuilder::Execute", "rdg", "void FRDGBuilder::Execute()", "Waits for setup as needed, resolves resource/graph work and executes the compiled pass schedule.", line=1766, kind="Entry definition"),
        call("FRDGBuilder::CollectPassBarriers", "rdg", "void FRDGBuilder::CollectPassBarriers()", "Collects transition/queue barrier work from declared pass resource accesses.", line=3872, kind="Helper definition"),
        call("FRDGBuilder::ExecutePass", "rdg", "void FRDGBuilder::ExecutePass(", "Runs a pass prologue, its callback and epilogue against the command list.", line=3511, kind="Helper definition"),
    ], [op("Finish graph construction", "Complete queued setup and define graph outputs/extractions; unused work may be culled under graph rules.", [0,1]),
        op("Resolve resource schedule", "Determine lifetimes, allocations/aliasing, external accesses and producer/consumer synchronization.", [1,2]),
        op("Execute pass callbacks", "Emit prologue barriers, draw/dispatch callbacks and epilogue transitions on the appropriate command lists.", [3]),
        op("Retain outputs & retire safely", "Extract/import persistent backing resources for history. Submission, GPU fences and presentation govern external lifetime; Execute returning is not proof the GPU finished.", [0,1])], sections=("textures","updates","fidelity")),
}
