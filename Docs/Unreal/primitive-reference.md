# Primitive classes and GPU storage

Unreal Engine 5.8.1, commit `71fe36aac5a8df5ccd66c763ffc902b29b6a9c43`.

Historical declaration/storage appendix, generated from `primitives.js` and authored storage associations. The current [scene representation guide](scene-types.html) organizes renderer input requirements by geometry, materials and other scene data; it does not display these inheritance trees.

## What each edge means

Every indentation below means a direct, uniquely resolved C++ base-class relationship. The two trees start at UPrimitiveComponent and FPrimitiveSceneProxy. Creating a proxy, owning a buffer, and producing a render target are associations, never tree edges. All bases, including interfaces and mixins outside these two roots, appear in the declaration inventory below. A multiply derived class can appear under each applicable primitive base. No class is reparented to make a module group.

Actual explicit class/struct inheritance rooted at UPrimitiveComponent and FPrimitiveSceneProxy, discovered transitively across Engine/Source/Runtime and Engine/Plugins (.h/.cpp/.inl). Includes optional plugins, editor helpers inside plugins, local classes and inactive conditional definitions; excludes ThirdParty, Test and Tests folders. Only uniquely resolved bases become tree edges. Aliases and unsupported declaration macros can remain unparsed; ambiguous descendants are reported separately. This is source-definition coverage, not a promise every class exists or renders in one build.

Discovered 371 primitive class definitions. Class IDs include source locations, so same-name local/conditional definitions remain distinct. The extractor does not model function names as scopes; paths and lines disambiguate local classes.

GPU associations below are reviewed explanations for major rendering families, not compiler-extracted ownership or a complete field layout for every optional plugin. Unmapped subclasses show their nearest mapped base profile explicitly as context; overrides may select different resources. Component-to-proxy choices listed are related implementations, not inheritance or a guarantee that every configuration instantiates every listed proxy. CPU wrappers and the data actually resident on the GPU are distinguished.

## Actual inheritance trees

- `UPrimitiveComponent` — `Engine/Source/Runtime/Engine/Classes/Components/PrimitiveComponent.h:307`
  - `UArrowComponent` — `Engine/Source/Runtime/Engine/Classes/Components/ArrowComponent.h:19`
  - `UAvaTickerComponent` — `Engine/Plugins/VirtualProduction/Avalanche/Source/Avalanche/Public/Framework/Ticker/AvaTickerComponent.h:60`
  - `UBakedShallowWaterSimulationComponent` — `Engine/Plugins/Experimental/Water/Source/Runtime/Public/BakedShallowWaterSimulationComponent.h:350`
  - `UBillboardComponent` — `Engine/Source/Runtime/Engine/Classes/Components/BillboardComponent.h:19`
  - `UBrushComponent` — `Engine/Source/Runtime/Engine/Classes/Components/BrushComponent.h:21`
  - `UClusterUnionComponent` — `Engine/Source/Runtime/Engine/Classes/PhysicsEngine/ClusterUnionComponent.h:210`
    - `UClusterUnionVehicleComponent` — `Engine/Plugins/Experimental/ChaosModularVehicle/Source/ChaosModularVehicleEngine/Public/ChaosModularVehicle/ClusterUnionVehicleComponent.h:12`
  - `UColorCorrectionInvisibleComponent` — `Engine/Plugins/Experimental/ColorCorrectRegions/Source/ColorCorrectRegions/Public/ColorCorrectRegion.h:438`
  - `UControlRigComponent` — `Engine/Plugins/Animation/ControlRig/Source/ControlRig/Public/ControlRigComponent.h:175`
  - `UDataflowComponent` — `Engine/Plugins/Dataflow/Source/DataflowEnginePlugin/Public/Dataflow/DataflowComponent.h:21`
  - `UDebugDrawComponent` — `Engine/Source/Runtime/Engine/Classes/Debug/DebugDrawComponent.h:49`
    - `UChaosPathedMovementDebugDrawComponent` — `Engine/Plugins/Experimental/ChaosMover/Source/ChaosMover/Public/ChaosMover/PathedMovement/ChaosPathedMovementDebugDrawComponent.h:41`
    - `UDataflowDebugDrawComponent` — `Engine/Source/Runtime/Dataflow/Engine/Public/Dataflow/DataflowDebugDrawComponent.h:12`
    - `UDataflowDebugMeshComponent` — `Engine/Plugins/Dataflow/Source/DataflowEditor/Public/DataflowRendering/DataflowDebugMeshComponent.h:13`
    - `UEQSRenderingComponent` — `Engine/Source/Runtime/AIModule/Classes/EnvironmentQuery/EQSRenderingComponent.h:80`
    - `UGameplayDebuggerRenderingComponent` — `Engine/Source/Runtime/GameplayDebugger/Public/GameplayDebuggerRenderingComponent.h:36`
    - `UISMPoolDebugDrawComponent` — `Engine/Source/Runtime/Experimental/ISMPool/Public/ISMPool/ISMPoolDebugDrawComponent.h:15`
    - `UMassNavigationTestingComponent` — `Engine/Plugins/AI/MassAI/Source/MassNavigationEditor/Private/MassNavigationTestingActor.h:33`
    - `UNavCorridorTestingComponent` — `Engine/Plugins/Runtime/NavCorridor/Source/NavCorridor/Public/NavCorridorTestingComponent.h:20`
    - `UNavMeshRenderingComponent` — `Engine/Source/Runtime/NavigationSystem/Public/NavMesh/NavMeshRenderingComponent.h:193`
    - `UNavTestRenderingComponent` — `Engine/Source/Runtime/NavigationSystem/Public/NavMesh/NavTestRenderingComponent.h:115`
    - `UPCGDebugDrawComponent` — `Engine/Plugins/PCG/Source/PCG/Public/PCGDebugDrawComponent.h:32`
    - `USmartObjectDebugRenderingComponent` — `Engine/Plugins/Runtime/SmartObjects/Source/SmartObjectsModule/Public/SmartObjectDebugRenderingComponent.h:17`
      - `USmartObjectSubsystemRenderingComponent` — `Engine/Plugins/Runtime/SmartObjects/Source/SmartObjectsModule/Private/SmartObjectSubsystemRenderingActor.h:12`
      - `USmartObjectTestRenderingComponent` — `Engine/Plugins/Runtime/SmartObjects/Source/SmartObjectsModule/Private/SmartObjectTestingActor.h:115`
    - `UZoneGraphAnnotationComponent` — `Engine/Plugins/Runtime/ZoneGraphAnnotations/Source/ZoneGraphAnnotations/Public/ZoneGraphAnnotationComponent.h:38`
      - `USmartObjectZoneAnnotations` — `Engine/Plugins/Runtime/MassGameplay/Source/MassSmartObjects/Public/SmartObjectZoneAnnotations.h:95`
      - `UZoneGraphCrowdLaneAnnotations` — `Engine/Plugins/AI/MassCrowd/Source/MassCrowd/Public/ZoneGraphCrowdLaneAnnotations.h:36`
      - `UZoneGraphDisturbanceAnnotation` — `Engine/Plugins/Runtime/ZoneGraphAnnotations/Source/ZoneGraphAnnotations/Public/Annotations/ZoneGraphDisturbanceAnnotation.h:154`
    - `UZoneGraphAnnotationTestingComponent` — `Engine/Plugins/Runtime/ZoneGraphAnnotations/Source/ZoneGraphAnnotations/Public/ZoneGraphAnnotationTestingActor.h:43`
  - `UDeformablePhysicsComponent` — `Engine/Plugins/Experimental/ChaosFlesh/Source/ChaosFleshEngine/Public/ChaosFlesh/ChaosDeformablePhysicsComponent.h:21`
    - `UDeformableCollisionsComponent` — `Engine/Plugins/Experimental/ChaosFlesh/Source/ChaosFleshEngine/Public/ChaosFlesh/ChaosDeformableCollisionsComponent.h:19`
    - `UDeformableConstraintsComponent` — `Engine/Plugins/Experimental/ChaosFlesh/Source/ChaosFleshEngine/Public/ChaosFlesh/ChaosDeformableConstraintsComponent.h:94`
    - `UDeformableTetrahedralComponent` — `Engine/Plugins/Experimental/ChaosFlesh/Source/ChaosFleshEngine/Public/ChaosFlesh/ChaosDeformableTetrahedralComponent.h:90`
      - `UDeformableGameplayComponent` — `Engine/Plugins/Experimental/ChaosFlesh/Source/ChaosFleshEngine/Public/ChaosFlesh/ChaosDeformableGameplayComponent.h:55`
        - `UFleshComponent` — `Engine/Plugins/Experimental/ChaosFlesh/Source/ChaosFleshEngine/Public/ChaosFlesh/FleshComponent.h:29`
          - `UFleshGeneratorComponent` — `Engine/Plugins/Animation/MLDeformer/ChaosFleshGenerator/Source/ChaosFleshGenerator/Private/FleshGeneratorComponent.h:20`
  - `UDrawFrustumComponent` — `Engine/Source/Runtime/Engine/Classes/Components/DrawFrustumComponent.h:18`
  - `UE::MeshPartition::UMeshPartitionCollisionComponent` — `Engine/Plugins/Experimental/MeshPartition/Source/MeshPartition/Public/MeshPartitionCollisionComponent.h:35`
  - `UE::MeshPartition::UMeshPartitionComponent` — `Engine/Plugins/Experimental/MeshPartition/Source/MeshPartition/Public/MeshPartitionComponent.h:24`
    - `UE::MeshPartition::UMeshPartitionEditorComponent` — `Engine/Plugins/Experimental/MeshPartition/Source/MeshPartitionEditor/Public/MeshPartitionEditorComponent.h:81`
  - `UE::MeshPartition::UModifierComponent` — `Engine/Plugins/Experimental/MeshPartition/Source/MeshPartitionEditor/Public/MeshPartitionModifierComponent.h:219`
    - `UE::MeshPartition::UEditableModifierBase` — `Engine/Plugins/Experimental/MeshPartition/Source/MeshPartitionEditor/Public/Modifiers/MeshPartitionEditableModifierBase.h:17`
      - `UE::MeshPartition::ULatticeModifier` — `Engine/Plugins/Experimental/MeshPartition/Source/MeshPartitionEditor/Public/Modifiers/MeshPartitionLatticeModifier.h:17`
      - `UE::MeshPartition::UProjectMeshLayersModifier` — `Engine/Plugins/Experimental/MeshPartition/Source/MeshPartitionEditor/Public/Modifiers/MeshPartitionProjectSculptLayersModifier.h:86`
    - `UE::MeshPartition::UInstancedProjectionModifier` — `Engine/Plugins/Experimental/MeshPartition/Source/MeshPartitionEditor/Public/Modifiers/MeshPartitionInstancedProjectionModifier.h:47`
    - `UE::MeshPartition::ULevelInstanceAdapter` — `Engine/Plugins/Experimental/MeshPartition/Source/MeshPartitionEditor/Public/Modifiers/MeshPartitionLevelInstanceAdapter.h:20`
    - `UE::MeshPartition::UMeshBasedModifierBase` — `Engine/Plugins/Experimental/MeshPartition/Source/MeshPartitionEditor/Public/Modifiers/MeshPartitionMeshBasedModifierBase.h:98`
      - `UE::MeshPartition::UBooleanModifier` — `Engine/Plugins/Experimental/MeshPartition/Source/MeshPartitionEditor/Public/Modifiers/MeshPartitionBooleanModifier.h:105`
      - `UE::MeshPartition::UMeshProjectModifier` — `Engine/Plugins/Experimental/MeshPartition/Source/MeshPartitionEditor/Public/Modifiers/MeshPartitionMeshProjectModifier.h:19`
    - `UE::MeshPartition::UMeshProviderModifier` — `Engine/Plugins/Experimental/MeshPartition/Source/MeshPartitionEditor/Public/Modifiers/MeshPartitionMeshProvider.h:28`
    - `UE::MeshPartition::UNoiseModifier` — `Engine/Plugins/Experimental/MeshPartition/Source/MeshPartitionEditor/Public/Modifiers/MeshPartitionNoiseModifier.h:66`
    - `UE::MeshPartition::UPCGAdapterComponent` — `Engine/Plugins/Experimental/PCGMeshPartitionInterop/Source/PCGMeshPartitionInteropEditor/Public/MeshPartitionPCGAdapterComponent.h:23`
    - `UE::MeshPartition::UPatchModifier` — `Engine/Plugins/Experimental/MeshPartition/Source/MeshPartitionEditor/Public/Modifiers/MeshPartitionPatchModifier.h:27`
      - `UE::MeshPartition::UInstancedPatchModifier` — `Engine/Plugins/Experimental/MeshPartition/Source/MeshPartitionEditor/Public/Modifiers/MeshPartitionInstancedPatchModifier.h:26`
    - `UE::MeshPartition::URemeshModifierBase` — `Engine/Plugins/Experimental/MeshPartition/Source/MeshPartitionEditor/Public/Modifiers/MeshPartitionRemeshModifier.h:33`
      - `UE::MeshPartition::URemeshModifier` — `Engine/Plugins/Experimental/MeshPartition/Source/MeshPartitionEditor/Public/Modifiers/MeshPartitionRemeshModifier.h:281`
      - `UE::MeshPartition::USplineRemeshModifier` — `Engine/Plugins/Experimental/MeshPartition/Source/MeshPartitionEditor/Public/Modifiers/MeshPartitionSplineRemeshModifier.h:21`
    - `UE::MeshPartition::USimpleWriteModifier` — `Engine/Plugins/Experimental/MeshPartition/Source/MeshPartitionEditor/Public/Modifiers/MeshPartitionSimpleWriteModifier.h:49`
    - `UE::MeshPartition::USplineModifier` — `Engine/Plugins/Experimental/MeshPartition/Source/MeshPartitionEditor/Public/Modifiers/MeshPartitionSplineModifier.h:121`
    - `UE::MeshPartition::UTexturePatchModifier` — `Engine/Plugins/Experimental/MeshPartition/Source/MeshPartitionEditor/Public/Modifiers/MeshPartitionTexturePatchModifier.h:363`
      - `UE::MeshPartition::UInstancedTexturePatchModifier` — `Engine/Plugins/Experimental/MeshPartition/Source/MeshPartitionEditor/Public/Modifiers/MeshPartitionInstancedTexturePatchModifier.h:15`
    - `UE::MeshPartition::UWaterModifier` — `Engine/Plugins/Experimental/MeshPartitionWater/Source/MeshPartitionWater/Public/MeshPartitionWaterModifier.h:22`
      - `UE::MeshPartition::ULakeModifier` — `Engine/Plugins/Experimental/MeshPartitionWater/Source/MeshPartitionWater/Private/MeshPartitionLakeModifier.h:26`
      - `UE::MeshPartition::UOceanModifier` — `Engine/Plugins/Experimental/MeshPartitionWater/Source/MeshPartitionWater/Private/MeshPartitionOceanModifier.h:13`
      - `UE::MeshPartition::URiverModifier` — `Engine/Plugins/Experimental/MeshPartitionWater/Source/MeshPartitionWater/Private/MeshPartitionRiverModifier.h:23`
    - `UE::MeshPartition::UWeightUtilityModifier` — `Engine/Plugins/Experimental/MeshPartition/Source/MeshPartitionEditor/Public/Modifiers/MeshPartitionWeightUtilityModifier.h:22`
  - `UFXSystemComponent` — `Engine/Source/Runtime/Engine/Classes/Particles/ParticleSystemComponent.h:379`
    - `UNiagaraComponent` — `Engine/Plugins/FX/Niagara/Source/Niagara/Public/NiagaraComponent.h:57`
      - `UCEClonerComponent` — `Engine/Plugins/VirtualProduction/ClonerEffector/Source/ClonerEffector/Public/Cloner/CEClonerComponent.h:26`
      - `UNiagaraCullProxyComponent` — `Engine/Plugins/FX/Niagara/Source/Niagara/Public/NiagaraCullProxyComponent.h:23`
      - `UNiagaraUIComponent` — `Engine/Plugins/FX/NiagaraUIRenderer/Source/NiagaraUIRenderer/Public/NiagaraUIComponent.h:10`
    - `UParticleSystemComponent` — `Engine/Source/Runtime/Engine/Classes/Particles/ParticleSystemComponent.h:491`
      - `UCascadeParticleSystemComponent` — `Engine/Plugins/FX/Cascade/Source/Cascade/Classes/CascadeParticleSystemComponent.h:14`
  - `UFastGeoSurrogateComponent` — `Engine/Plugins/Experimental/FastGeoStreaming/Source/FastGeoStreaming/Internal/FastGeoSurrogateComponent.h:13`
  - `UFieldSystemComponent` — `Engine/Source/Runtime/Experimental/FieldSystem/Source/FieldSystemEngine/Public/Field/FieldSystemComponent.h:37`
  - `UGizmoBaseComponent` — `Engine/Source/Runtime/InteractiveToolsFramework/Public/BaseGizmos/GizmoBaseComponent.h:41`
    - `UGizmoArrowComponent` — `Engine/Source/Runtime/InteractiveToolsFramework/Public/BaseGizmos/GizmoArrowComponent.h:15`
    - `UGizmoBoxComponent` — `Engine/Source/Runtime/InteractiveToolsFramework/Public/BaseGizmos/GizmoBoxComponent.h:15`
    - `UGizmoCircleComponent` — `Engine/Source/Runtime/InteractiveToolsFramework/Public/BaseGizmos/GizmoCircleComponent.h:15`
    - `UGizmoLineHandleComponent` — `Engine/Source/Runtime/InteractiveToolsFramework/Public/BaseGizmos/GizmoLineHandleComponent.h:16`
    - `UGizmoRectangleComponent` — `Engine/Source/Runtime/InteractiveToolsFramework/Public/BaseGizmos/GizmoRectangleComponent.h:15`
  - `UImagePlateComponent` — `Engine/Plugins/Experimental/ImagePlate/Source/ImagePlate/Public/ImagePlateComponent.h:58`
  - `UImagePlateFrustumComponent` — `Engine/Plugins/Experimental/ImagePlate/Source/ImagePlate/Private/ImagePlateFrustumComponent.h:13`
  - `UInstancedActorsModifierVolumeComponent` — `Engine/Plugins/Runtime/InstancedActors/Source/InstancedActors/Public/InstancedActorsModifierVolumeComponent.h:28`
    - `URemoveInstancesModifierVolumeComponent` — `Engine/Plugins/Runtime/InstancedActors/Source/InstancedActors/Public/InstancedActorsModifierVolumeComponent.h:138`
  - `ULakeCollisionComponent` — `Engine/Plugins/Experimental/Water/Source/Runtime/Public/LakeCollisionComponent.h:11`
  - `ULandscapeComponent` — `Engine/Source/Runtime/Landscape/Classes/LandscapeComponent.h:431`
  - `ULandscapeGizmoRenderComponent` — `Engine/Source/Runtime/Landscape/Classes/LandscapeGizmoRenderComponent.h:14`
  - `ULandscapeHeightfieldCollisionComponent` — `Engine/Source/Runtime/Landscape/Classes/LandscapeHeightfieldCollisionComponent.h:41`
  - `ULandscapeSplinesComponent` — `Engine/Source/Runtime/Landscape/Classes/LandscapeSplinesComponent.h:106`
  - `ULineBatchComponent` — `Engine/Source/Runtime/Engine/Classes/Components/LineBatchComponent.h:127`
  - `UMRMeshComponent` — `Engine/Source/Runtime/MRMesh/Public/MRMeshComponent.h:105`
  - `UMassCrowdLaneDataRenderingComponent` — `Engine/Plugins/AI/MassCrowd/Source/MassCrowd/Public/MassCrowdLaneDataRenderingComponent.h:15`
  - `UMaterialBillboardComponent` — `Engine/Source/Runtime/Engine/Classes/Components/MaterialBillboardComponent.h:61`
    - `UMixedRealityCaptureBillboard` — `Engine/Plugins/Runtime/MixedRealityCaptureFramework/Source/MixedRealityCaptureFramework/Private/MrcProjectionBillboard.h:12`
  - `UMeshComponent` — `Engine/Source/Runtime/Engine/Classes/Components/MeshComponent.h:24`
    - `UBaseDynamicMeshComponent` — `Engine/Source/Runtime/GeometryFramework/Public/Components/BaseDynamicMeshComponent.h:124`
      - `UDynamicMeshComponent` — `Engine/Source/Runtime/GeometryFramework/Public/Components/DynamicMeshComponent.h:171`
        - `UDataflowEditorCollectionComponent` — `Engine/Plugins/Dataflow/Source/DataflowEditor/Private/Dataflow/DataflowEditorCollectionComponent.h:16`
        - `UMetaHumanTemplateMesh` — `Engine/Plugins/MetaHuman/MetaHumanAnimator/Source/MetaHumanIdentity/Public/MetaHumanIdentityParts.h:649`
        - `USVGBaseDynamicMeshComponent` — `Engine/Plugins/VirtualProduction/SVGImporter/Source/SVGImporter/Public/ProceduralMeshes/SVGBaseDynamicMeshComponent.h:9`
          - `UJoinedSVGDynamicMeshComponent` — `Engine/Plugins/VirtualProduction/SVGImporter/Source/SVGImporter/Public/ProceduralMeshes/JoinedSVGDynamicMeshComponent.h:58`
          - `USVGDynamicMeshComponent` — `Engine/Plugins/VirtualProduction/SVGImporter/Source/SVGImporter/Public/ProceduralMeshes/SVGDynamicMeshComponent.h:46`
            - `USVGFillComponent` — `Engine/Plugins/VirtualProduction/SVGImporter/Source/SVGImporter/Private/ProceduralMeshes/SVGFillComponent.h:65`
            - `USVGStrokeComponent` — `Engine/Plugins/VirtualProduction/SVGImporter/Source/SVGImporter/Private/ProceduralMeshes/SVGStrokeComponent.h:35`
        - `USkeletalMeshBackedDynamicMeshComponent` — `Engine/Plugins/Animation/SkeletalMeshModelingTools/Source/SkeletalMeshModelingTools/Private/Components/SKMBackedDynaMeshComponent.h:29`
      - `UOctreeDynamicMeshComponent` — `Engine/Plugins/Runtime/MeshModelingToolset/Source/ModelingComponents/Public/Components/OctreeDynamicMeshComponent.h:37`
    - `UBasicLineSetComponentBase` — `Engine/Plugins/Editor/UVEditor/Source/UVEditorTools/Public/Drawing/BasicLineSetComponent.h:32`
      - `UBasic2DLineSetComponent` — `Engine/Plugins/Editor/UVEditor/Source/UVEditorTools/Public/Drawing/BasicLineSetComponent.h:111`
      - `UBasic3DLineSetComponent` — `Engine/Plugins/Editor/UVEditor/Source/UVEditorTools/Public/Drawing/BasicLineSetComponent.h:140`
    - `UBasicPointSetComponentBase` — `Engine/Plugins/Editor/UVEditor/Source/UVEditorTools/Public/Drawing/BasicPointSetComponent.h:32`
      - `UBasic2DPointSetComponent` — `Engine/Plugins/Editor/UVEditor/Source/UVEditorTools/Public/Drawing/BasicPointSetComponent.h:111`
      - `UBasic3DPointSetComponent` — `Engine/Plugins/Editor/UVEditor/Source/UVEditorTools/Public/Drawing/BasicPointSetComponent.h:140`
    - `UBasicTriangleSetComponentBase` — `Engine/Plugins/Editor/UVEditor/Source/UVEditorTools/Public/Drawing/BasicTriangleSetComponent.h:32`
      - `UBasic2DTriangleSetComponent` — `Engine/Plugins/Editor/UVEditor/Source/UVEditorTools/Public/Drawing/BasicTriangleSetComponent.h:89`
      - `UBasic3DTriangleSetComponent` — `Engine/Plugins/Editor/UVEditor/Source/UVEditorTools/Public/Drawing/BasicTriangleSetComponent.h:118`
    - `UCableComponent` — `Engine/Plugins/Runtime/CableComponent/Source/CableComponent/Classes/CableComponent.h:31`
    - `UCustomMeshComponent` — `Engine/Plugins/Runtime/CustomMeshComponent/Source/CustomMeshComponent/Classes/CustomMeshComponent.h:31`
    - `UE::MeshPartition::UPreviewMeshComponent` — `Engine/Plugins/Experimental/MeshPartition/Source/MeshPartitionEditor/Public/MeshPartitionPreviewComponents.h:38`
    - `UGeometryCacheComponent` — `Engine/Plugins/Runtime/GeometryCache/Source/GeometryCache/Classes/GeometryCacheComponent.h:37`
      - `UGeometryCacheAbcFileComponent` — `Engine/Plugins/Experimental/GeometryCacheAbcFile/Source/GeometryCacheAbcFile/Public/GeometryCacheAbcFileComponent.h:15`
      - `UGeometryCacheUsdComponent` — `Engine/Plugins/Importers/USDImporter/Source/GeometryCacheUSD/Public/GeometryCacheUSDComponent.h:16`
    - `UGeometryCollectionComponent` — `Engine/Source/Runtime/Experimental/GeometryCollectionEngine/Public/GeometryCollection/GeometryCollectionComponent.h:577`
    - `UGroomComponent` — `Engine/Plugins/Runtime/HairStrands/Source/HairStrandsCore/Public/GroomComponent.h:29`
    - `UGroomSolverComponent` — `Engine/Plugins/Runtime/HairStrands/Source/HairStrandsSolver/Public/GroomSolverComponent.h:99`
    - `UHeterogeneousVolumeComponent` — `Engine/Source/Runtime/Engine/Classes/Components/HeterogeneousVolumeComponent.h:20`
    - `ULidarPointCloudComponent` — `Engine/Plugins/Enterprise/LidarPointCloud/Source/LidarPointCloudRuntime/Public/LidarPointCloudComponent.h:24`
    - `ULineSetComponent` — `Engine/Plugins/Runtime/MeshModelingToolset/Source/ModelingComponents/Public/Drawing/LineSetComponent.h:42`
    - `UMeshWireframeComponent` — `Engine/Plugins/Runtime/MeshModelingToolset/Source/ModelingComponents/Public/Drawing/MeshWireframeComponent.h:92`
    - `UPaperFlipbookComponent` — `Engine/Plugins/2D/Paper2D/Source/Paper2D/Classes/PaperFlipbookComponent.h:24`
    - `UPaperGroupedSpriteComponent` — `Engine/Plugins/2D/Paper2D/Source/Paper2D/Classes/PaperGroupedSpriteComponent.h:58`
    - `UPaperSpriteComponent` — `Engine/Plugins/2D/Paper2D/Source/Paper2D/Classes/PaperSpriteComponent.h:29`
    - `UPaperTileMapComponent` — `Engine/Plugins/2D/Paper2D/Source/Paper2D/Classes/PaperTileMapComponent.h:38`
    - `UPointSetComponent` — `Engine/Plugins/Runtime/MeshModelingToolset/Source/ModelingComponents/Public/Drawing/PointSetComponent.h:50`
    - `UProceduralMeshComponent` — `Engine/Plugins/Runtime/ProceduralMeshComponent/Source/ProceduralMeshComponent/Public/ProceduralMeshComponent.h:149`
      - `UAppleARKitFaceMeshComponent` — `Engine/Plugins/Runtime/AR/AppleAR/AppleARKitFaceSupport/Source/AppleARKitFaceSupport/Public/AppleARKitFaceMeshComponent.h:110`
      - `UCalibrationPointComponent` — `Engine/Plugins/VirtualProduction/CameraCalibrationCore/Source/CameraCalibrationCore/Public/CalibrationPointComponent.h:30`
      - `UDisplayClusterStageIsosphereComponent` — `Engine/Plugins/Runtime/nDisplay/Source/DisplayCluster/Public/Components/DisplayClusterStageIsosphereComponent.h:12`
      - `UMetaHumanDepthMeshComponent` — `Engine/Plugins/MetaHuman/MetaHumanAnimator/Source/MetaHumanImageViewerEditor/Public/MetaHumanDepthMeshComponent.h:12`
    - `USkinnedMeshComponent` — `Engine/Source/Runtime/Engine/Classes/Components/SkinnedMeshComponent.h:267`
      - `UChaosClothComponent` — `Engine/Plugins/ChaosClothAsset/Source/ChaosClothAssetEngine/Public/ChaosClothAsset/ClothComponent.h:86`
        - `UClothGeneratorComponent` — `Engine/Plugins/Animation/MLDeformer/ChaosClothGenerator/Source/ChaosClothGenerator/Private/ClothGeneratorComponent.h:32`
      - `UDestructibleComponent` — `Engine/Plugins/Runtime/ApexDestruction/Source/ApexDestruction/Public/DestructibleComponent.h:31`
      - `UInstancedSkinnedMeshComponent` — `Engine/Source/Runtime/Engine/Classes/Components/InstancedSkinnedMeshComponent.h:58`
        - `UHLODInstancedSkinnedMeshComponent` — `Engine/Source/Runtime/Engine/Public/WorldPartition/HLOD/HLODInstancedSkinnedMeshComponent.h:12`
      - `UPoseableMeshComponent` — `Engine/Source/Runtime/Engine/Classes/Components/PoseableMeshComponent.h:17`
        - `UPoseSearchMeshComponent` — `Engine/Plugins/Animation/PoseSearch/Source/Editor/Private/PoseSearchMeshComponent.h:9`
      - `USkeletalMeshComponent` — `Engine/Source/Runtime/Engine/Classes/Components/SkeletalMeshComponent.h:341`
        - `UDirectMeshControlComponent` — `Engine/Plugins/Experimental/Animation/DirectMeshControl/Source/DirectMeshControl/Public/DirectMeshControlComponent.h:17`
        - `UInsightsSkeletalMeshComponent` — `Engine/Plugins/Animation/GameplayInsights/Source/GameplayInsightsEditor/Public/InsightsSkeletalMeshComponent.h:16`
        - `USkeletalGeneratorComponent` — `Engine/Plugins/Animation/MLDeformer/ChaosFleshGenerator/Source/ChaosFleshGenerator/Private/FleshGeneratorComponent.h:40`
        - `USkeletalMeshComponentBudgeted` — `Engine/Plugins/Runtime/AnimationBudgetAllocator/Source/AnimationBudgetAllocator/Public/SkeletalMeshComponentBudgeted.h:23`
    - `UStaticMeshComponent` — `Engine/Source/Runtime/Engine/Classes/Components/StaticMeshComponent.h:105`
      - `UCameraProxyMeshComponent` — `Engine/Source/Runtime/Engine/Classes/Camera/CameraComponent.h:19`
      - `UChaosVDStaticMeshComponent` — `Engine/Plugins/ChaosVD/Source/ChaosVD/Private/Components/ChaosVDStaticMeshComponent.h:12`
      - `UCompositeDepthMeshComponent` — `Engine/Plugins/Compositing/Composite/Source/Composite/Public/Components/CompositeDepthMeshComponent.h:13`
      - `UCompositeMeshComponent` — `Engine/Plugins/Compositing/Composite/Source/Composite/Public/Components/CompositeMeshComponent.h:27`
      - `UControlPointMeshComponent` — `Engine/Source/Runtime/Landscape/Classes/ControlPointMeshComponent.h:11`
      - `UCustomStaticMeshComponent` — `Engine/Plugins/Enterprise/DataprepEditor/Source/DataprepEditor/Private/Widgets/SDataprepEditorViewport.h:33`
      - `UDisplayClusterScreenComponent` — `Engine/Plugins/Runtime/nDisplay/Source/DisplayCluster/Public/Components/DisplayClusterScreenComponent.h:15`
      - `UDisplayClusterWorldOriginComponent` — `Engine/Plugins/Runtime/nDisplay/Source/DisplayClusterConfigurator/Private/Views/Viewport/DisplayClusterWorldOriginComponent.h:14`
      - `UE::MeshPartition::UMeshPartitionStaticMeshComponent` — `Engine/Plugins/Experimental/MeshPartition/Source/MeshPartition/Public/MeshPartitionStaticMeshComponent.h:15`
        - `UE::MeshPartition::UStaticMeshPreviewComponent` — `Engine/Plugins/Experimental/MeshPartition/Source/MeshPartitionEditor/Public/MeshPartitionPreviewComponents.h:25`
      - `UInstancedStaticMeshComponent` — `Engine/Source/Runtime/Engine/Classes/Components/InstancedStaticMeshComponent.h:158`
        - `UChaosVDInstancedStaticMeshComponent` — `Engine/Plugins/ChaosVD/Source/ChaosVD/Private/Components/ChaosVDInstancedStaticMeshComponent.h:26`
        - `UHLODInstancedStaticMeshComponent` — `Engine/Source/Runtime/Engine/Public/WorldPartition/HLOD/HLODInstancedStaticMeshComponent.h:13`
        - `UHierarchicalInstancedStaticMeshComponent` — `Engine/Source/Runtime/Engine/Classes/Components/HierarchicalInstancedStaticMeshComponent.h:135`
          - `UFoliageInstancedStaticMeshComponent` — `Engine/Source/Runtime/Foliage/Public/FoliageInstancedStaticMeshComponent.h:20`
          - `UGrassInstancedStaticMeshComponent` — `Engine/Source/Runtime/Foliage/Public/GrassInstancedStaticMeshComponent.h:10`
        - `ULiveLinkDataPreviewComponent` — `Engine/Plugins/Animation/LiveLink/Source/LiveLink/Public/Visualizers/LiveLinkDataPreviewComponent.h:29`
        - `ULiveLinkMarkerVisualizer` — `Engine/Plugins/Animation/LiveLink/Source/LiveLink/Private/Visualizers/LiveLinkMarkerVisualizer.h:29`
        - `UPCapBoneVisualiser` — `Engine/Plugins/VirtualProduction/PerformanceCaptureWorkflow/Source/PerformanceCaptureWorkflow/Private/Visualizers/PCapBoneVisualizer.h:21`
      - `UInteractiveFoliageComponent` — `Engine/Source/Runtime/Foliage/Private/InteractiveFoliageComponent.h:14`
      - `ULandscapeMeshProxyComponent` — `Engine/Source/Runtime/Landscape/Classes/LandscapeMeshProxyComponent.h:16`
      - `ULandscapeNaniteComponent` — `Engine/Source/Runtime/Landscape/Classes/LandscapeNaniteComponent.h:82`
      - `UMediaStreamComponent` — `Engine/Plugins/Experimental/MediaStream/Source/MediaStream/Public/MediaStreamComponent.h:14`
      - `UNaniteDisplacedMeshComponent` — `Engine/Plugins/Experimental/NaniteDisplacedMesh/Source/NaniteDisplacedMesh/Public/NaniteDisplacedMeshComponent.h:17`
      - `UNiagaraStaticMeshComponent` — `Engine/Plugins/FX/NiagaraNanite/Source/NiagaraNanite/Private/Renderer/NiagaraStaticMeshComponent.h:16`
      - `UPCGProceduralISMComponent` — `Engine/Plugins/PCG/Source/PCG/Private/Components/PCGProceduralISMComponent.h:34`
      - `USplineMeshComponent` — `Engine/Source/Runtime/Engine/Classes/Components/SplineMeshComponent.h:119`
      - `UStereoStaticMeshComponent` — `Engine/Plugins/Experimental/PanoramicCapture/Source/PanoramicCapture/Private/StereoStaticMeshComponent.h:23`
      - `UViewAdjustedStaticMeshGizmoComponent` — `Engine/Source/Runtime/InteractiveToolsFramework/Public/BaseGizmos/ViewAdjustedStaticMeshGizmoComponent.h:24`
      - `UWaterBodyMeshComponent` — `Engine/Plugins/Experimental/Water/Source/Runtime/Public/WaterBodyMeshComponent.h:19`
        - `UWaterBodyInfoMeshComponent` — `Engine/Plugins/Experimental/Water/Source/Runtime/Private/WaterBodyInfoMeshComponent.h:17`
        - `UWaterBodyStaticMeshComponent` — `Engine/Plugins/Experimental/Water/Source/Runtime/Private/WaterBodyStaticMeshComponent.h:18`
      - `UXRCreativeGizmoMeshComponent` — `Engine/Plugins/Experimental/XRCreativeFramework/Source/XRCreative/Public/XRCreativeGizmos.h:181`
      - `UXRDeviceVisualizationComponent` — `Engine/Plugins/Runtime/XRBase/Source/XRBase/Public/XRDeviceVisualizationComponent.h:18`
    - `UTriangleSetComponent` — `Engine/Plugins/Runtime/MeshModelingToolset/Source/ModelingComponents/Public/Drawing/TriangleSetComponent.h:87`
    - `UWaterMeshComponent` — `Engine/Plugins/Experimental/Water/Source/Runtime/Public/WaterMeshComponent.h:19`
    - `UWidgetComponent` — `Engine/Source/Runtime/UMG/Public/Components/WidgetComponent.h:95`
      - `UDisplayClusterWidgetComponent` — `Engine/Plugins/Runtime/nDisplay/Source/DisplayCluster/Private/Game/EngineClasses/Scene/DisplayClusterWidgetComponent.h:13`
  - `UMetaHumanFootageComponent` — `Engine/Plugins/MetaHuman/MetaHumanAnimator/Source/MetaHumanImageViewerEditor/Public/MetaHumanFootageComponent.h:36`
  - `UMetaHumanPerformanceControlRigComponent` — `Engine/Plugins/MetaHuman/MetaHumanAnimator/Source/MetaHumanPerformance/Private/UI/MetaHumanPerformanceControlRigComponent.h:14`
  - `UMetaHumanTemplateMeshComponent` — `Engine/Plugins/MetaHuman/MetaHumanAnimator/Source/MetaHumanIdentity/Public/MetaHumanTemplateMeshComponent.h:28`
  - `UModelComponent` — `Engine/Source/Runtime/Engine/Classes/Components/ModelComponent.h:33`
  - `UMotionControllerComponent` — `Engine/Source/Runtime/HeadMountedDisplay/Public/MotionControllerComponent.h:18`
  - `UNavLinkComponent` — `Engine/Source/Runtime/NavigationSystem/Public/NavLinkComponent.h:16`
  - `UNavLinkRenderingComponent` — `Engine/Source/Runtime/NavigationSystem/Public/NavLinkRenderingComponent.h:14`
  - `UOceanCollisionComponent` — `Engine/Plugins/Experimental/Water/Source/Runtime/Public/OceanCollisionComponent.h:13`
  - `UPCGCollisionVisComponent` — `Engine/Plugins/PCG/Source/PCGEditor/Public/DataVisualizations/PCGCollisionVisComponent.h:10`
  - `UPVBoneComponent` — `Engine/Plugins/Experimental/ProceduralVegetationEditor/Source/ProceduralVegetationEditor/Private/Visualizations/PVBoneComponent.h:47`
  - `UPVLineBatchComponent` — `Engine/Plugins/Experimental/ProceduralVegetationEditor/Source/ProceduralVegetationEditor/Private/Visualizations/PVLineBatchComponent.h:64`
    - `UPVScaleVisualizationComponent` — `Engine/Plugins/Experimental/ProceduralVegetationEditor/Source/ProceduralVegetationEditor/Private/Visualizations/PVScaleVisualizationComponent.h:15`
    - `UPVSkeletonVisualizerComponent` — `Engine/Plugins/Experimental/ProceduralVegetationEditor/Source/ProceduralVegetationEditor/Private/Visualizations/PVSkeletonVisualizerComponent.h:23`
  - `UPaperTerrainComponent` — `Engine/Plugins/2D/Paper2D/Source/Paper2D/Classes/PaperTerrainComponent.h:54`
  - `UShallowWaterRiverComponent` — `Engine/Plugins/Experimental/WaterAdvanced/Source/WaterAdvanced/Public/ShallowWaterRiverActor.h:38`
  - `UShapeComponent` — `Engine/Source/Runtime/Engine/Classes/Components/ShapeComponent.h:24`
    - `UBoxComponent` — `Engine/Source/Runtime/Engine/Classes/Components/BoxComponent.h:18`
      - `UInteractionTargetComponent` — `Engine/Plugins/Experimental/InteractionInterface/Source/InteractableInterface/Public/InteractionTargetComponent.h:20`
      - `UOceanBoxCollisionComponent` — `Engine/Plugins/Experimental/Water/Source/Runtime/Public/OceanCollisionComponent.h:49`
    - `UCapsuleComponent` — `Engine/Source/Runtime/Engine/Classes/Components/CapsuleComponent.h:16`
    - `USphereComponent` — `Engine/Source/Runtime/Engine/Classes/Components/SphereComponent.h:17`
      - `UDrawSphereComponent` — `Engine/Source/Runtime/Engine/Classes/Components/DrawSphereComponent.h:18`
  - `USmartObjectContainerRenderingComponent` — `Engine/Plugins/Runtime/SmartObjects/Source/SmartObjectsModule/Public/SmartObjectContainerRenderingComponent.h:14`
  - `USmartObjectRenderingComponent` — `Engine/Plugins/Runtime/SmartObjects/Source/SmartObjectsModule/Public/SmartObjectRenderingComponent.h:14`
  - `USparseVolumeTextureViewerComponent` — `Engine/Source/Runtime/Renderer/Private/SparseVolumeTexture/SparseVolumeTextureViewerComponent.h:35`
  - `USplineComponent` — `Engine/Source/Runtime/Engine/Classes/Components/SplineComponent.h:214`
    - `UCineSplineComponent` — `Engine/Plugins/Experimental/CineCameraRigs/Source/CineCameraRigs/Public/CineSplineComponent.h:19`
    - `UPaperTerrainSplineComponent` — `Engine/Plugins/2D/Paper2D/Source/Paper2D/Classes/PaperTerrainSplineComponent.h:12`
    - `UWaterSplineComponent` — `Engine/Plugins/Experimental/Water/Source/Runtime/Public/WaterSplineComponent.h:27`
  - `UTextRenderComponent` — `Engine/Source/Runtime/Engine/Classes/Components/TextRenderComponent.h:44`
  - `UUsdDrawModeComponent` — `Engine/Plugins/Runtime/USDCore/Source/USDClasses/Public/USDDrawModeComponent.h:61`
  - `UVectorFieldComponent` — `Engine/Source/Runtime/Engine/Classes/Components/VectorFieldComponent.h:18`
  - `UVehicleSimBaseComponent` — `Engine/Plugins/Experimental/ChaosModularVehicle/Source/ChaosModularVehicleEngine/Public/ChaosModularVehicle/VehicleSimBaseComponent.h:73`
    - `UVehicleSimAerofoilComponent` — `Engine/Plugins/Experimental/ChaosModularVehicle/Source/ChaosModularVehicleEngine/Public/ChaosModularVehicle/VehicleSimAerofoilComponent.h:24`
    - `UVehicleSimChassisComponent` — `Engine/Plugins/Experimental/ChaosModularVehicle/Source/ChaosModularVehicleEngine/Public/ChaosModularVehicle/VehicleSimChassisComponent.h:14`
    - `UVehicleSimClutchComponent` — `Engine/Plugins/Experimental/ChaosModularVehicle/Source/ChaosModularVehicleEngine/Public/ChaosModularVehicle/VehicleSimClutchComponent.h:14`
    - `UVehicleSimEngineComponent` — `Engine/Plugins/Experimental/ChaosModularVehicle/Source/ChaosModularVehicleEngine/Public/ChaosModularVehicle/VehicleSimEngineComponent.h:15`
    - `UVehicleSimSuspensionComponent` — `Engine/Plugins/Experimental/ChaosModularVehicle/Source/ChaosModularVehicleEngine/Public/ChaosModularVehicle/VehicleSimSuspensionComponent.h:14`
    - `UVehicleSimThrusterComponent` — `Engine/Plugins/Experimental/ChaosModularVehicle/Source/ChaosModularVehicleEngine/Public/ChaosModularVehicle/VehicleSimThrusterComponent.h:14`
    - `UVehicleSimTransmissionComponent` — `Engine/Plugins/Experimental/ChaosModularVehicle/Source/ChaosModularVehicleEngine/Public/ChaosModularVehicle/VehicleSimTransmissionComponent.h:22`
    - `UVehicleSimWheelComponent` — `Engine/Plugins/Experimental/ChaosModularVehicle/Source/ChaosModularVehicleEngine/Public/ChaosModularVehicle/VehicleSimWheelComponent.h:21`
  - `UVirtualHeightfieldMeshComponent` — `Engine/Plugins/Experimental/VirtualHeightfieldMesh/Source/VirtualHeightfieldMesh/Public/VirtualHeightfieldMeshComponent.h:18`
  - `UWaterBodyComponent` — `Engine/Plugins/Experimental/Water/Source/Runtime/Public/WaterBodyComponent.h:113`
    - `UWaterBodyCustomComponent` — `Engine/Plugins/Experimental/Water/Source/Runtime/Public/WaterBodyCustomComponent.h:15`
    - `UWaterBodyLakeComponent` — `Engine/Plugins/Experimental/Water/Source/Runtime/Public/WaterBodyLakeComponent.h:17`
    - `UWaterBodyOceanComponent` — `Engine/Plugins/Experimental/Water/Source/Runtime/Public/WaterBodyOceanComponent.h:16`
    - `UWaterBodyRiverComponent` — `Engine/Plugins/Experimental/Water/Source/Runtime/Public/WaterBodyRiverComponent.h:16`
  - `UXRCreativeITFRenderComponent` — `Engine/Plugins/Experimental/XRCreativeFramework/Source/XRCreative/Private/ITF/XRCreativeITFRenderComponent.h:28`
  - `UZoneGraphRenderingComponent` — `Engine/Plugins/Runtime/ZoneGraph/Source/ZoneGraph/Public/ZoneGraphRenderingComponent.h:62`
  - `UZoneGraphTestingComponent` — `Engine/Plugins/Runtime/ZoneGraph/Source/ZoneGraphDebug/Public/ZoneGraphTestingActor.h:41`
  - `UZoneShapeComponent` — `Engine/Plugins/Runtime/ZoneGraph/Source/ZoneGraph/Public/ZoneShapeComponent.h:39`

- `FPrimitiveSceneProxy` — `Engine/Source/Runtime/Engine/Public/PrimitiveSceneProxy.h:291`
  - `FArrowSceneProxy` — `Engine/Source/Runtime/Engine/Private/Components/ArrowComponent.cpp:30`
  - `FBaseDynamicMeshSceneProxy` — `Engine/Source/Runtime/GeometryFramework/Public/Components/BaseDynamicMeshSceneProxy.h:38`
    - `FDynamicMeshSceneProxy` — `Engine/Source/Runtime/GeometryFramework/Private/Components/DynamicMeshSceneProxy.h:22`
    - `FOctreeDynamicMeshSceneProxy` — `Engine/Plugins/Runtime/MeshModelingToolset/Source/ModelingComponents/Private/Components/OctreeDynamicMeshSceneProxy.h:31`
  - `FBasicLineSetSceneProxy` — `Engine/Plugins/Editor/UVEditor/Source/UVEditorTools/Private/Drawing/BasicLineSetComponent.cpp:25`
  - `FBasicPointSetSceneProxy` — `Engine/Plugins/Editor/UVEditor/Source/UVEditorTools/Private/Drawing/BasicPointSetComponent.cpp:25`
  - `FBasicTriangleSetSceneProxy` — `Engine/Plugins/Editor/UVEditor/Source/UVEditorTools/Private/Drawing/BasicTriangleSetComponent.cpp:25`
  - `FBoxSceneProxy` — `Engine/Source/Runtime/Engine/Private/Components/BoxComponent.cpp:132`
  - `FBrushSceneProxy` — `Engine/Source/Runtime/Engine/Private/Components/BrushComponent.cpp:98`
  - `FCableSceneProxy` — `Engine/Plugins/Runtime/CableComponent/Source/CableComponent/Private/CableComponent.cpp:84`
  - `FControlRigSceneProxy` — `Engine/Plugins/Animation/ControlRig/Source/ControlRig/Public/ControlRigComponent.h:699`
  - `FCustomMeshSceneProxy` — `Engine/Plugins/Runtime/CustomMeshComponent/Source/CustomMeshComponent/Private/CustomMeshComponent.cpp:21`
  - `FDataflowEngineSceneProxy` — `Engine/Plugins/Dataflow/Source/DataflowEnginePlugin/Private/Dataflow/DataflowEngineSceneProxy.h:34`
  - `FDebugRenderSceneProxy` — `Engine/Source/Runtime/Engine/Public/DebugRenderSceneProxy.h:40`
    - `FChaosPathedMovementDebugRenderSceneProxy` — `Engine/Plugins/Experimental/ChaosMover/Source/ChaosMover/Private/PathedMovement/ChaosPathedMovementDebugDrawComponent.cpp:26`
    - `FDataflowDebugMeshSceneProxy` — `Engine/Plugins/Dataflow/Source/DataflowEditor/Private/DataflowRendering/DataflowDebugMeshRenderableType.cpp:36`
    - `FDataflowDebugRenderSceneProxy` — `Engine/Source/Runtime/Dataflow/Engine/Public/Dataflow/DataflowDebugDrawComponent.h:21`
    - `FEQSSceneProxy` — `Engine/Source/Runtime/AIModule/Classes/EnvironmentQuery/EQSRenderingComponent.h:17`
    - `FGameplayDebuggerCompositeSceneProxy` — `Engine/Source/Runtime/GameplayDebugger/Private/GameplayDebuggerRenderingComponent.cpp:12`
    - `FGeometryCollectionISMPoolDebugDrawSceneProxy` — `Engine/Source/Runtime/Experimental/GeometryCollectionEngine/Public/GeometryCollection/GeometryCollectionISMPoolDebugDrawComponent.cpp:43`
    - `FISMPoolDebugDrawSceneProxy` — `Engine/Source/Runtime/Experimental/ISMPool/Public/ISMPool/ISMPoolDebugDrawComponent.cpp:40`
    - `FMassNavigationTestingSceneProxy` — `Engine/Plugins/AI/MassAI/Source/MassNavigationEditor/Private/MassNavigationTestingActor.h:20`
    - `FNavCorridorDebugRenderSceneProxy` — `Engine/Plugins/Runtime/NavCorridor/Source/NavCorridor/Private/NavCorridorTestingComponent.cpp:187`
    - `FNavLocalGridSceneProxy` — `Engine/Source/Runtime/AIModule/Private/GameplayDebugger/GameplayDebuggerCategory_NavLocalGrid.cpp:23`
    - `FNavMeshSceneProxy` — `Engine/Source/Runtime/NavigationSystem/Public/NavMesh/NavMeshRenderingComponent.h:121`
    - `FNavTestSceneProxy` — `Engine/Source/Runtime/NavigationSystem/Public/NavMesh/NavTestRenderingComponent.h:20`
    - `FPathDebugRenderSceneProxy` — `Engine/Source/Runtime/AIModule/Private/GameplayDebugger/GameplayDebuggerCategory_AI.cpp:390`
    - `FSOContainerRenderingSceneProxy` — `Engine/Plugins/Runtime/SmartObjects/Source/SmartObjectsModule/Private/SmartObjectContainerRenderingComponent.cpp:25`
    - `FSORenderingSceneProxy` — `Engine/Plugins/Runtime/SmartObjects/Source/SmartObjectsModule/Private/SmartObjectRenderingComponent.cpp:15`
    - `FSmartObjectDebugSceneProxy` — `Engine/Plugins/Runtime/SmartObjects/Source/SmartObjectsModule/Public/SmartObjectDebugSceneProxy.h:13`
    - `FZoneGraphAnnotationSceneProxy` — `Engine/Plugins/Runtime/ZoneGraphAnnotations/Source/ZoneGraphAnnotations/Public/ZoneGraphAnnotationComponent.h:22`
    - `FZoneGraphSceneProxy` — `Engine/Plugins/Runtime/ZoneGraph/Source/ZoneGraph/Public/ZoneGraphRenderingComponent.h:17`
      - `FMassCrowdLaneDataSceneProxy` — `Engine/Plugins/AI/MassCrowd/Source/MassCrowd/Private/MassCrowdLaneDataRenderingComponent.cpp:63`
  - `FDrawCylinderSceneProxy` — `Engine/Source/Runtime/Engine/Private/Components/CapsuleComponent.cpp:33`
  - `FDrawFrustumSceneProxy` — `Engine/Source/Runtime/Engine/Private/Components/DrawFrustumComponent.cpp:19`
  - `FEditorWidgetBillboardProxy` — `Engine/Plugins/Runtime/nDisplay/Source/DisplayCluster/Private/Game/EngineClasses/Scene/DisplayClusterWidgetComponent.cpp:23`
  - `FFieldSystemSceneProxy` — `Engine/Source/Runtime/Experimental/FieldSystem/Source/FieldSystemEngine/Private/Field/FieldSystemSceneProxy.h:20`
  - `FGeometryCacheSceneProxy` — `Engine/Plugins/Runtime/GeometryCache/Source/GeometryCache/Public/GeometryCacheSceneProxy.h:287`
    - `FGeometryCacheAbcFileSceneProxy` — `Engine/Plugins/Experimental/GeometryCacheAbcFile/Source/GeometryCacheAbcFile/Public/GeometryCacheAbcFileSceneProxy.h:9`
    - `FGeometryCacheUsdSceneProxy` — `Engine/Plugins/Importers/USDImporter/Source/GeometryCacheUSD/Public/GeometryCacheUSDSceneProxy.h:9`
  - `FGeometryCollectionSceneProxy` — `Engine/Source/Runtime/Experimental/GeometryCollectionEngine/Private/GeometryCollection/GeometryCollectionSceneProxy.h:273`
  - `FGizmoArrowComponentSceneProxy` — `Engine/Source/Runtime/InteractiveToolsFramework/Private/BaseGizmos/GizmoArrowComponent.cpp:62`
  - `FGizmoBoxComponentSceneProxy` — `Engine/Source/Runtime/InteractiveToolsFramework/Private/BaseGizmos/GizmoBoxComponent.cpp:53`
  - `FGizmoCircleComponentSceneProxy` — `Engine/Source/Runtime/InteractiveToolsFramework/Private/BaseGizmos/GizmoCircleComponent.cpp:31`
  - `FGizmoLineHandleComponentSceneProxy` — `Engine/Source/Runtime/InteractiveToolsFramework/Private/BaseGizmos/GizmoLineHandleComponent.cpp:15`
  - `FGizmoRectangleComponentSceneProxy` — `Engine/Source/Runtime/InteractiveToolsFramework/Private/BaseGizmos/GizmoRectangleComponent.cpp:83`
  - `FHairStrandsSceneProxy` — `Engine/Plugins/Runtime/HairStrands/Source/HairStrandsCore/Private/GroomComponent.cpp:447`
  - `FHeterogeneousVolumeSceneProxy` — `Engine/Source/Runtime/Engine/Private/Components/HeterogeneousVolumeComponent.cpp:32`
  - `FImagePlateFrustumSceneProxy` — `Engine/Plugins/Experimental/ImagePlate/Source/ImagePlate/Private/ImagePlateFrustumComponent.cpp:20`
  - `FImagePlateSceneProxy` — `Engine/Plugins/Experimental/ImagePlate/Source/ImagePlate/Private/ImagePlateComponent.cpp:49`
  - `FInstancedActorsModifierVolumeComponent` — `Engine/Plugins/Runtime/InstancedActors/Source/InstancedActors/Private/InstancedActorsModifierVolumeComponent.cpp:329`
  - `FLakeCollisionSceneProxy` — `Engine/Plugins/Experimental/Water/Source/Runtime/Private/LakeCollisionComponent.cpp:64`
  - `FLandscapeComponentSceneProxy` — `Engine/Source/Runtime/Landscape/Public/LandscapeRender.h:683`
  - `FLandscapeGizmoRenderSceneProxy` — `Engine/Source/Runtime/Landscape/Private/LandscapeGizmoActor.cpp:133`
  - `FLandscapeHeightfieldCollisionComponentSceneProxy` — `Engine/Source/Runtime/Landscape/Private/LandscapeCollision.cpp:573`
  - `FLandscapeSplinesSceneProxy` — `Engine/Source/Runtime/Landscape/Private/LandscapeSplines.cpp:139`
  - `FLidarPointCloudSceneProxy` — `Engine/Plugins/Enterprise/LidarPointCloud/Source/LidarPointCloudRuntime/Private/Rendering/LidarPointCloudRendering.cpp:197`
  - `FLineBatcherSceneProxy` — `Engine/Source/Runtime/Engine/Private/Components/LineBatchComponent.cpp:22`
  - `FLineSetSceneProxy` — `Engine/Plugins/Runtime/MeshModelingToolset/Source/ModelingComponents/Private/Drawing/LineSetComponent.cpp:33`
  - `FMRMeshProxy` — `Engine/Source/Runtime/MRMesh/Private/MRMeshComponent.cpp:173`
  - `FMaterialSpriteSceneProxy` — `Engine/Source/Runtime/Engine/Private/Components/MaterialBillboardComponent.cpp:54`
  - `FMeshWireframeSceneProxy` — `Engine/Plugins/Runtime/MeshModelingToolset/Source/ModelingComponents/Private/Drawing/MeshWireframeComponent.cpp:41`
  - `FModelSceneProxy` — `Engine/Source/Runtime/Engine/Private/ModelRender.cpp:206`
  - `FNavLinkRenderingProxy` — `Engine/Source/Runtime/NavigationSystem/Public/NavLinkRenderingProxy.h:15`
  - `FNiagaraSceneProxy` — `Engine/Plugins/FX/Niagara/Source/Niagara/Public/NiagaraSceneProxy.h:34`
  - `FOceanCollisionSceneProxy` — `Engine/Plugins/Experimental/Water/Source/Runtime/Private/OceanCollisionComponent.cpp:130`
  - `FPCGCollisionVisProxy` — `Engine/Plugins/PCG/Source/PCGEditor/Private/DataVisualizations/PCGCollisionVisComponent.cpp:16`
  - `FPVBoneSceneProxy` — `Engine/Plugins/Experimental/ProceduralVegetationEditor/Source/ProceduralVegetationEditor/Private/Visualizations/PVBoneComponent.h:15`
  - `FPVLineSceneProxy` — `Engine/Plugins/Experimental/ProceduralVegetationEditor/Source/ProceduralVegetationEditor/Private/Visualizations/PVLineBatchComponent.h:40`
  - `FPaperRenderSceneProxy` — `Engine/Plugins/2D/Paper2D/Source/Paper2D/Private/PaperRenderSceneProxy.h:124`
    - `FGroupedSpriteSceneProxy` — `Engine/Plugins/2D/Paper2D/Source/Paper2D/Private/GroupedSpriteSceneProxy.h:17`
    - `FPaperRenderSceneProxy_SpriteBase` — `Engine/Plugins/2D/Paper2D/Source/Paper2D/Private/PaperRenderSceneProxy.h:196`
      - `FPaperFlipbookSceneProxy` — `Engine/Plugins/2D/Paper2D/Source/Paper2D/Private/PaperFlipbookSceneProxy.h:10`
      - `FPaperSpriteSceneProxy` — `Engine/Plugins/2D/Paper2D/Source/Paper2D/Private/PaperSpriteSceneProxy.h:14`
    - `FPaperTerrainSceneProxy` — `Engine/Plugins/2D/Paper2D/Source/Paper2D/Private/Terrain/PaperTerrainComponent.cpp:62`
    - `FPaperTileMapRenderSceneProxy` — `Engine/Plugins/2D/Paper2D/Source/Paper2D/Private/PaperTileMapRenderSceneProxy.h:15`
  - `FParticleSystemSceneProxy` — `Engine/Source/Runtime/Engine/Public/ParticleSystemSceneProxy.h:36`
  - `FPointSetSceneProxy` — `Engine/Plugins/Runtime/MeshModelingToolset/Source/ModelingComponents/Private/Drawing/PointSetComponent.cpp:41`
  - `FProceduralMeshSceneProxy` — `Engine/Plugins/Runtime/ProceduralMeshComponent/Source/ProceduralMeshComponent/Private/ProceduralMeshComponent.cpp:92`
  - `FSkeletalMeshSceneProxy` — `Engine/Source/Runtime/Engine/Public/SkeletalMeshSceneProxy.h:21`
    - `FInstancedSkinnedMeshSceneProxy` — `Engine/Source/Runtime/Engine/Private/InstancedSkinnedMeshSceneProxy.h:86`
  - `FSparseVolumeTextureViewerSceneProxy` — `Engine/Source/Runtime/Renderer/Private/SparseVolumeTexture/SparseVolumeTextureViewerSceneProxy.h:21`
  - `FSphereSceneProxy` — `Engine/Source/Runtime/Engine/Private/Components/SphereComponent.cpp:111`
  - `FSplineMeshSceneProxy` — `Engine/Source/Runtime/Engine/Private/Components/SplineComponent.cpp:3409`
  - `FSplinePDISceneProxy` — `Engine/Source/Runtime/Engine/Private/Components/SplineComponent.cpp:3345`
  - `FSpriteSceneProxy` — `Engine/Source/Runtime/Engine/Private/Components/BillboardComponent.cpp:30`
  - `FStaticMeshSceneProxy` — `Engine/Source/Runtime/Engine/Public/StaticMeshSceneProxy.h:34`
    - `FCameraProxyMeshProxy` — `Engine/Source/Runtime/Engine/Private/Camera/CameraComponent.cpp:36`
    - `FInstancedStaticMeshSceneProxy` — `Engine/Source/Runtime/Engine/Classes/Engine/InstancedStaticMesh.h:435`
      - `FHierarchicalStaticMeshSceneProxy` — `Engine/Source/Runtime/Engine/Public/HierarchicalStaticMeshSceneProxy.h:35`
      - `NiagaraStaticMeshComponentPrivate::FMeshSceneProxy` — `Engine/Plugins/FX/NiagaraNanite/Source/NiagaraNanite/Private/Renderer/NiagaraStaticMeshComponent.cpp:22`
    - `FInteractiveFoliageSceneProxy` — `Engine/Source/Runtime/Foliage/Private/FoliageComponent.cpp:21`
    - `FLandscapeMeshProxySceneProxy` — `Engine/Source/Runtime/Landscape/Public/LandscapeRender.h:661`
    - `FSplineMeshSceneProxy` — `Engine/Source/Runtime/Engine/Public/SplineMeshSceneProxy.h:141`
    - `FStaticMeshSceneProxyExt` — `Engine/Plugins/Enterprise/DataprepEditor/Source/DataprepEditor/Private/Widgets/SDataprepEditorViewport.cpp:115`
    - `FStereoStaticMeshSceneProxy` — `Engine/Plugins/Experimental/PanoramicCapture/Source/PanoramicCapture/Private/StereoStaticMeshComponent.cpp:10`
    - `FWaterBodyInfoMeshSceneProxy` — `Engine/Plugins/Experimental/Water/Source/Runtime/Private/WaterBodyInfoMeshComponent.h:44`
    - `ViewAdjustedStaticMeshGizmoComponentLocals::FViewAdjustedStaticMeshGizmoComponentProxy` — `Engine/Source/Runtime/InteractiveToolsFramework/Private/BaseGizmos/ViewAdjustedStaticMeshGizmoComponent.cpp:43`
  - `FTextRenderSceneProxy` — `Engine/Source/Runtime/Engine/Private/Components/TextRenderComponent.cpp:586`
  - `FTriangleSetSceneProxy` — `Engine/Plugins/Runtime/MeshModelingToolset/Source/ModelingComponents/Private/Drawing/TriangleSetComponent.cpp:32`
  - `FVectorFieldSceneProxy` — `Engine/Source/Runtime/Engine/Private/VectorField.cpp:661`
  - `FVirtualHeightfieldMeshSceneProxy` — `Engine/Plugins/Experimental/VirtualHeightfieldMesh/Source/VirtualHeightfieldMesh/Private/VirtualHeightfieldMeshSceneProxy.h:12`
  - `FWaterMeshSceneProxy` — `Engine/Plugins/Experimental/Water/Source/Runtime/Public/WaterMeshSceneProxy.h:103`
  - `FWidget3DSceneProxy` — `Engine/Source/Runtime/UMG/Private/Components/WidgetComponent.cpp:310`
  - `FWidgetBoxProxy` — `Engine/Source/Runtime/UMG/Private/Components/WidgetComponent.cpp:844`
  - `FZoneGraphTestingSceneProxy` — `Engine/Plugins/Runtime/ZoneGraph/Source/ZoneGraphDebug/Private/ZoneGraphTestingActor.cpp:205`
  - `FZoneShapeSceneProxy` — `Engine/Plugins/Runtime/ZoneGraph/Source/ZoneGraph/Private/ZoneShapeComponent.cpp:769`
  - `Nanite::FSceneProxyBase` — `Engine/Source/Runtime/Engine/Public/NaniteSceneProxy.h:219`
    - `FNaniteGeometryCollectionSceneProxy` — `Engine/Source/Runtime/Experimental/GeometryCollectionEngine/Private/GeometryCollection/GeometryCollectionSceneProxy.h:343`
    - `Nanite::FGroomSceneProxy` — `Engine/Plugins/Runtime/HairStrands/Source/HairStrandsCore/Private/NaniteGroomAsset.h:66`
    - `Nanite::FSceneProxy` — `Engine/Source/Runtime/Engine/Public/NaniteSceneProxy.h:490`
      - `FLandscapeNaniteSceneProxy` — `Engine/Source/Runtime/Landscape/Private/LandscapeRender.cpp:4866`
      - `FNaniteSplineMeshSceneProxy` — `Engine/Source/Runtime/Engine/Public/SplineMeshSceneProxy.h:181`
      - `NiagaraStaticMeshComponentPrivate::FNaniteSceneProxy` — `Engine/Plugins/FX/NiagaraNanite/Source/NiagaraNanite/Private/Renderer/NiagaraStaticMeshComponent.cpp:12`
    - `Nanite::FSkinnedSceneProxy` — `Engine/Source/Runtime/Engine/Public/NaniteSceneProxy.h:757`
      - `FNaniteInstancedSkinnedMeshSceneProxy` — `Engine/Source/Runtime/Engine/Private/InstancedSkinnedMeshSceneProxy.h:30`
  - `UE::Avalanche::FTickerSceneProxy` — `Engine/Plugins/VirtualProduction/Avalanche/Source/Avalanche/Private/Framework/Ticker/AvaTickerSceneProxy.h:15`
  - `UE::MeshPartition::FDrawMeshPartitionCollisionSceneProxy` — `Engine/Plugins/Experimental/MeshPartition/Source/MeshPartition/Private/MeshPartitionCollisionComponent.cpp:337`
  - `UE::MeshPartition::FMegaMeshCustomPreviewSceneProxy` — `Engine/Plugins/Experimental/MeshPartition/Source/MeshPartitionEditor/Internal/MeshPartitionPreviewSceneProxy.h:25`
  - `UE::MeshPartition::MegaMeshModifierComponentLocals::FMegaMeshModifierComponentSceneProxy` — `Engine/Plugins/Experimental/MeshPartition/Source/MeshPartitionEditor/Private/MeshPartitionModifierComponent.cpp:58`
  - `UE::UAF::Debug::FAnimNextDebugSceneProxy` — `Engine/Plugins/Experimental/UAF/UAF/Source/UAF/Internal/AnimNextDebugDraw.h:28`
  - `UE::UsdDrawModeComponentImpl::Private::FUsdCardsSceneProxy` — `Engine/Plugins/Runtime/USDCore/Source/USDClasses/Private/USDDrawModeComponent.cpp:351`
  - `UE::UsdDrawModeComponentImpl::Private::FUsdLinesSceneProxy` — `Engine/Plugins/Runtime/USDCore/Source/USDClasses/Private/USDDrawModeComponent.cpp:53`
    - `UE::UsdDrawModeComponentImpl::Private::FUsdDrawModeLinesSceneProxy` — `Engine/Plugins/Runtime/USDCore/Source/USDClasses/Private/USDDrawModeComponent.cpp:285`
    - `UE::UsdDrawModeComponentImpl::Private::FUsdOriginLinesSceneProxy` — `Engine/Plugins/Runtime/USDCore/Source/USDClasses/Private/USDDrawModeComponent.cpp:303`
  - `UE::XRCreative::Private::FRenderComponentSceneProxy` — `Engine/Plugins/Experimental/XRCreativeFramework/Source/XRCreative/Private/ITF/XRCreativeITFRenderComponent.cpp:13`

## All declared bases

- `FArrowSceneProxy` (Engine/Source/Runtime/Engine/Private/Components/ArrowComponent.cpp:30) — bases: `FPrimitiveSceneProxy`.
- `FBaseDynamicMeshSceneProxy` (Engine/Source/Runtime/GeometryFramework/Public/Components/BaseDynamicMeshSceneProxy.h:38) — bases: `FPrimitiveSceneProxy`.
- `FBasicLineSetSceneProxy` (Engine/Plugins/Editor/UVEditor/Source/UVEditorTools/Private/Drawing/BasicLineSetComponent.cpp:25) — bases: `FPrimitiveSceneProxy`.
- `FBasicPointSetSceneProxy` (Engine/Plugins/Editor/UVEditor/Source/UVEditorTools/Private/Drawing/BasicPointSetComponent.cpp:25) — bases: `FPrimitiveSceneProxy`.
- `FBasicTriangleSetSceneProxy` (Engine/Plugins/Editor/UVEditor/Source/UVEditorTools/Private/Drawing/BasicTriangleSetComponent.cpp:25) — bases: `FPrimitiveSceneProxy`.
- `FBoxSceneProxy` (Engine/Source/Runtime/Engine/Private/Components/BoxComponent.cpp:132) — bases: `FPrimitiveSceneProxy`.
- `FBrushSceneProxy` (Engine/Source/Runtime/Engine/Private/Components/BrushComponent.cpp:98) — bases: `FPrimitiveSceneProxy`.
- `FCableSceneProxy` (Engine/Plugins/Runtime/CableComponent/Source/CableComponent/Private/CableComponent.cpp:84) — bases: `FPrimitiveSceneProxy`.
- `FCameraProxyMeshProxy` (Engine/Source/Runtime/Engine/Private/Camera/CameraComponent.cpp:36) — bases: `FStaticMeshSceneProxy`.
- `FChaosPathedMovementDebugRenderSceneProxy` (Engine/Plugins/Experimental/ChaosMover/Source/ChaosMover/Private/PathedMovement/ChaosPathedMovementDebugDrawComponent.cpp:26) — bases: `FDebugRenderSceneProxy`.
- `FControlRigSceneProxy` (Engine/Plugins/Animation/ControlRig/Source/ControlRig/Public/ControlRigComponent.h:699) — bases: `FPrimitiveSceneProxy`.
- `FCustomMeshSceneProxy` (Engine/Plugins/Runtime/CustomMeshComponent/Source/CustomMeshComponent/Private/CustomMeshComponent.cpp:21) — bases: `FPrimitiveSceneProxy`.
- `FDataflowDebugMeshSceneProxy` (Engine/Plugins/Dataflow/Source/DataflowEditor/Private/DataflowRendering/DataflowDebugMeshRenderableType.cpp:36) — bases: `FDebugRenderSceneProxy`.
- `FDataflowDebugRenderSceneProxy` (Engine/Source/Runtime/Dataflow/Engine/Public/Dataflow/DataflowDebugDrawComponent.h:21) — bases: `FDebugRenderSceneProxy`.
- `FDataflowEngineSceneProxy` (Engine/Plugins/Dataflow/Source/DataflowEnginePlugin/Private/Dataflow/DataflowEngineSceneProxy.h:34) — bases: `FPrimitiveSceneProxy`.
- `FDebugRenderSceneProxy` (Engine/Source/Runtime/Engine/Public/DebugRenderSceneProxy.h:40) — bases: `FPrimitiveSceneProxy`.
- `FDrawCylinderSceneProxy` (Engine/Source/Runtime/Engine/Private/Components/CapsuleComponent.cpp:33) — bases: `FPrimitiveSceneProxy`.
- `FDrawFrustumSceneProxy` (Engine/Source/Runtime/Engine/Private/Components/DrawFrustumComponent.cpp:19) — bases: `FPrimitiveSceneProxy`.
- `FDynamicMeshSceneProxy` (Engine/Source/Runtime/GeometryFramework/Private/Components/DynamicMeshSceneProxy.h:22) — bases: `FBaseDynamicMeshSceneProxy`.
- `FEQSSceneProxy` (Engine/Source/Runtime/AIModule/Classes/EnvironmentQuery/EQSRenderingComponent.h:17) — bases: `FDebugRenderSceneProxy`.
- `FEditorWidgetBillboardProxy` (Engine/Plugins/Runtime/nDisplay/Source/DisplayCluster/Private/Game/EngineClasses/Scene/DisplayClusterWidgetComponent.cpp:23) — bases: `FPrimitiveSceneProxy`.
- `FFieldSystemSceneProxy` (Engine/Source/Runtime/Experimental/FieldSystem/Source/FieldSystemEngine/Private/Field/FieldSystemSceneProxy.h:20) — bases: `FPrimitiveSceneProxy`.
- `FGameplayDebuggerCompositeSceneProxy` (Engine/Source/Runtime/GameplayDebugger/Private/GameplayDebuggerRenderingComponent.cpp:12) — bases: `FDebugRenderSceneProxy`.
- `FGeometryCacheAbcFileSceneProxy` (Engine/Plugins/Experimental/GeometryCacheAbcFile/Source/GeometryCacheAbcFile/Public/GeometryCacheAbcFileSceneProxy.h:9) — bases: `FGeometryCacheSceneProxy`.
- `FGeometryCacheSceneProxy` (Engine/Plugins/Runtime/GeometryCache/Source/GeometryCache/Public/GeometryCacheSceneProxy.h:287) — bases: `FPrimitiveSceneProxy`.
- `FGeometryCacheUsdSceneProxy` (Engine/Plugins/Importers/USDImporter/Source/GeometryCacheUSD/Public/GeometryCacheUSDSceneProxy.h:9) — bases: `FGeometryCacheSceneProxy`.
- `FGeometryCollectionISMPoolDebugDrawSceneProxy` (Engine/Source/Runtime/Experimental/GeometryCollectionEngine/Public/GeometryCollection/GeometryCollectionISMPoolDebugDrawComponent.cpp:43) — bases: `FDebugRenderSceneProxy`.
- `FGeometryCollectionSceneProxy` (Engine/Source/Runtime/Experimental/GeometryCollectionEngine/Private/GeometryCollection/GeometryCollectionSceneProxy.h:273) — bases: `FPrimitiveSceneProxy`, `FGeometryCollectionSceneProxyBase`.
- `FGizmoArrowComponentSceneProxy` (Engine/Source/Runtime/InteractiveToolsFramework/Private/BaseGizmos/GizmoArrowComponent.cpp:62) — bases: `FPrimitiveSceneProxy`.
- `FGizmoBoxComponentSceneProxy` (Engine/Source/Runtime/InteractiveToolsFramework/Private/BaseGizmos/GizmoBoxComponent.cpp:53) — bases: `FPrimitiveSceneProxy`.
- `FGizmoCircleComponentSceneProxy` (Engine/Source/Runtime/InteractiveToolsFramework/Private/BaseGizmos/GizmoCircleComponent.cpp:31) — bases: `FPrimitiveSceneProxy`.
- `FGizmoLineHandleComponentSceneProxy` (Engine/Source/Runtime/InteractiveToolsFramework/Private/BaseGizmos/GizmoLineHandleComponent.cpp:15) — bases: `FPrimitiveSceneProxy`.
- `FGizmoRectangleComponentSceneProxy` (Engine/Source/Runtime/InteractiveToolsFramework/Private/BaseGizmos/GizmoRectangleComponent.cpp:83) — bases: `FPrimitiveSceneProxy`.
- `FGroupedSpriteSceneProxy` (Engine/Plugins/2D/Paper2D/Source/Paper2D/Private/GroupedSpriteSceneProxy.h:17) — bases: `FPaperRenderSceneProxy`.
- `FHairStrandsSceneProxy` (Engine/Plugins/Runtime/HairStrands/Source/HairStrandsCore/Private/GroomComponent.cpp:447) — bases: `FPrimitiveSceneProxy`.
- `FHeterogeneousVolumeSceneProxy` (Engine/Source/Runtime/Engine/Private/Components/HeterogeneousVolumeComponent.cpp:32) — bases: `FPrimitiveSceneProxy`.
- `FHierarchicalStaticMeshSceneProxy` (Engine/Source/Runtime/Engine/Public/HierarchicalStaticMeshSceneProxy.h:35) — bases: `FInstancedStaticMeshSceneProxy`.
- `FISMPoolDebugDrawSceneProxy` (Engine/Source/Runtime/Experimental/ISMPool/Public/ISMPool/ISMPoolDebugDrawComponent.cpp:40) — bases: `FDebugRenderSceneProxy`.
- `FImagePlateFrustumSceneProxy` (Engine/Plugins/Experimental/ImagePlate/Source/ImagePlate/Private/ImagePlateFrustumComponent.cpp:20) — bases: `FPrimitiveSceneProxy`.
- `FImagePlateSceneProxy` (Engine/Plugins/Experimental/ImagePlate/Source/ImagePlate/Private/ImagePlateComponent.cpp:49) — bases: `FPrimitiveSceneProxy`.
- `FInstancedActorsModifierVolumeComponent` (Engine/Plugins/Runtime/InstancedActors/Source/InstancedActors/Private/InstancedActorsModifierVolumeComponent.cpp:329) — bases: `FPrimitiveSceneProxy`.
- `FInstancedSkinnedMeshSceneProxy` (Engine/Source/Runtime/Engine/Private/InstancedSkinnedMeshSceneProxy.h:86) — bases: `FSkeletalMeshSceneProxy`.
- `FInstancedStaticMeshSceneProxy` (Engine/Source/Runtime/Engine/Classes/Engine/InstancedStaticMesh.h:435) — bases: `FStaticMeshSceneProxy`.
- `FInteractiveFoliageSceneProxy` (Engine/Source/Runtime/Foliage/Private/FoliageComponent.cpp:21) — bases: `FStaticMeshSceneProxy`.
- `FLakeCollisionSceneProxy` (Engine/Plugins/Experimental/Water/Source/Runtime/Private/LakeCollisionComponent.cpp:64) — bases: `FPrimitiveSceneProxy`.
- `FLandscapeComponentSceneProxy` (Engine/Source/Runtime/Landscape/Public/LandscapeRender.h:683) — bases: `FPrimitiveSceneProxy`, `FLandscapeSectionInfo`.
- `FLandscapeGizmoRenderSceneProxy` (Engine/Source/Runtime/Landscape/Private/LandscapeGizmoActor.cpp:133) — bases: `FPrimitiveSceneProxy`.
- `FLandscapeHeightfieldCollisionComponentSceneProxy` (Engine/Source/Runtime/Landscape/Private/LandscapeCollision.cpp:573) — bases: `FPrimitiveSceneProxy`.
- `FLandscapeMeshProxySceneProxy` (Engine/Source/Runtime/Landscape/Public/LandscapeRender.h:661) — bases: `FStaticMeshSceneProxy`.
- `FLandscapeNaniteSceneProxy` (Engine/Source/Runtime/Landscape/Private/LandscapeRender.cpp:4866) — bases: `::Nanite::FSceneProxy`.
- `FLandscapeSplinesSceneProxy` (Engine/Source/Runtime/Landscape/Private/LandscapeSplines.cpp:139) — bases: `FPrimitiveSceneProxy`.
- `FLidarPointCloudSceneProxy` (Engine/Plugins/Enterprise/LidarPointCloud/Source/LidarPointCloudRuntime/Private/Rendering/LidarPointCloudRendering.cpp:197) — bases: `ILidarPointCloudSceneProxy`, `FPrimitiveSceneProxy`.
- `FLineBatcherSceneProxy` (Engine/Source/Runtime/Engine/Private/Components/LineBatchComponent.cpp:22) — bases: `FPrimitiveSceneProxy`.
- `FLineSetSceneProxy` (Engine/Plugins/Runtime/MeshModelingToolset/Source/ModelingComponents/Private/Drawing/LineSetComponent.cpp:33) — bases: `FPrimitiveSceneProxy`.
- `FMRMeshProxy` (Engine/Source/Runtime/MRMesh/Private/MRMeshComponent.cpp:173) — bases: `FPrimitiveSceneProxy`.
- `FMassCrowdLaneDataSceneProxy` (Engine/Plugins/AI/MassCrowd/Source/MassCrowd/Private/MassCrowdLaneDataRenderingComponent.cpp:63) — bases: `FZoneGraphSceneProxy`.
- `FMassNavigationTestingSceneProxy` (Engine/Plugins/AI/MassAI/Source/MassNavigationEditor/Private/MassNavigationTestingActor.h:20) — bases: `FDebugRenderSceneProxy`.
- `FMaterialSpriteSceneProxy` (Engine/Source/Runtime/Engine/Private/Components/MaterialBillboardComponent.cpp:54) — bases: `FPrimitiveSceneProxy`.
- `FMeshWireframeSceneProxy` (Engine/Plugins/Runtime/MeshModelingToolset/Source/ModelingComponents/Private/Drawing/MeshWireframeComponent.cpp:41) — bases: `FPrimitiveSceneProxy`.
- `FModelSceneProxy` (Engine/Source/Runtime/Engine/Private/ModelRender.cpp:206) — bases: `FPrimitiveSceneProxy`.
- `FNaniteGeometryCollectionSceneProxy` (Engine/Source/Runtime/Experimental/GeometryCollectionEngine/Private/GeometryCollection/GeometryCollectionSceneProxy.h:343) — bases: `Nanite::FSceneProxyBase`, `FGeometryCollectionSceneProxyBase`.
- `FNaniteInstancedSkinnedMeshSceneProxy` (Engine/Source/Runtime/Engine/Private/InstancedSkinnedMeshSceneProxy.h:30) — bases: `Nanite::FSkinnedSceneProxy`.
- `FNaniteSplineMeshSceneProxy` (Engine/Source/Runtime/Engine/Public/SplineMeshSceneProxy.h:181) — bases: `Nanite::FSceneProxy`, `FSplineMeshSceneProxyCommon`.
- `FNavCorridorDebugRenderSceneProxy` (Engine/Plugins/Runtime/NavCorridor/Source/NavCorridor/Private/NavCorridorTestingComponent.cpp:187) — bases: `FDebugRenderSceneProxy`.
- `FNavLinkRenderingProxy` (Engine/Source/Runtime/NavigationSystem/Public/NavLinkRenderingProxy.h:15) — bases: `FPrimitiveSceneProxy`.
- `FNavLocalGridSceneProxy` (Engine/Source/Runtime/AIModule/Private/GameplayDebugger/GameplayDebuggerCategory_NavLocalGrid.cpp:23) — bases: `FDebugRenderSceneProxy`.
- `FNavMeshSceneProxy` (Engine/Source/Runtime/NavigationSystem/Public/NavMesh/NavMeshRenderingComponent.h:121) — bases: `FDebugRenderSceneProxy`, `FNoncopyable`.
- `FNavTestSceneProxy` (Engine/Source/Runtime/NavigationSystem/Public/NavMesh/NavTestRenderingComponent.h:20) — bases: `FDebugRenderSceneProxy`.
- `FNiagaraSceneProxy` (Engine/Plugins/FX/Niagara/Source/Niagara/Public/NiagaraSceneProxy.h:34) — bases: `FPrimitiveSceneProxy`.
- `FOceanCollisionSceneProxy` (Engine/Plugins/Experimental/Water/Source/Runtime/Private/OceanCollisionComponent.cpp:130) — bases: `FPrimitiveSceneProxy`.
- `FOctreeDynamicMeshSceneProxy` (Engine/Plugins/Runtime/MeshModelingToolset/Source/ModelingComponents/Private/Components/OctreeDynamicMeshSceneProxy.h:31) — bases: `FBaseDynamicMeshSceneProxy`.
- `FPCGCollisionVisProxy` (Engine/Plugins/PCG/Source/PCGEditor/Private/DataVisualizations/PCGCollisionVisComponent.cpp:16) — bases: `FPrimitiveSceneProxy`.
- `FPVBoneSceneProxy` (Engine/Plugins/Experimental/ProceduralVegetationEditor/Source/ProceduralVegetationEditor/Private/Visualizations/PVBoneComponent.h:15) — bases: `FPrimitiveSceneProxy`.
- `FPVLineSceneProxy` (Engine/Plugins/Experimental/ProceduralVegetationEditor/Source/ProceduralVegetationEditor/Private/Visualizations/PVLineBatchComponent.h:40) — bases: `FPrimitiveSceneProxy`.
- `FPaperFlipbookSceneProxy` (Engine/Plugins/2D/Paper2D/Source/Paper2D/Private/PaperFlipbookSceneProxy.h:10) — bases: `FPaperRenderSceneProxy_SpriteBase`.
- `FPaperRenderSceneProxy` (Engine/Plugins/2D/Paper2D/Source/Paper2D/Private/PaperRenderSceneProxy.h:124) — bases: `FPrimitiveSceneProxy`.
- `FPaperRenderSceneProxy_SpriteBase` (Engine/Plugins/2D/Paper2D/Source/Paper2D/Private/PaperRenderSceneProxy.h:196) — bases: `FPaperRenderSceneProxy`.
- `FPaperSpriteSceneProxy` (Engine/Plugins/2D/Paper2D/Source/Paper2D/Private/PaperSpriteSceneProxy.h:14) — bases: `FPaperRenderSceneProxy_SpriteBase`.
- `FPaperTerrainSceneProxy` (Engine/Plugins/2D/Paper2D/Source/Paper2D/Private/Terrain/PaperTerrainComponent.cpp:62) — bases: `FPaperRenderSceneProxy`.
- `FPaperTileMapRenderSceneProxy` (Engine/Plugins/2D/Paper2D/Source/Paper2D/Private/PaperTileMapRenderSceneProxy.h:15) — bases: `FPaperRenderSceneProxy`.
- `FParticleSystemSceneProxy` (Engine/Source/Runtime/Engine/Public/ParticleSystemSceneProxy.h:36) — bases: `FPrimitiveSceneProxy`.
- `FPathDebugRenderSceneProxy` (Engine/Source/Runtime/AIModule/Private/GameplayDebugger/GameplayDebuggerCategory_AI.cpp:390) — bases: `FDebugRenderSceneProxy`.
- `FPointSetSceneProxy` (Engine/Plugins/Runtime/MeshModelingToolset/Source/ModelingComponents/Private/Drawing/PointSetComponent.cpp:41) — bases: `FPrimitiveSceneProxy`.
- `FPrimitiveSceneProxy` (Engine/Source/Runtime/Engine/Public/PrimitiveSceneProxy.h:291) — bases: none.
- `FProceduralMeshSceneProxy` (Engine/Plugins/Runtime/ProceduralMeshComponent/Source/ProceduralMeshComponent/Private/ProceduralMeshComponent.cpp:92) — bases: `FPrimitiveSceneProxy`.
- `FSOContainerRenderingSceneProxy` (Engine/Plugins/Runtime/SmartObjects/Source/SmartObjectsModule/Private/SmartObjectContainerRenderingComponent.cpp:25) — bases: `FDebugRenderSceneProxy`.
- `FSORenderingSceneProxy` (Engine/Plugins/Runtime/SmartObjects/Source/SmartObjectsModule/Private/SmartObjectRenderingComponent.cpp:15) — bases: `FDebugRenderSceneProxy`.
- `FSkeletalMeshSceneProxy` (Engine/Source/Runtime/Engine/Public/SkeletalMeshSceneProxy.h:21) — bases: `FPrimitiveSceneProxy`.
- `FSmartObjectDebugSceneProxy` (Engine/Plugins/Runtime/SmartObjects/Source/SmartObjectsModule/Public/SmartObjectDebugSceneProxy.h:13) — bases: `FDebugRenderSceneProxy`.
- `FSparseVolumeTextureViewerSceneProxy` (Engine/Source/Runtime/Renderer/Private/SparseVolumeTexture/SparseVolumeTextureViewerSceneProxy.h:21) — bases: `FPrimitiveSceneProxy`.
- `FSphereSceneProxy` (Engine/Source/Runtime/Engine/Private/Components/SphereComponent.cpp:111) — bases: `FPrimitiveSceneProxy`.
- `FSplineMeshSceneProxy` (Engine/Source/Runtime/Engine/Private/Components/SplineComponent.cpp:3409) — bases: `FPrimitiveSceneProxy`.
- `FSplineMeshSceneProxy` (Engine/Source/Runtime/Engine/Public/SplineMeshSceneProxy.h:141) — bases: `FStaticMeshSceneProxy`, `FSplineMeshSceneProxyCommon`.
- `FSplinePDISceneProxy` (Engine/Source/Runtime/Engine/Private/Components/SplineComponent.cpp:3345) — bases: `FPrimitiveSceneProxy`.
- `FSpriteSceneProxy` (Engine/Source/Runtime/Engine/Private/Components/BillboardComponent.cpp:30) — bases: `FPrimitiveSceneProxy`.
- `FStaticMeshSceneProxy` (Engine/Source/Runtime/Engine/Public/StaticMeshSceneProxy.h:34) — bases: `FPrimitiveSceneProxy`.
- `FStaticMeshSceneProxyExt` (Engine/Plugins/Enterprise/DataprepEditor/Source/DataprepEditor/Private/Widgets/SDataprepEditorViewport.cpp:115) — bases: `FStaticMeshSceneProxy`.
- `FStereoStaticMeshSceneProxy` (Engine/Plugins/Experimental/PanoramicCapture/Source/PanoramicCapture/Private/StereoStaticMeshComponent.cpp:10) — bases: `FStaticMeshSceneProxy`.
- `FTextRenderSceneProxy` (Engine/Source/Runtime/Engine/Private/Components/TextRenderComponent.cpp:586) — bases: `FPrimitiveSceneProxy`.
- `FTriangleSetSceneProxy` (Engine/Plugins/Runtime/MeshModelingToolset/Source/ModelingComponents/Private/Drawing/TriangleSetComponent.cpp:32) — bases: `FPrimitiveSceneProxy`.
- `FVectorFieldSceneProxy` (Engine/Source/Runtime/Engine/Private/VectorField.cpp:661) — bases: `FPrimitiveSceneProxy`.
- `FVirtualHeightfieldMeshSceneProxy` (Engine/Plugins/Experimental/VirtualHeightfieldMesh/Source/VirtualHeightfieldMesh/Private/VirtualHeightfieldMeshSceneProxy.h:12) — bases: `FPrimitiveSceneProxy`.
- `FWaterBodyInfoMeshSceneProxy` (Engine/Plugins/Experimental/Water/Source/Runtime/Private/WaterBodyInfoMeshComponent.h:44) — bases: `FStaticMeshSceneProxy`.
- `FWaterMeshSceneProxy` (Engine/Plugins/Experimental/Water/Source/Runtime/Public/WaterMeshSceneProxy.h:103) — bases: `FPrimitiveSceneProxy`.
- `FWidget3DSceneProxy` (Engine/Source/Runtime/UMG/Private/Components/WidgetComponent.cpp:310) — bases: `FPrimitiveSceneProxy`.
- `FWidgetBoxProxy` (Engine/Source/Runtime/UMG/Private/Components/WidgetComponent.cpp:844) — bases: `FPrimitiveSceneProxy`.
- `FZoneGraphAnnotationSceneProxy` (Engine/Plugins/Runtime/ZoneGraphAnnotations/Source/ZoneGraphAnnotations/Public/ZoneGraphAnnotationComponent.h:22) — bases: `FDebugRenderSceneProxy`.
- `FZoneGraphSceneProxy` (Engine/Plugins/Runtime/ZoneGraph/Source/ZoneGraph/Public/ZoneGraphRenderingComponent.h:17) — bases: `FDebugRenderSceneProxy`.
- `FZoneGraphTestingSceneProxy` (Engine/Plugins/Runtime/ZoneGraph/Source/ZoneGraphDebug/Private/ZoneGraphTestingActor.cpp:205) — bases: `FPrimitiveSceneProxy`.
- `FZoneShapeSceneProxy` (Engine/Plugins/Runtime/ZoneGraph/Source/ZoneGraph/Private/ZoneShapeComponent.cpp:769) — bases: `FPrimitiveSceneProxy`.
- `Nanite::FGroomSceneProxy` (Engine/Plugins/Runtime/HairStrands/Source/HairStrandsCore/Private/NaniteGroomAsset.h:66) — bases: `FSceneProxyBase`.
- `Nanite::FSceneProxy` (Engine/Source/Runtime/Engine/Public/NaniteSceneProxy.h:490) — bases: `FSceneProxyBase`.
- `Nanite::FSceneProxyBase` (Engine/Source/Runtime/Engine/Public/NaniteSceneProxy.h:219) — bases: `FPrimitiveSceneProxy`.
- `Nanite::FSkinnedSceneProxy` (Engine/Source/Runtime/Engine/Public/NaniteSceneProxy.h:757) — bases: `FSceneProxyBase`.
- `NiagaraStaticMeshComponentPrivate::FMeshSceneProxy` (Engine/Plugins/FX/NiagaraNanite/Source/NiagaraNanite/Private/Renderer/NiagaraStaticMeshComponent.cpp:22) — bases: `FInstancedStaticMeshSceneProxy`.
- `NiagaraStaticMeshComponentPrivate::FNaniteSceneProxy` (Engine/Plugins/FX/NiagaraNanite/Source/NiagaraNanite/Private/Renderer/NiagaraStaticMeshComponent.cpp:12) — bases: `Nanite::FSceneProxy`.
- `UAppleARKitFaceMeshComponent` (Engine/Plugins/Runtime/AR/AppleAR/AppleARKitFaceSupport/Source/AppleARKitFaceSupport/Public/AppleARKitFaceMeshComponent.h:110) — bases: `UProceduralMeshComponent`.
- `UArrowComponent` (Engine/Source/Runtime/Engine/Classes/Components/ArrowComponent.h:19) — bases: `UPrimitiveComponent`.
- `UAvaTickerComponent` (Engine/Plugins/VirtualProduction/Avalanche/Source/Avalanche/Public/Framework/Ticker/AvaTickerComponent.h:60) — bases: `UPrimitiveComponent`.
- `UBakedShallowWaterSimulationComponent` (Engine/Plugins/Experimental/Water/Source/Runtime/Public/BakedShallowWaterSimulationComponent.h:350) — bases: `UPrimitiveComponent`.
- `UBaseDynamicMeshComponent` (Engine/Source/Runtime/GeometryFramework/Public/Components/BaseDynamicMeshComponent.h:124) — bases: `UMeshComponent`, `IToolFrameworkComponent`, `IMeshVertexCommandChangeTarget`, `IMeshCommandChangeTarget`, `IMeshReplacementCommandChangeTarget`.
- `UBasic2DLineSetComponent` (Engine/Plugins/Editor/UVEditor/Source/UVEditorTools/Public/Drawing/BasicLineSetComponent.h:111) — bases: `UBasicLineSetComponentBase`, `TBasicElementSet<FVector2f, 2>`.
- `UBasic2DPointSetComponent` (Engine/Plugins/Editor/UVEditor/Source/UVEditorTools/Public/Drawing/BasicPointSetComponent.h:111) — bases: `UBasicPointSetComponentBase`, `TBasicElementSet<FVector2f, 1>`.
- `UBasic2DTriangleSetComponent` (Engine/Plugins/Editor/UVEditor/Source/UVEditorTools/Public/Drawing/BasicTriangleSetComponent.h:89) — bases: `UBasicTriangleSetComponentBase`, `TBasicElementSet<FVector2f, 3>`.
- `UBasic3DLineSetComponent` (Engine/Plugins/Editor/UVEditor/Source/UVEditorTools/Public/Drawing/BasicLineSetComponent.h:140) — bases: `UBasicLineSetComponentBase`, `TBasicElementSet<FVector3f, 2>`.
- `UBasic3DPointSetComponent` (Engine/Plugins/Editor/UVEditor/Source/UVEditorTools/Public/Drawing/BasicPointSetComponent.h:140) — bases: `UBasicPointSetComponentBase`, `TBasicElementSet<FVector3f, 1>`.
- `UBasic3DTriangleSetComponent` (Engine/Plugins/Editor/UVEditor/Source/UVEditorTools/Public/Drawing/BasicTriangleSetComponent.h:118) — bases: `UBasicTriangleSetComponentBase`, `TBasicElementSet<FVector3f, 3>`.
- `UBasicLineSetComponentBase` (Engine/Plugins/Editor/UVEditor/Source/UVEditorTools/Public/Drawing/BasicLineSetComponent.h:32) — bases: `UMeshComponent`.
- `UBasicPointSetComponentBase` (Engine/Plugins/Editor/UVEditor/Source/UVEditorTools/Public/Drawing/BasicPointSetComponent.h:32) — bases: `UMeshComponent`.
- `UBasicTriangleSetComponentBase` (Engine/Plugins/Editor/UVEditor/Source/UVEditorTools/Public/Drawing/BasicTriangleSetComponent.h:32) — bases: `UMeshComponent`.
- `UBillboardComponent` (Engine/Source/Runtime/Engine/Classes/Components/BillboardComponent.h:19) — bases: `UPrimitiveComponent`.
- `UBoxComponent` (Engine/Source/Runtime/Engine/Classes/Components/BoxComponent.h:18) — bases: `UShapeComponent`.
- `UBrushComponent` (Engine/Source/Runtime/Engine/Classes/Components/BrushComponent.h:21) — bases: `UPrimitiveComponent`.
- `UCEClonerComponent` (Engine/Plugins/VirtualProduction/ClonerEffector/Source/ClonerEffector/Public/Cloner/CEClonerComponent.h:26) — bases: `UNiagaraComponent`.
- `UCableComponent` (Engine/Plugins/Runtime/CableComponent/Source/CableComponent/Classes/CableComponent.h:31) — bases: `UMeshComponent`.
- `UCalibrationPointComponent` (Engine/Plugins/VirtualProduction/CameraCalibrationCore/Source/CameraCalibrationCore/Public/CalibrationPointComponent.h:30) — bases: `UProceduralMeshComponent`.
- `UCameraProxyMeshComponent` (Engine/Source/Runtime/Engine/Classes/Camera/CameraComponent.h:19) — bases: `UStaticMeshComponent`.
- `UCapsuleComponent` (Engine/Source/Runtime/Engine/Classes/Components/CapsuleComponent.h:16) — bases: `UShapeComponent`.
- `UCascadeParticleSystemComponent` (Engine/Plugins/FX/Cascade/Source/Cascade/Classes/CascadeParticleSystemComponent.h:14) — bases: `UParticleSystemComponent`.
- `UChaosClothComponent` (Engine/Plugins/ChaosClothAsset/Source/ChaosClothAssetEngine/Public/ChaosClothAsset/ClothComponent.h:86) — bases: `USkinnedMeshComponent`, `IDataflowPhysicsSolverInterface`, `UE::Chaos::ClothAsset::IClothComponentAdapter`.
- `UChaosPathedMovementDebugDrawComponent` (Engine/Plugins/Experimental/ChaosMover/Source/ChaosMover/Public/ChaosMover/PathedMovement/ChaosPathedMovementDebugDrawComponent.h:41) — bases: `UDebugDrawComponent`.
- `UChaosVDInstancedStaticMeshComponent` (Engine/Plugins/ChaosVD/Source/ChaosVD/Private/Components/ChaosVDInstancedStaticMeshComponent.h:26) — bases: `UInstancedStaticMeshComponent`, `IChaosVDGeometryComponent`, `IChaosVDPooledObject`.
- `UChaosVDStaticMeshComponent` (Engine/Plugins/ChaosVD/Source/ChaosVD/Private/Components/ChaosVDStaticMeshComponent.h:12) — bases: `UStaticMeshComponent`, `IChaosVDGeometryComponent`, `IChaosVDPooledObject`.
- `UCineSplineComponent` (Engine/Plugins/Experimental/CineCameraRigs/Source/CineCameraRigs/Public/CineSplineComponent.h:19) — bases: `USplineComponent`.
- `UClothGeneratorComponent` (Engine/Plugins/Animation/MLDeformer/ChaosClothGenerator/Source/ChaosClothGenerator/Private/ClothGeneratorComponent.h:32) — bases: `UChaosClothComponent`.
- `UClusterUnionComponent` (Engine/Source/Runtime/Engine/Classes/PhysicsEngine/ClusterUnionComponent.h:210) — bases: `UPrimitiveComponent`.
- `UClusterUnionVehicleComponent` (Engine/Plugins/Experimental/ChaosModularVehicle/Source/ChaosModularVehicleEngine/Public/ChaosModularVehicle/ClusterUnionVehicleComponent.h:12) — bases: `UClusterUnionComponent`.
- `UColorCorrectionInvisibleComponent` (Engine/Plugins/Experimental/ColorCorrectRegions/Source/ColorCorrectRegions/Public/ColorCorrectRegion.h:438) — bases: `UPrimitiveComponent`.
- `UCompositeDepthMeshComponent` (Engine/Plugins/Compositing/Composite/Source/Composite/Public/Components/CompositeDepthMeshComponent.h:13) — bases: `UStaticMeshComponent`.
- `UCompositeMeshComponent` (Engine/Plugins/Compositing/Composite/Source/Composite/Public/Components/CompositeMeshComponent.h:27) — bases: `UStaticMeshComponent`.
- `UControlPointMeshComponent` (Engine/Source/Runtime/Landscape/Classes/ControlPointMeshComponent.h:11) — bases: `UStaticMeshComponent`.
- `UControlRigComponent` (Engine/Plugins/Animation/ControlRig/Source/ControlRig/Public/ControlRigComponent.h:175) — bases: `UPrimitiveComponent`.
- `UCustomMeshComponent` (Engine/Plugins/Runtime/CustomMeshComponent/Source/CustomMeshComponent/Classes/CustomMeshComponent.h:31) — bases: `UMeshComponent`.
- `UCustomStaticMeshComponent` (Engine/Plugins/Enterprise/DataprepEditor/Source/DataprepEditor/Private/Widgets/SDataprepEditorViewport.h:33) — bases: `UStaticMeshComponent`.
- `UDataflowComponent` (Engine/Plugins/Dataflow/Source/DataflowEnginePlugin/Public/Dataflow/DataflowComponent.h:21) — bases: `UPrimitiveComponent`.
- `UDataflowDebugDrawComponent` (Engine/Source/Runtime/Dataflow/Engine/Public/Dataflow/DataflowDebugDrawComponent.h:12) — bases: `UDebugDrawComponent`.
- `UDataflowDebugMeshComponent` (Engine/Plugins/Dataflow/Source/DataflowEditor/Public/DataflowRendering/DataflowDebugMeshComponent.h:13) — bases: `UDebugDrawComponent`.
- `UDataflowEditorCollectionComponent` (Engine/Plugins/Dataflow/Source/DataflowEditor/Private/Dataflow/DataflowEditorCollectionComponent.h:16) — bases: `UDynamicMeshComponent`.
- `UDebugDrawComponent` (Engine/Source/Runtime/Engine/Classes/Debug/DebugDrawComponent.h:49) — bases: `UPrimitiveComponent`.
- `UDeformableCollisionsComponent` (Engine/Plugins/Experimental/ChaosFlesh/Source/ChaosFleshEngine/Public/ChaosFlesh/ChaosDeformableCollisionsComponent.h:19) — bases: `UDeformablePhysicsComponent`.
- `UDeformableConstraintsComponent` (Engine/Plugins/Experimental/ChaosFlesh/Source/ChaosFleshEngine/Public/ChaosFlesh/ChaosDeformableConstraintsComponent.h:94) — bases: `UDeformablePhysicsComponent`.
- `UDeformableGameplayComponent` (Engine/Plugins/Experimental/ChaosFlesh/Source/ChaosFleshEngine/Public/ChaosFlesh/ChaosDeformableGameplayComponent.h:55) — bases: `UDeformableTetrahedralComponent`.
- `UDeformablePhysicsComponent` (Engine/Plugins/Experimental/ChaosFlesh/Source/ChaosFleshEngine/Public/ChaosFlesh/ChaosDeformablePhysicsComponent.h:21) — bases: `UPrimitiveComponent`, `IDeformableInterface`.
- `UDeformableTetrahedralComponent` (Engine/Plugins/Experimental/ChaosFlesh/Source/ChaosFleshEngine/Public/ChaosFlesh/ChaosDeformableTetrahedralComponent.h:90) — bases: `UDeformablePhysicsComponent`, `IDataflowGeometryCachable`.
- `UDestructibleComponent` (Engine/Plugins/Runtime/ApexDestruction/Source/ApexDestruction/Public/DestructibleComponent.h:31) — bases: `USkinnedMeshComponent`, `IDestructibleInterface`.
- `UDirectMeshControlComponent` (Engine/Plugins/Experimental/Animation/DirectMeshControl/Source/DirectMeshControl/Public/DirectMeshControlComponent.h:17) — bases: `USkeletalMeshComponent`.
- `UDisplayClusterScreenComponent` (Engine/Plugins/Runtime/nDisplay/Source/DisplayCluster/Public/Components/DisplayClusterScreenComponent.h:15) — bases: `UStaticMeshComponent`.
- `UDisplayClusterStageIsosphereComponent` (Engine/Plugins/Runtime/nDisplay/Source/DisplayCluster/Public/Components/DisplayClusterStageIsosphereComponent.h:12) — bases: `UProceduralMeshComponent`.
- `UDisplayClusterWidgetComponent` (Engine/Plugins/Runtime/nDisplay/Source/DisplayCluster/Private/Game/EngineClasses/Scene/DisplayClusterWidgetComponent.h:13) — bases: `UWidgetComponent`.
- `UDisplayClusterWorldOriginComponent` (Engine/Plugins/Runtime/nDisplay/Source/DisplayClusterConfigurator/Private/Views/Viewport/DisplayClusterWorldOriginComponent.h:14) — bases: `UStaticMeshComponent`.
- `UDrawFrustumComponent` (Engine/Source/Runtime/Engine/Classes/Components/DrawFrustumComponent.h:18) — bases: `UPrimitiveComponent`.
- `UDrawSphereComponent` (Engine/Source/Runtime/Engine/Classes/Components/DrawSphereComponent.h:18) — bases: `USphereComponent`.
- `UDynamicMeshComponent` (Engine/Source/Runtime/GeometryFramework/Public/Components/DynamicMeshComponent.h:171) — bases: `UBaseDynamicMeshComponent`, `IInterface_CollisionDataProvider`.
- `UE::Avalanche::FTickerSceneProxy` (Engine/Plugins/VirtualProduction/Avalanche/Source/Avalanche/Private/Framework/Ticker/AvaTickerSceneProxy.h:15) — bases: `FPrimitiveSceneProxy`.
- `UE::MeshPartition::FDrawMeshPartitionCollisionSceneProxy` (Engine/Plugins/Experimental/MeshPartition/Source/MeshPartition/Private/MeshPartitionCollisionComponent.cpp:337) — bases: `FPrimitiveSceneProxy`.
- `UE::MeshPartition::FMegaMeshCustomPreviewSceneProxy` (Engine/Plugins/Experimental/MeshPartition/Source/MeshPartitionEditor/Internal/MeshPartitionPreviewSceneProxy.h:25) — bases: `FPrimitiveSceneProxy`.
- `UE::MeshPartition::MegaMeshModifierComponentLocals::FMegaMeshModifierComponentSceneProxy` (Engine/Plugins/Experimental/MeshPartition/Source/MeshPartitionEditor/Private/MeshPartitionModifierComponent.cpp:58) — bases: `FPrimitiveSceneProxy`.
- `UE::MeshPartition::UBooleanModifier` (Engine/Plugins/Experimental/MeshPartition/Source/MeshPartitionEditor/Public/Modifiers/MeshPartitionBooleanModifier.h:105) — bases: `MeshPartition::UMeshBasedModifierBase`.
- `UE::MeshPartition::UEditableModifierBase` (Engine/Plugins/Experimental/MeshPartition/Source/MeshPartitionEditor/Public/Modifiers/MeshPartitionEditableModifierBase.h:17) — bases: `MeshPartition::UModifierComponent`.
- `UE::MeshPartition::UInstancedPatchModifier` (Engine/Plugins/Experimental/MeshPartition/Source/MeshPartitionEditor/Public/Modifiers/MeshPartitionInstancedPatchModifier.h:26) — bases: `MeshPartition::UPatchModifier`, `ICodeReusableModifier`.
- `UE::MeshPartition::UInstancedProjectionModifier` (Engine/Plugins/Experimental/MeshPartition/Source/MeshPartitionEditor/Public/Modifiers/MeshPartitionInstancedProjectionModifier.h:47) — bases: `MeshPartition::UModifierComponent`, `ICodeReusableModifier`.
- `UE::MeshPartition::UInstancedTexturePatchModifier` (Engine/Plugins/Experimental/MeshPartition/Source/MeshPartitionEditor/Public/Modifiers/MeshPartitionInstancedTexturePatchModifier.h:15) — bases: `MeshPartition::UTexturePatchModifier`, `ICodeReusableModifier`.
- `UE::MeshPartition::ULakeModifier` (Engine/Plugins/Experimental/MeshPartitionWater/Source/MeshPartitionWater/Private/MeshPartitionLakeModifier.h:26) — bases: `MeshPartition::UWaterModifier`.
- `UE::MeshPartition::ULatticeModifier` (Engine/Plugins/Experimental/MeshPartition/Source/MeshPartitionEditor/Public/Modifiers/MeshPartitionLatticeModifier.h:17) — bases: `MeshPartition::UEditableModifierBase`, `ILatticeStateStorage`.
- `UE::MeshPartition::ULevelInstanceAdapter` (Engine/Plugins/Experimental/MeshPartition/Source/MeshPartitionEditor/Public/Modifiers/MeshPartitionLevelInstanceAdapter.h:20) — bases: `MeshPartition::UModifierComponent`.
- `UE::MeshPartition::UMeshBasedModifierBase` (Engine/Plugins/Experimental/MeshPartition/Source/MeshPartitionEditor/Public/Modifiers/MeshPartitionMeshBasedModifierBase.h:98) — bases: `MeshPartition::UModifierComponent`.
- `UE::MeshPartition::UMeshPartitionCollisionComponent` (Engine/Plugins/Experimental/MeshPartition/Source/MeshPartition/Public/MeshPartitionCollisionComponent.h:35) — bases: `UPrimitiveComponent`, `IInterface_CollisionDataProvider`.
- `UE::MeshPartition::UMeshPartitionComponent` (Engine/Plugins/Experimental/MeshPartition/Source/MeshPartition/Public/MeshPartitionComponent.h:24) — bases: `UPrimitiveComponent`.
- `UE::MeshPartition::UMeshPartitionEditorComponent` (Engine/Plugins/Experimental/MeshPartition/Source/MeshPartitionEditor/Public/MeshPartitionEditorComponent.h:81) — bases: `UMeshPartitionComponent`.
- `UE::MeshPartition::UMeshPartitionStaticMeshComponent` (Engine/Plugins/Experimental/MeshPartition/Source/MeshPartition/Public/MeshPartitionStaticMeshComponent.h:15) — bases: `UStaticMeshComponent`.
- `UE::MeshPartition::UMeshProjectModifier` (Engine/Plugins/Experimental/MeshPartition/Source/MeshPartitionEditor/Public/Modifiers/MeshPartitionMeshProjectModifier.h:19) — bases: `MeshPartition::UMeshBasedModifierBase`.
- `UE::MeshPartition::UMeshProviderModifier` (Engine/Plugins/Experimental/MeshPartition/Source/MeshPartitionEditor/Public/Modifiers/MeshPartitionMeshProvider.h:28) — bases: `MeshPartition::UModifierComponent`.
- `UE::MeshPartition::UModifierComponent` (Engine/Plugins/Experimental/MeshPartition/Source/MeshPartitionEditor/Public/MeshPartitionModifierComponent.h:219) — bases: `UPrimitiveComponent`, `MeshPartition::IModifierBlueprintInterface`.
- `UE::MeshPartition::UNoiseModifier` (Engine/Plugins/Experimental/MeshPartition/Source/MeshPartitionEditor/Public/Modifiers/MeshPartitionNoiseModifier.h:66) — bases: `MeshPartition::UModifierComponent`.
- `UE::MeshPartition::UOceanModifier` (Engine/Plugins/Experimental/MeshPartitionWater/Source/MeshPartitionWater/Private/MeshPartitionOceanModifier.h:13) — bases: `MeshPartition::UWaterModifier`.
- `UE::MeshPartition::UPCGAdapterComponent` (Engine/Plugins/Experimental/PCGMeshPartitionInterop/Source/PCGMeshPartitionInteropEditor/Public/MeshPartitionPCGAdapterComponent.h:23) — bases: `MeshPartition::UModifierComponent`.
- `UE::MeshPartition::UPatchModifier` (Engine/Plugins/Experimental/MeshPartition/Source/MeshPartitionEditor/Public/Modifiers/MeshPartitionPatchModifier.h:27) — bases: `MeshPartition::UModifierComponent`.
- `UE::MeshPartition::UPreviewMeshComponent` (Engine/Plugins/Experimental/MeshPartition/Source/MeshPartitionEditor/Public/MeshPartitionPreviewComponents.h:38) — bases: `UMeshComponent`, `IInterface_CollisionDataProvider`.
- `UE::MeshPartition::UProjectMeshLayersModifier` (Engine/Plugins/Experimental/MeshPartition/Source/MeshPartitionEditor/Public/Modifiers/MeshPartitionProjectSculptLayersModifier.h:86) — bases: `MeshPartition::UEditableModifierBase`, `IMeshSculptLayersManager`, `ICodeReusableModifier`.
- `UE::MeshPartition::URemeshModifier` (Engine/Plugins/Experimental/MeshPartition/Source/MeshPartitionEditor/Public/Modifiers/MeshPartitionRemeshModifier.h:281) — bases: `MeshPartition::URemeshModifierBase`.
- `UE::MeshPartition::URemeshModifierBase` (Engine/Plugins/Experimental/MeshPartition/Source/MeshPartitionEditor/Public/Modifiers/MeshPartitionRemeshModifier.h:33) — bases: `MeshPartition::UModifierComponent`.
- `UE::MeshPartition::URiverModifier` (Engine/Plugins/Experimental/MeshPartitionWater/Source/MeshPartitionWater/Private/MeshPartitionRiverModifier.h:23) — bases: `MeshPartition::UWaterModifier`.
- `UE::MeshPartition::USimpleWriteModifier` (Engine/Plugins/Experimental/MeshPartition/Source/MeshPartitionEditor/Public/Modifiers/MeshPartitionSimpleWriteModifier.h:49) — bases: `MeshPartition::UModifierComponent`, `ICodeReusableModifier`.
- `UE::MeshPartition::USplineModifier` (Engine/Plugins/Experimental/MeshPartition/Source/MeshPartitionEditor/Public/Modifiers/MeshPartitionSplineModifier.h:121) — bases: `MeshPartition::UModifierComponent`, `MeshPartition::ISplineModifierBlueprintInterface`.
- `UE::MeshPartition::USplineRemeshModifier` (Engine/Plugins/Experimental/MeshPartition/Source/MeshPartitionEditor/Public/Modifiers/MeshPartitionSplineRemeshModifier.h:21) — bases: `MeshPartition::URemeshModifierBase`.
- `UE::MeshPartition::UStaticMeshPreviewComponent` (Engine/Plugins/Experimental/MeshPartition/Source/MeshPartitionEditor/Public/MeshPartitionPreviewComponents.h:25) — bases: `UMeshPartitionStaticMeshComponent`.
- `UE::MeshPartition::UTexturePatchModifier` (Engine/Plugins/Experimental/MeshPartition/Source/MeshPartitionEditor/Public/Modifiers/MeshPartitionTexturePatchModifier.h:363) — bases: `MeshPartition::UModifierComponent`.
- `UE::MeshPartition::UWaterModifier` (Engine/Plugins/Experimental/MeshPartitionWater/Source/MeshPartitionWater/Public/MeshPartitionWaterModifier.h:22) — bases: `MeshPartition::UModifierComponent`.
- `UE::MeshPartition::UWeightUtilityModifier` (Engine/Plugins/Experimental/MeshPartition/Source/MeshPartitionEditor/Public/Modifiers/MeshPartitionWeightUtilityModifier.h:22) — bases: `MeshPartition::UModifierComponent`.
- `UE::UAF::Debug::FAnimNextDebugSceneProxy` (Engine/Plugins/Experimental/UAF/UAF/Source/UAF/Internal/AnimNextDebugDraw.h:28) — bases: `FPrimitiveSceneProxy`.
- `UE::UsdDrawModeComponentImpl::Private::FUsdCardsSceneProxy` (Engine/Plugins/Runtime/USDCore/Source/USDClasses/Private/USDDrawModeComponent.cpp:351) — bases: `FPrimitiveSceneProxy`.
- `UE::UsdDrawModeComponentImpl::Private::FUsdDrawModeLinesSceneProxy` (Engine/Plugins/Runtime/USDCore/Source/USDClasses/Private/USDDrawModeComponent.cpp:285) — bases: `FUsdLinesSceneProxy`.
- `UE::UsdDrawModeComponentImpl::Private::FUsdLinesSceneProxy` (Engine/Plugins/Runtime/USDCore/Source/USDClasses/Private/USDDrawModeComponent.cpp:53) — bases: `FPrimitiveSceneProxy`.
- `UE::UsdDrawModeComponentImpl::Private::FUsdOriginLinesSceneProxy` (Engine/Plugins/Runtime/USDCore/Source/USDClasses/Private/USDDrawModeComponent.cpp:303) — bases: `FUsdLinesSceneProxy`.
- `UE::XRCreative::Private::FRenderComponentSceneProxy` (Engine/Plugins/Experimental/XRCreativeFramework/Source/XRCreative/Private/ITF/XRCreativeITFRenderComponent.cpp:13) — bases: `FPrimitiveSceneProxy`.
- `UEQSRenderingComponent` (Engine/Source/Runtime/AIModule/Classes/EnvironmentQuery/EQSRenderingComponent.h:80) — bases: `UDebugDrawComponent`.
- `UFXSystemComponent` (Engine/Source/Runtime/Engine/Classes/Particles/ParticleSystemComponent.h:379) — bases: `UPrimitiveComponent`.
- `UFastGeoSurrogateComponent` (Engine/Plugins/Experimental/FastGeoStreaming/Source/FastGeoStreaming/Internal/FastGeoSurrogateComponent.h:13) — bases: `UPrimitiveComponent`.
- `UFieldSystemComponent` (Engine/Source/Runtime/Experimental/FieldSystem/Source/FieldSystemEngine/Public/Field/FieldSystemComponent.h:37) — bases: `UPrimitiveComponent`.
- `UFleshComponent` (Engine/Plugins/Experimental/ChaosFlesh/Source/ChaosFleshEngine/Public/ChaosFlesh/FleshComponent.h:29) — bases: `UDeformableGameplayComponent`.
- `UFleshGeneratorComponent` (Engine/Plugins/Animation/MLDeformer/ChaosFleshGenerator/Source/ChaosFleshGenerator/Private/FleshGeneratorComponent.h:20) — bases: `UFleshComponent`.
- `UFoliageInstancedStaticMeshComponent` (Engine/Source/Runtime/Foliage/Public/FoliageInstancedStaticMeshComponent.h:20) — bases: `UHierarchicalInstancedStaticMeshComponent`.
- `UGameplayDebuggerRenderingComponent` (Engine/Source/Runtime/GameplayDebugger/Public/GameplayDebuggerRenderingComponent.h:36) — bases: `UDebugDrawComponent`.
- `UGeometryCacheAbcFileComponent` (Engine/Plugins/Experimental/GeometryCacheAbcFile/Source/GeometryCacheAbcFile/Public/GeometryCacheAbcFileComponent.h:15) — bases: `UGeometryCacheComponent`.
- `UGeometryCacheComponent` (Engine/Plugins/Runtime/GeometryCache/Source/GeometryCache/Classes/GeometryCacheComponent.h:37) — bases: `UMeshComponent`.
- `UGeometryCacheUsdComponent` (Engine/Plugins/Importers/USDImporter/Source/GeometryCacheUSD/Public/GeometryCacheUSDComponent.h:16) — bases: `UGeometryCacheComponent`.
- `UGeometryCollectionComponent` (Engine/Source/Runtime/Experimental/GeometryCollectionEngine/Public/GeometryCollection/GeometryCollectionComponent.h:577) — bases: `UMeshComponent`, `IChaosNotifyHandlerInterface`.
- `UGizmoArrowComponent` (Engine/Source/Runtime/InteractiveToolsFramework/Public/BaseGizmos/GizmoArrowComponent.h:15) — bases: `UGizmoBaseComponent`.
- `UGizmoBaseComponent` (Engine/Source/Runtime/InteractiveToolsFramework/Public/BaseGizmos/GizmoBaseComponent.h:41) — bases: `UPrimitiveComponent`, `IGizmoBaseComponentInterface`.
- `UGizmoBoxComponent` (Engine/Source/Runtime/InteractiveToolsFramework/Public/BaseGizmos/GizmoBoxComponent.h:15) — bases: `UGizmoBaseComponent`.
- `UGizmoCircleComponent` (Engine/Source/Runtime/InteractiveToolsFramework/Public/BaseGizmos/GizmoCircleComponent.h:15) — bases: `UGizmoBaseComponent`.
- `UGizmoLineHandleComponent` (Engine/Source/Runtime/InteractiveToolsFramework/Public/BaseGizmos/GizmoLineHandleComponent.h:16) — bases: `UGizmoBaseComponent`.
- `UGizmoRectangleComponent` (Engine/Source/Runtime/InteractiveToolsFramework/Public/BaseGizmos/GizmoRectangleComponent.h:15) — bases: `UGizmoBaseComponent`.
- `UGrassInstancedStaticMeshComponent` (Engine/Source/Runtime/Foliage/Public/GrassInstancedStaticMeshComponent.h:10) — bases: `UHierarchicalInstancedStaticMeshComponent`.
- `UGroomComponent` (Engine/Plugins/Runtime/HairStrands/Source/HairStrandsCore/Public/GroomComponent.h:29) — bases: `UMeshComponent`, `ILODSyncInterface`, `INiagaraPhysicsAssetDICollectorInterface`.
- `UGroomSolverComponent` (Engine/Plugins/Runtime/HairStrands/Source/HairStrandsSolver/Public/GroomSolverComponent.h:99) — bases: `UMeshComponent`, `IDataflowPhysicsSolverInterface`.
- `UHLODInstancedSkinnedMeshComponent` (Engine/Source/Runtime/Engine/Public/WorldPartition/HLOD/HLODInstancedSkinnedMeshComponent.h:12) — bases: `UInstancedSkinnedMeshComponent`.
- `UHLODInstancedStaticMeshComponent` (Engine/Source/Runtime/Engine/Public/WorldPartition/HLOD/HLODInstancedStaticMeshComponent.h:13) — bases: `UInstancedStaticMeshComponent`.
- `UHeterogeneousVolumeComponent` (Engine/Source/Runtime/Engine/Classes/Components/HeterogeneousVolumeComponent.h:20) — bases: `UMeshComponent`.
- `UHierarchicalInstancedStaticMeshComponent` (Engine/Source/Runtime/Engine/Classes/Components/HierarchicalInstancedStaticMeshComponent.h:135) — bases: `UInstancedStaticMeshComponent`.
- `UISMPoolDebugDrawComponent` (Engine/Source/Runtime/Experimental/ISMPool/Public/ISMPool/ISMPoolDebugDrawComponent.h:15) — bases: `UDebugDrawComponent`.
- `UImagePlateComponent` (Engine/Plugins/Experimental/ImagePlate/Source/ImagePlate/Public/ImagePlateComponent.h:58) — bases: `UPrimitiveComponent`.
- `UImagePlateFrustumComponent` (Engine/Plugins/Experimental/ImagePlate/Source/ImagePlate/Private/ImagePlateFrustumComponent.h:13) — bases: `UPrimitiveComponent`.
- `UInsightsSkeletalMeshComponent` (Engine/Plugins/Animation/GameplayInsights/Source/GameplayInsightsEditor/Public/InsightsSkeletalMeshComponent.h:16) — bases: `USkeletalMeshComponent`.
- `UInstancedActorsModifierVolumeComponent` (Engine/Plugins/Runtime/InstancedActors/Source/InstancedActors/Public/InstancedActorsModifierVolumeComponent.h:28) — bases: `UPrimitiveComponent`.
- `UInstancedSkinnedMeshComponent` (Engine/Source/Runtime/Engine/Classes/Components/InstancedSkinnedMeshComponent.h:58) — bases: `USkinnedMeshComponent`.
- `UInstancedStaticMeshComponent` (Engine/Source/Runtime/Engine/Classes/Components/InstancedStaticMeshComponent.h:158) — bases: `UStaticMeshComponent`, `ISMInstanceManager`.
- `UInteractionTargetComponent` (Engine/Plugins/Experimental/InteractionInterface/Source/InteractableInterface/Public/InteractionTargetComponent.h:20) — bases: `UBoxComponent`, `IInteractionTarget`.
- `UInteractiveFoliageComponent` (Engine/Source/Runtime/Foliage/Private/InteractiveFoliageComponent.h:14) — bases: `UStaticMeshComponent`.
- `UJoinedSVGDynamicMeshComponent` (Engine/Plugins/VirtualProduction/SVGImporter/Source/SVGImporter/Public/ProceduralMeshes/JoinedSVGDynamicMeshComponent.h:58) — bases: `USVGBaseDynamicMeshComponent`.
- `ULakeCollisionComponent` (Engine/Plugins/Experimental/Water/Source/Runtime/Public/LakeCollisionComponent.h:11) — bases: `UPrimitiveComponent`.
- `ULandscapeComponent` (Engine/Source/Runtime/Landscape/Classes/LandscapeComponent.h:431) — bases: `UPrimitiveComponent`.
- `ULandscapeGizmoRenderComponent` (Engine/Source/Runtime/Landscape/Classes/LandscapeGizmoRenderComponent.h:14) — bases: `UPrimitiveComponent`.
- `ULandscapeHeightfieldCollisionComponent` (Engine/Source/Runtime/Landscape/Classes/LandscapeHeightfieldCollisionComponent.h:41) — bases: `UPrimitiveComponent`.
- `ULandscapeMeshProxyComponent` (Engine/Source/Runtime/Landscape/Classes/LandscapeMeshProxyComponent.h:16) — bases: `UStaticMeshComponent`.
- `ULandscapeNaniteComponent` (Engine/Source/Runtime/Landscape/Classes/LandscapeNaniteComponent.h:82) — bases: `UStaticMeshComponent`.
- `ULandscapeSplinesComponent` (Engine/Source/Runtime/Landscape/Classes/LandscapeSplinesComponent.h:106) — bases: `UPrimitiveComponent`.
- `ULidarPointCloudComponent` (Engine/Plugins/Enterprise/LidarPointCloud/Source/LidarPointCloudRuntime/Public/LidarPointCloudComponent.h:24) — bases: `UMeshComponent`.
- `ULineBatchComponent` (Engine/Source/Runtime/Engine/Classes/Components/LineBatchComponent.h:127) — bases: `UPrimitiveComponent`.
- `ULineSetComponent` (Engine/Plugins/Runtime/MeshModelingToolset/Source/ModelingComponents/Public/Drawing/LineSetComponent.h:42) — bases: `UMeshComponent`.
- `ULiveLinkDataPreviewComponent` (Engine/Plugins/Animation/LiveLink/Source/LiveLink/Public/Visualizers/LiveLinkDataPreviewComponent.h:29) — bases: `UInstancedStaticMeshComponent`.
- `ULiveLinkMarkerVisualizer` (Engine/Plugins/Animation/LiveLink/Source/LiveLink/Private/Visualizers/LiveLinkMarkerVisualizer.h:29) — bases: `UInstancedStaticMeshComponent`.
- `UMRMeshComponent` (Engine/Source/Runtime/MRMesh/Public/MRMeshComponent.h:105) — bases: `UPrimitiveComponent`, `IMRMesh`.
- `UMassCrowdLaneDataRenderingComponent` (Engine/Plugins/AI/MassCrowd/Source/MassCrowd/Public/MassCrowdLaneDataRenderingComponent.h:15) — bases: `UPrimitiveComponent`.
- `UMassNavigationTestingComponent` (Engine/Plugins/AI/MassAI/Source/MassNavigationEditor/Private/MassNavigationTestingActor.h:33) — bases: `UDebugDrawComponent`.
- `UMaterialBillboardComponent` (Engine/Source/Runtime/Engine/Classes/Components/MaterialBillboardComponent.h:61) — bases: `UPrimitiveComponent`.
- `UMediaStreamComponent` (Engine/Plugins/Experimental/MediaStream/Source/MediaStream/Public/MediaStreamComponent.h:14) — bases: `UStaticMeshComponent`.
- `UMeshComponent` (Engine/Source/Runtime/Engine/Classes/Components/MeshComponent.h:24) — bases: `UPrimitiveComponent`.
- `UMeshWireframeComponent` (Engine/Plugins/Runtime/MeshModelingToolset/Source/ModelingComponents/Public/Drawing/MeshWireframeComponent.h:92) — bases: `UMeshComponent`.
- `UMetaHumanDepthMeshComponent` (Engine/Plugins/MetaHuman/MetaHumanAnimator/Source/MetaHumanImageViewerEditor/Public/MetaHumanDepthMeshComponent.h:12) — bases: `UProceduralMeshComponent`.
- `UMetaHumanFootageComponent` (Engine/Plugins/MetaHuman/MetaHumanAnimator/Source/MetaHumanImageViewerEditor/Public/MetaHumanFootageComponent.h:36) — bases: `UPrimitiveComponent`.
- `UMetaHumanPerformanceControlRigComponent` (Engine/Plugins/MetaHuman/MetaHumanAnimator/Source/MetaHumanPerformance/Private/UI/MetaHumanPerformanceControlRigComponent.h:14) — bases: `UPrimitiveComponent`.
- `UMetaHumanTemplateMesh` (Engine/Plugins/MetaHuman/MetaHumanAnimator/Source/MetaHumanIdentity/Public/MetaHumanIdentityParts.h:649) — bases: `UDynamicMeshComponent`.
- `UMetaHumanTemplateMeshComponent` (Engine/Plugins/MetaHuman/MetaHumanAnimator/Source/MetaHumanIdentity/Public/MetaHumanTemplateMeshComponent.h:28) — bases: `UPrimitiveComponent`.
- `UMixedRealityCaptureBillboard` (Engine/Plugins/Runtime/MixedRealityCaptureFramework/Source/MixedRealityCaptureFramework/Private/MrcProjectionBillboard.h:12) — bases: `UMaterialBillboardComponent`.
- `UModelComponent` (Engine/Source/Runtime/Engine/Classes/Components/ModelComponent.h:33) — bases: `UPrimitiveComponent`, `IInterface_CollisionDataProvider`.
- `UMotionControllerComponent` (Engine/Source/Runtime/HeadMountedDisplay/Public/MotionControllerComponent.h:18) — bases: `UPrimitiveComponent`.
- `UNaniteDisplacedMeshComponent` (Engine/Plugins/Experimental/NaniteDisplacedMesh/Source/NaniteDisplacedMesh/Public/NaniteDisplacedMeshComponent.h:17) — bases: `UStaticMeshComponent`.
- `UNavCorridorTestingComponent` (Engine/Plugins/Runtime/NavCorridor/Source/NavCorridor/Public/NavCorridorTestingComponent.h:20) — bases: `UDebugDrawComponent`.
- `UNavLinkComponent` (Engine/Source/Runtime/NavigationSystem/Public/NavLinkComponent.h:16) — bases: `UPrimitiveComponent`, `INavLinkHostInterface`.
- `UNavLinkRenderingComponent` (Engine/Source/Runtime/NavigationSystem/Public/NavLinkRenderingComponent.h:14) — bases: `UPrimitiveComponent`.
- `UNavMeshRenderingComponent` (Engine/Source/Runtime/NavigationSystem/Public/NavMesh/NavMeshRenderingComponent.h:193) — bases: `UDebugDrawComponent`.
- `UNavTestRenderingComponent` (Engine/Source/Runtime/NavigationSystem/Public/NavMesh/NavTestRenderingComponent.h:115) — bases: `UDebugDrawComponent`.
- `UNiagaraComponent` (Engine/Plugins/FX/Niagara/Source/Niagara/Public/NiagaraComponent.h:57) — bases: `UFXSystemComponent`.
- `UNiagaraCullProxyComponent` (Engine/Plugins/FX/Niagara/Source/Niagara/Public/NiagaraCullProxyComponent.h:23) — bases: `UNiagaraComponent`.
- `UNiagaraStaticMeshComponent` (Engine/Plugins/FX/NiagaraNanite/Source/NiagaraNanite/Private/Renderer/NiagaraStaticMeshComponent.h:16) — bases: `UStaticMeshComponent`.
- `UNiagaraUIComponent` (Engine/Plugins/FX/NiagaraUIRenderer/Source/NiagaraUIRenderer/Public/NiagaraUIComponent.h:10) — bases: `UNiagaraComponent`.
- `UOceanBoxCollisionComponent` (Engine/Plugins/Experimental/Water/Source/Runtime/Public/OceanCollisionComponent.h:49) — bases: `UBoxComponent`.
- `UOceanCollisionComponent` (Engine/Plugins/Experimental/Water/Source/Runtime/Public/OceanCollisionComponent.h:13) — bases: `UPrimitiveComponent`.
- `UOctreeDynamicMeshComponent` (Engine/Plugins/Runtime/MeshModelingToolset/Source/ModelingComponents/Public/Components/OctreeDynamicMeshComponent.h:37) — bases: `UBaseDynamicMeshComponent`.
- `UPCGCollisionVisComponent` (Engine/Plugins/PCG/Source/PCGEditor/Public/DataVisualizations/PCGCollisionVisComponent.h:10) — bases: `UPrimitiveComponent`.
- `UPCGDebugDrawComponent` (Engine/Plugins/PCG/Source/PCG/Public/PCGDebugDrawComponent.h:32) — bases: `UDebugDrawComponent`.
- `UPCGProceduralISMComponent` (Engine/Plugins/PCG/Source/PCG/Private/Components/PCGProceduralISMComponent.h:34) — bases: `UStaticMeshComponent`.
- `UPCapBoneVisualiser` (Engine/Plugins/VirtualProduction/PerformanceCaptureWorkflow/Source/PerformanceCaptureWorkflow/Private/Visualizers/PCapBoneVisualizer.h:21) — bases: `UInstancedStaticMeshComponent`.
- `UPVBoneComponent` (Engine/Plugins/Experimental/ProceduralVegetationEditor/Source/ProceduralVegetationEditor/Private/Visualizations/PVBoneComponent.h:47) — bases: `UPrimitiveComponent`.
- `UPVLineBatchComponent` (Engine/Plugins/Experimental/ProceduralVegetationEditor/Source/ProceduralVegetationEditor/Private/Visualizations/PVLineBatchComponent.h:64) — bases: `UPrimitiveComponent`.
- `UPVScaleVisualizationComponent` (Engine/Plugins/Experimental/ProceduralVegetationEditor/Source/ProceduralVegetationEditor/Private/Visualizations/PVScaleVisualizationComponent.h:15) — bases: `UPVLineBatchComponent`.
- `UPVSkeletonVisualizerComponent` (Engine/Plugins/Experimental/ProceduralVegetationEditor/Source/ProceduralVegetationEditor/Private/Visualizations/PVSkeletonVisualizerComponent.h:23) — bases: `UPVLineBatchComponent`.
- `UPaperFlipbookComponent` (Engine/Plugins/2D/Paper2D/Source/Paper2D/Classes/PaperFlipbookComponent.h:24) — bases: `UMeshComponent`.
- `UPaperGroupedSpriteComponent` (Engine/Plugins/2D/Paper2D/Source/Paper2D/Classes/PaperGroupedSpriteComponent.h:58) — bases: `UMeshComponent`.
- `UPaperSpriteComponent` (Engine/Plugins/2D/Paper2D/Source/Paper2D/Classes/PaperSpriteComponent.h:29) — bases: `UMeshComponent`.
- `UPaperTerrainComponent` (Engine/Plugins/2D/Paper2D/Source/Paper2D/Classes/PaperTerrainComponent.h:54) — bases: `UPrimitiveComponent`.
- `UPaperTerrainSplineComponent` (Engine/Plugins/2D/Paper2D/Source/Paper2D/Classes/PaperTerrainSplineComponent.h:12) — bases: `USplineComponent`.
- `UPaperTileMapComponent` (Engine/Plugins/2D/Paper2D/Source/Paper2D/Classes/PaperTileMapComponent.h:38) — bases: `UMeshComponent`.
- `UParticleSystemComponent` (Engine/Source/Runtime/Engine/Classes/Particles/ParticleSystemComponent.h:491) — bases: `UFXSystemComponent`.
- `UPointSetComponent` (Engine/Plugins/Runtime/MeshModelingToolset/Source/ModelingComponents/Public/Drawing/PointSetComponent.h:50) — bases: `UMeshComponent`.
- `UPoseSearchMeshComponent` (Engine/Plugins/Animation/PoseSearch/Source/Editor/Private/PoseSearchMeshComponent.h:9) — bases: `UPoseableMeshComponent`.
- `UPoseableMeshComponent` (Engine/Source/Runtime/Engine/Classes/Components/PoseableMeshComponent.h:17) — bases: `USkinnedMeshComponent`.
- `UPrimitiveComponent` (Engine/Source/Runtime/Engine/Classes/Components/PrimitiveComponent.h:307) — bases: `USceneComponent`, `INavRelevantInterface`, `IInterface_AsyncCompilation`, `IPhysicsComponent`, `FRenderAssetOwnerStreamingState`, `IPhysicsBodyInstanceOwner`, `IPhysicsBodyInstanceOwnerResolver`.
- `UProceduralMeshComponent` (Engine/Plugins/Runtime/ProceduralMeshComponent/Source/ProceduralMeshComponent/Public/ProceduralMeshComponent.h:149) — bases: `UMeshComponent`, `IInterface_CollisionDataProvider`.
- `URemoveInstancesModifierVolumeComponent` (Engine/Plugins/Runtime/InstancedActors/Source/InstancedActors/Public/InstancedActorsModifierVolumeComponent.h:138) — bases: `UInstancedActorsModifierVolumeComponent`.
- `USVGBaseDynamicMeshComponent` (Engine/Plugins/VirtualProduction/SVGImporter/Source/SVGImporter/Public/ProceduralMeshes/SVGBaseDynamicMeshComponent.h:9) — bases: `UDynamicMeshComponent`.
- `USVGDynamicMeshComponent` (Engine/Plugins/VirtualProduction/SVGImporter/Source/SVGImporter/Public/ProceduralMeshes/SVGDynamicMeshComponent.h:46) — bases: `USVGBaseDynamicMeshComponent`.
- `USVGFillComponent` (Engine/Plugins/VirtualProduction/SVGImporter/Source/SVGImporter/Private/ProceduralMeshes/SVGFillComponent.h:65) — bases: `USVGDynamicMeshComponent`.
- `USVGStrokeComponent` (Engine/Plugins/VirtualProduction/SVGImporter/Source/SVGImporter/Private/ProceduralMeshes/SVGStrokeComponent.h:35) — bases: `USVGDynamicMeshComponent`.
- `UShallowWaterRiverComponent` (Engine/Plugins/Experimental/WaterAdvanced/Source/WaterAdvanced/Public/ShallowWaterRiverActor.h:38) — bases: `UPrimitiveComponent`.
- `UShapeComponent` (Engine/Source/Runtime/Engine/Classes/Components/ShapeComponent.h:24) — bases: `UPrimitiveComponent`.
- `USkeletalGeneratorComponent` (Engine/Plugins/Animation/MLDeformer/ChaosFleshGenerator/Source/ChaosFleshGenerator/Private/FleshGeneratorComponent.h:40) — bases: `USkeletalMeshComponent`.
- `USkeletalMeshBackedDynamicMeshComponent` (Engine/Plugins/Animation/SkeletalMeshModelingTools/Source/SkeletalMeshModelingTools/Private/Components/SKMBackedDynaMeshComponent.h:29) — bases: `UDynamicMeshComponent`.
- `USkeletalMeshComponent` (Engine/Source/Runtime/Engine/Classes/Components/SkeletalMeshComponent.h:341) — bases: `USkinnedMeshComponent`, `IInterface_CollisionDataProvider`.
- `USkeletalMeshComponentBudgeted` (Engine/Plugins/Runtime/AnimationBudgetAllocator/Source/AnimationBudgetAllocator/Public/SkeletalMeshComponentBudgeted.h:23) — bases: `USkeletalMeshComponent`.
- `USkinnedMeshComponent` (Engine/Source/Runtime/Engine/Classes/Components/SkinnedMeshComponent.h:267) — bases: `UMeshComponent`, `ILODSyncInterface`, `IClothSimulationDataProvider`.
- `USmartObjectContainerRenderingComponent` (Engine/Plugins/Runtime/SmartObjects/Source/SmartObjectsModule/Public/SmartObjectContainerRenderingComponent.h:14) — bases: `UPrimitiveComponent`.
- `USmartObjectDebugRenderingComponent` (Engine/Plugins/Runtime/SmartObjects/Source/SmartObjectsModule/Public/SmartObjectDebugRenderingComponent.h:17) — bases: `UDebugDrawComponent`.
- `USmartObjectRenderingComponent` (Engine/Plugins/Runtime/SmartObjects/Source/SmartObjectsModule/Public/SmartObjectRenderingComponent.h:14) — bases: `UPrimitiveComponent`.
- `USmartObjectSubsystemRenderingComponent` (Engine/Plugins/Runtime/SmartObjects/Source/SmartObjectsModule/Private/SmartObjectSubsystemRenderingActor.h:12) — bases: `USmartObjectDebugRenderingComponent`.
- `USmartObjectTestRenderingComponent` (Engine/Plugins/Runtime/SmartObjects/Source/SmartObjectsModule/Private/SmartObjectTestingActor.h:115) — bases: `USmartObjectDebugRenderingComponent`.
- `USmartObjectZoneAnnotations` (Engine/Plugins/Runtime/MassGameplay/Source/MassSmartObjects/Public/SmartObjectZoneAnnotations.h:95) — bases: `UZoneGraphAnnotationComponent`.
- `USparseVolumeTextureViewerComponent` (Engine/Source/Runtime/Renderer/Private/SparseVolumeTexture/SparseVolumeTextureViewerComponent.h:35) — bases: `UPrimitiveComponent`.
- `USphereComponent` (Engine/Source/Runtime/Engine/Classes/Components/SphereComponent.h:17) — bases: `UShapeComponent`.
- `USplineComponent` (Engine/Source/Runtime/Engine/Classes/Components/SplineComponent.h:214) — bases: `UPrimitiveComponent`.
- `USplineMeshComponent` (Engine/Source/Runtime/Engine/Classes/Components/SplineMeshComponent.h:119) — bases: `UStaticMeshComponent`, `IInterface_CollisionDataProvider`.
- `UStaticMeshComponent` (Engine/Source/Runtime/Engine/Classes/Components/StaticMeshComponent.h:105) — bases: `UMeshComponent`.
- `UStereoStaticMeshComponent` (Engine/Plugins/Experimental/PanoramicCapture/Source/PanoramicCapture/Private/StereoStaticMeshComponent.h:23) — bases: `UStaticMeshComponent`.
- `UTextRenderComponent` (Engine/Source/Runtime/Engine/Classes/Components/TextRenderComponent.h:44) — bases: `UPrimitiveComponent`.
- `UTriangleSetComponent` (Engine/Plugins/Runtime/MeshModelingToolset/Source/ModelingComponents/Public/Drawing/TriangleSetComponent.h:87) — bases: `UMeshComponent`.
- `UUsdDrawModeComponent` (Engine/Plugins/Runtime/USDCore/Source/USDClasses/Public/USDDrawModeComponent.h:61) — bases: `UPrimitiveComponent`.
- `UVectorFieldComponent` (Engine/Source/Runtime/Engine/Classes/Components/VectorFieldComponent.h:18) — bases: `UPrimitiveComponent`.
- `UVehicleSimAerofoilComponent` (Engine/Plugins/Experimental/ChaosModularVehicle/Source/ChaosModularVehicleEngine/Public/ChaosModularVehicle/VehicleSimAerofoilComponent.h:24) — bases: `UVehicleSimBaseComponent`.
- `UVehicleSimBaseComponent` (Engine/Plugins/Experimental/ChaosModularVehicle/Source/ChaosModularVehicleEngine/Public/ChaosModularVehicle/VehicleSimBaseComponent.h:73) — bases: `UPrimitiveComponent`, `IVehicleSimBaseComponentInterface`.
- `UVehicleSimChassisComponent` (Engine/Plugins/Experimental/ChaosModularVehicle/Source/ChaosModularVehicleEngine/Public/ChaosModularVehicle/VehicleSimChassisComponent.h:14) — bases: `UVehicleSimBaseComponent`.
- `UVehicleSimClutchComponent` (Engine/Plugins/Experimental/ChaosModularVehicle/Source/ChaosModularVehicleEngine/Public/ChaosModularVehicle/VehicleSimClutchComponent.h:14) — bases: `UVehicleSimBaseComponent`.
- `UVehicleSimEngineComponent` (Engine/Plugins/Experimental/ChaosModularVehicle/Source/ChaosModularVehicleEngine/Public/ChaosModularVehicle/VehicleSimEngineComponent.h:15) — bases: `UVehicleSimBaseComponent`.
- `UVehicleSimSuspensionComponent` (Engine/Plugins/Experimental/ChaosModularVehicle/Source/ChaosModularVehicleEngine/Public/ChaosModularVehicle/VehicleSimSuspensionComponent.h:14) — bases: `UVehicleSimBaseComponent`.
- `UVehicleSimThrusterComponent` (Engine/Plugins/Experimental/ChaosModularVehicle/Source/ChaosModularVehicleEngine/Public/ChaosModularVehicle/VehicleSimThrusterComponent.h:14) — bases: `UVehicleSimBaseComponent`.
- `UVehicleSimTransmissionComponent` (Engine/Plugins/Experimental/ChaosModularVehicle/Source/ChaosModularVehicleEngine/Public/ChaosModularVehicle/VehicleSimTransmissionComponent.h:22) — bases: `UVehicleSimBaseComponent`.
- `UVehicleSimWheelComponent` (Engine/Plugins/Experimental/ChaosModularVehicle/Source/ChaosModularVehicleEngine/Public/ChaosModularVehicle/VehicleSimWheelComponent.h:21) — bases: `UVehicleSimBaseComponent`.
- `UViewAdjustedStaticMeshGizmoComponent` (Engine/Source/Runtime/InteractiveToolsFramework/Public/BaseGizmos/ViewAdjustedStaticMeshGizmoComponent.h:24) — bases: `UStaticMeshComponent`, `IGizmoBaseComponentInterface`.
- `UVirtualHeightfieldMeshComponent` (Engine/Plugins/Experimental/VirtualHeightfieldMesh/Source/VirtualHeightfieldMesh/Public/VirtualHeightfieldMeshComponent.h:18) — bases: `UPrimitiveComponent`.
- `UWaterBodyComponent` (Engine/Plugins/Experimental/Water/Source/Runtime/Public/WaterBodyComponent.h:113) — bases: `UPrimitiveComponent`.
- `UWaterBodyCustomComponent` (Engine/Plugins/Experimental/Water/Source/Runtime/Public/WaterBodyCustomComponent.h:15) — bases: `UWaterBodyComponent`.
- `UWaterBodyInfoMeshComponent` (Engine/Plugins/Experimental/Water/Source/Runtime/Private/WaterBodyInfoMeshComponent.h:17) — bases: `UWaterBodyMeshComponent`.
- `UWaterBodyLakeComponent` (Engine/Plugins/Experimental/Water/Source/Runtime/Public/WaterBodyLakeComponent.h:17) — bases: `UWaterBodyComponent`.
- `UWaterBodyMeshComponent` (Engine/Plugins/Experimental/Water/Source/Runtime/Public/WaterBodyMeshComponent.h:19) — bases: `UStaticMeshComponent`.
- `UWaterBodyOceanComponent` (Engine/Plugins/Experimental/Water/Source/Runtime/Public/WaterBodyOceanComponent.h:16) — bases: `UWaterBodyComponent`.
- `UWaterBodyRiverComponent` (Engine/Plugins/Experimental/Water/Source/Runtime/Public/WaterBodyRiverComponent.h:16) — bases: `UWaterBodyComponent`.
- `UWaterBodyStaticMeshComponent` (Engine/Plugins/Experimental/Water/Source/Runtime/Private/WaterBodyStaticMeshComponent.h:18) — bases: `UWaterBodyMeshComponent`.
- `UWaterMeshComponent` (Engine/Plugins/Experimental/Water/Source/Runtime/Public/WaterMeshComponent.h:19) — bases: `UMeshComponent`.
- `UWaterSplineComponent` (Engine/Plugins/Experimental/Water/Source/Runtime/Public/WaterSplineComponent.h:27) — bases: `USplineComponent`.
- `UWidgetComponent` (Engine/Source/Runtime/UMG/Public/Components/WidgetComponent.h:95) — bases: `UMeshComponent`.
- `UXRCreativeGizmoMeshComponent` (Engine/Plugins/Experimental/XRCreativeFramework/Source/XRCreative/Public/XRCreativeGizmos.h:181) — bases: `UStaticMeshComponent`.
- `UXRCreativeITFRenderComponent` (Engine/Plugins/Experimental/XRCreativeFramework/Source/XRCreative/Private/ITF/XRCreativeITFRenderComponent.h:28) — bases: `UPrimitiveComponent`.
- `UXRDeviceVisualizationComponent` (Engine/Plugins/Runtime/XRBase/Source/XRBase/Public/XRDeviceVisualizationComponent.h:18) — bases: `UStaticMeshComponent`.
- `UZoneGraphAnnotationComponent` (Engine/Plugins/Runtime/ZoneGraphAnnotations/Source/ZoneGraphAnnotations/Public/ZoneGraphAnnotationComponent.h:38) — bases: `UDebugDrawComponent`.
- `UZoneGraphAnnotationTestingComponent` (Engine/Plugins/Runtime/ZoneGraphAnnotations/Source/ZoneGraphAnnotations/Public/ZoneGraphAnnotationTestingActor.h:43) — bases: `UDebugDrawComponent`.
- `UZoneGraphCrowdLaneAnnotations` (Engine/Plugins/AI/MassCrowd/Source/MassCrowd/Public/ZoneGraphCrowdLaneAnnotations.h:36) — bases: `UZoneGraphAnnotationComponent`.
- `UZoneGraphDisturbanceAnnotation` (Engine/Plugins/Runtime/ZoneGraphAnnotations/Source/ZoneGraphAnnotations/Public/Annotations/ZoneGraphDisturbanceAnnotation.h:154) — bases: `UZoneGraphAnnotationComponent`.
- `UZoneGraphRenderingComponent` (Engine/Plugins/Runtime/ZoneGraph/Source/ZoneGraph/Public/ZoneGraphRenderingComponent.h:62) — bases: `UPrimitiveComponent`.
- `UZoneGraphTestingComponent` (Engine/Plugins/Runtime/ZoneGraph/Source/ZoneGraphDebug/Public/ZoneGraphTestingActor.h:41) — bases: `UPrimitiveComponent`.
- `UZoneShapeComponent` (Engine/Plugins/Runtime/ZoneGraph/Source/ZoneGraph/Public/ZoneShapeComponent.h:39) — bases: `UPrimitiveComponent`.
- `ViewAdjustedStaticMeshGizmoComponentLocals::FViewAdjustedStaticMeshGizmoComponentProxy` (Engine/Source/Runtime/InteractiveToolsFramework/Private/BaseGizmos/ViewAdjustedStaticMeshGizmoComponent.cpp:43) — bases: `FStaticMeshSceneProxy`.

## Shared GPU storage and CPU boundaries

### GPU Scene records · FGPUSceneResourceParameters

GPU contents: StructuredBuffer<float4> primitive, instance, instance-payload and lightmap records; ByteAddressBuffer light data. Shader indices connect the records.

CPU role / conditions: FGPUScene manages allocation and uploads; FPrimitiveSceneProxy and FPrimitiveSceneInfo remain CPU objects. Only eligible primitives/instances participate, and updates are incremental.

Source: `Engine/Source/Runtime/Renderer/Private/GPUScene.h:60` — `GPUScenePrimitiveSceneData`.

### Draw resources · FVertexBuffer / FIndexBuffer / FVertexFactory / FMeshBatch

GPU contents: Vertex/index allocations and SRVs, shader uniform data, textures and optional indirect argument buffers feed a draw or dispatch.

CPU role / conditions: The C++ wrappers, vertex-factory object, FMeshBatch and FMeshDrawCommand describe/bind GPU work on the CPU. A mesh batch is not itself a GPU allocation.

Source: `Engine/Source/Runtime/Engine/Public/MeshBatch.h:98` — `struct FMeshBatch`.

### RHI storage · FRHIBuffer / FRHITexture

GPU contents: Buffers (vertex, index, structured, byte-address, uniform, indirect) and textures (2D, arrays, 3D, cube, render/depth targets) hold bytes and texels. SRV/UAV views interpret existing storage.

CPU role / conditions: FRHI resources are CPU handles to device resources; FRDGBuffer/FRDGTexture describe render-graph use and lifetimes. Views and RDG wrappers do not imply independent copies of data.

Source: `Engine/Source/Runtime/RHI/Public/RHIResources.h:1627` — `class FRHIBuffer`.

### Optional derived renderer representations

GPU contents: Ray-tracing BLAS/TLAS, distance-field volumes, Lumen card/surface-cache textures, Nanite visibility buffers and virtual shadow-map pages can represent or be affected by a primitive.

CPU role / conditions: These are feature-dependent shared caches, acceleration structures or per-view outputs. They are not subclasses of the component/proxy, nor allocations owned by every instance. See the pipeline for build/update/consume stages.

Source: `Engine/Source/Runtime/Renderer/Private/ScenePrivate.h:131` — `class FScene`.

## Class-specific storage associations

### Primitive component contract (component)

Mapped definitions: `UPrimitiveComponent` (Engine/Source/Runtime/Engine/Classes/Components/PrimitiveComponent.h:307).

Game-thread scene component with primitive bounds, transform, visibility, collision and render-state hooks. CreateSceneProxy can create a render-thread representation or return no proxy.

UPrimitiveComponent inherits USceneComponent and several interfaces/mixins shown below. The tree is deliberately rooted here. Neither the UObject nor the proxy is copied wholesale into a GPU buffer.

Related implementations (association only): `FPrimitiveSceneProxy`.

### Primitive scene proxy contract (proxy)

Mapped definitions: `FPrimitiveSceneProxy` (Engine/Source/Runtime/Engine/Public/PrimitiveSceneProxy.h:291).

Render-thread representation of a primitive: exposes relevance, bounds, materials and drawing/instance integration to the scene renderer.

A subclass can draw meshes, use a specialized rendering path, draw debug geometry, or draw nothing under the active settings. Its C++ inheritance alone does not specify a GPU memory layout.

Related implementations (association only): `UPrimitiveComponent`.

### Mesh component contract (mesh-base)

Mapped definitions: `UMeshComponent` (Engine/Source/Runtime/Engine/Classes/Components/MeshComponent.h:24).

Adds mesh material interfaces and overrides to the primitive component contract; concrete subclasses choose geometry and proxy implementations.

UMeshComponent does not prescribe a single vertex format or allocate a universal mesh buffer.

### Effects component contract (fx-base)

Mapped definitions: `UFXSystemComponent` (Engine/Source/Runtime/Engine/Classes/Particles/ParticleSystemComponent.h:379).

Common component interface for effects systems such as Cascade and Niagara.

This is a direct UPrimitiveComponent subclass, not a UMeshComponent subclass. Emitter simulation and renderer choices determine storage.

### Static mesh triangles (static)

Mapped definitions: `UStaticMeshComponent` (Engine/Source/Runtime/Engine/Classes/Components/StaticMeshComponent.h:105), `FStaticMeshSceneProxy` (Engine/Source/Runtime/Engine/Public/StaticMeshSceneProxy.h:34).

Renders a static mesh asset using section/material/LOD data. Conventional rendering reads shared mesh streams; eligible components can instead select a Nanite proxy.

Materials may add textures, uniform data and vertex deformation. Collision BodySetup is not the rendered triangle allocation. Nanite and fallback representations are alternatives, not inherited classes of FStaticMeshSceneProxy.

Related implementations (association only): `FStaticMeshSceneProxy`, `Nanite::FSceneProxy`.

### FStaticMeshLODResources / FStaticMeshVertexBuffers

GPU contents: Position, tangent/normal and UV streams, optional vertex colors; primary and depth-only index buffers plus optional reversed/wireframe indices. Section ranges select triangles.

CPU role / conditions: LOD and section metadata and FPositionVertexBuffer/FStaticMeshVertexBuffer/FColorVertexBuffer/FRawStaticIndexBuffer wrappers live on the CPU and initialize/bind the actual GPU buffers.

Source: `Engine/Source/Runtime/Engine/Public/StaticMeshResources.h:247` — `struct FStaticMeshLODResources`.

### Instanced static meshes & foliage (instances)

Mapped definitions: `UInstancedStaticMeshComponent` (Engine/Source/Runtime/Engine/Classes/Components/InstancedStaticMeshComponent.h:158), `UHierarchicalInstancedStaticMeshComponent` (Engine/Source/Runtime/Engine/Classes/Components/HierarchicalInstancedStaticMeshComponent.h:135), `FInstancedStaticMeshSceneProxy` (Engine/Source/Runtime/Engine/Classes/Engine/InstancedStaticMesh.h:435), `FHierarchicalStaticMeshSceneProxy` (Engine/Source/Runtime/Engine/Public/HierarchicalStaticMeshSceneProxy.h:35).

Reuses one mesh asset for many transformed instances; HISM adds hierarchical instance organization and foliage derives from the HISM component branch.

Instance data is additional to the mesh's geometry. Proxy choice and GPU Scene support depend on feature/platform settings; Nanite uses its instance/cluster path.

Related implementations (association only): `FInstancedStaticMeshSceneProxy`, `FHierarchicalStaticMeshSceneProxy`, `Nanite::FSceneProxy`.

### FInstanceSceneDataBuffers + shared static-mesh streams

GPU contents: GPU Scene instance records/payloads contain transforms, bounds and optional custom data; shared vertex/index buffers describe the mesh. The legacy vertex-factory path can use instance vertex streams.

CPU role / conditions: FInstanceSceneDataBuffers is CPU-side instance storage/upload input. HISM cluster organization is not a separate GPU triangle format.

Source: `Engine/Source/Runtime/Engine/Public/InstanceDataSceneProxy.h:134` — `class FInstanceSceneDataBuffers`.

### Spline-deformed mesh (spline)

Mapped definitions: `USplineMeshComponent` (Engine/Source/Runtime/Engine/Classes/Components/SplineMeshComponent.h:119), `FSplineMeshSceneProxy` (Engine/Source/Runtime/Engine/Public/SplineMeshSceneProxy.h:141), `FNaniteSplineMeshSceneProxy` (Engine/Source/Runtime/Engine/Public/SplineMeshSceneProxy.h:181).

Deforms a source mesh along a spline using spline parameters and the selected conventional or Nanite mesh path.

The function-local FSplineMeshSceneProxy in SplineComponent.cpp is a different debug proxy. Names are disambiguated by source location; only the global mesh proxy has FStaticMeshSceneProxy as a base.

Related implementations (association only): `FSplineMeshSceneProxy`, `FNaniteSplineMeshSceneProxy`.

### FSplineMeshSceneProxyCommon + source mesh resources

GPU contents: Source geometry buffers plus spline deformation parameters used during rendering. Ray-tracing deformation may require updated geometry buffers.

CPU role / conditions: Spline control state and proxy helper/mixin remain CPU objects; changing a spline does not imply a permanent duplicated mesh asset.

Source: `Engine/Source/Runtime/Engine/Public/SplineMeshSceneProxy.h:97` — `FSplineMeshSceneProxyCommon`.

### Skinned / skeletal meshes (skinned)

Mapped definitions: `USkinnedMeshComponent` (Engine/Source/Runtime/Engine/Classes/Components/SkinnedMeshComponent.h:267), `FSkeletalMeshSceneProxy` (Engine/Source/Runtime/Engine/Public/SkeletalMeshSceneProxy.h:21).

Supplies deformable mesh geometry. Skeletal animation, poseable meshes and instanced skinned meshes specialize the component branch; supported assets may choose a Nanite skinned proxy.

CPU skinning, GPU vertex skinning, compute skin cache and Nanite are different paths. Not all optional buffers exist together.

Related implementations (association only): `FSkeletalMeshSceneProxy`, `Nanite::FSkinnedSceneProxy`.

### FSkeletalMeshLODRenderData

GPU contents: Static position/tangent/UV/color streams, indices, FSkinWeightVertexBuffer bone indices and weights, optional cloth and morph data. Bone transforms drive skinning; GPU Skin Cache can output deformed positions/tangents reused by later passes.

CPU role / conditions: LOD/section metadata and FSkeletalMeshObject rendering state are CPU objects. Skinning paths and precision/weight layouts vary; current/previous deformation data supports velocity.

Source: `Engine/Source/Runtime/Engine/Public/Rendering/SkeletalMeshLODRenderData.h:152` — `SkinWeightVertexBuffer;`.

### Instanced skinned meshes (instanced-skinned)

Mapped definitions: `UInstancedSkinnedMeshComponent` (Engine/Source/Runtime/Engine/Classes/Components/InstancedSkinnedMeshComponent.h:58), `FInstancedSkinnedMeshSceneProxy` (Engine/Source/Runtime/Engine/Private/InstancedSkinnedMeshSceneProxy.h:86), `FNaniteInstancedSkinnedMeshSceneProxy` (Engine/Source/Runtime/Engine/Private/InstancedSkinnedMeshSceneProxy.h:30).

Combines skinned mesh rendering with per-instance state and chooses the conventional or Nanite skinned proxy implementation.

Do not infer UInstancedStaticMeshComponent ancestry from the word 'instanced'.

Related implementations (association only): `FInstancedSkinnedMeshSceneProxy`, `FNaniteInstancedSkinnedMeshSceneProxy`.

### Instanced skinned proxy + skeletal/Nanite data

GPU contents: Shared deformable geometry and skinning resources plus instance scene records. The selected path determines conventional streams/skin outputs or Nanite cluster data.

CPU role / conditions: The component is a USkinnedMeshComponent subclass. FInstancedSkinnedMeshSceneProxy derives FSkeletalMeshSceneProxy, while the Nanite counterpart derives Nanite::FSkinnedSceneProxy.

Source: `Engine/Source/Runtime/Engine/Private/InstancedSkinnedMeshSceneProxy.h:86` — `class FInstancedSkinnedMeshSceneProxy`.

### Nanite virtualized geometry (nanite)

Mapped definitions: `Nanite::FSceneProxyBase` (Engine/Source/Runtime/Engine/Public/NaniteSceneProxy.h:219), `Nanite::FSceneProxy` (Engine/Source/Runtime/Engine/Public/NaniteSceneProxy.h:490), `Nanite::FSkinnedSceneProxy` (Engine/Source/Runtime/Engine/Public/NaniteSceneProxy.h:757), `ULandscapeNaniteComponent` (Engine/Source/Runtime/Landscape/Classes/LandscapeNaniteComponent.h:82).

Connects supported geometry to Nanite's streamed cluster representation. Static and skinned scene proxies are siblings beneath Nanite::FSceneProxyBase.

Nanite::FSkinnedSceneProxy is not derived from Nanite::FSceneProxy. Deformation resources extend the skinned path. Conventional fallback meshes and ray-tracing representations have their own storage; the component may select a different proxy when Nanite is unavailable.

Related implementations (association only): `Nanite::FSceneProxyBase`, `Nanite::FSceneProxy`, `Nanite::FSkinnedSceneProxy`.

### Nanite::FResources / Nanite streaming

GPU contents: Packed geometry cluster pages, hierarchy nodes and streaming metadata resident in GPU buffers; GPU Scene supplies primitive/instance transforms. Culling generates visible clusters and raster/shading work.

CPU role / conditions: FResources holds cooked asset/streaming data and resource identity. The streaming manager manages resident GPU allocations. Visibility buffers, candidate queues and shading bins are renderer work/output storage.

Source: `Engine/Source/Runtime/Engine/Public/Rendering/NaniteResources.h:452` — `struct FResources`.

### Landscape heightfield (landscape)

Mapped definitions: `ULandscapeComponent` (Engine/Source/Runtime/Landscape/Classes/LandscapeComponent.h:431), `FLandscapeComponentSceneProxy` (Engine/Source/Runtime/Landscape/Public/LandscapeRender.h:683).

Represents terrain sections using shared grid geometry and sampled landscape height/normal/weight textures.

FLandscapeNaniteSceneProxy derives Nanite::FSceneProxy; FLandscapeMeshProxySceneProxy derives FStaticMeshSceneProxy. Those are separate definition branches, not children of FLandscapeComponentSceneProxy.

Related implementations (association only): `FLandscapeComponentSceneProxy`, `FLandscapeNaniteSceneProxy`, `FLandscapeMeshProxySceneProxy`.

### FLandscapeSharedBuffers / landscape textures

GPU contents: Shared grid vertex/index buffers and LOD-dependent indices; heightmap textures encode height/normal information, weightmaps select/blend layers, and visibility weights mask holes.

CPU role / conditions: FLandscapeComponentSceneProxy and FLandscapeSectionInfo organize sections/LOD. FLandscapeSharedBuffers owns render-resource wrappers, not a UObject per GPU vertex.

Source: `Engine/Source/Runtime/Landscape/Public/LandscapeRender.h:343` — `class FLandscapeSharedBuffers`.

### Geometry Collection fracture pieces (collection)

Mapped definitions: `UGeometryCollectionComponent` (Engine/Source/Runtime/Experimental/GeometryCollectionEngine/Public/GeometryCollection/GeometryCollectionComponent.h:577), `FGeometryCollectionSceneProxy` (Engine/Source/Runtime/Experimental/GeometryCollectionEngine/Private/GeometryCollection/GeometryCollectionSceneProxy.h:273), `FNaniteGeometryCollectionSceneProxy` (Engine/Source/Runtime/Experimental/GeometryCollectionEngine/Private/GeometryCollection/GeometryCollectionSceneProxy.h:343).

Renders rigid fracture pieces using collection geometry and per-piece transforms; conventional and Nanite proxy implementations share a collection helper base.

The shared collection helper is a secondary C++ base. The Nanite proxy's primitive ancestry is through Nanite::FSceneProxyBase.

Related implementations (association only): `FGeometryCollectionSceneProxy`, `FNaniteGeometryCollectionSceneProxy`.

### FGeometryCollectionTransformBuffer / collection geometry

GPU contents: Current/previous piece transform buffers accompany conventional mesh geometry or Nanite clusters. Transform updates move fracture pieces without rebuilding all source triangles.

CPU role / conditions: Collection physics/hierarchy data and FGeometryCollectionSceneProxyBase remain CPU state. Only required transforms and rendering data are uploaded.

Source: `Engine/Source/Runtime/Experimental/GeometryCollectionEngine/Private/GeometryCollection/GeometryCollectionSceneProxy.h:33` — `class FGeometryCollectionTransformBuffer`.

### Geometry Cache animated vertices (cache)

Mapped definitions: `UGeometryCacheComponent` (Engine/Plugins/Runtime/GeometryCache/Source/GeometryCache/Classes/GeometryCacheComponent.h:37), `FGeometryCacheSceneProxy` (Engine/Plugins/Runtime/GeometryCache/Source/GeometryCache/Public/GeometryCacheSceneProxy.h:287).

Streams or decodes time-sampled mesh data rather than applying a skeleton to one rest mesh.

Geometry Cache is not the skin-weight/bone-transform representation of a skeletal mesh.

Related implementations (association only): `FGeometryCacheSceneProxy`.

### FGeometryCacheSceneProxy render data

GPU contents: Position buffers (including a pair for sample/history handling), tangent, texture-coordinate, color and index buffers provide the current animated mesh.

CPU role / conditions: Track/sample decoding and proxy metadata prepare uploads. GeometryCache subclasses can use different source formats such as Alembic or USD.

Source: `Engine/Plugins/Runtime/GeometryCache/Source/GeometryCache/Public/GeometryCacheSceneProxy.h:251` — `PositionBuffers[2]`.

### Editable dynamic mesh (dynamic)

Mapped definitions: `UBaseDynamicMeshComponent` (Engine/Source/Runtime/GeometryFramework/Public/Components/BaseDynamicMeshComponent.h:124), `FBaseDynamicMeshSceneProxy` (Engine/Source/Runtime/GeometryFramework/Public/Components/BaseDynamicMeshSceneProxy.h:38).

Converts editable mesh topology into render-buffer sets and updates changed geometry or subsets.

Subclasses can split geometry into chunks or octree-based render sets; this does not change their declared component/proxy ancestry.

Related implementations (association only): `FBaseDynamicMeshSceneProxy`, `FDynamicMeshSceneProxy`.

### FMeshRenderBufferSet

GPU contents: Position, static-mesh tangent/UV and color buffers plus FDynamicMeshIndexBuffer32 primary/secondary triangle indices; optional ray-tracing geometry uses these streams.

CPU role / conditions: UDynamicMesh / FDynamicMesh3 store editable CPU topology. FMeshRenderBufferSet manages GPU stream wrappers and upload operations; the topology object itself is not a GPU mesh.

Source: `Engine/Source/Runtime/GeometryFramework/Public/Components/MeshRenderBufferSet.h:38` — `class FMeshRenderBufferSet`.

### Procedural triangle sections (procedural)

Mapped definitions: `UProceduralMeshComponent` (Engine/Plugins/Runtime/ProceduralMeshComponent/Source/ProceduralMeshComponent/Public/ProceduralMeshComponent.h:149), `FProceduralMeshSceneProxy` (Engine/Plugins/Runtime/ProceduralMeshComponent/Source/ProceduralMeshComponent/Private/ProceduralMeshComponent.cpp:92).

Builds per-section triangle render resources from supplied procedural vertices and indices.

Procedural describes how geometry is produced, not a new hardware primitive topology.

Related implementations (association only): `FProceduralMeshSceneProxy`.

### FProcMeshProxySection

GPU contents: FStaticMeshVertexBuffers, FDynamicMeshIndexBuffer32 and a local vertex factory feed section draws; optional ray-tracing geometry references the position/index buffers.

CPU role / conditions: CPU section arrays and collision data are distinct from the uploaded render streams.

Source: `Engine/Plugins/Runtime/ProceduralMeshComponent/Source/ProceduralMeshComponent/Private/ProceduralMeshComponent.cpp:40` — `class FProcMeshProxySection`.

### Cable tube geometry (cable)

Mapped definitions: `UCableComponent` (Engine/Plugins/Runtime/CableComponent/Source/CableComponent/Classes/CableComponent.h:31), `FCableSceneProxy` (Engine/Plugins/Runtime/CableComponent/Source/CableComponent/Private/CableComponent.cpp:84).

Builds a tube mesh around simulated cable points and updates it as the cable moves.

There is no dedicated RHI 'cable' topology.

Related implementations (association only): `FCableSceneProxy`.

### FCableSceneProxy / FCableIndexBuffer

GPU contents: Generated tube vertex streams and triangle indices describe the cable surface.

CPU role / conditions: Cable simulation particles and constraints are CPU component state; the rendered surface uses ordinary mesh buffers.

Source: `Engine/Plugins/Runtime/CableComponent/Source/CableComponent/Private/CableComponent.cpp:58` — `class FCableIndexBuffer`.

### Niagara simulation & renderer data (niagara)

Mapped definitions: `UNiagaraComponent` (Engine/Plugins/FX/Niagara/Source/Niagara/Public/NiagaraComponent.h:57), `FNiagaraSceneProxy` (Engine/Plugins/FX/Niagara/Source/Niagara/Public/NiagaraSceneProxy.h:34).

Connects a Niagara system to scene rendering. Sprite, mesh, ribbon, volume, light, decal and component renderers interpret emitter data differently.

Niagara renderer classes are not subclasses of FNiagaraSceneProxy. Light/component/decal renderers do not all produce a triangle mesh or share one attribute layout. NiagaraNanite adds its own static-mesh component and proxies.

Related implementations (association only): `FNiagaraSceneProxy`.

### FNiagaraDataBuffer

GPU contents: GPUBufferFloat, GPUBufferHalf and GPUBufferInt hold separated particle attribute streams; GPUIDToIndexTable maps IDs. Counts, sorting and indirect arguments support GPU work. Mesh renderers also reference mesh geometry; ribbons generate segment geometry.

CPU role / conditions: CPU simulation uses CPU attribute arrays and uploads render data as needed; GPU simulation uses GPU buffers. FNiagaraDataBuffer and FNiagaraSceneProxy are CPU owners/interfaces, not particle structs placed wholesale on the GPU.

Source: `Engine/Plugins/FX/Niagara/Source/Niagara/Classes/NiagaraDataSet.h:230` — `FRWBuffer GPUBufferFloat;`.

### Cascade particle emitters (cascade)

Mapped definitions: `UParticleSystemComponent` (Engine/Source/Runtime/Engine/Classes/Particles/ParticleSystemComponent.h:491), `FParticleSystemSceneProxy` (Engine/Source/Runtime/Engine/Public/ParticleSystemSceneProxy.h:36).

Legacy particle scene integration: emitter render data supplies sprites, meshes and beam/trail geometry, with CPU or GPU simulation according to emitter type.

Cascade and Niagara are separate UFXSystemComponent subclasses. A scene primitive can represent many particles.

Related implementations (association only): `FParticleSystemSceneProxy`.

### FParticleSystemSceneProxy / emitter render data

GPU contents: Dynamic particle streams, referenced mesh buffers and emitter-specific GPU simulation resources provide the selected renderer's data.

CPU role / conditions: Emitter payload and proxy objects are CPU state; simulation mode determines which data is uploaded or generated on the GPU.

Source: `Engine/Source/Runtime/Engine/Public/ParticleSystemSceneProxy.h:36` — `class FParticleSystemSceneProxy`.

### Groom strands, cards & mesh LODs (hair)

Mapped definitions: `UGroomComponent` (Engine/Plugins/Runtime/HairStrands/Source/HairStrandsCore/Public/GroomComponent.h:29), `FHairStrandsSceneProxy` (Engine/Plugins/Runtime/HairStrands/Source/HairStrandsCore/Private/GroomComponent.cpp:447), `Nanite::FGroomSceneProxy` (Engine/Plugins/Runtime/HairStrands/Source/HairStrandsCore/Private/NaniteGroomAsset.h:66).

Represents hair as strands or selected card/mesh LODs with deformation, interpolation and culling resources; a Nanite groom proxy is a separate supported path.

The active representation determines which buffers exist. Nanite::FGroomSceneProxy derives Nanite::FSceneProxyBase, not FHairStrandsSceneProxy.

Related implementations (association only): `FHairStrandsSceneProxy`, `Nanite::FGroomSceneProxy`.

### FHairStrandsRestResource / FHairStrandsDeformedResource

GPU contents: Rest/current/previous strand point positions, curve/point attributes, interpolation/root-binding data and culling buffers; card/mesh LODs use position/index/UV/material streams and textures.

CPU role / conditions: Groom asset and resource wrappers organize GPU allocations. Hair voxelization and visibility data are renderer-generated resources, not a CPU groom UObject copied to VRAM.

Source: `Engine/Plugins/Runtime/HairStrands/Source/HairStrandsCore/Public/GroomResources.h:137` — `struct FHairStrandsRestResource`.

### Sparse volume / heterogeneous volume (volume)

Mapped definitions: `UHeterogeneousVolumeComponent` (Engine/Source/Runtime/Engine/Classes/Components/HeterogeneousVolumeComponent.h:20), `FHeterogeneousVolumeSceneProxy` (Engine/Source/Runtime/Engine/Private/Components/HeterogeneousVolumeComponent.cpp:32), `USparseVolumeTextureViewerComponent` (Engine/Source/Runtime/Renderer/Private/SparseVolumeTexture/SparseVolumeTextureViewerComponent.h:35), `FSparseVolumeTextureViewerSceneProxy` (Engine/Source/Runtime/Renderer/Private/SparseVolumeTexture/SparseVolumeTextureViewerSceneProxy.h:21).

Supplies a bounded participating medium to volumetric rendering, often from animated sparse volume textures; the viewer proxy provides a separate inspection path.

A bounding primitive need not be a visible triangle surface; ray marching consumes a volume representation. Material/source choices can change the volume data.

Related implementations (association only): `FHeterogeneousVolumeSceneProxy`, `FSparseVolumeTextureViewerSceneProxy`.

### UE::SVT::FTextureRenderResources

GPU contents: Page-table texture and PhysicalTileDataA / PhysicalTileDataB textures store sparse indirection and physical voxel attributes. Volume shaders sample these to integrate scattering/extinction/emission.

CPU role / conditions: SparseVolumeTextureData CPU pages/tiles and streaming state prepare texture uploads. FTextureRenderResources contains RHI texture references, not the voxel data inline in a scene proxy.

Source: `Engine/Source/Runtime/Engine/Classes/SparseVolumeTexture/SparseVolumeTexture.h:260` — `class FTextureRenderResources`.

### Water mesh tiles (water)

Mapped definitions: `UWaterMeshComponent` (Engine/Plugins/Experimental/Water/Source/Runtime/Public/WaterMeshComponent.h:19), `FWaterMeshSceneProxy` (Engine/Plugins/Experimental/Water/Source/Runtime/Public/WaterMeshSceneProxy.h:103).

Draws selected water surface tiles/LODs using water mesh resources and per-tile instance data.

UWaterBodyMeshComponent instead derives UStaticMeshComponent and uses the mesh representation. Water material passes are discussed separately in the pipeline.

Related implementations (association only): `FWaterMeshSceneProxy`.

### FWaterMeshSceneProxy

GPU contents: Tile mesh vertex/index data and dynamic per-tile rendering data support the selected water surface; materials sample additional water parameters/textures.

CPU role / conditions: Water quadtree/LOD selection and water-body components organize tiles on the CPU. UWaterBodyComponent is a separate primitive-component branch and is not the water mesh component itself.

Source: `Engine/Plugins/Experimental/Water/Source/Runtime/Public/WaterMeshSceneProxy.h:103` — `class FWaterMeshSceneProxy`.

### Water body controller (water-body)

Mapped definitions: `UWaterBodyComponent` (Engine/Plugins/Experimental/Water/Source/Runtime/Public/WaterBodyComponent.h:113).

Defines a water body's shape and behavior and coordinates its associated water rendering/collision components.

Do not assume every UWaterBodyComponent directly creates FWaterMeshSceneProxy. UWaterMeshComponent and UWaterBodyMeshComponent are separate classes with different declared bases.

Related implementations (association only): `UWaterMeshComponent`, `UWaterBodyMeshComponent`.

### Virtual heightfield mesh (heightfield)

Mapped definitions: `UVirtualHeightfieldMeshComponent` (Engine/Plugins/Experimental/VirtualHeightfieldMesh/Source/VirtualHeightfieldMesh/Public/VirtualHeightfieldMeshComponent.h:18), `FVirtualHeightfieldMeshSceneProxy` (Engine/Plugins/Experimental/VirtualHeightfieldMesh/Source/VirtualHeightfieldMesh/Private/VirtualHeightfieldMeshSceneProxy.h:12).

Renders a heightfield surface driven by runtime virtual texture height data and tile/LOD selection.

This plugin's heightfield representation is distinct from Landscape and Nanite even when they depict similar terrain.

Related implementations (association only): `FVirtualHeightfieldMeshSceneProxy`.

### FVirtualHeightfieldMeshSceneProxy / virtual texture resources

GPU contents: Virtual texture page-table/physical height tiles, height min/max and LOD bias textures accompany patch geometry and selected tile work.

CPU role / conditions: The proxy references IAllocatedVirtualTexture and vertex-factory state; virtual texture storage is managed separately from the component.

Source: `Engine/Plugins/Experimental/VirtualHeightfieldMesh/Source/VirtualHeightfieldMesh/Private/VirtualHeightfieldMeshSceneProxy.h:42` — `HeightMinMaxTexture`.

### LiDAR point cloud (points)

Mapped definitions: `ULidarPointCloudComponent` (Engine/Plugins/Enterprise/LidarPointCloud/Source/LidarPointCloudRuntime/Public/LidarPointCloudComponent.h:24), `FLidarPointCloudSceneProxy` (Engine/Plugins/Enterprise/LidarPointCloud/Source/LidarPointCloudRuntime/Private/Rendering/LidarPointCloudRendering.cpp:197).

Selects and renders visible point-cloud batches with the LiDAR plugin's point rendering resources.

Do not infer PT_PointList just from the asset name: point/splat expansion and shader path determine actual draw topology.

Related implementations (association only): `FLidarPointCloudSceneProxy`.

### LiDAR point-cloud render buffers

GPU contents: Selected point records and point-rendering buffers are uploaded for the configured point/splat display. A point-cloud scene primitive can contain many points.

CPU role / conditions: The point-cloud octree and batch selection are CPU organization; they are not the draw topology enum itself.

Source: `Engine/Plugins/Enterprise/LidarPointCloud/Source/LidarPointCloudRuntime/Private/Rendering/LidarPointCloudRenderBuffers.h:14` — `class FLidarPointCloud`.

### Paper2D sprites, flipbooks & tiles (paper)

Mapped definitions: `UPaperSpriteComponent` (Engine/Plugins/2D/Paper2D/Source/Paper2D/Classes/PaperSpriteComponent.h:29), `UPaperFlipbookComponent` (Engine/Plugins/2D/Paper2D/Source/Paper2D/Classes/PaperFlipbookComponent.h:24), `UPaperGroupedSpriteComponent` (Engine/Plugins/2D/Paper2D/Source/Paper2D/Classes/PaperGroupedSpriteComponent.h:58), `UPaperTileMapComponent` (Engine/Plugins/2D/Paper2D/Source/Paper2D/Classes/PaperTileMapComponent.h:38), `UPaperTerrainComponent` (Engine/Plugins/2D/Paper2D/Source/Paper2D/Classes/PaperTerrainComponent.h:54), `FPaperRenderSceneProxy` (Engine/Plugins/2D/Paper2D/Source/Paper2D/Private/PaperRenderSceneProxy.h:124).

Converts sprite, flipbook, grouped-sprite, tile-map or 2D terrain render data into textured geometry.

A sprite scene proxy is not a special GPU UObject or hardware sprite topology.

Related implementations (association only): `FPaperRenderSceneProxy`.

### FPaperRenderSceneProxy

GPU contents: Sprite/tile vertex and triangle index data plus atlas/material textures supply batched 2D draws in the 3D scene.

CPU role / conditions: Sprite assets, animation frames and tile layers choose the render data on the CPU. Sprite proxy subclasses are real inheritance; sprite textures are associations.

Source: `Engine/Plugins/2D/Paper2D/Source/Paper2D/Private/PaperRenderSceneProxy.h:124` — `class FPaperRenderSceneProxy`.

### World-space widget surface (widget)

Mapped definitions: `UWidgetComponent` (Engine/Source/Runtime/UMG/Public/Components/WidgetComponent.h:95), `FWidget3DSceneProxy` (Engine/Source/Runtime/UMG/Private/Components/WidgetComponent.cpp:310).

Displays a rendered widget on geometry in the world, using the widget component's material and render target.

Screen-space widgets do not necessarily use this world primitive path. Collision/debug proxy alternatives are separate classes.

Related implementations (association only): `FWidget3DSceneProxy`.

### FWidget3DSceneProxy / widget render target

GPU contents: Widget render-target texels plus quad or cylindrical surface geometry are consumed by the world-space material.

CPU role / conditions: UMG/Slate widget objects and layout are CPU-side UI state; they are not scene-proxy subclasses or GPU allocations.

Source: `Engine/Source/Runtime/UMG/Private/Components/WidgetComponent.cpp:310` — `class FWidget3DSceneProxy`.

### Shapes, lines & debug geometry (debug)

Mapped definitions: `UShapeComponent` (Engine/Source/Runtime/Engine/Classes/Components/ShapeComponent.h:24), `UDebugDrawComponent` (Engine/Source/Runtime/Engine/Classes/Debug/DebugDrawComponent.h:49), `UArrowComponent` (Engine/Source/Runtime/Engine/Classes/Components/ArrowComponent.h:19), `ULineBatchComponent` (Engine/Source/Runtime/Engine/Classes/Components/LineBatchComponent.h:127), `UDrawFrustumComponent` (Engine/Source/Runtime/Engine/Classes/Components/DrawFrustumComponent.h:18), `USplineComponent` (Engine/Source/Runtime/Engine/Classes/Components/SplineComponent.h:214), `FDebugRenderSceneProxy` (Engine/Source/Runtime/Engine/Public/DebugRenderSceneProxy.h:40), `FArrowSceneProxy` (Engine/Source/Runtime/Engine/Private/Components/ArrowComponent.cpp:30), `FLineBatcherSceneProxy` (Engine/Source/Runtime/Engine/Private/Components/LineBatchComponent.cpp:22), `FBoxSceneProxy` (Engine/Source/Runtime/Engine/Private/Components/BoxComponent.cpp:132), `FSphereSceneProxy` (Engine/Source/Runtime/Engine/Private/Components/SphereComponent.cpp:111), `FDrawCylinderSceneProxy` (Engine/Source/Runtime/Engine/Private/Components/CapsuleComponent.cpp:33), `FDrawFrustumSceneProxy` (Engine/Source/Runtime/Engine/Private/Components/DrawFrustumComponent.cpp:19), `FSplinePDISceneProxy` (Engine/Source/Runtime/Engine/Private/Components/SplineComponent.cpp:3345), `FSplineMeshSceneProxy` (Engine/Source/Runtime/Engine/Private/Components/SplineComponent.cpp:3409), `UBrushComponent` (Engine/Source/Runtime/Engine/Classes/Components/BrushComponent.h:21), `FBrushSceneProxy` (Engine/Source/Runtime/Engine/Private/Components/BrushComponent.cpp:98).

Represents collision shapes, helper lines or debug visualization. Rendering, when enabled, submits generated line/triangle geometry through primitive drawing interfaces.

A scene primitive, a physics primitive and an RHI draw primitive are different concepts. USplineComponent's local debug mesh proxy is separate from the global spline-mesh renderer.

### FDebugRenderSceneProxy / dynamic drawing

GPU contents: Transient line/triangle vertex and index data can be uploaded for visualization. A collision box, sphere or capsule does not require its own persistent visible GPU mesh.

CPU role / conditions: Collision/physics shapes, editor helpers and debug draw lists are CPU state. Show flags and relevance decide whether any rendering work is submitted.

Source: `Engine/Source/Runtime/Engine/Public/DebugRenderSceneProxy.h:40` — `class FDebugRenderSceneProxy`.

### Text & billboard geometry (text-sprite)

Mapped definitions: `UTextRenderComponent` (Engine/Source/Runtime/Engine/Classes/Components/TextRenderComponent.h:44), `FTextRenderSceneProxy` (Engine/Source/Runtime/Engine/Private/Components/TextRenderComponent.cpp:586), `UBillboardComponent` (Engine/Source/Runtime/Engine/Classes/Components/BillboardComponent.h:19), `FSpriteSceneProxy` (Engine/Source/Runtime/Engine/Private/Components/BillboardComponent.cpp:30), `UMaterialBillboardComponent` (Engine/Source/Runtime/Engine/Classes/Components/MaterialBillboardComponent.h:61), `FMaterialSpriteSceneProxy` (Engine/Source/Runtime/Engine/Private/Components/MaterialBillboardComponent.cpp:54).

Generates camera-facing sprite or glyph geometry with material/texture data for scene rendering.

Text/billboard scene classes do not imply a distinct hardware topology.

### FTextRenderSceneProxy / billboard drawing

GPU contents: Glyph or quad vertex/index data plus font/sprite textures and material uniforms supply draws.

CPU role / conditions: Text layout, component state and font/sprite references select CPU-generated render data.

Source: `Engine/Source/Runtime/Engine/Private/Components/TextRenderComponent.cpp:586` — `class FTextRenderSceneProxy`.

### BSP model surfaces (model)

Mapped definitions: `UModelComponent` (Engine/Source/Runtime/Engine/Classes/Components/ModelComponent.h:33), `FModelSceneProxy` (Engine/Source/Runtime/Engine/Private/ModelRender.cpp:206).

Draws model/BSP surfaces using model render data and material groupings.

UBrushComponent debug/collision visualization is not the same as the rendered UModelComponent surface representation.

Related implementations (association only): `FModelSceneProxy`.

### FModelSceneProxy

GPU contents: Model surface vertex/index data, material textures and lighting data feed triangle draws.

CPU role / conditions: BSP model nodes/surfaces and the proxy are CPU organization. A brush used for construction/collision is a different branch.

Source: `Engine/Source/Runtime/Engine/Private/ModelRender.cpp:206` — `class FModelSceneProxy`.

### Vector-field simulation data (vector)

Mapped definitions: `UVectorFieldComponent` (Engine/Source/Runtime/Engine/Classes/Components/VectorFieldComponent.h:18), `FVectorFieldSceneProxy` (Engine/Source/Runtime/Engine/Private/VectorField.cpp:661).

Places a vector-field volume for effects simulation and optional field visualization.

A simulation volume does not automatically create an opaque mesh draw.

Related implementations (association only): `FVectorFieldSceneProxy`.

### Vector field resource

GPU contents: A 3D texture stores vector samples for simulation; debug drawing can create visualization geometry.

CPU role / conditions: The component and field instance locate/configure the field on the CPU; the texture stores sampled vectors.

Source: `Engine/Source/Runtime/Engine/Private/VectorField.cpp:661` — `FVectorFieldSceneProxy`.

### Custom mesh triangles (custom)

Mapped definitions: `UCustomMeshComponent` (Engine/Plugins/Runtime/CustomMeshComponent/Source/CustomMeshComponent/Classes/CustomMeshComponent.h:31), `FCustomMeshSceneProxy` (Engine/Plugins/Runtime/CustomMeshComponent/Source/CustomMeshComponent/Private/CustomMeshComponent.cpp:21).

Uploads supplied custom triangle geometry through the CustomMeshComponent plugin's scene proxy.

This plugin is a different class branch from UProceduralMeshComponent even though both render generated triangles.

Related implementations (association only): `FCustomMeshSceneProxy`.

### FCustomMeshSceneProxy

GPU contents: Generated vertex/index buffers and material bindings supply conventional triangle draws.

CPU role / conditions: Custom triangle arrays and proxy state are CPU-side inputs to render resource creation.

Source: `Engine/Plugins/Runtime/CustomMeshComponent/Source/CustomMeshComponent/Private/CustomMeshComponent.cpp:21` — `class FCustomMeshSceneProxy`.

## EPrimitiveType · hardware draw topology

This enum is separate from the scene-class tree. It describes how a draw interprets vertices/indices: PT_TriangleList, PT_TriangleStrip, PT_LineList, PT_QuadList, PT_PointList and PT_RectList in this checkout. Quad and rectangle topology require the corresponding RHI capability. Nanite clusters, hair strands, particles, voxels and spline meshes are scene/data representations; their implementation chooses raster topology or compute work.

Source: `Engine/Source/Runtime/RHI/Public/RHIDefinitions.h:815`.

## Per-class explanation provenance

- `FArrowSceneProxy` (Engine/Source/Runtime/Engine/Private/Components/ArrowComponent.cpp:30): debug (direct mapping).
- `FBaseDynamicMeshSceneProxy` (Engine/Source/Runtime/GeometryFramework/Public/Components/BaseDynamicMeshSceneProxy.h:38): dynamic (direct mapping).
- `FBasicLineSetSceneProxy` (Engine/Plugins/Editor/UVEditor/Source/UVEditorTools/Private/Drawing/BasicLineSetComponent.cpp:25): proxy (base context from FPrimitiveSceneProxy; subclass storage not separately reviewed).
- `FBasicPointSetSceneProxy` (Engine/Plugins/Editor/UVEditor/Source/UVEditorTools/Private/Drawing/BasicPointSetComponent.cpp:25): proxy (base context from FPrimitiveSceneProxy; subclass storage not separately reviewed).
- `FBasicTriangleSetSceneProxy` (Engine/Plugins/Editor/UVEditor/Source/UVEditorTools/Private/Drawing/BasicTriangleSetComponent.cpp:25): proxy (base context from FPrimitiveSceneProxy; subclass storage not separately reviewed).
- `FBoxSceneProxy` (Engine/Source/Runtime/Engine/Private/Components/BoxComponent.cpp:132): debug (direct mapping).
- `FBrushSceneProxy` (Engine/Source/Runtime/Engine/Private/Components/BrushComponent.cpp:98): debug (direct mapping).
- `FCableSceneProxy` (Engine/Plugins/Runtime/CableComponent/Source/CableComponent/Private/CableComponent.cpp:84): cable (direct mapping).
- `FCameraProxyMeshProxy` (Engine/Source/Runtime/Engine/Private/Camera/CameraComponent.cpp:36): static (base context from FStaticMeshSceneProxy; subclass storage not separately reviewed).
- `FChaosPathedMovementDebugRenderSceneProxy` (Engine/Plugins/Experimental/ChaosMover/Source/ChaosMover/Private/PathedMovement/ChaosPathedMovementDebugDrawComponent.cpp:26): debug (base context from FDebugRenderSceneProxy; subclass storage not separately reviewed).
- `FControlRigSceneProxy` (Engine/Plugins/Animation/ControlRig/Source/ControlRig/Public/ControlRigComponent.h:699): proxy (base context from FPrimitiveSceneProxy; subclass storage not separately reviewed).
- `FCustomMeshSceneProxy` (Engine/Plugins/Runtime/CustomMeshComponent/Source/CustomMeshComponent/Private/CustomMeshComponent.cpp:21): custom (direct mapping).
- `FDataflowDebugMeshSceneProxy` (Engine/Plugins/Dataflow/Source/DataflowEditor/Private/DataflowRendering/DataflowDebugMeshRenderableType.cpp:36): debug (base context from FDebugRenderSceneProxy; subclass storage not separately reviewed).
- `FDataflowDebugRenderSceneProxy` (Engine/Source/Runtime/Dataflow/Engine/Public/Dataflow/DataflowDebugDrawComponent.h:21): debug (base context from FDebugRenderSceneProxy; subclass storage not separately reviewed).
- `FDataflowEngineSceneProxy` (Engine/Plugins/Dataflow/Source/DataflowEnginePlugin/Private/Dataflow/DataflowEngineSceneProxy.h:34): proxy (base context from FPrimitiveSceneProxy; subclass storage not separately reviewed).
- `FDebugRenderSceneProxy` (Engine/Source/Runtime/Engine/Public/DebugRenderSceneProxy.h:40): debug (direct mapping).
- `FDrawCylinderSceneProxy` (Engine/Source/Runtime/Engine/Private/Components/CapsuleComponent.cpp:33): debug (direct mapping).
- `FDrawFrustumSceneProxy` (Engine/Source/Runtime/Engine/Private/Components/DrawFrustumComponent.cpp:19): debug (direct mapping).
- `FDynamicMeshSceneProxy` (Engine/Source/Runtime/GeometryFramework/Private/Components/DynamicMeshSceneProxy.h:22): dynamic (base context from FBaseDynamicMeshSceneProxy; subclass storage not separately reviewed).
- `FEQSSceneProxy` (Engine/Source/Runtime/AIModule/Classes/EnvironmentQuery/EQSRenderingComponent.h:17): debug (base context from FDebugRenderSceneProxy; subclass storage not separately reviewed).
- `FEditorWidgetBillboardProxy` (Engine/Plugins/Runtime/nDisplay/Source/DisplayCluster/Private/Game/EngineClasses/Scene/DisplayClusterWidgetComponent.cpp:23): proxy (base context from FPrimitiveSceneProxy; subclass storage not separately reviewed).
- `FFieldSystemSceneProxy` (Engine/Source/Runtime/Experimental/FieldSystem/Source/FieldSystemEngine/Private/Field/FieldSystemSceneProxy.h:20): proxy (base context from FPrimitiveSceneProxy; subclass storage not separately reviewed).
- `FGameplayDebuggerCompositeSceneProxy` (Engine/Source/Runtime/GameplayDebugger/Private/GameplayDebuggerRenderingComponent.cpp:12): debug (base context from FDebugRenderSceneProxy; subclass storage not separately reviewed).
- `FGeometryCacheAbcFileSceneProxy` (Engine/Plugins/Experimental/GeometryCacheAbcFile/Source/GeometryCacheAbcFile/Public/GeometryCacheAbcFileSceneProxy.h:9): cache (base context from FGeometryCacheSceneProxy; subclass storage not separately reviewed).
- `FGeometryCacheSceneProxy` (Engine/Plugins/Runtime/GeometryCache/Source/GeometryCache/Public/GeometryCacheSceneProxy.h:287): cache (direct mapping).
- `FGeometryCacheUsdSceneProxy` (Engine/Plugins/Importers/USDImporter/Source/GeometryCacheUSD/Public/GeometryCacheUSDSceneProxy.h:9): cache (base context from FGeometryCacheSceneProxy; subclass storage not separately reviewed).
- `FGeometryCollectionISMPoolDebugDrawSceneProxy` (Engine/Source/Runtime/Experimental/GeometryCollectionEngine/Public/GeometryCollection/GeometryCollectionISMPoolDebugDrawComponent.cpp:43): debug (base context from FDebugRenderSceneProxy; subclass storage not separately reviewed).
- `FGeometryCollectionSceneProxy` (Engine/Source/Runtime/Experimental/GeometryCollectionEngine/Private/GeometryCollection/GeometryCollectionSceneProxy.h:273): collection (direct mapping).
- `FGizmoArrowComponentSceneProxy` (Engine/Source/Runtime/InteractiveToolsFramework/Private/BaseGizmos/GizmoArrowComponent.cpp:62): proxy (base context from FPrimitiveSceneProxy; subclass storage not separately reviewed).
- `FGizmoBoxComponentSceneProxy` (Engine/Source/Runtime/InteractiveToolsFramework/Private/BaseGizmos/GizmoBoxComponent.cpp:53): proxy (base context from FPrimitiveSceneProxy; subclass storage not separately reviewed).
- `FGizmoCircleComponentSceneProxy` (Engine/Source/Runtime/InteractiveToolsFramework/Private/BaseGizmos/GizmoCircleComponent.cpp:31): proxy (base context from FPrimitiveSceneProxy; subclass storage not separately reviewed).
- `FGizmoLineHandleComponentSceneProxy` (Engine/Source/Runtime/InteractiveToolsFramework/Private/BaseGizmos/GizmoLineHandleComponent.cpp:15): proxy (base context from FPrimitiveSceneProxy; subclass storage not separately reviewed).
- `FGizmoRectangleComponentSceneProxy` (Engine/Source/Runtime/InteractiveToolsFramework/Private/BaseGizmos/GizmoRectangleComponent.cpp:83): proxy (base context from FPrimitiveSceneProxy; subclass storage not separately reviewed).
- `FGroupedSpriteSceneProxy` (Engine/Plugins/2D/Paper2D/Source/Paper2D/Private/GroupedSpriteSceneProxy.h:17): paper (base context from FPaperRenderSceneProxy; subclass storage not separately reviewed).
- `FHairStrandsSceneProxy` (Engine/Plugins/Runtime/HairStrands/Source/HairStrandsCore/Private/GroomComponent.cpp:447): hair (direct mapping).
- `FHeterogeneousVolumeSceneProxy` (Engine/Source/Runtime/Engine/Private/Components/HeterogeneousVolumeComponent.cpp:32): volume (direct mapping).
- `FHierarchicalStaticMeshSceneProxy` (Engine/Source/Runtime/Engine/Public/HierarchicalStaticMeshSceneProxy.h:35): instances (direct mapping).
- `FISMPoolDebugDrawSceneProxy` (Engine/Source/Runtime/Experimental/ISMPool/Public/ISMPool/ISMPoolDebugDrawComponent.cpp:40): debug (base context from FDebugRenderSceneProxy; subclass storage not separately reviewed).
- `FImagePlateFrustumSceneProxy` (Engine/Plugins/Experimental/ImagePlate/Source/ImagePlate/Private/ImagePlateFrustumComponent.cpp:20): proxy (base context from FPrimitiveSceneProxy; subclass storage not separately reviewed).
- `FImagePlateSceneProxy` (Engine/Plugins/Experimental/ImagePlate/Source/ImagePlate/Private/ImagePlateComponent.cpp:49): proxy (base context from FPrimitiveSceneProxy; subclass storage not separately reviewed).
- `FInstancedActorsModifierVolumeComponent` (Engine/Plugins/Runtime/InstancedActors/Source/InstancedActors/Private/InstancedActorsModifierVolumeComponent.cpp:329): proxy (base context from FPrimitiveSceneProxy; subclass storage not separately reviewed).
- `FInstancedSkinnedMeshSceneProxy` (Engine/Source/Runtime/Engine/Private/InstancedSkinnedMeshSceneProxy.h:86): instanced-skinned (direct mapping).
- `FInstancedStaticMeshSceneProxy` (Engine/Source/Runtime/Engine/Classes/Engine/InstancedStaticMesh.h:435): instances (direct mapping).
- `FInteractiveFoliageSceneProxy` (Engine/Source/Runtime/Foliage/Private/FoliageComponent.cpp:21): static (base context from FStaticMeshSceneProxy; subclass storage not separately reviewed).
- `FLakeCollisionSceneProxy` (Engine/Plugins/Experimental/Water/Source/Runtime/Private/LakeCollisionComponent.cpp:64): proxy (base context from FPrimitiveSceneProxy; subclass storage not separately reviewed).
- `FLandscapeComponentSceneProxy` (Engine/Source/Runtime/Landscape/Public/LandscapeRender.h:683): landscape (direct mapping).
- `FLandscapeGizmoRenderSceneProxy` (Engine/Source/Runtime/Landscape/Private/LandscapeGizmoActor.cpp:133): proxy (base context from FPrimitiveSceneProxy; subclass storage not separately reviewed).
- `FLandscapeHeightfieldCollisionComponentSceneProxy` (Engine/Source/Runtime/Landscape/Private/LandscapeCollision.cpp:573): proxy (base context from FPrimitiveSceneProxy; subclass storage not separately reviewed).
- `FLandscapeMeshProxySceneProxy` (Engine/Source/Runtime/Landscape/Public/LandscapeRender.h:661): static (base context from FStaticMeshSceneProxy; subclass storage not separately reviewed).
- `FLandscapeNaniteSceneProxy` (Engine/Source/Runtime/Landscape/Private/LandscapeRender.cpp:4866): nanite (base context from Nanite::FSceneProxy; subclass storage not separately reviewed).
- `FLandscapeSplinesSceneProxy` (Engine/Source/Runtime/Landscape/Private/LandscapeSplines.cpp:139): proxy (base context from FPrimitiveSceneProxy; subclass storage not separately reviewed).
- `FLidarPointCloudSceneProxy` (Engine/Plugins/Enterprise/LidarPointCloud/Source/LidarPointCloudRuntime/Private/Rendering/LidarPointCloudRendering.cpp:197): points (direct mapping).
- `FLineBatcherSceneProxy` (Engine/Source/Runtime/Engine/Private/Components/LineBatchComponent.cpp:22): debug (direct mapping).
- `FLineSetSceneProxy` (Engine/Plugins/Runtime/MeshModelingToolset/Source/ModelingComponents/Private/Drawing/LineSetComponent.cpp:33): proxy (base context from FPrimitiveSceneProxy; subclass storage not separately reviewed).
- `FMRMeshProxy` (Engine/Source/Runtime/MRMesh/Private/MRMeshComponent.cpp:173): proxy (base context from FPrimitiveSceneProxy; subclass storage not separately reviewed).
- `FMassCrowdLaneDataSceneProxy` (Engine/Plugins/AI/MassCrowd/Source/MassCrowd/Private/MassCrowdLaneDataRenderingComponent.cpp:63): debug (base context from FDebugRenderSceneProxy; subclass storage not separately reviewed).
- `FMassNavigationTestingSceneProxy` (Engine/Plugins/AI/MassAI/Source/MassNavigationEditor/Private/MassNavigationTestingActor.h:20): debug (base context from FDebugRenderSceneProxy; subclass storage not separately reviewed).
- `FMaterialSpriteSceneProxy` (Engine/Source/Runtime/Engine/Private/Components/MaterialBillboardComponent.cpp:54): text-sprite (direct mapping).
- `FMeshWireframeSceneProxy` (Engine/Plugins/Runtime/MeshModelingToolset/Source/ModelingComponents/Private/Drawing/MeshWireframeComponent.cpp:41): proxy (base context from FPrimitiveSceneProxy; subclass storage not separately reviewed).
- `FModelSceneProxy` (Engine/Source/Runtime/Engine/Private/ModelRender.cpp:206): model (direct mapping).
- `FNaniteGeometryCollectionSceneProxy` (Engine/Source/Runtime/Experimental/GeometryCollectionEngine/Private/GeometryCollection/GeometryCollectionSceneProxy.h:343): collection (direct mapping).
- `FNaniteInstancedSkinnedMeshSceneProxy` (Engine/Source/Runtime/Engine/Private/InstancedSkinnedMeshSceneProxy.h:30): instanced-skinned (direct mapping).
- `FNaniteSplineMeshSceneProxy` (Engine/Source/Runtime/Engine/Public/SplineMeshSceneProxy.h:181): spline (direct mapping).
- `FNavCorridorDebugRenderSceneProxy` (Engine/Plugins/Runtime/NavCorridor/Source/NavCorridor/Private/NavCorridorTestingComponent.cpp:187): debug (base context from FDebugRenderSceneProxy; subclass storage not separately reviewed).
- `FNavLinkRenderingProxy` (Engine/Source/Runtime/NavigationSystem/Public/NavLinkRenderingProxy.h:15): proxy (base context from FPrimitiveSceneProxy; subclass storage not separately reviewed).
- `FNavLocalGridSceneProxy` (Engine/Source/Runtime/AIModule/Private/GameplayDebugger/GameplayDebuggerCategory_NavLocalGrid.cpp:23): debug (base context from FDebugRenderSceneProxy; subclass storage not separately reviewed).
- `FNavMeshSceneProxy` (Engine/Source/Runtime/NavigationSystem/Public/NavMesh/NavMeshRenderingComponent.h:121): debug (base context from FDebugRenderSceneProxy; subclass storage not separately reviewed).
- `FNavTestSceneProxy` (Engine/Source/Runtime/NavigationSystem/Public/NavMesh/NavTestRenderingComponent.h:20): debug (base context from FDebugRenderSceneProxy; subclass storage not separately reviewed).
- `FNiagaraSceneProxy` (Engine/Plugins/FX/Niagara/Source/Niagara/Public/NiagaraSceneProxy.h:34): niagara (direct mapping).
- `FOceanCollisionSceneProxy` (Engine/Plugins/Experimental/Water/Source/Runtime/Private/OceanCollisionComponent.cpp:130): proxy (base context from FPrimitiveSceneProxy; subclass storage not separately reviewed).
- `FOctreeDynamicMeshSceneProxy` (Engine/Plugins/Runtime/MeshModelingToolset/Source/ModelingComponents/Private/Components/OctreeDynamicMeshSceneProxy.h:31): dynamic (base context from FBaseDynamicMeshSceneProxy; subclass storage not separately reviewed).
- `FPCGCollisionVisProxy` (Engine/Plugins/PCG/Source/PCGEditor/Private/DataVisualizations/PCGCollisionVisComponent.cpp:16): proxy (base context from FPrimitiveSceneProxy; subclass storage not separately reviewed).
- `FPVBoneSceneProxy` (Engine/Plugins/Experimental/ProceduralVegetationEditor/Source/ProceduralVegetationEditor/Private/Visualizations/PVBoneComponent.h:15): proxy (base context from FPrimitiveSceneProxy; subclass storage not separately reviewed).
- `FPVLineSceneProxy` (Engine/Plugins/Experimental/ProceduralVegetationEditor/Source/ProceduralVegetationEditor/Private/Visualizations/PVLineBatchComponent.h:40): proxy (base context from FPrimitiveSceneProxy; subclass storage not separately reviewed).
- `FPaperFlipbookSceneProxy` (Engine/Plugins/2D/Paper2D/Source/Paper2D/Private/PaperFlipbookSceneProxy.h:10): paper (base context from FPaperRenderSceneProxy; subclass storage not separately reviewed).
- `FPaperRenderSceneProxy` (Engine/Plugins/2D/Paper2D/Source/Paper2D/Private/PaperRenderSceneProxy.h:124): paper (direct mapping).
- `FPaperRenderSceneProxy_SpriteBase` (Engine/Plugins/2D/Paper2D/Source/Paper2D/Private/PaperRenderSceneProxy.h:196): paper (base context from FPaperRenderSceneProxy; subclass storage not separately reviewed).
- `FPaperSpriteSceneProxy` (Engine/Plugins/2D/Paper2D/Source/Paper2D/Private/PaperSpriteSceneProxy.h:14): paper (base context from FPaperRenderSceneProxy; subclass storage not separately reviewed).
- `FPaperTerrainSceneProxy` (Engine/Plugins/2D/Paper2D/Source/Paper2D/Private/Terrain/PaperTerrainComponent.cpp:62): paper (base context from FPaperRenderSceneProxy; subclass storage not separately reviewed).
- `FPaperTileMapRenderSceneProxy` (Engine/Plugins/2D/Paper2D/Source/Paper2D/Private/PaperTileMapRenderSceneProxy.h:15): paper (base context from FPaperRenderSceneProxy; subclass storage not separately reviewed).
- `FParticleSystemSceneProxy` (Engine/Source/Runtime/Engine/Public/ParticleSystemSceneProxy.h:36): cascade (direct mapping).
- `FPathDebugRenderSceneProxy` (Engine/Source/Runtime/AIModule/Private/GameplayDebugger/GameplayDebuggerCategory_AI.cpp:390): debug (base context from FDebugRenderSceneProxy; subclass storage not separately reviewed).
- `FPointSetSceneProxy` (Engine/Plugins/Runtime/MeshModelingToolset/Source/ModelingComponents/Private/Drawing/PointSetComponent.cpp:41): proxy (base context from FPrimitiveSceneProxy; subclass storage not separately reviewed).
- `FPrimitiveSceneProxy` (Engine/Source/Runtime/Engine/Public/PrimitiveSceneProxy.h:291): proxy (direct mapping).
- `FProceduralMeshSceneProxy` (Engine/Plugins/Runtime/ProceduralMeshComponent/Source/ProceduralMeshComponent/Private/ProceduralMeshComponent.cpp:92): procedural (direct mapping).
- `FSOContainerRenderingSceneProxy` (Engine/Plugins/Runtime/SmartObjects/Source/SmartObjectsModule/Private/SmartObjectContainerRenderingComponent.cpp:25): debug (base context from FDebugRenderSceneProxy; subclass storage not separately reviewed).
- `FSORenderingSceneProxy` (Engine/Plugins/Runtime/SmartObjects/Source/SmartObjectsModule/Private/SmartObjectRenderingComponent.cpp:15): debug (base context from FDebugRenderSceneProxy; subclass storage not separately reviewed).
- `FSkeletalMeshSceneProxy` (Engine/Source/Runtime/Engine/Public/SkeletalMeshSceneProxy.h:21): skinned (direct mapping).
- `FSmartObjectDebugSceneProxy` (Engine/Plugins/Runtime/SmartObjects/Source/SmartObjectsModule/Public/SmartObjectDebugSceneProxy.h:13): debug (base context from FDebugRenderSceneProxy; subclass storage not separately reviewed).
- `FSparseVolumeTextureViewerSceneProxy` (Engine/Source/Runtime/Renderer/Private/SparseVolumeTexture/SparseVolumeTextureViewerSceneProxy.h:21): volume (direct mapping).
- `FSphereSceneProxy` (Engine/Source/Runtime/Engine/Private/Components/SphereComponent.cpp:111): debug (direct mapping).
- `FSplineMeshSceneProxy` (Engine/Source/Runtime/Engine/Private/Components/SplineComponent.cpp:3409): debug (direct mapping).
- `FSplineMeshSceneProxy` (Engine/Source/Runtime/Engine/Public/SplineMeshSceneProxy.h:141): spline (direct mapping).
- `FSplinePDISceneProxy` (Engine/Source/Runtime/Engine/Private/Components/SplineComponent.cpp:3345): debug (direct mapping).
- `FSpriteSceneProxy` (Engine/Source/Runtime/Engine/Private/Components/BillboardComponent.cpp:30): text-sprite (direct mapping).
- `FStaticMeshSceneProxy` (Engine/Source/Runtime/Engine/Public/StaticMeshSceneProxy.h:34): static (direct mapping).
- `FStaticMeshSceneProxyExt` (Engine/Plugins/Enterprise/DataprepEditor/Source/DataprepEditor/Private/Widgets/SDataprepEditorViewport.cpp:115): static (base context from FStaticMeshSceneProxy; subclass storage not separately reviewed).
- `FStereoStaticMeshSceneProxy` (Engine/Plugins/Experimental/PanoramicCapture/Source/PanoramicCapture/Private/StereoStaticMeshComponent.cpp:10): static (base context from FStaticMeshSceneProxy; subclass storage not separately reviewed).
- `FTextRenderSceneProxy` (Engine/Source/Runtime/Engine/Private/Components/TextRenderComponent.cpp:586): text-sprite (direct mapping).
- `FTriangleSetSceneProxy` (Engine/Plugins/Runtime/MeshModelingToolset/Source/ModelingComponents/Private/Drawing/TriangleSetComponent.cpp:32): proxy (base context from FPrimitiveSceneProxy; subclass storage not separately reviewed).
- `FVectorFieldSceneProxy` (Engine/Source/Runtime/Engine/Private/VectorField.cpp:661): vector (direct mapping).
- `FVirtualHeightfieldMeshSceneProxy` (Engine/Plugins/Experimental/VirtualHeightfieldMesh/Source/VirtualHeightfieldMesh/Private/VirtualHeightfieldMeshSceneProxy.h:12): heightfield (direct mapping).
- `FWaterBodyInfoMeshSceneProxy` (Engine/Plugins/Experimental/Water/Source/Runtime/Private/WaterBodyInfoMeshComponent.h:44): static (base context from FStaticMeshSceneProxy; subclass storage not separately reviewed).
- `FWaterMeshSceneProxy` (Engine/Plugins/Experimental/Water/Source/Runtime/Public/WaterMeshSceneProxy.h:103): water (direct mapping).
- `FWidget3DSceneProxy` (Engine/Source/Runtime/UMG/Private/Components/WidgetComponent.cpp:310): widget (direct mapping).
- `FWidgetBoxProxy` (Engine/Source/Runtime/UMG/Private/Components/WidgetComponent.cpp:844): proxy (base context from FPrimitiveSceneProxy; subclass storage not separately reviewed).
- `FZoneGraphAnnotationSceneProxy` (Engine/Plugins/Runtime/ZoneGraphAnnotations/Source/ZoneGraphAnnotations/Public/ZoneGraphAnnotationComponent.h:22): debug (base context from FDebugRenderSceneProxy; subclass storage not separately reviewed).
- `FZoneGraphSceneProxy` (Engine/Plugins/Runtime/ZoneGraph/Source/ZoneGraph/Public/ZoneGraphRenderingComponent.h:17): debug (base context from FDebugRenderSceneProxy; subclass storage not separately reviewed).
- `FZoneGraphTestingSceneProxy` (Engine/Plugins/Runtime/ZoneGraph/Source/ZoneGraphDebug/Private/ZoneGraphTestingActor.cpp:205): proxy (base context from FPrimitiveSceneProxy; subclass storage not separately reviewed).
- `FZoneShapeSceneProxy` (Engine/Plugins/Runtime/ZoneGraph/Source/ZoneGraph/Private/ZoneShapeComponent.cpp:769): proxy (base context from FPrimitiveSceneProxy; subclass storage not separately reviewed).
- `Nanite::FGroomSceneProxy` (Engine/Plugins/Runtime/HairStrands/Source/HairStrandsCore/Private/NaniteGroomAsset.h:66): hair (direct mapping).
- `Nanite::FSceneProxy` (Engine/Source/Runtime/Engine/Public/NaniteSceneProxy.h:490): nanite (direct mapping).
- `Nanite::FSceneProxyBase` (Engine/Source/Runtime/Engine/Public/NaniteSceneProxy.h:219): nanite (direct mapping).
- `Nanite::FSkinnedSceneProxy` (Engine/Source/Runtime/Engine/Public/NaniteSceneProxy.h:757): nanite (direct mapping).
- `NiagaraStaticMeshComponentPrivate::FMeshSceneProxy` (Engine/Plugins/FX/NiagaraNanite/Source/NiagaraNanite/Private/Renderer/NiagaraStaticMeshComponent.cpp:22): instances (base context from FInstancedStaticMeshSceneProxy; subclass storage not separately reviewed).
- `NiagaraStaticMeshComponentPrivate::FNaniteSceneProxy` (Engine/Plugins/FX/NiagaraNanite/Source/NiagaraNanite/Private/Renderer/NiagaraStaticMeshComponent.cpp:12): nanite (base context from Nanite::FSceneProxy; subclass storage not separately reviewed).
- `UAppleARKitFaceMeshComponent` (Engine/Plugins/Runtime/AR/AppleAR/AppleARKitFaceSupport/Source/AppleARKitFaceSupport/Public/AppleARKitFaceMeshComponent.h:110): procedural (base context from UProceduralMeshComponent; subclass storage not separately reviewed).
- `UArrowComponent` (Engine/Source/Runtime/Engine/Classes/Components/ArrowComponent.h:19): debug (direct mapping).
- `UAvaTickerComponent` (Engine/Plugins/VirtualProduction/Avalanche/Source/Avalanche/Public/Framework/Ticker/AvaTickerComponent.h:60): component (base context from UPrimitiveComponent; subclass storage not separately reviewed).
- `UBakedShallowWaterSimulationComponent` (Engine/Plugins/Experimental/Water/Source/Runtime/Public/BakedShallowWaterSimulationComponent.h:350): component (base context from UPrimitiveComponent; subclass storage not separately reviewed).
- `UBaseDynamicMeshComponent` (Engine/Source/Runtime/GeometryFramework/Public/Components/BaseDynamicMeshComponent.h:124): dynamic (direct mapping).
- `UBasic2DLineSetComponent` (Engine/Plugins/Editor/UVEditor/Source/UVEditorTools/Public/Drawing/BasicLineSetComponent.h:111): mesh-base (base context from UMeshComponent; subclass storage not separately reviewed).
- `UBasic2DPointSetComponent` (Engine/Plugins/Editor/UVEditor/Source/UVEditorTools/Public/Drawing/BasicPointSetComponent.h:111): mesh-base (base context from UMeshComponent; subclass storage not separately reviewed).
- `UBasic2DTriangleSetComponent` (Engine/Plugins/Editor/UVEditor/Source/UVEditorTools/Public/Drawing/BasicTriangleSetComponent.h:89): mesh-base (base context from UMeshComponent; subclass storage not separately reviewed).
- `UBasic3DLineSetComponent` (Engine/Plugins/Editor/UVEditor/Source/UVEditorTools/Public/Drawing/BasicLineSetComponent.h:140): mesh-base (base context from UMeshComponent; subclass storage not separately reviewed).
- `UBasic3DPointSetComponent` (Engine/Plugins/Editor/UVEditor/Source/UVEditorTools/Public/Drawing/BasicPointSetComponent.h:140): mesh-base (base context from UMeshComponent; subclass storage not separately reviewed).
- `UBasic3DTriangleSetComponent` (Engine/Plugins/Editor/UVEditor/Source/UVEditorTools/Public/Drawing/BasicTriangleSetComponent.h:118): mesh-base (base context from UMeshComponent; subclass storage not separately reviewed).
- `UBasicLineSetComponentBase` (Engine/Plugins/Editor/UVEditor/Source/UVEditorTools/Public/Drawing/BasicLineSetComponent.h:32): mesh-base (base context from UMeshComponent; subclass storage not separately reviewed).
- `UBasicPointSetComponentBase` (Engine/Plugins/Editor/UVEditor/Source/UVEditorTools/Public/Drawing/BasicPointSetComponent.h:32): mesh-base (base context from UMeshComponent; subclass storage not separately reviewed).
- `UBasicTriangleSetComponentBase` (Engine/Plugins/Editor/UVEditor/Source/UVEditorTools/Public/Drawing/BasicTriangleSetComponent.h:32): mesh-base (base context from UMeshComponent; subclass storage not separately reviewed).
- `UBillboardComponent` (Engine/Source/Runtime/Engine/Classes/Components/BillboardComponent.h:19): text-sprite (direct mapping).
- `UBoxComponent` (Engine/Source/Runtime/Engine/Classes/Components/BoxComponent.h:18): debug (base context from UShapeComponent; subclass storage not separately reviewed).
- `UBrushComponent` (Engine/Source/Runtime/Engine/Classes/Components/BrushComponent.h:21): debug (direct mapping).
- `UCEClonerComponent` (Engine/Plugins/VirtualProduction/ClonerEffector/Source/ClonerEffector/Public/Cloner/CEClonerComponent.h:26): niagara (base context from UNiagaraComponent; subclass storage not separately reviewed).
- `UCableComponent` (Engine/Plugins/Runtime/CableComponent/Source/CableComponent/Classes/CableComponent.h:31): cable (direct mapping).
- `UCalibrationPointComponent` (Engine/Plugins/VirtualProduction/CameraCalibrationCore/Source/CameraCalibrationCore/Public/CalibrationPointComponent.h:30): procedural (base context from UProceduralMeshComponent; subclass storage not separately reviewed).
- `UCameraProxyMeshComponent` (Engine/Source/Runtime/Engine/Classes/Camera/CameraComponent.h:19): static (base context from UStaticMeshComponent; subclass storage not separately reviewed).
- `UCapsuleComponent` (Engine/Source/Runtime/Engine/Classes/Components/CapsuleComponent.h:16): debug (base context from UShapeComponent; subclass storage not separately reviewed).
- `UCascadeParticleSystemComponent` (Engine/Plugins/FX/Cascade/Source/Cascade/Classes/CascadeParticleSystemComponent.h:14): cascade (base context from UParticleSystemComponent; subclass storage not separately reviewed).
- `UChaosClothComponent` (Engine/Plugins/ChaosClothAsset/Source/ChaosClothAssetEngine/Public/ChaosClothAsset/ClothComponent.h:86): skinned (base context from USkinnedMeshComponent; subclass storage not separately reviewed).
- `UChaosPathedMovementDebugDrawComponent` (Engine/Plugins/Experimental/ChaosMover/Source/ChaosMover/Public/ChaosMover/PathedMovement/ChaosPathedMovementDebugDrawComponent.h:41): debug (base context from UDebugDrawComponent; subclass storage not separately reviewed).
- `UChaosVDInstancedStaticMeshComponent` (Engine/Plugins/ChaosVD/Source/ChaosVD/Private/Components/ChaosVDInstancedStaticMeshComponent.h:26): instances (base context from UInstancedStaticMeshComponent; subclass storage not separately reviewed).
- `UChaosVDStaticMeshComponent` (Engine/Plugins/ChaosVD/Source/ChaosVD/Private/Components/ChaosVDStaticMeshComponent.h:12): static (base context from UStaticMeshComponent; subclass storage not separately reviewed).
- `UCineSplineComponent` (Engine/Plugins/Experimental/CineCameraRigs/Source/CineCameraRigs/Public/CineSplineComponent.h:19): debug (base context from USplineComponent; subclass storage not separately reviewed).
- `UClothGeneratorComponent` (Engine/Plugins/Animation/MLDeformer/ChaosClothGenerator/Source/ChaosClothGenerator/Private/ClothGeneratorComponent.h:32): skinned (base context from USkinnedMeshComponent; subclass storage not separately reviewed).
- `UClusterUnionComponent` (Engine/Source/Runtime/Engine/Classes/PhysicsEngine/ClusterUnionComponent.h:210): component (base context from UPrimitiveComponent; subclass storage not separately reviewed).
- `UClusterUnionVehicleComponent` (Engine/Plugins/Experimental/ChaosModularVehicle/Source/ChaosModularVehicleEngine/Public/ChaosModularVehicle/ClusterUnionVehicleComponent.h:12): component (base context from UPrimitiveComponent; subclass storage not separately reviewed).
- `UColorCorrectionInvisibleComponent` (Engine/Plugins/Experimental/ColorCorrectRegions/Source/ColorCorrectRegions/Public/ColorCorrectRegion.h:438): component (base context from UPrimitiveComponent; subclass storage not separately reviewed).
- `UCompositeDepthMeshComponent` (Engine/Plugins/Compositing/Composite/Source/Composite/Public/Components/CompositeDepthMeshComponent.h:13): static (base context from UStaticMeshComponent; subclass storage not separately reviewed).
- `UCompositeMeshComponent` (Engine/Plugins/Compositing/Composite/Source/Composite/Public/Components/CompositeMeshComponent.h:27): static (base context from UStaticMeshComponent; subclass storage not separately reviewed).
- `UControlPointMeshComponent` (Engine/Source/Runtime/Landscape/Classes/ControlPointMeshComponent.h:11): static (base context from UStaticMeshComponent; subclass storage not separately reviewed).
- `UControlRigComponent` (Engine/Plugins/Animation/ControlRig/Source/ControlRig/Public/ControlRigComponent.h:175): component (base context from UPrimitiveComponent; subclass storage not separately reviewed).
- `UCustomMeshComponent` (Engine/Plugins/Runtime/CustomMeshComponent/Source/CustomMeshComponent/Classes/CustomMeshComponent.h:31): custom (direct mapping).
- `UCustomStaticMeshComponent` (Engine/Plugins/Enterprise/DataprepEditor/Source/DataprepEditor/Private/Widgets/SDataprepEditorViewport.h:33): static (base context from UStaticMeshComponent; subclass storage not separately reviewed).
- `UDataflowComponent` (Engine/Plugins/Dataflow/Source/DataflowEnginePlugin/Public/Dataflow/DataflowComponent.h:21): component (base context from UPrimitiveComponent; subclass storage not separately reviewed).
- `UDataflowDebugDrawComponent` (Engine/Source/Runtime/Dataflow/Engine/Public/Dataflow/DataflowDebugDrawComponent.h:12): debug (base context from UDebugDrawComponent; subclass storage not separately reviewed).
- `UDataflowDebugMeshComponent` (Engine/Plugins/Dataflow/Source/DataflowEditor/Public/DataflowRendering/DataflowDebugMeshComponent.h:13): debug (base context from UDebugDrawComponent; subclass storage not separately reviewed).
- `UDataflowEditorCollectionComponent` (Engine/Plugins/Dataflow/Source/DataflowEditor/Private/Dataflow/DataflowEditorCollectionComponent.h:16): dynamic (base context from UBaseDynamicMeshComponent; subclass storage not separately reviewed).
- `UDebugDrawComponent` (Engine/Source/Runtime/Engine/Classes/Debug/DebugDrawComponent.h:49): debug (direct mapping).
- `UDeformableCollisionsComponent` (Engine/Plugins/Experimental/ChaosFlesh/Source/ChaosFleshEngine/Public/ChaosFlesh/ChaosDeformableCollisionsComponent.h:19): component (base context from UPrimitiveComponent; subclass storage not separately reviewed).
- `UDeformableConstraintsComponent` (Engine/Plugins/Experimental/ChaosFlesh/Source/ChaosFleshEngine/Public/ChaosFlesh/ChaosDeformableConstraintsComponent.h:94): component (base context from UPrimitiveComponent; subclass storage not separately reviewed).
- `UDeformableGameplayComponent` (Engine/Plugins/Experimental/ChaosFlesh/Source/ChaosFleshEngine/Public/ChaosFlesh/ChaosDeformableGameplayComponent.h:55): component (base context from UPrimitiveComponent; subclass storage not separately reviewed).
- `UDeformablePhysicsComponent` (Engine/Plugins/Experimental/ChaosFlesh/Source/ChaosFleshEngine/Public/ChaosFlesh/ChaosDeformablePhysicsComponent.h:21): component (base context from UPrimitiveComponent; subclass storage not separately reviewed).
- `UDeformableTetrahedralComponent` (Engine/Plugins/Experimental/ChaosFlesh/Source/ChaosFleshEngine/Public/ChaosFlesh/ChaosDeformableTetrahedralComponent.h:90): component (base context from UPrimitiveComponent; subclass storage not separately reviewed).
- `UDestructibleComponent` (Engine/Plugins/Runtime/ApexDestruction/Source/ApexDestruction/Public/DestructibleComponent.h:31): skinned (base context from USkinnedMeshComponent; subclass storage not separately reviewed).
- `UDirectMeshControlComponent` (Engine/Plugins/Experimental/Animation/DirectMeshControl/Source/DirectMeshControl/Public/DirectMeshControlComponent.h:17): skinned (base context from USkinnedMeshComponent; subclass storage not separately reviewed).
- `UDisplayClusterScreenComponent` (Engine/Plugins/Runtime/nDisplay/Source/DisplayCluster/Public/Components/DisplayClusterScreenComponent.h:15): static (base context from UStaticMeshComponent; subclass storage not separately reviewed).
- `UDisplayClusterStageIsosphereComponent` (Engine/Plugins/Runtime/nDisplay/Source/DisplayCluster/Public/Components/DisplayClusterStageIsosphereComponent.h:12): procedural (base context from UProceduralMeshComponent; subclass storage not separately reviewed).
- `UDisplayClusterWidgetComponent` (Engine/Plugins/Runtime/nDisplay/Source/DisplayCluster/Private/Game/EngineClasses/Scene/DisplayClusterWidgetComponent.h:13): widget (base context from UWidgetComponent; subclass storage not separately reviewed).
- `UDisplayClusterWorldOriginComponent` (Engine/Plugins/Runtime/nDisplay/Source/DisplayClusterConfigurator/Private/Views/Viewport/DisplayClusterWorldOriginComponent.h:14): static (base context from UStaticMeshComponent; subclass storage not separately reviewed).
- `UDrawFrustumComponent` (Engine/Source/Runtime/Engine/Classes/Components/DrawFrustumComponent.h:18): debug (direct mapping).
- `UDrawSphereComponent` (Engine/Source/Runtime/Engine/Classes/Components/DrawSphereComponent.h:18): debug (base context from UShapeComponent; subclass storage not separately reviewed).
- `UDynamicMeshComponent` (Engine/Source/Runtime/GeometryFramework/Public/Components/DynamicMeshComponent.h:171): dynamic (base context from UBaseDynamicMeshComponent; subclass storage not separately reviewed).
- `UE::Avalanche::FTickerSceneProxy` (Engine/Plugins/VirtualProduction/Avalanche/Source/Avalanche/Private/Framework/Ticker/AvaTickerSceneProxy.h:15): proxy (base context from FPrimitiveSceneProxy; subclass storage not separately reviewed).
- `UE::MeshPartition::FDrawMeshPartitionCollisionSceneProxy` (Engine/Plugins/Experimental/MeshPartition/Source/MeshPartition/Private/MeshPartitionCollisionComponent.cpp:337): proxy (base context from FPrimitiveSceneProxy; subclass storage not separately reviewed).
- `UE::MeshPartition::FMegaMeshCustomPreviewSceneProxy` (Engine/Plugins/Experimental/MeshPartition/Source/MeshPartitionEditor/Internal/MeshPartitionPreviewSceneProxy.h:25): proxy (base context from FPrimitiveSceneProxy; subclass storage not separately reviewed).
- `UE::MeshPartition::MegaMeshModifierComponentLocals::FMegaMeshModifierComponentSceneProxy` (Engine/Plugins/Experimental/MeshPartition/Source/MeshPartitionEditor/Private/MeshPartitionModifierComponent.cpp:58): proxy (base context from FPrimitiveSceneProxy; subclass storage not separately reviewed).
- `UE::MeshPartition::UBooleanModifier` (Engine/Plugins/Experimental/MeshPartition/Source/MeshPartitionEditor/Public/Modifiers/MeshPartitionBooleanModifier.h:105): component (base context from UPrimitiveComponent; subclass storage not separately reviewed).
- `UE::MeshPartition::UEditableModifierBase` (Engine/Plugins/Experimental/MeshPartition/Source/MeshPartitionEditor/Public/Modifiers/MeshPartitionEditableModifierBase.h:17): component (base context from UPrimitiveComponent; subclass storage not separately reviewed).
- `UE::MeshPartition::UInstancedPatchModifier` (Engine/Plugins/Experimental/MeshPartition/Source/MeshPartitionEditor/Public/Modifiers/MeshPartitionInstancedPatchModifier.h:26): component (base context from UPrimitiveComponent; subclass storage not separately reviewed).
- `UE::MeshPartition::UInstancedProjectionModifier` (Engine/Plugins/Experimental/MeshPartition/Source/MeshPartitionEditor/Public/Modifiers/MeshPartitionInstancedProjectionModifier.h:47): component (base context from UPrimitiveComponent; subclass storage not separately reviewed).
- `UE::MeshPartition::UInstancedTexturePatchModifier` (Engine/Plugins/Experimental/MeshPartition/Source/MeshPartitionEditor/Public/Modifiers/MeshPartitionInstancedTexturePatchModifier.h:15): component (base context from UPrimitiveComponent; subclass storage not separately reviewed).
- `UE::MeshPartition::ULakeModifier` (Engine/Plugins/Experimental/MeshPartitionWater/Source/MeshPartitionWater/Private/MeshPartitionLakeModifier.h:26): component (base context from UPrimitiveComponent; subclass storage not separately reviewed).
- `UE::MeshPartition::ULatticeModifier` (Engine/Plugins/Experimental/MeshPartition/Source/MeshPartitionEditor/Public/Modifiers/MeshPartitionLatticeModifier.h:17): component (base context from UPrimitiveComponent; subclass storage not separately reviewed).
- `UE::MeshPartition::ULevelInstanceAdapter` (Engine/Plugins/Experimental/MeshPartition/Source/MeshPartitionEditor/Public/Modifiers/MeshPartitionLevelInstanceAdapter.h:20): component (base context from UPrimitiveComponent; subclass storage not separately reviewed).
- `UE::MeshPartition::UMeshBasedModifierBase` (Engine/Plugins/Experimental/MeshPartition/Source/MeshPartitionEditor/Public/Modifiers/MeshPartitionMeshBasedModifierBase.h:98): component (base context from UPrimitiveComponent; subclass storage not separately reviewed).
- `UE::MeshPartition::UMeshPartitionCollisionComponent` (Engine/Plugins/Experimental/MeshPartition/Source/MeshPartition/Public/MeshPartitionCollisionComponent.h:35): component (base context from UPrimitiveComponent; subclass storage not separately reviewed).
- `UE::MeshPartition::UMeshPartitionComponent` (Engine/Plugins/Experimental/MeshPartition/Source/MeshPartition/Public/MeshPartitionComponent.h:24): component (base context from UPrimitiveComponent; subclass storage not separately reviewed).
- `UE::MeshPartition::UMeshPartitionEditorComponent` (Engine/Plugins/Experimental/MeshPartition/Source/MeshPartitionEditor/Public/MeshPartitionEditorComponent.h:81): component (base context from UPrimitiveComponent; subclass storage not separately reviewed).
- `UE::MeshPartition::UMeshPartitionStaticMeshComponent` (Engine/Plugins/Experimental/MeshPartition/Source/MeshPartition/Public/MeshPartitionStaticMeshComponent.h:15): static (base context from UStaticMeshComponent; subclass storage not separately reviewed).
- `UE::MeshPartition::UMeshProjectModifier` (Engine/Plugins/Experimental/MeshPartition/Source/MeshPartitionEditor/Public/Modifiers/MeshPartitionMeshProjectModifier.h:19): component (base context from UPrimitiveComponent; subclass storage not separately reviewed).
- `UE::MeshPartition::UMeshProviderModifier` (Engine/Plugins/Experimental/MeshPartition/Source/MeshPartitionEditor/Public/Modifiers/MeshPartitionMeshProvider.h:28): component (base context from UPrimitiveComponent; subclass storage not separately reviewed).
- `UE::MeshPartition::UModifierComponent` (Engine/Plugins/Experimental/MeshPartition/Source/MeshPartitionEditor/Public/MeshPartitionModifierComponent.h:219): component (base context from UPrimitiveComponent; subclass storage not separately reviewed).
- `UE::MeshPartition::UNoiseModifier` (Engine/Plugins/Experimental/MeshPartition/Source/MeshPartitionEditor/Public/Modifiers/MeshPartitionNoiseModifier.h:66): component (base context from UPrimitiveComponent; subclass storage not separately reviewed).
- `UE::MeshPartition::UOceanModifier` (Engine/Plugins/Experimental/MeshPartitionWater/Source/MeshPartitionWater/Private/MeshPartitionOceanModifier.h:13): component (base context from UPrimitiveComponent; subclass storage not separately reviewed).
- `UE::MeshPartition::UPCGAdapterComponent` (Engine/Plugins/Experimental/PCGMeshPartitionInterop/Source/PCGMeshPartitionInteropEditor/Public/MeshPartitionPCGAdapterComponent.h:23): component (base context from UPrimitiveComponent; subclass storage not separately reviewed).
- `UE::MeshPartition::UPatchModifier` (Engine/Plugins/Experimental/MeshPartition/Source/MeshPartitionEditor/Public/Modifiers/MeshPartitionPatchModifier.h:27): component (base context from UPrimitiveComponent; subclass storage not separately reviewed).
- `UE::MeshPartition::UPreviewMeshComponent` (Engine/Plugins/Experimental/MeshPartition/Source/MeshPartitionEditor/Public/MeshPartitionPreviewComponents.h:38): mesh-base (base context from UMeshComponent; subclass storage not separately reviewed).
- `UE::MeshPartition::UProjectMeshLayersModifier` (Engine/Plugins/Experimental/MeshPartition/Source/MeshPartitionEditor/Public/Modifiers/MeshPartitionProjectSculptLayersModifier.h:86): component (base context from UPrimitiveComponent; subclass storage not separately reviewed).
- `UE::MeshPartition::URemeshModifier` (Engine/Plugins/Experimental/MeshPartition/Source/MeshPartitionEditor/Public/Modifiers/MeshPartitionRemeshModifier.h:281): component (base context from UPrimitiveComponent; subclass storage not separately reviewed).
- `UE::MeshPartition::URemeshModifierBase` (Engine/Plugins/Experimental/MeshPartition/Source/MeshPartitionEditor/Public/Modifiers/MeshPartitionRemeshModifier.h:33): component (base context from UPrimitiveComponent; subclass storage not separately reviewed).
- `UE::MeshPartition::URiverModifier` (Engine/Plugins/Experimental/MeshPartitionWater/Source/MeshPartitionWater/Private/MeshPartitionRiverModifier.h:23): component (base context from UPrimitiveComponent; subclass storage not separately reviewed).
- `UE::MeshPartition::USimpleWriteModifier` (Engine/Plugins/Experimental/MeshPartition/Source/MeshPartitionEditor/Public/Modifiers/MeshPartitionSimpleWriteModifier.h:49): component (base context from UPrimitiveComponent; subclass storage not separately reviewed).
- `UE::MeshPartition::USplineModifier` (Engine/Plugins/Experimental/MeshPartition/Source/MeshPartitionEditor/Public/Modifiers/MeshPartitionSplineModifier.h:121): component (base context from UPrimitiveComponent; subclass storage not separately reviewed).
- `UE::MeshPartition::USplineRemeshModifier` (Engine/Plugins/Experimental/MeshPartition/Source/MeshPartitionEditor/Public/Modifiers/MeshPartitionSplineRemeshModifier.h:21): component (base context from UPrimitiveComponent; subclass storage not separately reviewed).
- `UE::MeshPartition::UStaticMeshPreviewComponent` (Engine/Plugins/Experimental/MeshPartition/Source/MeshPartitionEditor/Public/MeshPartitionPreviewComponents.h:25): static (base context from UStaticMeshComponent; subclass storage not separately reviewed).
- `UE::MeshPartition::UTexturePatchModifier` (Engine/Plugins/Experimental/MeshPartition/Source/MeshPartitionEditor/Public/Modifiers/MeshPartitionTexturePatchModifier.h:363): component (base context from UPrimitiveComponent; subclass storage not separately reviewed).
- `UE::MeshPartition::UWaterModifier` (Engine/Plugins/Experimental/MeshPartitionWater/Source/MeshPartitionWater/Public/MeshPartitionWaterModifier.h:22): component (base context from UPrimitiveComponent; subclass storage not separately reviewed).
- `UE::MeshPartition::UWeightUtilityModifier` (Engine/Plugins/Experimental/MeshPartition/Source/MeshPartitionEditor/Public/Modifiers/MeshPartitionWeightUtilityModifier.h:22): component (base context from UPrimitiveComponent; subclass storage not separately reviewed).
- `UE::UAF::Debug::FAnimNextDebugSceneProxy` (Engine/Plugins/Experimental/UAF/UAF/Source/UAF/Internal/AnimNextDebugDraw.h:28): proxy (base context from FPrimitiveSceneProxy; subclass storage not separately reviewed).
- `UE::UsdDrawModeComponentImpl::Private::FUsdCardsSceneProxy` (Engine/Plugins/Runtime/USDCore/Source/USDClasses/Private/USDDrawModeComponent.cpp:351): proxy (base context from FPrimitiveSceneProxy; subclass storage not separately reviewed).
- `UE::UsdDrawModeComponentImpl::Private::FUsdDrawModeLinesSceneProxy` (Engine/Plugins/Runtime/USDCore/Source/USDClasses/Private/USDDrawModeComponent.cpp:285): proxy (base context from FPrimitiveSceneProxy; subclass storage not separately reviewed).
- `UE::UsdDrawModeComponentImpl::Private::FUsdLinesSceneProxy` (Engine/Plugins/Runtime/USDCore/Source/USDClasses/Private/USDDrawModeComponent.cpp:53): proxy (base context from FPrimitiveSceneProxy; subclass storage not separately reviewed).
- `UE::UsdDrawModeComponentImpl::Private::FUsdOriginLinesSceneProxy` (Engine/Plugins/Runtime/USDCore/Source/USDClasses/Private/USDDrawModeComponent.cpp:303): proxy (base context from FPrimitiveSceneProxy; subclass storage not separately reviewed).
- `UE::XRCreative::Private::FRenderComponentSceneProxy` (Engine/Plugins/Experimental/XRCreativeFramework/Source/XRCreative/Private/ITF/XRCreativeITFRenderComponent.cpp:13): proxy (base context from FPrimitiveSceneProxy; subclass storage not separately reviewed).
- `UEQSRenderingComponent` (Engine/Source/Runtime/AIModule/Classes/EnvironmentQuery/EQSRenderingComponent.h:80): debug (base context from UDebugDrawComponent; subclass storage not separately reviewed).
- `UFXSystemComponent` (Engine/Source/Runtime/Engine/Classes/Particles/ParticleSystemComponent.h:379): fx-base (direct mapping).
- `UFastGeoSurrogateComponent` (Engine/Plugins/Experimental/FastGeoStreaming/Source/FastGeoStreaming/Internal/FastGeoSurrogateComponent.h:13): component (base context from UPrimitiveComponent; subclass storage not separately reviewed).
- `UFieldSystemComponent` (Engine/Source/Runtime/Experimental/FieldSystem/Source/FieldSystemEngine/Public/Field/FieldSystemComponent.h:37): component (base context from UPrimitiveComponent; subclass storage not separately reviewed).
- `UFleshComponent` (Engine/Plugins/Experimental/ChaosFlesh/Source/ChaosFleshEngine/Public/ChaosFlesh/FleshComponent.h:29): component (base context from UPrimitiveComponent; subclass storage not separately reviewed).
- `UFleshGeneratorComponent` (Engine/Plugins/Animation/MLDeformer/ChaosFleshGenerator/Source/ChaosFleshGenerator/Private/FleshGeneratorComponent.h:20): component (base context from UPrimitiveComponent; subclass storage not separately reviewed).
- `UFoliageInstancedStaticMeshComponent` (Engine/Source/Runtime/Foliage/Public/FoliageInstancedStaticMeshComponent.h:20): instances (base context from UHierarchicalInstancedStaticMeshComponent; subclass storage not separately reviewed).
- `UGameplayDebuggerRenderingComponent` (Engine/Source/Runtime/GameplayDebugger/Public/GameplayDebuggerRenderingComponent.h:36): debug (base context from UDebugDrawComponent; subclass storage not separately reviewed).
- `UGeometryCacheAbcFileComponent` (Engine/Plugins/Experimental/GeometryCacheAbcFile/Source/GeometryCacheAbcFile/Public/GeometryCacheAbcFileComponent.h:15): cache (base context from UGeometryCacheComponent; subclass storage not separately reviewed).
- `UGeometryCacheComponent` (Engine/Plugins/Runtime/GeometryCache/Source/GeometryCache/Classes/GeometryCacheComponent.h:37): cache (direct mapping).
- `UGeometryCacheUsdComponent` (Engine/Plugins/Importers/USDImporter/Source/GeometryCacheUSD/Public/GeometryCacheUSDComponent.h:16): cache (base context from UGeometryCacheComponent; subclass storage not separately reviewed).
- `UGeometryCollectionComponent` (Engine/Source/Runtime/Experimental/GeometryCollectionEngine/Public/GeometryCollection/GeometryCollectionComponent.h:577): collection (direct mapping).
- `UGizmoArrowComponent` (Engine/Source/Runtime/InteractiveToolsFramework/Public/BaseGizmos/GizmoArrowComponent.h:15): component (base context from UPrimitiveComponent; subclass storage not separately reviewed).
- `UGizmoBaseComponent` (Engine/Source/Runtime/InteractiveToolsFramework/Public/BaseGizmos/GizmoBaseComponent.h:41): component (base context from UPrimitiveComponent; subclass storage not separately reviewed).
- `UGizmoBoxComponent` (Engine/Source/Runtime/InteractiveToolsFramework/Public/BaseGizmos/GizmoBoxComponent.h:15): component (base context from UPrimitiveComponent; subclass storage not separately reviewed).
- `UGizmoCircleComponent` (Engine/Source/Runtime/InteractiveToolsFramework/Public/BaseGizmos/GizmoCircleComponent.h:15): component (base context from UPrimitiveComponent; subclass storage not separately reviewed).
- `UGizmoLineHandleComponent` (Engine/Source/Runtime/InteractiveToolsFramework/Public/BaseGizmos/GizmoLineHandleComponent.h:16): component (base context from UPrimitiveComponent; subclass storage not separately reviewed).
- `UGizmoRectangleComponent` (Engine/Source/Runtime/InteractiveToolsFramework/Public/BaseGizmos/GizmoRectangleComponent.h:15): component (base context from UPrimitiveComponent; subclass storage not separately reviewed).
- `UGrassInstancedStaticMeshComponent` (Engine/Source/Runtime/Foliage/Public/GrassInstancedStaticMeshComponent.h:10): instances (base context from UHierarchicalInstancedStaticMeshComponent; subclass storage not separately reviewed).
- `UGroomComponent` (Engine/Plugins/Runtime/HairStrands/Source/HairStrandsCore/Public/GroomComponent.h:29): hair (direct mapping).
- `UGroomSolverComponent` (Engine/Plugins/Runtime/HairStrands/Source/HairStrandsSolver/Public/GroomSolverComponent.h:99): mesh-base (base context from UMeshComponent; subclass storage not separately reviewed).
- `UHLODInstancedSkinnedMeshComponent` (Engine/Source/Runtime/Engine/Public/WorldPartition/HLOD/HLODInstancedSkinnedMeshComponent.h:12): instanced-skinned (base context from UInstancedSkinnedMeshComponent; subclass storage not separately reviewed).
- `UHLODInstancedStaticMeshComponent` (Engine/Source/Runtime/Engine/Public/WorldPartition/HLOD/HLODInstancedStaticMeshComponent.h:13): instances (base context from UInstancedStaticMeshComponent; subclass storage not separately reviewed).
- `UHeterogeneousVolumeComponent` (Engine/Source/Runtime/Engine/Classes/Components/HeterogeneousVolumeComponent.h:20): volume (direct mapping).
- `UHierarchicalInstancedStaticMeshComponent` (Engine/Source/Runtime/Engine/Classes/Components/HierarchicalInstancedStaticMeshComponent.h:135): instances (direct mapping).
- `UISMPoolDebugDrawComponent` (Engine/Source/Runtime/Experimental/ISMPool/Public/ISMPool/ISMPoolDebugDrawComponent.h:15): debug (base context from UDebugDrawComponent; subclass storage not separately reviewed).
- `UImagePlateComponent` (Engine/Plugins/Experimental/ImagePlate/Source/ImagePlate/Public/ImagePlateComponent.h:58): component (base context from UPrimitiveComponent; subclass storage not separately reviewed).
- `UImagePlateFrustumComponent` (Engine/Plugins/Experimental/ImagePlate/Source/ImagePlate/Private/ImagePlateFrustumComponent.h:13): component (base context from UPrimitiveComponent; subclass storage not separately reviewed).
- `UInsightsSkeletalMeshComponent` (Engine/Plugins/Animation/GameplayInsights/Source/GameplayInsightsEditor/Public/InsightsSkeletalMeshComponent.h:16): skinned (base context from USkinnedMeshComponent; subclass storage not separately reviewed).
- `UInstancedActorsModifierVolumeComponent` (Engine/Plugins/Runtime/InstancedActors/Source/InstancedActors/Public/InstancedActorsModifierVolumeComponent.h:28): component (base context from UPrimitiveComponent; subclass storage not separately reviewed).
- `UInstancedSkinnedMeshComponent` (Engine/Source/Runtime/Engine/Classes/Components/InstancedSkinnedMeshComponent.h:58): instanced-skinned (direct mapping).
- `UInstancedStaticMeshComponent` (Engine/Source/Runtime/Engine/Classes/Components/InstancedStaticMeshComponent.h:158): instances (direct mapping).
- `UInteractionTargetComponent` (Engine/Plugins/Experimental/InteractionInterface/Source/InteractableInterface/Public/InteractionTargetComponent.h:20): debug (base context from UShapeComponent; subclass storage not separately reviewed).
- `UInteractiveFoliageComponent` (Engine/Source/Runtime/Foliage/Private/InteractiveFoliageComponent.h:14): static (base context from UStaticMeshComponent; subclass storage not separately reviewed).
- `UJoinedSVGDynamicMeshComponent` (Engine/Plugins/VirtualProduction/SVGImporter/Source/SVGImporter/Public/ProceduralMeshes/JoinedSVGDynamicMeshComponent.h:58): dynamic (base context from UBaseDynamicMeshComponent; subclass storage not separately reviewed).
- `ULakeCollisionComponent` (Engine/Plugins/Experimental/Water/Source/Runtime/Public/LakeCollisionComponent.h:11): component (base context from UPrimitiveComponent; subclass storage not separately reviewed).
- `ULandscapeComponent` (Engine/Source/Runtime/Landscape/Classes/LandscapeComponent.h:431): landscape (direct mapping).
- `ULandscapeGizmoRenderComponent` (Engine/Source/Runtime/Landscape/Classes/LandscapeGizmoRenderComponent.h:14): component (base context from UPrimitiveComponent; subclass storage not separately reviewed).
- `ULandscapeHeightfieldCollisionComponent` (Engine/Source/Runtime/Landscape/Classes/LandscapeHeightfieldCollisionComponent.h:41): component (base context from UPrimitiveComponent; subclass storage not separately reviewed).
- `ULandscapeMeshProxyComponent` (Engine/Source/Runtime/Landscape/Classes/LandscapeMeshProxyComponent.h:16): static (base context from UStaticMeshComponent; subclass storage not separately reviewed).
- `ULandscapeNaniteComponent` (Engine/Source/Runtime/Landscape/Classes/LandscapeNaniteComponent.h:82): nanite (direct mapping).
- `ULandscapeSplinesComponent` (Engine/Source/Runtime/Landscape/Classes/LandscapeSplinesComponent.h:106): component (base context from UPrimitiveComponent; subclass storage not separately reviewed).
- `ULidarPointCloudComponent` (Engine/Plugins/Enterprise/LidarPointCloud/Source/LidarPointCloudRuntime/Public/LidarPointCloudComponent.h:24): points (direct mapping).
- `ULineBatchComponent` (Engine/Source/Runtime/Engine/Classes/Components/LineBatchComponent.h:127): debug (direct mapping).
- `ULineSetComponent` (Engine/Plugins/Runtime/MeshModelingToolset/Source/ModelingComponents/Public/Drawing/LineSetComponent.h:42): mesh-base (base context from UMeshComponent; subclass storage not separately reviewed).
- `ULiveLinkDataPreviewComponent` (Engine/Plugins/Animation/LiveLink/Source/LiveLink/Public/Visualizers/LiveLinkDataPreviewComponent.h:29): instances (base context from UInstancedStaticMeshComponent; subclass storage not separately reviewed).
- `ULiveLinkMarkerVisualizer` (Engine/Plugins/Animation/LiveLink/Source/LiveLink/Private/Visualizers/LiveLinkMarkerVisualizer.h:29): instances (base context from UInstancedStaticMeshComponent; subclass storage not separately reviewed).
- `UMRMeshComponent` (Engine/Source/Runtime/MRMesh/Public/MRMeshComponent.h:105): component (base context from UPrimitiveComponent; subclass storage not separately reviewed).
- `UMassCrowdLaneDataRenderingComponent` (Engine/Plugins/AI/MassCrowd/Source/MassCrowd/Public/MassCrowdLaneDataRenderingComponent.h:15): component (base context from UPrimitiveComponent; subclass storage not separately reviewed).
- `UMassNavigationTestingComponent` (Engine/Plugins/AI/MassAI/Source/MassNavigationEditor/Private/MassNavigationTestingActor.h:33): debug (base context from UDebugDrawComponent; subclass storage not separately reviewed).
- `UMaterialBillboardComponent` (Engine/Source/Runtime/Engine/Classes/Components/MaterialBillboardComponent.h:61): text-sprite (direct mapping).
- `UMediaStreamComponent` (Engine/Plugins/Experimental/MediaStream/Source/MediaStream/Public/MediaStreamComponent.h:14): static (base context from UStaticMeshComponent; subclass storage not separately reviewed).
- `UMeshComponent` (Engine/Source/Runtime/Engine/Classes/Components/MeshComponent.h:24): mesh-base (direct mapping).
- `UMeshWireframeComponent` (Engine/Plugins/Runtime/MeshModelingToolset/Source/ModelingComponents/Public/Drawing/MeshWireframeComponent.h:92): mesh-base (base context from UMeshComponent; subclass storage not separately reviewed).
- `UMetaHumanDepthMeshComponent` (Engine/Plugins/MetaHuman/MetaHumanAnimator/Source/MetaHumanImageViewerEditor/Public/MetaHumanDepthMeshComponent.h:12): procedural (base context from UProceduralMeshComponent; subclass storage not separately reviewed).
- `UMetaHumanFootageComponent` (Engine/Plugins/MetaHuman/MetaHumanAnimator/Source/MetaHumanImageViewerEditor/Public/MetaHumanFootageComponent.h:36): component (base context from UPrimitiveComponent; subclass storage not separately reviewed).
- `UMetaHumanPerformanceControlRigComponent` (Engine/Plugins/MetaHuman/MetaHumanAnimator/Source/MetaHumanPerformance/Private/UI/MetaHumanPerformanceControlRigComponent.h:14): component (base context from UPrimitiveComponent; subclass storage not separately reviewed).
- `UMetaHumanTemplateMesh` (Engine/Plugins/MetaHuman/MetaHumanAnimator/Source/MetaHumanIdentity/Public/MetaHumanIdentityParts.h:649): dynamic (base context from UBaseDynamicMeshComponent; subclass storage not separately reviewed).
- `UMetaHumanTemplateMeshComponent` (Engine/Plugins/MetaHuman/MetaHumanAnimator/Source/MetaHumanIdentity/Public/MetaHumanTemplateMeshComponent.h:28): component (base context from UPrimitiveComponent; subclass storage not separately reviewed).
- `UMixedRealityCaptureBillboard` (Engine/Plugins/Runtime/MixedRealityCaptureFramework/Source/MixedRealityCaptureFramework/Private/MrcProjectionBillboard.h:12): text-sprite (base context from UMaterialBillboardComponent; subclass storage not separately reviewed).
- `UModelComponent` (Engine/Source/Runtime/Engine/Classes/Components/ModelComponent.h:33): model (direct mapping).
- `UMotionControllerComponent` (Engine/Source/Runtime/HeadMountedDisplay/Public/MotionControllerComponent.h:18): component (base context from UPrimitiveComponent; subclass storage not separately reviewed).
- `UNaniteDisplacedMeshComponent` (Engine/Plugins/Experimental/NaniteDisplacedMesh/Source/NaniteDisplacedMesh/Public/NaniteDisplacedMeshComponent.h:17): static (base context from UStaticMeshComponent; subclass storage not separately reviewed).
- `UNavCorridorTestingComponent` (Engine/Plugins/Runtime/NavCorridor/Source/NavCorridor/Public/NavCorridorTestingComponent.h:20): debug (base context from UDebugDrawComponent; subclass storage not separately reviewed).
- `UNavLinkComponent` (Engine/Source/Runtime/NavigationSystem/Public/NavLinkComponent.h:16): component (base context from UPrimitiveComponent; subclass storage not separately reviewed).
- `UNavLinkRenderingComponent` (Engine/Source/Runtime/NavigationSystem/Public/NavLinkRenderingComponent.h:14): component (base context from UPrimitiveComponent; subclass storage not separately reviewed).
- `UNavMeshRenderingComponent` (Engine/Source/Runtime/NavigationSystem/Public/NavMesh/NavMeshRenderingComponent.h:193): debug (base context from UDebugDrawComponent; subclass storage not separately reviewed).
- `UNavTestRenderingComponent` (Engine/Source/Runtime/NavigationSystem/Public/NavMesh/NavTestRenderingComponent.h:115): debug (base context from UDebugDrawComponent; subclass storage not separately reviewed).
- `UNiagaraComponent` (Engine/Plugins/FX/Niagara/Source/Niagara/Public/NiagaraComponent.h:57): niagara (direct mapping).
- `UNiagaraCullProxyComponent` (Engine/Plugins/FX/Niagara/Source/Niagara/Public/NiagaraCullProxyComponent.h:23): niagara (base context from UNiagaraComponent; subclass storage not separately reviewed).
- `UNiagaraStaticMeshComponent` (Engine/Plugins/FX/NiagaraNanite/Source/NiagaraNanite/Private/Renderer/NiagaraStaticMeshComponent.h:16): static (base context from UStaticMeshComponent; subclass storage not separately reviewed).
- `UNiagaraUIComponent` (Engine/Plugins/FX/NiagaraUIRenderer/Source/NiagaraUIRenderer/Public/NiagaraUIComponent.h:10): niagara (base context from UNiagaraComponent; subclass storage not separately reviewed).
- `UOceanBoxCollisionComponent` (Engine/Plugins/Experimental/Water/Source/Runtime/Public/OceanCollisionComponent.h:49): debug (base context from UShapeComponent; subclass storage not separately reviewed).
- `UOceanCollisionComponent` (Engine/Plugins/Experimental/Water/Source/Runtime/Public/OceanCollisionComponent.h:13): component (base context from UPrimitiveComponent; subclass storage not separately reviewed).
- `UOctreeDynamicMeshComponent` (Engine/Plugins/Runtime/MeshModelingToolset/Source/ModelingComponents/Public/Components/OctreeDynamicMeshComponent.h:37): dynamic (base context from UBaseDynamicMeshComponent; subclass storage not separately reviewed).
- `UPCGCollisionVisComponent` (Engine/Plugins/PCG/Source/PCGEditor/Public/DataVisualizations/PCGCollisionVisComponent.h:10): component (base context from UPrimitiveComponent; subclass storage not separately reviewed).
- `UPCGDebugDrawComponent` (Engine/Plugins/PCG/Source/PCG/Public/PCGDebugDrawComponent.h:32): debug (base context from UDebugDrawComponent; subclass storage not separately reviewed).
- `UPCGProceduralISMComponent` (Engine/Plugins/PCG/Source/PCG/Private/Components/PCGProceduralISMComponent.h:34): static (base context from UStaticMeshComponent; subclass storage not separately reviewed).
- `UPCapBoneVisualiser` (Engine/Plugins/VirtualProduction/PerformanceCaptureWorkflow/Source/PerformanceCaptureWorkflow/Private/Visualizers/PCapBoneVisualizer.h:21): instances (base context from UInstancedStaticMeshComponent; subclass storage not separately reviewed).
- `UPVBoneComponent` (Engine/Plugins/Experimental/ProceduralVegetationEditor/Source/ProceduralVegetationEditor/Private/Visualizations/PVBoneComponent.h:47): component (base context from UPrimitiveComponent; subclass storage not separately reviewed).
- `UPVLineBatchComponent` (Engine/Plugins/Experimental/ProceduralVegetationEditor/Source/ProceduralVegetationEditor/Private/Visualizations/PVLineBatchComponent.h:64): component (base context from UPrimitiveComponent; subclass storage not separately reviewed).
- `UPVScaleVisualizationComponent` (Engine/Plugins/Experimental/ProceduralVegetationEditor/Source/ProceduralVegetationEditor/Private/Visualizations/PVScaleVisualizationComponent.h:15): component (base context from UPrimitiveComponent; subclass storage not separately reviewed).
- `UPVSkeletonVisualizerComponent` (Engine/Plugins/Experimental/ProceduralVegetationEditor/Source/ProceduralVegetationEditor/Private/Visualizations/PVSkeletonVisualizerComponent.h:23): component (base context from UPrimitiveComponent; subclass storage not separately reviewed).
- `UPaperFlipbookComponent` (Engine/Plugins/2D/Paper2D/Source/Paper2D/Classes/PaperFlipbookComponent.h:24): paper (direct mapping).
- `UPaperGroupedSpriteComponent` (Engine/Plugins/2D/Paper2D/Source/Paper2D/Classes/PaperGroupedSpriteComponent.h:58): paper (direct mapping).
- `UPaperSpriteComponent` (Engine/Plugins/2D/Paper2D/Source/Paper2D/Classes/PaperSpriteComponent.h:29): paper (direct mapping).
- `UPaperTerrainComponent` (Engine/Plugins/2D/Paper2D/Source/Paper2D/Classes/PaperTerrainComponent.h:54): paper (direct mapping).
- `UPaperTerrainSplineComponent` (Engine/Plugins/2D/Paper2D/Source/Paper2D/Classes/PaperTerrainSplineComponent.h:12): debug (base context from USplineComponent; subclass storage not separately reviewed).
- `UPaperTileMapComponent` (Engine/Plugins/2D/Paper2D/Source/Paper2D/Classes/PaperTileMapComponent.h:38): paper (direct mapping).
- `UParticleSystemComponent` (Engine/Source/Runtime/Engine/Classes/Particles/ParticleSystemComponent.h:491): cascade (direct mapping).
- `UPointSetComponent` (Engine/Plugins/Runtime/MeshModelingToolset/Source/ModelingComponents/Public/Drawing/PointSetComponent.h:50): mesh-base (base context from UMeshComponent; subclass storage not separately reviewed).
- `UPoseSearchMeshComponent` (Engine/Plugins/Animation/PoseSearch/Source/Editor/Private/PoseSearchMeshComponent.h:9): skinned (base context from USkinnedMeshComponent; subclass storage not separately reviewed).
- `UPoseableMeshComponent` (Engine/Source/Runtime/Engine/Classes/Components/PoseableMeshComponent.h:17): skinned (base context from USkinnedMeshComponent; subclass storage not separately reviewed).
- `UPrimitiveComponent` (Engine/Source/Runtime/Engine/Classes/Components/PrimitiveComponent.h:307): component (direct mapping).
- `UProceduralMeshComponent` (Engine/Plugins/Runtime/ProceduralMeshComponent/Source/ProceduralMeshComponent/Public/ProceduralMeshComponent.h:149): procedural (direct mapping).
- `URemoveInstancesModifierVolumeComponent` (Engine/Plugins/Runtime/InstancedActors/Source/InstancedActors/Public/InstancedActorsModifierVolumeComponent.h:138): component (base context from UPrimitiveComponent; subclass storage not separately reviewed).
- `USVGBaseDynamicMeshComponent` (Engine/Plugins/VirtualProduction/SVGImporter/Source/SVGImporter/Public/ProceduralMeshes/SVGBaseDynamicMeshComponent.h:9): dynamic (base context from UBaseDynamicMeshComponent; subclass storage not separately reviewed).
- `USVGDynamicMeshComponent` (Engine/Plugins/VirtualProduction/SVGImporter/Source/SVGImporter/Public/ProceduralMeshes/SVGDynamicMeshComponent.h:46): dynamic (base context from UBaseDynamicMeshComponent; subclass storage not separately reviewed).
- `USVGFillComponent` (Engine/Plugins/VirtualProduction/SVGImporter/Source/SVGImporter/Private/ProceduralMeshes/SVGFillComponent.h:65): dynamic (base context from UBaseDynamicMeshComponent; subclass storage not separately reviewed).
- `USVGStrokeComponent` (Engine/Plugins/VirtualProduction/SVGImporter/Source/SVGImporter/Private/ProceduralMeshes/SVGStrokeComponent.h:35): dynamic (base context from UBaseDynamicMeshComponent; subclass storage not separately reviewed).
- `UShallowWaterRiverComponent` (Engine/Plugins/Experimental/WaterAdvanced/Source/WaterAdvanced/Public/ShallowWaterRiverActor.h:38): component (base context from UPrimitiveComponent; subclass storage not separately reviewed).
- `UShapeComponent` (Engine/Source/Runtime/Engine/Classes/Components/ShapeComponent.h:24): debug (direct mapping).
- `USkeletalGeneratorComponent` (Engine/Plugins/Animation/MLDeformer/ChaosFleshGenerator/Source/ChaosFleshGenerator/Private/FleshGeneratorComponent.h:40): skinned (base context from USkinnedMeshComponent; subclass storage not separately reviewed).
- `USkeletalMeshBackedDynamicMeshComponent` (Engine/Plugins/Animation/SkeletalMeshModelingTools/Source/SkeletalMeshModelingTools/Private/Components/SKMBackedDynaMeshComponent.h:29): dynamic (base context from UBaseDynamicMeshComponent; subclass storage not separately reviewed).
- `USkeletalMeshComponent` (Engine/Source/Runtime/Engine/Classes/Components/SkeletalMeshComponent.h:341): skinned (base context from USkinnedMeshComponent; subclass storage not separately reviewed).
- `USkeletalMeshComponentBudgeted` (Engine/Plugins/Runtime/AnimationBudgetAllocator/Source/AnimationBudgetAllocator/Public/SkeletalMeshComponentBudgeted.h:23): skinned (base context from USkinnedMeshComponent; subclass storage not separately reviewed).
- `USkinnedMeshComponent` (Engine/Source/Runtime/Engine/Classes/Components/SkinnedMeshComponent.h:267): skinned (direct mapping).
- `USmartObjectContainerRenderingComponent` (Engine/Plugins/Runtime/SmartObjects/Source/SmartObjectsModule/Public/SmartObjectContainerRenderingComponent.h:14): component (base context from UPrimitiveComponent; subclass storage not separately reviewed).
- `USmartObjectDebugRenderingComponent` (Engine/Plugins/Runtime/SmartObjects/Source/SmartObjectsModule/Public/SmartObjectDebugRenderingComponent.h:17): debug (base context from UDebugDrawComponent; subclass storage not separately reviewed).
- `USmartObjectRenderingComponent` (Engine/Plugins/Runtime/SmartObjects/Source/SmartObjectsModule/Public/SmartObjectRenderingComponent.h:14): component (base context from UPrimitiveComponent; subclass storage not separately reviewed).
- `USmartObjectSubsystemRenderingComponent` (Engine/Plugins/Runtime/SmartObjects/Source/SmartObjectsModule/Private/SmartObjectSubsystemRenderingActor.h:12): debug (base context from UDebugDrawComponent; subclass storage not separately reviewed).
- `USmartObjectTestRenderingComponent` (Engine/Plugins/Runtime/SmartObjects/Source/SmartObjectsModule/Private/SmartObjectTestingActor.h:115): debug (base context from UDebugDrawComponent; subclass storage not separately reviewed).
- `USmartObjectZoneAnnotations` (Engine/Plugins/Runtime/MassGameplay/Source/MassSmartObjects/Public/SmartObjectZoneAnnotations.h:95): debug (base context from UDebugDrawComponent; subclass storage not separately reviewed).
- `USparseVolumeTextureViewerComponent` (Engine/Source/Runtime/Renderer/Private/SparseVolumeTexture/SparseVolumeTextureViewerComponent.h:35): volume (direct mapping).
- `USphereComponent` (Engine/Source/Runtime/Engine/Classes/Components/SphereComponent.h:17): debug (base context from UShapeComponent; subclass storage not separately reviewed).
- `USplineComponent` (Engine/Source/Runtime/Engine/Classes/Components/SplineComponent.h:214): debug (direct mapping).
- `USplineMeshComponent` (Engine/Source/Runtime/Engine/Classes/Components/SplineMeshComponent.h:119): spline (direct mapping).
- `UStaticMeshComponent` (Engine/Source/Runtime/Engine/Classes/Components/StaticMeshComponent.h:105): static (direct mapping).
- `UStereoStaticMeshComponent` (Engine/Plugins/Experimental/PanoramicCapture/Source/PanoramicCapture/Private/StereoStaticMeshComponent.h:23): static (base context from UStaticMeshComponent; subclass storage not separately reviewed).
- `UTextRenderComponent` (Engine/Source/Runtime/Engine/Classes/Components/TextRenderComponent.h:44): text-sprite (direct mapping).
- `UTriangleSetComponent` (Engine/Plugins/Runtime/MeshModelingToolset/Source/ModelingComponents/Public/Drawing/TriangleSetComponent.h:87): mesh-base (base context from UMeshComponent; subclass storage not separately reviewed).
- `UUsdDrawModeComponent` (Engine/Plugins/Runtime/USDCore/Source/USDClasses/Public/USDDrawModeComponent.h:61): component (base context from UPrimitiveComponent; subclass storage not separately reviewed).
- `UVectorFieldComponent` (Engine/Source/Runtime/Engine/Classes/Components/VectorFieldComponent.h:18): vector (direct mapping).
- `UVehicleSimAerofoilComponent` (Engine/Plugins/Experimental/ChaosModularVehicle/Source/ChaosModularVehicleEngine/Public/ChaosModularVehicle/VehicleSimAerofoilComponent.h:24): component (base context from UPrimitiveComponent; subclass storage not separately reviewed).
- `UVehicleSimBaseComponent` (Engine/Plugins/Experimental/ChaosModularVehicle/Source/ChaosModularVehicleEngine/Public/ChaosModularVehicle/VehicleSimBaseComponent.h:73): component (base context from UPrimitiveComponent; subclass storage not separately reviewed).
- `UVehicleSimChassisComponent` (Engine/Plugins/Experimental/ChaosModularVehicle/Source/ChaosModularVehicleEngine/Public/ChaosModularVehicle/VehicleSimChassisComponent.h:14): component (base context from UPrimitiveComponent; subclass storage not separately reviewed).
- `UVehicleSimClutchComponent` (Engine/Plugins/Experimental/ChaosModularVehicle/Source/ChaosModularVehicleEngine/Public/ChaosModularVehicle/VehicleSimClutchComponent.h:14): component (base context from UPrimitiveComponent; subclass storage not separately reviewed).
- `UVehicleSimEngineComponent` (Engine/Plugins/Experimental/ChaosModularVehicle/Source/ChaosModularVehicleEngine/Public/ChaosModularVehicle/VehicleSimEngineComponent.h:15): component (base context from UPrimitiveComponent; subclass storage not separately reviewed).
- `UVehicleSimSuspensionComponent` (Engine/Plugins/Experimental/ChaosModularVehicle/Source/ChaosModularVehicleEngine/Public/ChaosModularVehicle/VehicleSimSuspensionComponent.h:14): component (base context from UPrimitiveComponent; subclass storage not separately reviewed).
- `UVehicleSimThrusterComponent` (Engine/Plugins/Experimental/ChaosModularVehicle/Source/ChaosModularVehicleEngine/Public/ChaosModularVehicle/VehicleSimThrusterComponent.h:14): component (base context from UPrimitiveComponent; subclass storage not separately reviewed).
- `UVehicleSimTransmissionComponent` (Engine/Plugins/Experimental/ChaosModularVehicle/Source/ChaosModularVehicleEngine/Public/ChaosModularVehicle/VehicleSimTransmissionComponent.h:22): component (base context from UPrimitiveComponent; subclass storage not separately reviewed).
- `UVehicleSimWheelComponent` (Engine/Plugins/Experimental/ChaosModularVehicle/Source/ChaosModularVehicleEngine/Public/ChaosModularVehicle/VehicleSimWheelComponent.h:21): component (base context from UPrimitiveComponent; subclass storage not separately reviewed).
- `UViewAdjustedStaticMeshGizmoComponent` (Engine/Source/Runtime/InteractiveToolsFramework/Public/BaseGizmos/ViewAdjustedStaticMeshGizmoComponent.h:24): static (base context from UStaticMeshComponent; subclass storage not separately reviewed).
- `UVirtualHeightfieldMeshComponent` (Engine/Plugins/Experimental/VirtualHeightfieldMesh/Source/VirtualHeightfieldMesh/Public/VirtualHeightfieldMeshComponent.h:18): heightfield (direct mapping).
- `UWaterBodyComponent` (Engine/Plugins/Experimental/Water/Source/Runtime/Public/WaterBodyComponent.h:113): water-body (direct mapping).
- `UWaterBodyCustomComponent` (Engine/Plugins/Experimental/Water/Source/Runtime/Public/WaterBodyCustomComponent.h:15): water-body (base context from UWaterBodyComponent; subclass storage not separately reviewed).
- `UWaterBodyInfoMeshComponent` (Engine/Plugins/Experimental/Water/Source/Runtime/Private/WaterBodyInfoMeshComponent.h:17): static (base context from UStaticMeshComponent; subclass storage not separately reviewed).
- `UWaterBodyLakeComponent` (Engine/Plugins/Experimental/Water/Source/Runtime/Public/WaterBodyLakeComponent.h:17): water-body (base context from UWaterBodyComponent; subclass storage not separately reviewed).
- `UWaterBodyMeshComponent` (Engine/Plugins/Experimental/Water/Source/Runtime/Public/WaterBodyMeshComponent.h:19): static (base context from UStaticMeshComponent; subclass storage not separately reviewed).
- `UWaterBodyOceanComponent` (Engine/Plugins/Experimental/Water/Source/Runtime/Public/WaterBodyOceanComponent.h:16): water-body (base context from UWaterBodyComponent; subclass storage not separately reviewed).
- `UWaterBodyRiverComponent` (Engine/Plugins/Experimental/Water/Source/Runtime/Public/WaterBodyRiverComponent.h:16): water-body (base context from UWaterBodyComponent; subclass storage not separately reviewed).
- `UWaterBodyStaticMeshComponent` (Engine/Plugins/Experimental/Water/Source/Runtime/Private/WaterBodyStaticMeshComponent.h:18): static (base context from UStaticMeshComponent; subclass storage not separately reviewed).
- `UWaterMeshComponent` (Engine/Plugins/Experimental/Water/Source/Runtime/Public/WaterMeshComponent.h:19): water (direct mapping).
- `UWaterSplineComponent` (Engine/Plugins/Experimental/Water/Source/Runtime/Public/WaterSplineComponent.h:27): debug (base context from USplineComponent; subclass storage not separately reviewed).
- `UWidgetComponent` (Engine/Source/Runtime/UMG/Public/Components/WidgetComponent.h:95): widget (direct mapping).
- `UXRCreativeGizmoMeshComponent` (Engine/Plugins/Experimental/XRCreativeFramework/Source/XRCreative/Public/XRCreativeGizmos.h:181): static (base context from UStaticMeshComponent; subclass storage not separately reviewed).
- `UXRCreativeITFRenderComponent` (Engine/Plugins/Experimental/XRCreativeFramework/Source/XRCreative/Private/ITF/XRCreativeITFRenderComponent.h:28): component (base context from UPrimitiveComponent; subclass storage not separately reviewed).
- `UXRDeviceVisualizationComponent` (Engine/Plugins/Runtime/XRBase/Source/XRBase/Public/XRDeviceVisualizationComponent.h:18): static (base context from UStaticMeshComponent; subclass storage not separately reviewed).
- `UZoneGraphAnnotationComponent` (Engine/Plugins/Runtime/ZoneGraphAnnotations/Source/ZoneGraphAnnotations/Public/ZoneGraphAnnotationComponent.h:38): debug (base context from UDebugDrawComponent; subclass storage not separately reviewed).
- `UZoneGraphAnnotationTestingComponent` (Engine/Plugins/Runtime/ZoneGraphAnnotations/Source/ZoneGraphAnnotations/Public/ZoneGraphAnnotationTestingActor.h:43): debug (base context from UDebugDrawComponent; subclass storage not separately reviewed).
- `UZoneGraphCrowdLaneAnnotations` (Engine/Plugins/AI/MassCrowd/Source/MassCrowd/Public/ZoneGraphCrowdLaneAnnotations.h:36): debug (base context from UDebugDrawComponent; subclass storage not separately reviewed).
- `UZoneGraphDisturbanceAnnotation` (Engine/Plugins/Runtime/ZoneGraphAnnotations/Source/ZoneGraphAnnotations/Public/Annotations/ZoneGraphDisturbanceAnnotation.h:154): debug (base context from UDebugDrawComponent; subclass storage not separately reviewed).
- `UZoneGraphRenderingComponent` (Engine/Plugins/Runtime/ZoneGraph/Source/ZoneGraph/Public/ZoneGraphRenderingComponent.h:62): component (base context from UPrimitiveComponent; subclass storage not separately reviewed).
- `UZoneGraphTestingComponent` (Engine/Plugins/Runtime/ZoneGraph/Source/ZoneGraphDebug/Public/ZoneGraphTestingActor.h:41): component (base context from UPrimitiveComponent; subclass storage not separately reviewed).
- `UZoneShapeComponent` (Engine/Plugins/Runtime/ZoneGraph/Source/ZoneGraph/Public/ZoneShapeComponent.h:39): component (base context from UPrimitiveComponent; subclass storage not separately reviewed).
- `ViewAdjustedStaticMeshGizmoComponentLocals::FViewAdjustedStaticMeshGizmoComponentProxy` (Engine/Source/Runtime/InteractiveToolsFramework/Private/BaseGizmos/ViewAdjustedStaticMeshGizmoComponent.cpp:43): static (base context from FStaticMeshSceneProxy; subclass storage not separately reviewed).
