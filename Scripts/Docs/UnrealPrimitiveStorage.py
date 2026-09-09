"""Authored storage associations; these are never C++ inheritance edges.

Each source query is checked against the local Unreal checkout by the builder.
Class selectors may include @path-suffix to disambiguate local declarations.
"""

RUNTIME = "Engine/Source/Runtime/"
PLUGINS = "Engine/Plugins/"


def resource(name, gpu, cpu, path, query):
    return dict(name=name, gpu=gpu, cpu=cpu, sourceQuery=(path, query))


def profile(id, title, classes, role, resources, caveat, peers=""):
    return dict(id=id, title=title, classes=classes.split("|"), role=role,
                resources=resources, caveat=caveat, peers=peers.split("|") if peers else [])


COMMON = [
    resource("GPU Scene records · FGPUSceneResourceParameters",
        "StructuredBuffer<float4> primitive, instance, instance-payload and lightmap records; ByteAddressBuffer light data. Shader indices connect the records.",
        "FGPUScene manages allocation and uploads; FPrimitiveSceneProxy and FPrimitiveSceneInfo remain CPU objects. Only eligible primitives/instances participate, and updates are incremental.",
        RUNTIME + "Renderer/Private/GPUScene.h", "GPUScenePrimitiveSceneData"),
    resource("Draw resources · FVertexBuffer / FIndexBuffer / FVertexFactory / FMeshBatch",
        "Vertex/index allocations and SRVs, shader uniform data, textures and optional indirect argument buffers feed a draw or dispatch.",
        "The C++ wrappers, vertex-factory object, FMeshBatch and FMeshDrawCommand describe/bind GPU work on the CPU. A mesh batch is not itself a GPU allocation.",
        RUNTIME + "Engine/Public/MeshBatch.h", "struct FMeshBatch"),
    resource("RHI storage · FRHIBuffer / FRHITexture",
        "Buffers (vertex, index, structured, byte-address, uniform, indirect) and textures (2D, arrays, 3D, cube, render/depth targets) hold bytes and texels. SRV/UAV views interpret existing storage.",
        "FRHI resources are CPU handles to device resources; FRDGBuffer/FRDGTexture describe render-graph use and lifetimes. Views and RDG wrappers do not imply independent copies of data.",
        RUNTIME + "RHI/Public/RHIResources.h", "class FRHIBuffer"),
    resource("Optional derived renderer representations",
        "Ray-tracing BLAS/TLAS, distance-field volumes, Lumen card/surface-cache textures, Nanite visibility buffers and virtual shadow-map pages can represent or be affected by a primitive.",
        "These are feature-dependent shared caches, acceleration structures or per-view outputs. They are not subclasses of the component/proxy, nor allocations owned by every instance. See the pipeline for build/update/consume stages.",
        RUNTIME + "Renderer/Private/ScenePrivate.h", "class FScene"),
]

PROFILES = [
    profile("component", "Primitive component contract", "UPrimitiveComponent",
        "Game-thread scene component with primitive bounds, transform, visibility, collision and render-state hooks. CreateSceneProxy can create a render-thread representation or return no proxy.", [],
        "UPrimitiveComponent inherits USceneComponent and several interfaces/mixins shown below. The tree is deliberately rooted here. Neither the UObject nor the proxy is copied wholesale into a GPU buffer.", "FPrimitiveSceneProxy"),
    profile("proxy", "Primitive scene proxy contract", "FPrimitiveSceneProxy",
        "Render-thread representation of a primitive: exposes relevance, bounds, materials and drawing/instance integration to the scene renderer.", [],
        "A subclass can draw meshes, use a specialized rendering path, draw debug geometry, or draw nothing under the active settings. Its C++ inheritance alone does not specify a GPU memory layout.", "UPrimitiveComponent"),
    profile("mesh-base", "Mesh component contract", "UMeshComponent",
        "Adds mesh material interfaces and overrides to the primitive component contract; concrete subclasses choose geometry and proxy implementations.", [],
        "UMeshComponent does not prescribe a single vertex format or allocate a universal mesh buffer."),
    profile("fx-base", "Effects component contract", "UFXSystemComponent",
        "Common component interface for effects systems such as Cascade and Niagara.", [],
        "This is a direct UPrimitiveComponent subclass, not a UMeshComponent subclass. Emitter simulation and renderer choices determine storage."),
    profile("static", "Static mesh triangles", "UStaticMeshComponent|FStaticMeshSceneProxy",
        "Renders a static mesh asset using section/material/LOD data. Conventional rendering reads shared mesh streams; eligible components can instead select a Nanite proxy.", [
            resource("FStaticMeshLODResources / FStaticMeshVertexBuffers",
                "Position, tangent/normal and UV streams, optional vertex colors; primary and depth-only index buffers plus optional reversed/wireframe indices. Section ranges select triangles.",
                "LOD and section metadata and FPositionVertexBuffer/FStaticMeshVertexBuffer/FColorVertexBuffer/FRawStaticIndexBuffer wrappers live on the CPU and initialize/bind the actual GPU buffers.",
                RUNTIME + "Engine/Public/StaticMeshResources.h", "struct FStaticMeshLODResources"),
        ], "Materials may add textures, uniform data and vertex deformation. Collision BodySetup is not the rendered triangle allocation. Nanite and fallback representations are alternatives, not inherited classes of FStaticMeshSceneProxy.",
        "FStaticMeshSceneProxy|Nanite::FSceneProxy"),
    profile("instances", "Instanced static meshes & foliage", "UInstancedStaticMeshComponent|UHierarchicalInstancedStaticMeshComponent|FInstancedStaticMeshSceneProxy|FHierarchicalStaticMeshSceneProxy",
        "Reuses one mesh asset for many transformed instances; HISM adds hierarchical instance organization and foliage derives from the HISM component branch.", [
            resource("FInstanceSceneDataBuffers + shared static-mesh streams",
                "GPU Scene instance records/payloads contain transforms, bounds and optional custom data; shared vertex/index buffers describe the mesh. The legacy vertex-factory path can use instance vertex streams.",
                "FInstanceSceneDataBuffers is CPU-side instance storage/upload input. HISM cluster organization is not a separate GPU triangle format.",
                RUNTIME + "Engine/Public/InstanceDataSceneProxy.h", "class FInstanceSceneDataBuffers"),
        ], "Instance data is additional to the mesh's geometry. Proxy choice and GPU Scene support depend on feature/platform settings; Nanite uses its instance/cluster path.", "FInstancedStaticMeshSceneProxy|FHierarchicalStaticMeshSceneProxy|Nanite::FSceneProxy"),
    profile("spline", "Spline-deformed mesh", "USplineMeshComponent|FSplineMeshSceneProxy@Engine/Public/SplineMeshSceneProxy.h|FNaniteSplineMeshSceneProxy",
        "Deforms a source mesh along a spline using spline parameters and the selected conventional or Nanite mesh path.", [
            resource("FSplineMeshSceneProxyCommon + source mesh resources",
                "Source geometry buffers plus spline deformation parameters used during rendering. Ray-tracing deformation may require updated geometry buffers.",
                "Spline control state and proxy helper/mixin remain CPU objects; changing a spline does not imply a permanent duplicated mesh asset.",
                RUNTIME + "Engine/Public/SplineMeshSceneProxy.h", "FSplineMeshSceneProxyCommon"),
        ], "The function-local FSplineMeshSceneProxy in SplineComponent.cpp is a different debug proxy. Names are disambiguated by source location; only the global mesh proxy has FStaticMeshSceneProxy as a base.",
        "FSplineMeshSceneProxy@Engine/Public/SplineMeshSceneProxy.h|FNaniteSplineMeshSceneProxy"),
    profile("skinned", "Skinned / skeletal meshes", "USkinnedMeshComponent|FSkeletalMeshSceneProxy",
        "Supplies deformable mesh geometry. Skeletal animation, poseable meshes and instanced skinned meshes specialize the component branch; supported assets may choose a Nanite skinned proxy.", [
            resource("FSkeletalMeshLODRenderData",
                "Static position/tangent/UV/color streams, indices, FSkinWeightVertexBuffer bone indices and weights, optional cloth and morph data. Bone transforms drive skinning; GPU Skin Cache can output deformed positions/tangents reused by later passes.",
                "LOD/section metadata and FSkeletalMeshObject rendering state are CPU objects. Skinning paths and precision/weight layouts vary; current/previous deformation data supports velocity.",
                RUNTIME + "Engine/Public/Rendering/SkeletalMeshLODRenderData.h", "SkinWeightVertexBuffer;"),
        ], "CPU skinning, GPU vertex skinning, compute skin cache and Nanite are different paths. Not all optional buffers exist together.", "FSkeletalMeshSceneProxy|Nanite::FSkinnedSceneProxy"),
    profile("instanced-skinned", "Instanced skinned meshes", "UInstancedSkinnedMeshComponent|FInstancedSkinnedMeshSceneProxy|FNaniteInstancedSkinnedMeshSceneProxy",
        "Combines skinned mesh rendering with per-instance state and chooses the conventional or Nanite skinned proxy implementation.", [
            resource("Instanced skinned proxy + skeletal/Nanite data",
                "Shared deformable geometry and skinning resources plus instance scene records. The selected path determines conventional streams/skin outputs or Nanite cluster data.",
                "The component is a USkinnedMeshComponent subclass. FInstancedSkinnedMeshSceneProxy derives FSkeletalMeshSceneProxy, while the Nanite counterpart derives Nanite::FSkinnedSceneProxy.",
                RUNTIME + "Engine/Private/InstancedSkinnedMeshSceneProxy.h", "class FInstancedSkinnedMeshSceneProxy"),
        ], "Do not infer UInstancedStaticMeshComponent ancestry from the word 'instanced'.", "FInstancedSkinnedMeshSceneProxy|FNaniteInstancedSkinnedMeshSceneProxy"),
    profile("nanite", "Nanite virtualized geometry", "Nanite::FSceneProxyBase|Nanite::FSceneProxy|Nanite::FSkinnedSceneProxy|ULandscapeNaniteComponent",
        "Connects supported geometry to Nanite's streamed cluster representation. Static and skinned scene proxies are siblings beneath Nanite::FSceneProxyBase.", [
            resource("Nanite::FResources / Nanite streaming",
                "Packed geometry cluster pages, hierarchy nodes and streaming metadata resident in GPU buffers; GPU Scene supplies primitive/instance transforms. Culling generates visible clusters and raster/shading work.",
                "FResources holds cooked asset/streaming data and resource identity. The streaming manager manages resident GPU allocations. Visibility buffers, candidate queues and shading bins are renderer work/output storage.",
                RUNTIME + "Engine/Public/Rendering/NaniteResources.h", "struct FResources"),
        ], "Nanite::FSkinnedSceneProxy is not derived from Nanite::FSceneProxy. Deformation resources extend the skinned path. Conventional fallback meshes and ray-tracing representations have their own storage; the component may select a different proxy when Nanite is unavailable.",
        "Nanite::FSceneProxyBase|Nanite::FSceneProxy|Nanite::FSkinnedSceneProxy"),
    profile("landscape", "Landscape heightfield", "ULandscapeComponent|FLandscapeComponentSceneProxy",
        "Represents terrain sections using shared grid geometry and sampled landscape height/normal/weight textures.", [
            resource("FLandscapeSharedBuffers / landscape textures",
                "Shared grid vertex/index buffers and LOD-dependent indices; heightmap textures encode height/normal information, weightmaps select/blend layers, and visibility weights mask holes.",
                "FLandscapeComponentSceneProxy and FLandscapeSectionInfo organize sections/LOD. FLandscapeSharedBuffers owns render-resource wrappers, not a UObject per GPU vertex.",
                RUNTIME + "Landscape/Public/LandscapeRender.h", "class FLandscapeSharedBuffers"),
        ], "FLandscapeNaniteSceneProxy derives Nanite::FSceneProxy; FLandscapeMeshProxySceneProxy derives FStaticMeshSceneProxy. Those are separate definition branches, not children of FLandscapeComponentSceneProxy.",
        "FLandscapeComponentSceneProxy|FLandscapeNaniteSceneProxy|FLandscapeMeshProxySceneProxy"),
    profile("collection", "Geometry Collection fracture pieces", "UGeometryCollectionComponent|FGeometryCollectionSceneProxy|FNaniteGeometryCollectionSceneProxy",
        "Renders rigid fracture pieces using collection geometry and per-piece transforms; conventional and Nanite proxy implementations share a collection helper base.", [
            resource("FGeometryCollectionTransformBuffer / collection geometry",
                "Current/previous piece transform buffers accompany conventional mesh geometry or Nanite clusters. Transform updates move fracture pieces without rebuilding all source triangles.",
                "Collection physics/hierarchy data and FGeometryCollectionSceneProxyBase remain CPU state. Only required transforms and rendering data are uploaded.",
                RUNTIME + "Experimental/GeometryCollectionEngine/Private/GeometryCollection/GeometryCollectionSceneProxy.h", "class FGeometryCollectionTransformBuffer"),
        ], "The shared collection helper is a secondary C++ base. The Nanite proxy's primitive ancestry is through Nanite::FSceneProxyBase.", "FGeometryCollectionSceneProxy|FNaniteGeometryCollectionSceneProxy"),
    profile("cache", "Geometry Cache animated vertices", "UGeometryCacheComponent|FGeometryCacheSceneProxy",
        "Streams or decodes time-sampled mesh data rather than applying a skeleton to one rest mesh.", [
            resource("FGeometryCacheSceneProxy render data",
                "Position buffers (including a pair for sample/history handling), tangent, texture-coordinate, color and index buffers provide the current animated mesh.",
                "Track/sample decoding and proxy metadata prepare uploads. GeometryCache subclasses can use different source formats such as Alembic or USD.",
                PLUGINS + "Runtime/GeometryCache/Source/GeometryCache/Public/GeometryCacheSceneProxy.h", "PositionBuffers[2]"),
        ], "Geometry Cache is not the skin-weight/bone-transform representation of a skeletal mesh.", "FGeometryCacheSceneProxy"),
    profile("dynamic", "Editable dynamic mesh", "UBaseDynamicMeshComponent|FBaseDynamicMeshSceneProxy",
        "Converts editable mesh topology into render-buffer sets and updates changed geometry or subsets.", [
            resource("FMeshRenderBufferSet",
                "Position, static-mesh tangent/UV and color buffers plus FDynamicMeshIndexBuffer32 primary/secondary triangle indices; optional ray-tracing geometry uses these streams.",
                "UDynamicMesh / FDynamicMesh3 store editable CPU topology. FMeshRenderBufferSet manages GPU stream wrappers and upload operations; the topology object itself is not a GPU mesh.",
                RUNTIME + "GeometryFramework/Public/Components/MeshRenderBufferSet.h", "class FMeshRenderBufferSet"),
        ], "Subclasses can split geometry into chunks or octree-based render sets; this does not change their declared component/proxy ancestry.", "FBaseDynamicMeshSceneProxy|FDynamicMeshSceneProxy"),
    profile("procedural", "Procedural triangle sections", "UProceduralMeshComponent|FProceduralMeshSceneProxy",
        "Builds per-section triangle render resources from supplied procedural vertices and indices.", [
            resource("FProcMeshProxySection",
                "FStaticMeshVertexBuffers, FDynamicMeshIndexBuffer32 and a local vertex factory feed section draws; optional ray-tracing geometry references the position/index buffers.",
                "CPU section arrays and collision data are distinct from the uploaded render streams.",
                PLUGINS + "Runtime/ProceduralMeshComponent/Source/ProceduralMeshComponent/Private/ProceduralMeshComponent.cpp", "class FProcMeshProxySection"),
        ], "Procedural describes how geometry is produced, not a new hardware primitive topology.", "FProceduralMeshSceneProxy"),
    profile("cable", "Cable tube geometry", "UCableComponent|FCableSceneProxy",
        "Builds a tube mesh around simulated cable points and updates it as the cable moves.", [
            resource("FCableSceneProxy / FCableIndexBuffer",
                "Generated tube vertex streams and triangle indices describe the cable surface.",
                "Cable simulation particles and constraints are CPU component state; the rendered surface uses ordinary mesh buffers.",
                PLUGINS + "Runtime/CableComponent/Source/CableComponent/Private/CableComponent.cpp", "class FCableIndexBuffer"),
        ], "There is no dedicated RHI 'cable' topology.", "FCableSceneProxy"),
    profile("niagara", "Niagara simulation & renderer data", "UNiagaraComponent|FNiagaraSceneProxy",
        "Connects a Niagara system to scene rendering. Sprite, mesh, ribbon, volume, light, decal and component renderers interpret emitter data differently.", [
            resource("FNiagaraDataBuffer",
                "GPUBufferFloat, GPUBufferHalf and GPUBufferInt hold separated particle attribute streams; GPUIDToIndexTable maps IDs. Counts, sorting and indirect arguments support GPU work. Mesh renderers also reference mesh geometry; ribbons generate segment geometry.",
                "CPU simulation uses CPU attribute arrays and uploads render data as needed; GPU simulation uses GPU buffers. FNiagaraDataBuffer and FNiagaraSceneProxy are CPU owners/interfaces, not particle structs placed wholesale on the GPU.",
                PLUGINS + "FX/Niagara/Source/Niagara/Classes/NiagaraDataSet.h", "FRWBuffer GPUBufferFloat;"),
        ], "Niagara renderer classes are not subclasses of FNiagaraSceneProxy. Light/component/decal renderers do not all produce a triangle mesh or share one attribute layout. NiagaraNanite adds its own static-mesh component and proxies.", "FNiagaraSceneProxy"),
    profile("cascade", "Cascade particle emitters", "UParticleSystemComponent|FParticleSystemSceneProxy",
        "Legacy particle scene integration: emitter render data supplies sprites, meshes and beam/trail geometry, with CPU or GPU simulation according to emitter type.", [
            resource("FParticleSystemSceneProxy / emitter render data",
                "Dynamic particle streams, referenced mesh buffers and emitter-specific GPU simulation resources provide the selected renderer's data.",
                "Emitter payload and proxy objects are CPU state; simulation mode determines which data is uploaded or generated on the GPU.",
                RUNTIME + "Engine/Public/ParticleSystemSceneProxy.h", "class FParticleSystemSceneProxy"),
        ], "Cascade and Niagara are separate UFXSystemComponent subclasses. A scene primitive can represent many particles.", "FParticleSystemSceneProxy"),
    profile("hair", "Groom strands, cards & mesh LODs", "UGroomComponent|FHairStrandsSceneProxy|Nanite::FGroomSceneProxy",
        "Represents hair as strands or selected card/mesh LODs with deformation, interpolation and culling resources; a Nanite groom proxy is a separate supported path.", [
            resource("FHairStrandsRestResource / FHairStrandsDeformedResource",
                "Rest/current/previous strand point positions, curve/point attributes, interpolation/root-binding data and culling buffers; card/mesh LODs use position/index/UV/material streams and textures.",
                "Groom asset and resource wrappers organize GPU allocations. Hair voxelization and visibility data are renderer-generated resources, not a CPU groom UObject copied to VRAM.",
                PLUGINS + "Runtime/HairStrands/Source/HairStrandsCore/Public/GroomResources.h", "struct FHairStrandsRestResource"),
        ], "The active representation determines which buffers exist. Nanite::FGroomSceneProxy derives Nanite::FSceneProxyBase, not FHairStrandsSceneProxy.", "FHairStrandsSceneProxy|Nanite::FGroomSceneProxy"),
    profile("volume", "Sparse volume / heterogeneous volume", "UHeterogeneousVolumeComponent|FHeterogeneousVolumeSceneProxy|USparseVolumeTextureViewerComponent|FSparseVolumeTextureViewerSceneProxy",
        "Supplies a bounded participating medium to volumetric rendering, often from animated sparse volume textures; the viewer proxy provides a separate inspection path.", [
            resource("UE::SVT::FTextureRenderResources",
                "Page-table texture and PhysicalTileDataA / PhysicalTileDataB textures store sparse indirection and physical voxel attributes. Volume shaders sample these to integrate scattering/extinction/emission.",
                "SparseVolumeTextureData CPU pages/tiles and streaming state prepare texture uploads. FTextureRenderResources contains RHI texture references, not the voxel data inline in a scene proxy.",
                RUNTIME + "Engine/Classes/SparseVolumeTexture/SparseVolumeTexture.h", "class FTextureRenderResources"),
        ], "A bounding primitive need not be a visible triangle surface; ray marching consumes a volume representation. Material/source choices can change the volume data.", "FHeterogeneousVolumeSceneProxy|FSparseVolumeTextureViewerSceneProxy"),
    profile("water", "Water mesh tiles", "UWaterMeshComponent|FWaterMeshSceneProxy",
        "Draws selected water surface tiles/LODs using water mesh resources and per-tile instance data.", [
            resource("FWaterMeshSceneProxy",
                "Tile mesh vertex/index data and dynamic per-tile rendering data support the selected water surface; materials sample additional water parameters/textures.",
                "Water quadtree/LOD selection and water-body components organize tiles on the CPU. UWaterBodyComponent is a separate primitive-component branch and is not the water mesh component itself.",
                PLUGINS + "Experimental/Water/Source/Runtime/Public/WaterMeshSceneProxy.h", "class FWaterMeshSceneProxy"),
        ], "UWaterBodyMeshComponent instead derives UStaticMeshComponent and uses the mesh representation. Water material passes are discussed separately in the pipeline.", "FWaterMeshSceneProxy"),
    profile("water-body", "Water body controller", "UWaterBodyComponent",
        "Defines a water body's shape and behavior and coordinates its associated water rendering/collision components.", [],
        "Do not assume every UWaterBodyComponent directly creates FWaterMeshSceneProxy. UWaterMeshComponent and UWaterBodyMeshComponent are separate classes with different declared bases.", "UWaterMeshComponent|UWaterBodyMeshComponent"),
    profile("heightfield", "Virtual heightfield mesh", "UVirtualHeightfieldMeshComponent|FVirtualHeightfieldMeshSceneProxy",
        "Renders a heightfield surface driven by runtime virtual texture height data and tile/LOD selection.", [
            resource("FVirtualHeightfieldMeshSceneProxy / virtual texture resources",
                "Virtual texture page-table/physical height tiles, height min/max and LOD bias textures accompany patch geometry and selected tile work.",
                "The proxy references IAllocatedVirtualTexture and vertex-factory state; virtual texture storage is managed separately from the component.",
                PLUGINS + "Experimental/VirtualHeightfieldMesh/Source/VirtualHeightfieldMesh/Private/VirtualHeightfieldMeshSceneProxy.h", "HeightMinMaxTexture"),
        ], "This plugin's heightfield representation is distinct from Landscape and Nanite even when they depict similar terrain.", "FVirtualHeightfieldMeshSceneProxy"),
    profile("points", "LiDAR point cloud", "ULidarPointCloudComponent|FLidarPointCloudSceneProxy",
        "Selects and renders visible point-cloud batches with the LiDAR plugin's point rendering resources.", [
            resource("LiDAR point-cloud render buffers",
                "Selected point records and point-rendering buffers are uploaded for the configured point/splat display. A point-cloud scene primitive can contain many points.",
                "The point-cloud octree and batch selection are CPU organization; they are not the draw topology enum itself.",
                PLUGINS + "Enterprise/LidarPointCloud/Source/LidarPointCloudRuntime/Private/Rendering/LidarPointCloudRenderBuffers.h", "class FLidarPointCloud"),
        ], "Do not infer PT_PointList just from the asset name: point/splat expansion and shader path determine actual draw topology.", "FLidarPointCloudSceneProxy"),
    profile("paper", "Paper2D sprites, flipbooks & tiles", "UPaperSpriteComponent|UPaperFlipbookComponent|UPaperGroupedSpriteComponent|UPaperTileMapComponent|UPaperTerrainComponent|FPaperRenderSceneProxy",
        "Converts sprite, flipbook, grouped-sprite, tile-map or 2D terrain render data into textured geometry.", [
            resource("FPaperRenderSceneProxy",
                "Sprite/tile vertex and triangle index data plus atlas/material textures supply batched 2D draws in the 3D scene.",
                "Sprite assets, animation frames and tile layers choose the render data on the CPU. Sprite proxy subclasses are real inheritance; sprite textures are associations.",
                PLUGINS + "2D/Paper2D/Source/Paper2D/Private/PaperRenderSceneProxy.h", "class FPaperRenderSceneProxy"),
        ], "A sprite scene proxy is not a special GPU UObject or hardware sprite topology.", "FPaperRenderSceneProxy"),
    profile("widget", "World-space widget surface", "UWidgetComponent|FWidget3DSceneProxy",
        "Displays a rendered widget on geometry in the world, using the widget component's material and render target.", [
            resource("FWidget3DSceneProxy / widget render target",
                "Widget render-target texels plus quad or cylindrical surface geometry are consumed by the world-space material.",
                "UMG/Slate widget objects and layout are CPU-side UI state; they are not scene-proxy subclasses or GPU allocations.",
                RUNTIME + "UMG/Private/Components/WidgetComponent.cpp", "class FWidget3DSceneProxy"),
        ], "Screen-space widgets do not necessarily use this world primitive path. Collision/debug proxy alternatives are separate classes.", "FWidget3DSceneProxy"),
    profile("debug", "Shapes, lines & debug geometry", "UShapeComponent|UDebugDrawComponent|UArrowComponent|ULineBatchComponent|UDrawFrustumComponent|USplineComponent|FDebugRenderSceneProxy|FArrowSceneProxy|FLineBatcherSceneProxy|FBoxSceneProxy|FSphereSceneProxy|FDrawCylinderSceneProxy|FDrawFrustumSceneProxy|FSplinePDISceneProxy|FSplineMeshSceneProxy@Engine/Private/Components/SplineComponent.cpp|UBrushComponent|FBrushSceneProxy",
        "Represents collision shapes, helper lines or debug visualization. Rendering, when enabled, submits generated line/triangle geometry through primitive drawing interfaces.", [
            resource("FDebugRenderSceneProxy / dynamic drawing",
                "Transient line/triangle vertex and index data can be uploaded for visualization. A collision box, sphere or capsule does not require its own persistent visible GPU mesh.",
                "Collision/physics shapes, editor helpers and debug draw lists are CPU state. Show flags and relevance decide whether any rendering work is submitted.",
                RUNTIME + "Engine/Public/DebugRenderSceneProxy.h", "class FDebugRenderSceneProxy"),
        ], "A scene primitive, a physics primitive and an RHI draw primitive are different concepts. USplineComponent's local debug mesh proxy is separate from the global spline-mesh renderer."),
    profile("text-sprite", "Text & billboard geometry", "UTextRenderComponent|FTextRenderSceneProxy|UBillboardComponent|FSpriteSceneProxy|UMaterialBillboardComponent|FMaterialSpriteSceneProxy",
        "Generates camera-facing sprite or glyph geometry with material/texture data for scene rendering.", [
            resource("FTextRenderSceneProxy / billboard drawing",
                "Glyph or quad vertex/index data plus font/sprite textures and material uniforms supply draws.",
                "Text layout, component state and font/sprite references select CPU-generated render data.",
                RUNTIME + "Engine/Private/Components/TextRenderComponent.cpp", "class FTextRenderSceneProxy"),
        ], "Text/billboard scene classes do not imply a distinct hardware topology."),
    profile("model", "BSP model surfaces", "UModelComponent|FModelSceneProxy",
        "Draws model/BSP surfaces using model render data and material groupings.", [
            resource("FModelSceneProxy",
                "Model surface vertex/index data, material textures and lighting data feed triangle draws.",
                "BSP model nodes/surfaces and the proxy are CPU organization. A brush used for construction/collision is a different branch.",
                RUNTIME + "Engine/Private/ModelRender.cpp", "class FModelSceneProxy"),
        ], "UBrushComponent debug/collision visualization is not the same as the rendered UModelComponent surface representation.", "FModelSceneProxy"),
    profile("vector", "Vector-field simulation data", "UVectorFieldComponent|FVectorFieldSceneProxy",
        "Places a vector-field volume for effects simulation and optional field visualization.", [
            resource("Vector field resource",
                "A 3D texture stores vector samples for simulation; debug drawing can create visualization geometry.",
                "The component and field instance locate/configure the field on the CPU; the texture stores sampled vectors.",
                RUNTIME + "Engine/Private/VectorField.cpp", "FVectorFieldSceneProxy"),
        ], "A simulation volume does not automatically create an opaque mesh draw.", "FVectorFieldSceneProxy"),
    profile("custom", "Custom mesh triangles", "UCustomMeshComponent|FCustomMeshSceneProxy",
        "Uploads supplied custom triangle geometry through the CustomMeshComponent plugin's scene proxy.", [
            resource("FCustomMeshSceneProxy",
                "Generated vertex/index buffers and material bindings supply conventional triangle draws.",
                "Custom triangle arrays and proxy state are CPU-side inputs to render resource creation.",
                PLUGINS + "Runtime/CustomMeshComponent/Source/CustomMeshComponent/Private/CustomMeshComponent.cpp", "class FCustomMeshSceneProxy"),
        ], "This plugin is a different class branch from UProceduralMeshComponent even though both render generated triangles.", "FCustomMeshSceneProxy"),
]

TOPOLOGY = dict(name="EPrimitiveType · hardware draw topology",
    text="This enum is separate from the scene-class tree. It describes how a draw interprets vertices/indices: PT_TriangleList, PT_TriangleStrip, PT_LineList, PT_QuadList, PT_PointList and PT_RectList in this checkout. Quad and rectangle topology require the corresponding RHI capability. Nanite clusters, hair strands, particles, voxels and spline meshes are scene/data representations; their implementation chooses raster topology or compute work.",
    sourceQuery=(RUNTIME + "RHI/Public/RHIDefinitions.h", "enum EPrimitiveType"))
