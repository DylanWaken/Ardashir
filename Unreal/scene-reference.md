# Unreal scene representation for an independent renderer

Source: Unreal Engine 5.8.1, commit `71fe36aac5a8df5ccd66c763ffc902b29b6a9c43`.

This is an authored integration/implementation guide inferred from source responsibilities. Suggested records, conversion choices and acceptance tests are not a public Unreal export ABI or claims of implemented Ardashir support. Source anchors are one-based and checked against the pinned local checkout. The interactive page is [scene-types.html](scene-types.html).

## Define the Unreal → renderer contract (contract)

Extract semantic render data at a known frame boundary, then let your renderer own its scene and GPU realization.

### Choose the integration boundary

Two useful modes are an in-process Unreal adapter consuming render-thread snapshots/resources, or an exported/copied engine-neutral scene. The required semantic records are similar; resource access, shader reuse and synchronization differ.

**Data to carry**

- Producer/version and Unreal revision; feature/platform settings; snapshot time; stable scene/asset/object IDs; supported capability manifest.
- For copied export: bytes/formats and dependencies. For borrowed GPU resources: device identity, lifetime lease, view/subresource range, ready/completion fences and state/queue handoff.

**Work / conversion policy**

- Implement extraction inside Unreal for live/cooked scenes; a .umap plus .uasset filenames is not a universal decoded scene format.
- A borrowed Unreal texture or FRHIBuffer does not make Unreal material shaders compatible with your shader ABI. Device sharing and material execution are separate contracts.
- Keep the producer adapter distinct from the renderer's own database. The existing ArdaScene plan describes immutable semantic snapshots; this page is an integration requirements guide, not a claim that the Unreal adapter already exists.

Source: `Engine/Source/Runtime/Engine/Public/SceneInterface.h:119` — `class FSceneInterface`.

### Proposed record families

These names are a suggested neutral schema, not Unreal class layouts or new implemented ArdaScene APIs.

**Data to carry**

- SceneSnapshot: revision, time, world origin, active views, object records and capability/omission report.
- GeometryRecord + DeformationRecord: shared streams/topology and evaluated deformation inputs/results.
- PrimitiveRecord + InstanceRecord: placement, bounds, flags, section/material bindings and current/previous state.
- MaterialRecord + TextureRecord + SamplerRecord: executable or converted surface behavior and resource bindings.
- LightRecord + EnvironmentRecord + DecalRecord + VolumeRecord: illumination, media and non-mesh scene effects.
- ViewRecord + RenderSettings + UpdateBatch: camera/output/temporal state, feature choices and versioned changes.

**Work / conversion policy**

- Keep semantic fields independent from GPU packing, bindless indices, PSOs and acceleration-structure allocation.
- Separate shared assets from placements; represent section-level material bindings and instance overrides.
- Version every reference and retain dependencies until all consumers of a snapshot have finished.


### Coordinates, units & precision

Convert coordinate conventions once at the adapter boundary and document the result.

**Data to carry**

- Unreal world coordinates use left-handed, Z-up conventions and centimeters; asset/local coordinates and imported source axes still need their recorded transforms.
- World/local transforms, world-origin offset and high-precision positions; normal/tangent basis and handedness; triangle winding; negative/nonuniform scale.
- Current/previous world-to-view and projection matrices; translated-world origins; depth range/reversed-Z policy; viewport and exposure conventions.

**Work / conversion policy**

- Use a consistent large-world origin/high-precision strategy and camera-relative GPU coordinates. Do not cast large absolute world positions directly to float without a policy.
- Transform normals correctly under nonuniform scale and reconcile mirrored winding/culling. Test a known orientation/scale scene.
- Choose a canonical linear working color space and explicit physical-light/exposure conversion; numeric values alone do not guarantee visual equivalence.

Source: `Engine/Source/Runtime/Engine/Public/SceneView.h:319` — `struct FViewMatrices`.
[Epic: coordinate system and spaces](https://dev.epicgames.com/documentation/unreal-engine/coordinate-system-and-spaces-in-unreal-engine).
[Epic: units of measurement](https://dev.epicgames.com/documentation/en-us/unreal-engine/units-of-measurement-in-unreal-engine).

## Geometry representations you must recognize (geometry)

Each family needs an export/conversion handler. The first implementation may support a subset, but must report every approximation or omission.

### Triangle mesh

Renders a static mesh asset using section/material/LOD data. Conventional rendering reads shared mesh streams; eligible components can instead select a Nanite proxy.

**Data to carry**

- Asset ID/revision; LODs; position/tangent/normal/UV/color layouts; index width and ranges; section-to-material slots; local bounds; winding and mirrored-transform policy.

**Work / conversion policy**

- Export a cooked, available conventional render LOD. Preserve seams and section ranges; share geometry among placements. CPU access to cooked buffers is a cook/runtime policy, not guaranteed by possessing a UStaticMesh.

Unreal examples: `FStaticMeshSceneProxy`, `UStaticMeshComponent`.

Storage example: FStaticMeshLODResources / FStaticMeshVertexBuffers

GPU data: Position, tangent/normal and UV streams, optional vertex colors; primary and depth-only index buffers plus optional reversed/wireframe indices. Section ranges select triangles.

CPU boundary: LOD and section metadata and FPositionVertexBuffer/FStaticMeshVertexBuffer/FColorVertexBuffer/FRawStaticIndexBuffer wrappers live on the CPU and initialize/bind the actual GPU buffers.

Source: `Engine/Source/Runtime/Engine/Public/StaticMeshResources.h:247` — `struct FStaticMeshLODResources`.

### Instancing, HISM, foliage & grass

Reuses one mesh asset for many transformed instances; HISM adds hierarchical instance organization and foliage derives from the HISM component branch.

**Data to carry**

- Shared geometry ID; stable instance IDs; current/previous transforms; per-instance custom values; bounds; visibility/fade/LOD state; material overrides.

**Work / conversion policy**

- Resolve HISM/foliage/grass into instance records. Do not reuse compact Unreal array indices as permanent IDs. Instance order can change after insert/remove/compaction.

Unreal examples: `FHierarchicalStaticMeshSceneProxy`, `FInstancedStaticMeshSceneProxy`, `UHierarchicalInstancedStaticMeshComponent`, `UInstancedStaticMeshComponent`.

Storage example: FInstanceSceneDataBuffers + shared static-mesh streams

GPU data: GPU Scene instance records/payloads contain transforms, bounds and optional custom data; shared vertex/index buffers describe the mesh. The legacy vertex-factory path can use instance vertex streams.

CPU boundary: FInstanceSceneDataBuffers is CPU-side instance storage/upload input. HISM cluster organization is not a separate GPU triangle format.

Source: `Engine/Source/Runtime/Engine/Public/InstanceDataSceneProxy.h:134` — `class FInstanceSceneDataBuffers`.

### Nanite geometry

Connects supported geometry to Nanite's streamed cluster representation. Static and skinned scene proxies are siblings beneath Nanite::FSceneProxyBase.

**Data to carry**

- Chosen fidelity mode; conventional fallback availability/error; or native cluster/page hierarchy, streaming mappings, raster/material bins and deformation inputs.

**Work / conversion policy**

- For an initial independent renderer, explicitly request a usable fallback or offline converted mesh. Record the fidelity loss. Native Nanite data is a virtualized encoding requiring streaming, traversal and material integration; it is not an ordinary index buffer.

Unreal examples: `Nanite::FSceneProxy`, `Nanite::FSceneProxyBase`, `Nanite::FSkinnedSceneProxy`, `ULandscapeNaniteComponent`.

Storage example: Nanite::FResources / Nanite streaming

GPU data: Packed geometry cluster pages, hierarchy nodes and streaming metadata resident in GPU buffers; GPU Scene supplies primitive/instance transforms. Culling generates visible clusters and raster/shading work.

CPU boundary: FResources holds cooked asset/streaming data and resource identity. The streaming manager manages resident GPU allocations. Visibility buffers, candidate queues and shading bins are renderer work/output storage.

Source: `Engine/Source/Runtime/Engine/Public/Rendering/NaniteResources.h:452` — `struct FResources`.

### Skeletal / skinned mesh

Supplies deformable mesh geometry. Skeletal animation, poseable meshes and instanced skinned meshes specialize the component branch; supported assets may choose a Nanite skinned proxy.

**Data to carry**

- Rest streams; bone-index/weight layout; section bone palette; bind/reference pose; current/previous pose or deformed streams; morph weights/deltas; cloth/deformer output; bounds.

**Work / conversion policy**

- Choose whether Unreal supplies final deformed geometry or your renderer evaluates skin/morph/cloth/deformer behavior. Include material WPO separately. Use the same frame's deformation for raster, rays and velocity.

Unreal examples: `FSkeletalMeshSceneProxy`, `USkinnedMeshComponent`.

Storage example: FSkeletalMeshLODRenderData

GPU data: Static position/tangent/UV/color streams, indices, FSkinWeightVertexBuffer bone indices and weights, optional cloth and morph data. Bone transforms drive skinning; GPU Skin Cache can output deformed positions/tangents reused by later passes.

CPU boundary: LOD/section metadata and FSkeletalMeshObject rendering state are CPU objects. Skinning paths and precision/weight layouts vary; current/previous deformation data supports velocity.

Source: `Engine/Source/Runtime/Engine/Public/Rendering/SkeletalMeshLODRenderData.h:152` — `SkinWeightVertexBuffer;`.

### Instanced skinned mesh

Combines skinned mesh rendering with per-instance state and chooses the conventional or Nanite skinned proxy implementation.

**Data to carry**

- Shared skinned geometry; per-instance transform/identity; animation/deformation association; previous state; conventional versus Nanite path.

**Work / conversion policy**

- Preserve which pose/deformation source belongs to which instance. One shared mesh does not imply one shared pose. Query supported runtime representations explicitly.

Unreal examples: `FInstancedSkinnedMeshSceneProxy`, `FNaniteInstancedSkinnedMeshSceneProxy`, `UInstancedSkinnedMeshComponent`.

Storage example: Instanced skinned proxy + skeletal/Nanite data

GPU data: Shared deformable geometry and skinning resources plus instance scene records. The selected path determines conventional streams/skin outputs or Nanite cluster data.

CPU boundary: The component is a USkinnedMeshComponent subclass. FInstancedSkinnedMeshSceneProxy derives FSkeletalMeshSceneProxy, while the Nanite counterpart derives Nanite::FSkinnedSceneProxy.

Source: `Engine/Source/Runtime/Engine/Private/InstancedSkinnedMeshSceneProxy.h:86` — `class FInstancedSkinnedMeshSceneProxy`.

### Spline mesh

Deforms a source mesh along a spline using spline parameters and the selected conventional or Nanite mesh path.

**Data to carry**

- Source mesh; spline endpoints/tangents, scales, roll, offsets and deformation parameters; current/previous deformation; bounds.

**Work / conversion policy**

- Evaluate the spline deformation yourself or receive baked/deformed vertices. Exporting only the source static mesh loses the actual scene shape.

Unreal examples: `FNaniteSplineMeshSceneProxy`, `FSplineMeshSceneProxy`, `USplineMeshComponent`.

Storage example: FSplineMeshSceneProxyCommon + source mesh resources

GPU data: Source geometry buffers plus spline deformation parameters used during rendering. Ray-tracing deformation may require updated geometry buffers.

CPU boundary: Spline control state and proxy helper/mixin remain CPU objects; changing a spline does not imply a permanent duplicated mesh asset.

Source: `Engine/Source/Runtime/Engine/Public/SplineMeshSceneProxy.h:97` — `FSplineMeshSceneProxyCommon`.

### Landscape terrain

Represents terrain sections using shared grid geometry and sampled landscape height/normal/weight textures.

**Data to carry**

- Section/grid transform; height/normal textures; layer weights, visibility holes and material layer mapping; LOD/neighbor stitching; runtime virtual texture dependencies.

**Work / conversion policy**

- Implement heightfield sampling and layer shading, or tessellate/bake a declared terrain representation. Treat Nanite Landscape and landscape mesh proxies according to their selected geometry path.

Unreal examples: `FLandscapeComponentSceneProxy`, `ULandscapeComponent`.

Storage example: FLandscapeSharedBuffers / landscape textures

GPU data: Shared grid vertex/index buffers and LOD-dependent indices; heightmap textures encode height/normal information, weightmaps select/blend layers, and visibility weights mask holes.

CPU boundary: FLandscapeComponentSceneProxy and FLandscapeSectionInfo organize sections/LOD. FLandscapeSharedBuffers owns render-resource wrappers, not a UObject per GPU vertex.

Source: `Engine/Source/Runtime/Landscape/Public/LandscapeRender.h:343` — `class FLandscapeSharedBuffers`.

### Dynamic mesh / modeling tools

Converts editable mesh topology into render-buffer sets and updates changed geometry or subsets.

**Data to carry**

- Versioned vertex streams and section/subset indices; material slots; dirty ranges; current/previous geometry and bounds.

**Work / conversion policy**

- Convert editable CPU topology into renderer streams. Update only changed chunks where possible; topology edits invalidate draw ranges and acceleration structures.

Unreal examples: `FBaseDynamicMeshSceneProxy`, `UBaseDynamicMeshComponent`.

Storage example: FMeshRenderBufferSet

GPU data: Position, static-mesh tangent/UV and color buffers plus FDynamicMeshIndexBuffer32 primary/secondary triangle indices; optional ray-tracing geometry uses these streams.

CPU boundary: UDynamicMesh / FDynamicMesh3 store editable CPU topology. FMeshRenderBufferSet manages GPU stream wrappers and upload operations; the topology object itself is not a GPU mesh.

Source: `Engine/Source/Runtime/GeometryFramework/Public/Components/MeshRenderBufferSet.h:38` — `class FMeshRenderBufferSet`.

### Procedural / custom mesh / cable

Builds per-section triangle render resources from supplied procedural vertices and indices.

**Data to carry**

- Generated positions, attributes, triangle sections and material slots; generation/revision token; deformation history.

**Work / conversion policy**

- Read the generated rendered mesh, not only collision triangles or simulation particles. CustomMesh and cable use different producers but can normalize to triangle streams.

Unreal examples: `FProceduralMeshSceneProxy`, `UProceduralMeshComponent`.

Storage example: FProcMeshProxySection

GPU data: FStaticMeshVertexBuffers, FDynamicMeshIndexBuffer32 and a local vertex factory feed section draws; optional ray-tracing geometry references the position/index buffers.

CPU boundary: CPU section arrays and collision data are distinct from the uploaded render streams.

Source: `Engine/Plugins/Runtime/ProceduralMeshComponent/Source/ProceduralMeshComponent/Private/ProceduralMeshComponent.cpp:40` — `class FProcMeshProxySection`.

### Geometry Collection

Renders rigid fracture pieces using collection geometry and per-piece transforms; conventional and Nanite proxy implementations share a collection helper base.

**Data to carry**

- Piece mesh ranges; piece-to-transform mapping; current/previous rigid transforms; hidden/fractured state; material sections; chosen Nanite/conventional representation.

**Work / conversion policy**

- Export the visible fracture pieces and their current transforms. The collection physics hierarchy is source state, not the GPU geometry itself.

Unreal examples: `FGeometryCollectionSceneProxy`, `FNaniteGeometryCollectionSceneProxy`, `UGeometryCollectionComponent`.

Storage example: FGeometryCollectionTransformBuffer / collection geometry

GPU data: Current/previous piece transform buffers accompany conventional mesh geometry or Nanite clusters. Transform updates move fracture pieces without rebuilding all source triangles.

CPU boundary: Collection physics/hierarchy data and FGeometryCollectionSceneProxyBase remain CPU state. Only required transforms and rendering data are uploaded.

Source: `Engine/Source/Runtime/Experimental/GeometryCollectionEngine/Private/GeometryCollection/GeometryCollectionSceneProxy.h:33` — `class FGeometryCollectionTransformBuffer`.

### Geometry Cache

Streams or decodes time-sampled mesh data rather than applying a skeleton to one rest mesh.

**Data to carry**

- Decoded sample streams, indices and sample time; interpolation/topology-change policy; paired/current/previous positions; section materials.

**Work / conversion policy**

- Decode or request the evaluated sample at the snapshot time. Handle topology changes rather than assuming a fixed skeletal rest mesh.

Unreal examples: `FGeometryCacheSceneProxy`, `UGeometryCacheComponent`.

Storage example: FGeometryCacheSceneProxy render data

GPU data: Position buffers (including a pair for sample/history handling), tangent, texture-coordinate, color and index buffers provide the current animated mesh.

CPU boundary: Track/sample decoding and proxy metadata prepare uploads. GeometryCache subclasses can use different source formats such as Alembic or USD.

Source: `Engine/Plugins/Runtime/GeometryCache/Source/GeometryCache/Public/GeometryCacheSceneProxy.h:251` — `PositionBuffers[2]`.

### Niagara / Cascade effects

Connects a Niagara system to scene rendering. Sprite, mesh, ribbon, volume, light, decal and component renderers interpret emitter data differently.

**Data to carry**

- Renderer kind; evaluated float/half/int attributes; ID mapping/count; material bindings; sprite axes, mesh references, ribbon connectivity, sorting and bounds.

**Work / conversion policy**

- Decide whether Unreal continues simulation and supplies evaluated render data or you implement the simulation. Sprites, meshes, ribbons, lights, decals and volume renderers need different conversion handlers. A particle component is not a ready-made triangle mesh.

Unreal examples: `FNiagaraSceneProxy`, `UNiagaraComponent`.

Storage example: FNiagaraDataBuffer

GPU data: GPUBufferFloat, GPUBufferHalf and GPUBufferInt hold separated particle attribute streams; GPUIDToIndexTable maps IDs. Counts, sorting and indirect arguments support GPU work. Mesh renderers also reference mesh geometry; ribbons generate segment geometry.

CPU boundary: CPU simulation uses CPU attribute arrays and uploads render data as needed; GPU simulation uses GPU buffers. FNiagaraDataBuffer and FNiagaraSceneProxy are CPU owners/interfaces, not particle structs placed wholesale on the GPU.

Source: `Engine/Plugins/FX/Niagara/Source/Niagara/Classes/NiagaraDataSet.h:230` — `FRWBuffer GPUBufferFloat;`.

### Groom / hair

Represents hair as strands or selected card/mesh LODs with deformation, interpolation and culling resources; a Nanite groom proxy is a separate supported path.

**Data to carry**

- Active strand/card/mesh/Nanite LOD; point and curve attributes; root bindings/interpolation; current/previous deformation; material and culling data.

**Work / conversion policy**

- Start with an explicitly selected card/mesh approximation if strand support is unavailable. Strand curves require geometry generation/intersection, specialized shading, transparency/visibility and shadowing.

Unreal examples: `FHairStrandsSceneProxy`, `Nanite::FGroomSceneProxy`, `UGroomComponent`.

Storage example: FHairStrandsRestResource / FHairStrandsDeformedResource

GPU data: Rest/current/previous strand point positions, curve/point attributes, interpolation/root-binding data and culling buffers; card/mesh LODs use position/index/UV/material streams and textures.

CPU boundary: Groom asset and resource wrappers organize GPU allocations. Hair voxelization and visibility data are renderer-generated resources, not a CPU groom UObject copied to VRAM.

Source: `Engine/Plugins/Runtime/HairStrands/Source/HairStrandsCore/Public/GroomResources.h:137` — `struct FHairStrandsRestResource`.

### Heterogeneous / sparse volume

Supplies a bounded participating medium to volumetric rendering, often from animated sparse volume textures; the viewer proxy provides a separate inspection path.

**Data to carry**

- Volume transform and bounds; voxel attribute meaning and units; sparse page table/physical tiles or dense conversion; sample time; scattering/extinction/emission parameters.

**Work / conversion policy**

- Sample the volume and integrate participating media, or report it unsupported. A bounding box rendered as an opaque surface is not an equivalent conversion.

Unreal examples: `FHeterogeneousVolumeSceneProxy`, `FSparseVolumeTextureViewerSceneProxy`, `UHeterogeneousVolumeComponent`, `USparseVolumeTextureViewerComponent`.

Storage example: UE::SVT::FTextureRenderResources

GPU data: Page-table texture and PhysicalTileDataA / PhysicalTileDataB textures store sparse indirection and physical voxel attributes. Volume shaders sample these to integrate scattering/extinction/emission.

CPU boundary: SparseVolumeTextureData CPU pages/tiles and streaming state prepare texture uploads. FTextureRenderResources contains RHI texture references, not the voxel data inline in a scene proxy.

Source: `Engine/Source/Runtime/Engine/Classes/SparseVolumeTexture/SparseVolumeTexture.h:260` — `class FTextureRenderResources`.

### Water & virtual heightfield surfaces

Draws selected water surface tiles/LODs using water mesh resources and per-tile instance data.

**Data to carry**

- Tile/patch selection; source height/displacement textures and virtual mappings; material parameters; water body/depth/underwater information.

**Work / conversion policy**

- Normalize generated surface geometry or reproduce tile selection/displacement. Water also needs a deliberate refraction, absorption, reflection and underwater-composition policy; controller components are not necessarily render geometry.

Unreal examples: `FWaterMeshSceneProxy`, `UWaterMeshComponent`.

Storage example: FWaterMeshSceneProxy

GPU data: Tile mesh vertex/index data and dynamic per-tile rendering data support the selected water surface; materials sample additional water parameters/textures.

CPU boundary: Water quadtree/LOD selection and water-body components organize tiles on the CPU. UWaterBodyComponent is a separate primitive-component branch and is not the water mesh component itself.

Source: `Engine/Plugins/Experimental/Water/Source/Runtime/Public/WaterMeshSceneProxy.h:103` — `class FWaterMeshSceneProxy`.

### Point clouds / points / lines

Selects and renders visible point-cloud batches with the LiDAR plugin's point rendering resources.

**Data to carry**

- Point/segment attributes, positions, widths/radii, colors; selected batches and bounds; point-versus-splat expansion policy.

**Work / conversion policy**

- Choose raster expansion or analytic intersections. An asset called a point cloud does not guarantee PT_PointList draw topology. Debug helpers can be filtered explicitly.

Unreal examples: `FLidarPointCloudSceneProxy`, `ULidarPointCloudComponent`.

Storage example: LiDAR point-cloud render buffers

GPU data: Selected point records and point-rendering buffers are uploaded for the configured point/splat display. A point-cloud scene primitive can contain many points.

CPU boundary: The point-cloud octree and batch selection are CPU organization; they are not the draw topology enum itself.

Source: `Engine/Plugins/Enterprise/LidarPointCloud/Source/LidarPointCloudRuntime/Private/Rendering/LidarPointCloudRenderBuffers.h:14` — `class FLidarPointCloud`.

### Paper2D, billboards, text & widgets

Converts sprite, flipbook, grouped-sprite, tile-map or 2D terrain render data into textured geometry.

**Data to carry**

- Evaluated sprite/glyph/tile or world-widget geometry; atlas/font/render-target textures; material, orientation and sorting policy.

**Work / conversion policy**

- Preserve camera-facing and animation behavior or bake it at snapshot time. World-space widgets use surfaces and render targets; screen-space UI is a separate output-composition decision.

Unreal examples: `FPaperRenderSceneProxy`, `UPaperFlipbookComponent`, `UPaperGroupedSpriteComponent`, `UPaperSpriteComponent`, `UPaperTerrainComponent`, `UPaperTileMapComponent`.

Storage example: FPaperRenderSceneProxy

GPU data: Sprite/tile vertex and triangle index data plus atlas/material textures supply batched 2D draws in the 3D scene.

CPU boundary: Sprite assets, animation frames and tile layers choose the render data on the CPU. Sprite proxy subclasses are real inheritance; sprite textures are associations.

Source: `Engine/Plugins/2D/Paper2D/Source/Paper2D/Private/PaperRenderSceneProxy.h:124` — `class FPaperRenderSceneProxy`.

### BSP/model & plugin-defined geometry

Draws model/BSP surfaces using model render data and material groupings.

**Data to carry**

- Rendered model surfaces or plugin-owned streams; material ranges; bounds; lifetime; source capability and revision.

**Work / conversion policy**

- Use a producer registry for unknown primitive/proxy families. Require a supported geometry export, an explicit approximation, or a reported omission. Never silently treat an unknown primitive as a static mesh.

Unreal examples: `FModelSceneProxy`, `UModelComponent`.

Storage example: FModelSceneProxy

GPU data: Model surface vertex/index data, material textures and lighting data feed triangle draws.

CPU boundary: BSP model nodes/surfaces and the proxy are CPU organization. A brush used for construction/collision is a different branch.

Source: `Engine/Source/Runtime/Engine/Private/ModelRender.cpp:206` — `class FModelSceneProxy`.

### EPrimitiveType

Hardware draw topology, not the scene's geometry family. Quad/rectangle modes require RHI capabilities; convert or reject unsupported topology explicitly.

- `PT_TriangleList`
- `PT_TriangleStrip`
- `PT_LineList`
- `PT_QuadList`
- `PT_PointList`
- `PT_RectList`

Source: `Engine/Source/Runtime/RHI/Public/RHIDefinitions.h:815`.

## Materials are executable behavior (materials)

A list of textures and a base color cannot reproduce arbitrary Unreal materials. Choose shader integration, graph translation, baking, or an explicitly limited material model.

### Material identity, bindings & evaluation

Resolve UMaterialInterface/UMaterialInstance through their parent, static switches and runtime parameters, with per-section/per-instance overrides.

**Data to carry**

- Material ID/revision; parent/static permutation key; domain; blend mode; shading-model set; two-sidedness, culling and depth/shadow policy.
- Scalar/vector/texture parameters, sampler state, parameter collections, per-primitive/per-instance custom data and time-dependent inputs.
- Vertex factory/deformation requirements; pass permutations; graph/custom-HLSL dependencies and material usage flags.

**Work / conversion policy**

- For shader reuse, satisfy Unreal's material/vertex-factory/pass bindings, uniform expressions, permutations and renderer services; compiled bytecode is not a portable shader contract.
- For translation, implement the selected graph operations and reject unsupported nodes. Resolve dynamic parameters at the correct snapshot time.
- For baking, state what is baked and at which view/time. WPO, scene-texture reads, refraction and arbitrary procedural/time/view-dependent effects cannot generally be preserved by a few baked maps.

Source: `Engine/Source/Runtime/Engine/Public/MaterialShared.h:50` — `class FMaterial`.

### Surface parameters and special lobes

Provide the inputs required by the selected shading model, not one fixed PBR tuple for every material.

**Data to carry**

- Typical baseline: base color, metallic, roughness, specular, emissive, normal/tangent, opacity/mask and ambient occlusion.
- Special models can need subsurface color/profile, clear-coat amount/roughness/normal, anisotropy, hair/fiber parameters, cloth fuzz, eye refraction or water absorption/scattering.
- A material can select shading models through expressions. Substrate represents layered closures and material topology, not merely a rename of Default Lit.

**Work / conversion policy**

- Start with an agreed Default Lit/Unlit subset, plus Masked if needed; enumerate special-model conversion policies.
- Keep emissive radiance and baked/direct/indirect illumination distinct to avoid adding light twice.
- For Substrate, integrate compatible closure evaluation/storage or translate/bake a restricted subset and report the loss.

Source: `Engine/Source/Runtime/Engine/Classes/Engine/EngineTypes.h:707` — `enum EMaterialShadingModel`.

### Coverage, deformation & transparency

Depth, shadow, ray and color paths must agree about the material's geometry and coverage.

**Data to carry**

- Opacity-mask threshold; two-sided normal policy; WPO/displacement inputs; pixel-depth offset; current/previous deformation.
- Blend equations and ordering; premultiplied versus straight alpha; refraction/distortion; depth testing/writes; translucency lighting mode; custom depth/stencil; render-after-DOF policy.

**Work / conversion policy**

- Apply mask/deformation consistently in depth, base, shadow and ray geometry. Compute conservative bounds for moved vertices.
- Implement transparent sorting/composition or a declared OIT method; do not route all translucent materials through the opaque GBuffer.
- Define how ray intersections handle alpha, two-sidedness and displaced/deformed surfaces. Record raster/trace representation mismatches.

Source: `Engine/Source/Runtime/Engine/Classes/Engine/EngineTypes.h:245` — `enum EBlendMode`.

### Domains beyond mesh surfaces

Surface, decal, light-function, volume, post-process and UI domains run at different places and have different inputs.

**Data to carry**

- Material domain; required scene/depth/normal inputs; target/composition policy; projection volume or light association.
- For decals: transform, bounds, sort order, fade, affected channels, blend and receiver flags. For post-process materials: blend location and scene-texture dependencies.

**Work / conversion policy**

- Dispatch each domain to the appropriate renderer subsystem. A decal material is not just another mesh surface material.
- Treat editor/deprecated enum values explicitly rather than assuming every value represents a supported current path.

Source: `Engine/Source/Runtime/Engine/Public/MaterialDomain.h:12` — `enum EMaterialDomain`.

### EMaterialDomain

Domain determines which subsystem executes the material. MD_RuntimeVirtualTexture is marked deprecated/hidden in this checkout.

- `MD_Surface`
- `MD_DeferredDecal`
- `MD_LightFunction`
- `MD_Volume`
- `MD_PostProcess`
- `MD_UI`
- `MD_RuntimeVirtualTexture`

Source: `Engine/Source/Runtime/Engine/Public/MaterialDomain.h:12`.

### EBlendMode

Opaque/Masked, translucent/additive/modulate, alpha composite/holdout and Substrate transmittance choices require different coverage/composition. Hidden aliases share values; they are not extra independent GPU blend equations.

- `BLEND_Opaque`
- `BLEND_Masked`
- `BLEND_Translucent`
- `BLEND_Additive`
- `BLEND_Modulate`
- `BLEND_AlphaComposite`
- `BLEND_AlphaHoldout`
- `BLEND_TranslucentColoredTransmittance`
- `BLEND_TranslucentGreyTransmittance = BLEND_Translucent`
- `BLEND_ColoredTransmittanceOnly = BLEND_Modulate`

Source: `Engine/Source/Runtime/Engine/Classes/Engine/EngineTypes.h:245`.

### EMaterialShadingModel

Includes Unlit/DefaultLit and specialized subsurface, coat, foliage, hair, cloth, eye, water and thin-translucent models. MSM_Strata is the hidden Substrate value; MSM_FromMaterialExpression selects via graph behavior.

- `MSM_Unlit`
- `MSM_DefaultLit`
- `MSM_Subsurface`
- `MSM_PreintegratedSkin`
- `MSM_ClearCoat`
- `MSM_SubsurfaceProfile`
- `MSM_TwoSidedFoliage`
- `MSM_Hair`
- `MSM_Cloth`
- `MSM_Eye`
- `MSM_SingleLayerWater`
- `MSM_ThinTranslucent`
- `MSM_Strata`
- `MSM_FromMaterialExpression`

Source: `Engine/Source/Runtime/Engine/Classes/Engine/EngineTypes.h:707`.

### ETranslucencyLightingMode

Volumetric versus surface and per-vertex versus per-pixel lighting choices are separate from blend mode.

- `TLM_VolumetricNonDirectional`
- `TLM_VolumetricDirectional`
- `TLM_VolumetricPerVertexNonDirectional`
- `TLM_VolumetricPerVertexDirectional`
- `TLM_Surface`
- `TLM_SurfacePerPixelLighting`

Source: `Engine/Source/Runtime/Engine/Classes/Engine/EngineTypes.h:316`.

## Texture, buffer & sampler resources (textures)

Transfer the actual format, interpretation, residency and access contract along with the data.

### Texture classes and layouts

Recognize ordinary 2D images, arrays, cube/cube-array maps, volume textures, render targets, streamed/virtual textures and sparse-volume textures.

**Data to carry**

- Dimensions, slices/faces/depth, mip count/resident range; pixel/block format; row/slice pitch; sRGB/linear interpretation and channel semantics.
- Normal-map packing, alpha meaning, compression/transcoding; sampler filter/address/mip bias/anisotropy; UV set or procedural coordinate source.
- Render-target producer and ready frame; virtual texture page tables/physical tiles and feedback, or an explicit resolved/baked representation.

**Work / conversion policy**

- Decode or directly support cooked formats for the target device; do not infer channels from filenames.
- Preserve mip/slice/subresource selection and streaming changes. A missing mip, default texture and unsupported texture type need separate diagnostics.
- Track dynamic render targets, video/UI/scene captures and virtual-texture dependencies as producers. Prevent cycles or define previous-frame reads.

Source: `Engine/Source/Runtime/Engine/Classes/Engine/Texture.h:644` — `class UTexture`.

### CPU export versus GPU borrowing

Unreal wrappers remain CPU objects. Their RHI handles reference allocations whose ownership, state and lifetime require an explicit integration contract.

**Data to carry**

- Buffer element type/stride/count; byte offsets; index width; SRV/UAV interpretation; source revision and resident bytes.
- For external resources: compatible device/API, descriptor/view ownership, queue ownership/access state, synchronization, replacement and retirement notifications.

**Work / conversion policy**

- For a first renderer, prefer owned copies where feasible and let the renderer allocate its own resources. Readback is a synchronization/cost decision, not a zero-cost export.
- If resources are borrowed, wait for producers, declare accesses and return completion/state ownership; retain them across in-flight frames.
- Never serialize raw UObject, proxy, FRDG or RHI pointers as portable scene data.

Source: `Engine/Source/Runtime/RHI/Public/RHIResources.h:1627` — `class FRHIBuffer`.

## Primitive placement, visibility & draw bindings (instances)

A mesh asset alone does not identify what should appear in a view. Placements and sections carry essential rendering state.

### Primitive and instance records

Represent one component's renderable contribution and any additional instances separately from shared geometry.

**Data to carry**

- Stable ID/generation; geometry and deformation references; current/previous transform; world/local bounds; active LOD or LOD policy.
- Section-to-material mapping and overrides; per-instance/custom primitive data; visibility masks, hidden/owner-only flags, distance cull/fade and mobility.
- Cast/receive-shadow policy, lighting channels, custom depth/stencil, decal reception and ray-tracing visibility; negative-scale/culling state.

**Work / conversion policy**

- Evaluate visibility for the intended view and rendering mode; hidden in one view does not imply absent from shadows, reflections or other views.
- Maintain instance identity through compaction. Update bounds after deformation and invalidate only affected cached draws.
- Treat FMeshBatch/FMeshDrawCommand as render-thread submission descriptions, not a universal export format or a portable GPU mesh object.

Source: `Engine/Source/Runtime/Engine/Public/PrimitiveSceneProxy.h:291` — `class FPrimitiveSceneProxy`.

### Draw and intersection descriptors

Your renderer needs a normalized binding between a geometry range, material and instance, regardless of whether it rasterizes or traces rays.

**Data to carry**

- Topology; vertex layout; index/base-vertex ranges; section/material ID; instance range; deformation/skin bindings; raster state.
- For ray consumers: geometry/instance mapping, opaque/alpha flags, hit-material lookup, transforms, build/update policy and lifetime.

**Work / conversion policy**

- Build your own PSO/draw/dispatch representation from the semantic scene. Decide which RHI topology forms to support or convert.
- Use conventional triangles, curves, procedural intersections or volume traversal deliberately; a scene primitive is not the same thing as a hardware primitive.

Source: `Engine/Source/Runtime/Engine/Public/MeshBatch.h:98` — `struct FMeshBatch`.

## Lights, sky, captures & baked lighting (lighting)

Export light semantics and choose how illumination is evaluated. Dynamic, baked and cached contributions must not be counted twice.

### Directional, point, spot & rect lights

Handle light shape and emission together with the settings that change which objects they illuminate.

**Data to carry**

- Light ID/type; transform/direction; linear color, intensity and units; temperature conversion; attenuation radius/falloff.
- Spot inner/outer cones; rect size/orientation/barn doors/source texture; source radius/length; IES profile and light-function material.
- Mobility; lighting/view channels; shadow enable/bias/contact settings; volumetric/indirect contribution and ray-tracing policy.

**Work / conversion policy**

- Convert units/exposure consistently for each shape; do not use one point-light attenuation model for every light type.
- Choose shadow maps, ray shadows or another stated method; Unreal shadow resources are derived outputs, not required copies of scene input.
- Handle material light functions and IES as evaluated directional modulation or a documented approximation.

Source: `Engine/Source/Runtime/Engine/Public/LightSceneProxy.h:33` — `class FLightSceneProxy`.

### Sky light, reflection captures & planar reflections

Environment illumination and local reflection approximations are separate from finite local lights.

**Data to carry**

- Sky capture/cubemap or real-time producer; rotation, intensity and convolution representation.
- Sphere/box reflection-capture transforms, influence extents, blend/brightness and captured cubemaps; planar reflection plane and view dependencies.
- Chosen diffuse GI and specular reflection methods and the conditions under which captures supplement/replace them.

**Work / conversion policy**

- Define a consistent environment-lighting and reflection blend policy. Avoid counting sky/capture radiance again on top of an already complete traced contribution.
- Re-render planar captures or explicitly omit/approximate them; a planar-reflection component is not just an ordinary environment cube.

Source: `Engine/Source/Runtime/Renderer/Private/ScenePrivate.h:131` — `class FScene`.

### Lightmaps, shadowmaps & volumetric lightmaps

Precomputed lighting is part of some scenes' intended appearance and may be absent or disabled in others.

**Data to carry**

- Static mesh lightmap UV channel and scale/bias; lightmap/shadowmap textures and decode coefficients; per-primitive light-cache association.
- Volumetric-lightmap brick/indirection/sample data for movable objects where used; build validity and world transforms.

**Work / conversion policy**

- Choose to decode/import baked lighting or deliberately relight from source geometry/lights. Exporting only lights cannot reconstruct the exact baked result.
- Document which static/stationary contributions are already baked so the renderer does not add them again.

Source: `Engine/Source/Runtime/Engine/Public/LightMap.h:18` — `class FLightMap`.

## Atmosphere, media & non-mesh scene effects (environment)

Scene appearance also depends on global and local media, surface projections and generated textures.

### Sky atmosphere, fog & clouds

These are rendering inputs/systems, not ordinary primitive meshes.

**Data to carry**

- Atmosphere planet/origin and Rayleigh/Mie/ozone settings; atmosphere lights; sky/aerial-perspective parameters.
- Exponential height fog, volumetric fog and local fog volume transforms/density/scattering; cloud layer bounds, density/material and lighting settings.
- Time-varying weather/material inputs, shadow interactions and history policy.

**Work / conversion policy**

- Implement LUT generation and participating-media integration, accept a declared baked/approximate sky, or flag unsupported media.
- Separate atmosphere inputs from generated LUTs, froxel grids and history buffers. The renderer can build its own derived representations.
- Compose fog, clouds, water and translucency according to depth and camera position; ordering changes appearance.

Source: `Engine/Source/Runtime/Renderer/Private/SkyAtmosphereRendering.cpp:1478` — `RenderSkyAtmosphereLookUpTables`.

### Decals, captures, volumes & output layers

Register scene contributions that may not arrive as standard mesh component geometry.

**Data to carry**

- Decal projection/material/channel state; heterogeneous and local volume records; scene-capture view/target dependencies.
- World-widget surfaces versus screen UI; post-process volumes and blend weights; water body/underwater state; optional debug/editor contributions.

**Work / conversion policy**

- Classify these producers before extracting only UPrimitiveComponent descendants. Lights, decals, camera and environment systems also enter the render scene.
- Resolve capture/update order and feedback loops. Emit a coverage report with producer/type and reason whenever a contribution is excluded.

Source: `Engine/Source/Runtime/Engine/Public/SceneInterface.h:119` — `class FSceneInterface`.

## Views, temporal state & final image (views)

Geometry and materials cannot determine the final frame without camera, output and exposure settings.

### Camera and view family

Capture the evaluated view used to render the scene, including any view extensions and camera/post-process blending you intend to support.

**Data to carry**

- Projection mode, current/previous matrices, camera-relative origin, near/far/depth convention, frustum, view rectangle and render/output resolution.
- Temporal jitter, camera-cut flag, frame/sample time, stereo/multiview identity, show flags, visibility sets and per-view feature choices.
- Exposure/pre-exposure, working color space, white balance, tone curve/output transfer, bloom/DOF/motion blur and upscaler selection.

**Work / conversion policy**

- Keep separate histories for separate views; reset/reject history for camera cuts, resize, time jumps or incompatible method changes.
- Choose whether matching means scene-linear radiance, pre-exposed scene color or final display output; compare images at that same stage.
- Provide correct current/previous deformation and transforms for velocity, not only previous camera matrices.

Source: `Engine/Source/Runtime/Engine/Public/SceneView.h:25` — `class FSceneView`.

## Streaming, synchronization & scene updates (updates)

The adapter must keep the same contracts true as the world changes; a one-time dump is only a static-scene mode.

### Versioned snapshot and change protocol

Publish coherent immutable snapshots or ordered change batches at a defined scene/render boundary.

**Data to carry**

- Add/update/remove events; monotonically interpreted revisions; asset residency/replacement; topology/material/deformation/transform dirty categories.
- World Partition/level streaming, world-origin changes, object destruction, asynchronous cooking/compilation readiness and resource leases.
- Frame provenance for animation, GPU effects and render targets; previous state and invalidation reasons.

**Work / conversion policy**

- Never read mutable game-thread objects from arbitrary render work. Copy/snapshot the needed values through supported thread boundaries.
- Remove or replace objects without leaving dangling material/geometry references; defer memory reclamation until queued GPU consumers complete.
- Changes to geometry/material/light state must invalidate relevant bounds, commands, acceleration structures and caches; a transform-only update is not enough for every change.
- Capture a snapshot plus capability report for reproducible comparisons and debugging.

Source: `Engine/Source/Runtime/Renderer/Private/SceneRendering.cpp:4198` — `IVisibilityTaskData* FSceneRenderer::OnRenderBegin`.

## Decide what to reproduce, convert or rebuild (fidelity)

Ingesting an Unreal scene and reproducing Unreal's image are different fidelity targets. Make the supported target explicit and measurable.

### Generated renderer data

GPU Scene packing, Nanite visible work, BLAS/TLAS, Lumen cards/caches, VSM pages, GBuffer and temporal histories are renderer-specific representations.

**Data to carry**

- Preserve source geometry/material/light/view semantics and the selected fallback assets.
- For any reused cache/native encoding: its revision, packing, shader consumers, update/invalidation rules, ownership and feature/build compatibility.

**Work / conversion policy**

- A conventional renderer can ingest the scene without reimplementing Nanite, Lumen or VSM if it provides declared alternatives and usable source/fallback data.
- For Unreal-like fidelity, implement the required geometry, BRDF, shadow/GI/reflection, media and temporal behaviors. Reusing a cache requires its full producer/consumer contract; copying one texture is insufficient.
- Do not export FRDG handles as persistent resources. Extract/import backing allocations only with an explicit lifetime contract.

Source: `Engine/Source/Runtime/Renderer/Private/GPUScene.h:56` — `FGPUSceneResourceParameters`.

### Minimum useful implementation and acceptance suite

Build a small coherent renderer first and expand coverage with named test scenes.

**Data to carry**

- Baseline: indexed triangle meshes, instances, supported static/deformed geometry, opaque/masked Default Lit/Unlit materials, decoded textures/samplers, camera/exposure and supported lights.
- Coverage report: exact / converted / approximated / omitted / unavailable, with object ID, type, material feature and reason. These are proposed adapter statuses, not claims of existing support.

**Work / conversion policy**

- Test a textured mesh with multiple material sections; instancing with custom data; mirrored/nonuniform transforms; large-world coordinates; masks and two-sided foliage.
- Test skeletal/spline motion and camera cuts; dynamic material/texture changes; light unit/exposure consistency; streaming add/remove; resource replacement with multiple frames in flight.
- Use targeted scenes for transparency, hair, volumes, terrain, water, decals and baked lighting before claiming those features. Compare geometry/depth/normals/material outputs before final color.
- Then add native virtualized geometry, GI/reflections, scalable shadows and advanced temporal processing as required. The deferred-frame page gives operation-level work and source call sites.

Source: `Engine/Source/Runtime/Engine/Public/StaticMeshResources.h:247` — `struct FStaticMeshLODResources`.
