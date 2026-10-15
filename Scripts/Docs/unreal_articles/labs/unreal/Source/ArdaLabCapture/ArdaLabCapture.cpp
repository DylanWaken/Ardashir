#include "ArdaLabCapture.h"
#include "Modules/ModuleManager.h"
#include "LevelEditor.h"
#include "SLevelViewport.h"
#include "LevelEditorViewport.h"
#include "MaterialEditingLibrary.h"
#include "Materials/MaterialExpression.h"
#include "Engine/StaticMesh.h"
#include "StaticMeshResources.h"
#include "Rendering/NaniteResources.h"
#include "Slate/SceneViewport.h"
#include "UnrealClient.h"
IMPLEMENT_MODULE(FDefaultModuleImpl, ArdaLabCapture)

void UArdaLabCapture::RenderViewport()
{
    auto Viewport = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor").GetFirstActiveLevelViewport();
    if (!Viewport.IsValid()) return;
    auto SceneViewport = Viewport->GetSharedActiveViewport();
    SceneViewport->SetFixedViewportSize(1280, 720);
    SceneViewport->Draw(false);
}
void UArdaLabCapture::CaptureViewport(const FString& Filename)
{
    FScreenshotRequest::RequestScreenshot(Filename, false, false);
    RenderViewport();
}

void UArdaLabCapture::SetViewportMode(FName Mode, FName Buffer)
{
    auto Viewport = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor").GetFirstActiveLevelViewport();
    if (!Viewport.IsValid()) return;
    auto& Client = Viewport->GetLevelViewportClient();
    if (Mode == "buffer") { Client.SetViewMode(VMI_VisualizeBuffer); Client.ChangeBufferVisualizationMode(Buffer); }
    else if (Mode == "wireframe") Client.SetViewMode(VMI_Wireframe);
    else if (Mode == "unlit") Client.SetViewMode(VMI_Unlit);
    else Client.SetViewMode(VMI_Lit);
    Client.Invalidate();
}
bool UArdaLabCapture::ConnectDisplacement(UMaterialExpression* Expression)
{
    return UMaterialEditingLibrary::ConnectMaterialProperty(Expression, FString(), MP_Displacement);
}
FString UArdaLabCapture::NaniteStats(UStaticMesh* Mesh)
{
    if (!Mesh || !Mesh->GetRenderData() || !Mesh->GetRenderData()->NaniteResourcesPtr) return "{}";
    const auto& R = *Mesh->GetRenderData()->NaniteResourcesPtr;
    return FString::Printf(TEXT("{\"input_triangles\":%u,\"input_vertices\":%u,\"clusters_all_lods\":%u,\"root_pages\":%u,\"pages\":%d,\"hierarchy_nodes\":%d,\"root_data_bytes\":%d,\"streamable_data_bytes\":%lld,\"position_precision\":%d}"),
        R.NumInputTriangles, R.NumInputVertices, R.NumClusters, R.NumRootPages, R.PageStreamingStates.Num(), R.HierarchyNodes.Num(), R.RootData.Num(), R.StreamablePages.GetBulkDataSize(), R.PositionPrecision);
}
