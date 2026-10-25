#include "WowUiModelRenderer.h"
#include "WowFrameManager.h"
#include "LuaApi/LuaApiRegistry.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/MeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/CapsuleComponent.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/StrongObjectPtr.h"

struct FWowUiModelRenderer::FSlot
{
    TWeakObjectPtr<UImage> Image;
    TWeakObjectPtr<ASceneCapture2D> Camera;
    TStrongObjectPtr<UTextureRenderTarget2D> Target;
    TStrongObjectPtr<UMaterialInstanceDynamic> Material;
    uint64 Guid=0;
    int64 Handle=-1;
    bool bPortrait=false;
    float Yaw=0;
    double NextCapture=0;
    bool bLogged=false;
};

FWowUiModelRenderer::FWowUiModelRenderer(FWowFrameManager& InFrames,FWowLuaContext& InContext):Frames(InFrames),Context(InContext) {}
FWowUiModelRenderer::~FWowUiModelRenderer()
{
    for(auto& Pair:Slots) if(Pair.Value->Camera.IsValid()) Pair.Value->Camera->Destroy();
}

void FWowUiModelRenderer::Configure(const FString& Key,UImage* Image,uint64 Guid,int64 Handle,bool Portrait)
{
    if(!Image) return;
    auto& Owned=Slots.FindOrAdd(Key);
    if(!Owned)
    {
        Owned=MakeUnique<FSlot>(); auto& S=*Owned;
        S.Image=Image; S.Handle=Handle; S.bPortrait=Portrait;
        S.Target.Reset(NewObject<UTextureRenderTarget2D>());
        S.Target->RenderTargetFormat=RTF_RGBA8;
        S.Target->ClearColor=FLinearColor::Black;
        S.Target->InitAutoFormat(Portrait?128:256,Portrait?128:384);
        S.Target->UpdateResourceImmediate(true);
        if(auto* Base=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Materials/M_WowUiImage.M_WowUiImage")))
        {
            S.Material.Reset(UMaterialInstanceDynamic::Create(Base,GetTransientPackage()));
            S.Material->SetTextureParameterValue(TEXT("Image"),S.Target.Get());
            S.Material->SetScalarParameterValue(TEXT("Circle"),Portrait?1:0);
        }
        FSlateBrush Brush; Brush.DrawAs=ESlateBrushDrawType::Image;
        Brush.SetResourceObject(S.Material.IsValid()?static_cast<UObject*>(S.Material.Get()):static_cast<UObject*>(S.Target.Get()));
        Brush.ImageSize=FVector2D(Portrait?128:256,Portrait?128:384);
        Image->SetBrush(Brush); Image->SetRenderOpacity(1); Image->SetColorAndOpacity(FLinearColor::White);
    }
    if(Owned->Guid!=Guid) { Owned->Guid=Guid; Owned->NextCapture=0; }
    Image->SetVisibility(Guid?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
}

void FWowUiModelRenderer::Portrait(const FString& Name,uint64 Guid)
{
    Configure(Name,Frames.GetNamedTexture(Name),Guid,-1,true);
}

void FWowUiModelRenderer::Model(int64 Handle,uint64 Guid)
{
    const FString Key=FString::Printf(TEXT("Model_%lld"),Handle);
    if(Slots.Contains(Key)) { auto& S=*Slots[Key]; Configure(Key,S.Image.Get(),Guid,Handle,false); return; }
    if(auto* Canvas=Cast<UCanvasPanel>(Frames.GetWidgetForHandle(Handle)))
    {
        auto* Image=NewObject<UImage>(Canvas); Image->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
        if(auto* Slot=Canvas->AddChildToCanvas(Image)) { Slot->SetAnchors(FAnchors(0,0,1,1)); Slot->SetOffsets(FMargin(0)); Slot->SetZOrder(100); }
        Configure(Key,Image,Guid,Handle,false);
    }
}

void FWowUiModelRenderer::Facing(int64 Handle,float Radians)
{
    if(auto* S=Slots.Find(FString::Printf(TEXT("Model_%lld"),Handle))) { (*S)->Yaw=FMath::RadiansToDegrees(Radians); (*S)->NextCapture=0; }
}

void FWowUiModelRenderer::Tick(UWorld* World)
{
    if(!World || !Context.ResolveActor) return;
    const double Now=FPlatformTime::Seconds();
    for(auto& Pair:Slots)
    {
        auto& S=*Pair.Value;
        if(!S.Image.IsValid() || !S.Guid || Now<S.NextCapture || (S.Handle>=0 && !Frames.IsFrameDrawn(S.Handle))) continue;
        if(S.Image->GetVisibility()==ESlateVisibility::Collapsed) continue;
        auto* Actor=Context.ResolveActor(S.Guid); if(!IsValid(Actor)) continue;
        if(!S.Camera.IsValid() || S.Camera->GetWorld()!=World)
        {
            FActorSpawnParameters Params; Params.ObjectFlags=RF_Transient;
            S.Camera=World->SpawnActor<ASceneCapture2D>(ASceneCapture2D::StaticClass(),FTransform::Identity,Params);
            if(!S.Camera.IsValid()) continue;
            S.Camera->SetActorEnableCollision(false);
            auto* Capture=S.Camera->GetCaptureComponent2D();
            Capture->bCaptureEveryFrame=false; Capture->bCaptureOnMovement=false;
            Capture->TextureTarget=S.Target.Get(); Capture->CaptureSource=SCS_FinalColorLDR;
            Capture->PrimitiveRenderMode=ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
            Capture->ShowFlags.SetAtmosphere(false); Capture->ShowFlags.SetFog(false); Capture->ShowFlags.SetSkyLighting(false);
            Capture->ShowFlags.SetLighting(false); Capture->ShowFlags.SetTemporalAA(false);
            Capture->PostProcessSettings.bOverride_AutoExposureMethod=true;
            Capture->PostProcessSettings.AutoExposureMethod=AEM_Manual;
        }
        auto* Capture=S.Camera->GetCaptureComponent2D();
        Capture->ShowOnlyComponents.Reset();
        TInlineComponentArray<UMeshComponent*> Meshes; Actor->GetComponents(Meshes);
        USkeletalMeshComponent* Body=nullptr;
        for(auto* Mesh:Meshes) if(Mesh && Mesh->IsRegistered() && Mesh->IsVisible())
        {
            Capture->ShowOnlyComponent(Mesh);
            if(!Body) Body=Cast<USkeletalMeshComponent>(Mesh);
        }
        if(!Body) continue;
        const auto Bounds=Body->Bounds;
        if(!S.bLogged) { UE_LOG(LogTemp,Log,TEXT("UiModel: %s guid=%llu actor=%s mesh=%s origin=%s half=%s"),*Pair.Key,S.Guid,*Actor->GetName(),*GetNameSafe(Body),*Bounds.Origin.ToString(),*Bounds.BoxExtent.ToString()); S.bLogged=true; }
        // Animated M2 culling bounds cover every pose and are much larger than
        // a standing character. Frame its physical body instead of that box.
        const auto* Capsule=Actor->FindComponentByClass<UCapsuleComponent>();
        float HalfHeight=Capsule?Capsule->GetScaledCapsuleHalfHeight():FMath::Max(float(Bounds.BoxExtent.Z)*.5f,40.f);
        FVector Focus=Capsule?Capsule->GetComponentLocation():Bounds.Origin;
        if(Body->DoesSocketExist(TEXT("WowPortraitHead")))
        {
            const FVector Head=Body->GetSocketLocation(TEXT("WowPortraitHead")); const FVector Feet=Body->GetComponentLocation();
            HalfHeight=FMath::Max(float((Head.Z+25-Feet.Z)*.5),40.f);
            Focus=S.bPortrait?Head+FVector(0,0,16):FVector(Head.X,Head.Y,(Head.Z+40+Feet.Z)*.5);
            if(!S.bPortrait) HalfHeight=FMath::Max(float((Head.Z+55-Feet.Z)*.5),HalfHeight);
        }
        else if(S.bPortrait) Focus.Z+=HalfHeight*.9f;
        Capture->FOVAngle=S.bPortrait?25:30;
        const float Aspect=S.bPortrait?1.f:2.f/3.f;
        const float Distance=S.bPortrait?FMath::Max(HalfHeight*1.8f,140.f):HalfHeight*.9f/FMath::Tan(FMath::DegreesToRadians(15.f));
        const FVector Direction=Actor->GetActorForwardVector().RotateAngleAxis(S.Yaw,FVector::UpVector);
        const FVector Position=Focus+Direction*Distance;
        Capture->SetWorldLocationAndRotation(Position,(Focus-Position).Rotation());
        Capture->CaptureScene(); S.NextCapture=Now+.1;
    }
}
