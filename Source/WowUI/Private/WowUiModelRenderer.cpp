#include "WowUiModelRenderer.h"
#include "WowUiDressUpPreview.h"
#include "WowEntityManager.h"
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
#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
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
    TUniquePtr<FWowUiDressUpPreview> Preview;
    float ModelScale=1,BaseModelScale=1;
    FVector Position=FVector::ZeroVector;
    TWeakObjectPtr<USkeletalMesh> FramedMesh;
    FBox ReferenceBounds=FBox(ForceInit);
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
        S.Target->RenderTargetFormat=Portrait?RTF_RGBA8:RTF_RGBA16f;
        S.Target->ClearColor=Portrait?FLinearColor::Black:FLinearColor(0,0,0,1);
        S.Target->InitAutoFormat(Portrait?128:256,Portrait?128:384);
        S.Target->UpdateResourceImmediate(true);
        if(auto* Base=LoadObject<UMaterialInterface>(nullptr,Portrait?TEXT("/Game/Materials/M_WowUiImage.M_WowUiImage"):TEXT("/Game/Materials/M_WowUiModel.M_WowUiModel")))
        {
            S.Material.Reset(UMaterialInstanceDynamic::Create(Base,GetTransientPackage()));
            S.Material->SetTextureParameterValue(TEXT("Image"),S.Target.Get());
            S.Material->SetScalarParameterValue(TEXT("Circle"),Portrait?1:0);
        }
        if(Handle>=0)
        {
            const auto& View=Views.FindOrAdd(Handle); S.Yaw=FMath::RadiansToDegrees(View.Facing); S.Position=View.Position;
            if(const auto* Def=Frames.GetFrameDef(Handle))
            {
                S.BaseModelScale=Def->ModelScale;
                if(Def->Type==EWowFrameType::DressUpModel || (Context.EntityManager && Context.EntityManager->FindPlayer(Guid))) S.Preview=MakeUnique<FWowUiDressUpPreview>();
            }
            S.ModelScale=View.Scale>0?View.Scale:S.BaseModelScale;
        }
    }
    // Stock panels temporarily replace portraits with static art (e.g. buyback).
    // SetPortraitTexture must restore the capture even when the GUID is unchanged.
    auto& Slot=*Owned;
    FSlateBrush Brush; Brush.DrawAs=ESlateBrushDrawType::Image;
    Brush.SetResourceObject(Slot.Material.IsValid()?static_cast<UObject*>(Slot.Material.Get()):static_cast<UObject*>(Slot.Target.Get()));
    Brush.ImageSize=FVector2D(Portrait?128:256,Portrait?128:384);
    Image->SetBrush(Brush); Image->SetRenderOpacity(1); Image->SetColorAndOpacity(FLinearColor::White);
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
    if(Slots.Contains(Key))
    {
        auto& S=*Slots[Key]; Configure(Key,S.Image.Get(),Guid,Handle,false);
        // FrameXML creates some model frames before the network player exists.
        const auto* Def=Frames.GetFrameDef(Handle);
        const bool OwnPreview=(Def && Def->Type==EWowFrameType::DressUpModel) || (Context.EntityManager && Context.EntityManager->FindPlayer(Guid));
        if(OwnPreview && !S.Preview) S.Preview=MakeUnique<FWowUiDressUpPreview>();
        if(!OwnPreview) S.Preview.Reset();
        if(S.Preview) S.Preview->SetUnit(Guid);
        return;
    }
    if(auto* Canvas=Cast<UCanvasPanel>(Frames.GetWidgetForHandle(Handle)))
    {
        auto* Image=NewObject<UImage>(Canvas); Image->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
        if(auto* Slot=Canvas->AddChildToCanvas(Image)) { Slot->SetAnchors(FAnchors(0,0,1,1)); Slot->SetOffsets(FMargin(0)); Slot->SetZOrder(100); }
        Configure(Key,Image,Guid,Handle,false);
        if(auto* S=Slots.Find(Key); S && (*S)->Preview) (*S)->Preview->SetUnit(Guid);
    }
}

void FWowUiModelRenderer::Facing(int64 Handle,float Radians)
{
    Views.FindOrAdd(Handle).Facing=Radians;
    if(auto* S=Slots.Find(FString::Printf(TEXT("Model_%lld"),Handle))) { (*S)->Yaw=FMath::RadiansToDegrees(Radians); (*S)->NextCapture=0; }
}
void FWowUiModelRenderer::Scale(int64 Handle,float Value)
{
    Views.FindOrAdd(Handle).Scale=Value;
    if(auto* S=Slots.Find(FString::Printf(TEXT("Model_%lld"),Handle))) { (*S)->ModelScale=Value; (*S)->NextCapture=0; }
}
void FWowUiModelRenderer::Position(int64 Handle,const FVector& Value)
{
    Views.FindOrAdd(Handle).Position=Value;
    if(auto* S=Slots.Find(FString::Printf(TEXT("Model_%lld"),Handle))) { (*S)->Position=Value; (*S)->NextCapture=0; }
}
void FWowUiModelRenderer::TryOn(int64 Handle,uint32 ItemId,uint32 Type)
{
    if(auto* S=Slots.Find(FString::Printf(TEXT("Model_%lld"),Handle)))
    {
        if(!(*S)->Preview) { (*S)->Preview=MakeUnique<FWowUiDressUpPreview>(); (*S)->Preview->SetUnit((*S)->Guid); }
        (*S)->Preview->TryOn(ItemId,Type); (*S)->NextCapture=0;
    }
}
void FWowUiModelRenderer::Dress(int64 Handle,bool Dressed)
{
    if(auto* S=Slots.Find(FString::Printf(TEXT("Model_%lld"),Handle)))
    {
        if(!(*S)->Preview) { (*S)->Preview=MakeUnique<FWowUiDressUpPreview>(); (*S)->Preview->SetUnit((*S)->Guid); }
        (*S)->Preview->Dress(Dressed); (*S)->NextCapture=0;
    }
}

void FWowUiModelRenderer::Tick(UWorld* World)
{
    if(!World || !Context.ResolveActor) return;
    const double Now=FPlatformTime::Seconds();
    for(auto& Pair:Slots)
    {
        auto& S=*Pair.Value;
        if(S.Handle>=0 && !Frames.IsFrameDrawn(S.Handle)) { if(S.Preview && S.Preview->GetActor()) S.Preview->ReleaseActor(); continue; }
        if(!S.Image.IsValid() || !S.Guid || Now<S.NextCapture) continue;
        if(S.Image->GetVisibility()==ESlateVisibility::Collapsed) continue;
        auto* Actor=Context.ResolveActor(S.Guid);
        if(S.Preview) { if(!S.Preview->Rebuild(World,Context,Mpq,Cache)) { S.NextCapture=Now+.2; continue; } Actor=S.Preview->GetActor(); }
        if(!IsValid(Actor)) continue;
        if(!S.Camera.IsValid() || S.Camera->GetWorld()!=World)
        {
            FActorSpawnParameters Params; Params.ObjectFlags=RF_Transient;
            S.Camera=World->SpawnActor<ASceneCapture2D>(ASceneCapture2D::StaticClass(),FTransform::Identity,Params);
            if(!S.Camera.IsValid()) continue;
            S.Camera->SetActorEnableCollision(false);
            auto* Capture=S.Camera->GetCaptureComponent2D();
            Capture->bCaptureEveryFrame=false; Capture->bCaptureOnMovement=false;
            Capture->TextureTarget=S.Target.Get(); Capture->CaptureSource=S.bPortrait?SCS_FinalColorLDR:SCS_SceneColorHDR;
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
        if(!S.bPortrait)
        {
            const FVector2D Size=S.Image->GetCachedGeometry().GetLocalSize();
            const int32 Width=FMath::Clamp(FMath::RoundToInt(384*Size.X/FMath::Max(Size.Y,1.)/8)*8,128,1024);
            if(S.Target->SizeX!=Width) S.Target->ResizeTarget(Width,384);
        }
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
        if(!S.bPortrait)
        {
            auto* Mesh=Body->GetSkeletalMeshAsset();
            if(S.FramedMesh.Get()!=Mesh)
            {
                S.FramedMesh=Mesh; S.ReferenceBounds=FBox(ForceInit);
                // Runtime M2 imported bounds include every animation. The rest
                // vertices give the actual body height without camera/capsule offsets.
                if(const auto* Data=Mesh?Mesh->GetResourceForRendering():nullptr; Data && !Data->LODRenderData.IsEmpty())
                {
                    const auto& Vertices=Data->LODRenderData[0].StaticVertexBuffers.PositionVertexBuffer;
                    for(uint32 I=0;I<Vertices.GetNumVertices();++I) S.ReferenceBounds+=FVector(Vertices.VertexPosition(I));
                }
            }
            if(S.ReferenceBounds.IsValid)
            {
                const auto Physical=S.ReferenceBounds.TransformBy(Body->GetComponentTransform());
                Focus=Physical.GetCenter(); HalfHeight=FMath::Max(float(Physical.GetExtent().Z),40.f);
            }
        }
        Capture->FOVAngle=S.bPortrait?25:30;
        const float Aspect=S.bPortrait?1.f:float(S.Target->SizeX)/S.Target->SizeY;
        // FOVAngle is horizontal: retain the full body in wide dress-up panels too.
        float Distance=S.bPortrait?FMath::Max(HalfHeight*1.8f,140.f):HalfHeight*Aspect*1.12f/FMath::Tan(FMath::DegreesToRadians(15.f));
        if(!S.bPortrait) Distance=FMath::Max(40.f,Distance*S.BaseModelScale/FMath::Max(S.ModelScale,.05f)-S.Position.X*100);
        const FVector Direction=Actor->GetActorForwardVector().RotateAngleAxis(S.Yaw,FVector::UpVector);
        if(!S.bPortrait) Focus-=FVector::CrossProduct(FVector::UpVector,Direction)*S.Position.Y*100+FVector::UpVector*S.Position.Z*100;
        const FVector Position=Focus+Direction*Distance;
        Capture->SetWorldLocationAndRotation(Position,(Focus-Position).Rotation());
        Capture->CaptureScene(); S.NextCapture=Now+.1;
    }
}
