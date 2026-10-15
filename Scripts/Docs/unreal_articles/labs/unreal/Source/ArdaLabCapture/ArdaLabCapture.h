#pragma once
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ArdaLabCapture.generated.h"
class UMaterialExpression;
class UStaticMesh;
UCLASS()
class UArdaLabCapture : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable) static void SetViewportMode(FName Mode, FName Buffer);
    UFUNCTION(BlueprintCallable) static bool ConnectDisplacement(UMaterialExpression* Expression);
    UFUNCTION(BlueprintCallable) static FString NaniteStats(UStaticMesh* Mesh);
    UFUNCTION(BlueprintCallable) static void RenderViewport();
    UFUNCTION(BlueprintCallable) static void CaptureViewport(const FString& Filename);
};
