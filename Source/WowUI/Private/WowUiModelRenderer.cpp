#include "WowUiModelRenderer.h"
#include "WowUiDressUpPreview.h"
#include "WowPortraitCameraData.h"
#include "WowM2AnimInstance.h"
#include "WowEntityManager.h"
#include "WowConnectionManager.h"
#include "Formats/Dbc/DbcStore.h"
#include "WowFrameManager.h"
#include "LuaApi/LuaApiRegistry.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/MeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/CapsuleComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/Material.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
constexpr int32 PortraitTextureSize=64; // Wrath's unit portrait render target and mask.

bool WaitingForPortraitMesh(const UMeshComponent* Mesh)
{
    if(!Mesh || !Mesh->IsRegistered() || !Mesh->IsVisible()) return false;
    if(const auto* Skel=Cast<USkeletalMeshComponent>(Mesh))
    {
        const auto* Asset=Skel->GetSkeletalMeshAsset();
        const auto* Data=Asset?Asset->GetResourceForRendering():nullptr;
        if(!Data || Data->LODRenderData.IsEmpty() || !Data->LODRenderData[0].GetNumVertices()) return false;
    }
    else if(const auto* Static=Cast<UStaticMeshComponent>(Mesh))
    {
        const UStaticMesh* Asset=Static->GetStaticMesh();
        if(!Asset || !Asset->GetNumLODs() || !Asset->GetNumVertices(0)) return false;
    }
    else return false;
    return Mesh->IsRenderStateDirty() || Mesh->IsPSOPrecaching() || !Mesh->GetSceneProxy();
}

void LightPortrait(TConstArrayView<UMeshComponent*> Meshes,const FTransform& BodyTransform)
{
    static TWeakObjectPtr<UMaterialInterface> PortraitMaterial,CharacterMaterial;
    if(!PortraitMaterial.IsValid()) PortraitMaterial=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Materials/M_WowPortrait.M_WowPortrait"));
    if(!CharacterMaterial.IsValid()) CharacterMaterial=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Materials/M_WowCharacter.M_WowCharacter"));
    if(!PortraitMaterial.IsValid() || !CharacterMaterial.IsValid()) return;

    // The portrait's light travels (-1,0,-1) in WoW model space. The imported
    // M2 swaps X and Y, so every attachment sees the same incoming direction.
    const FVector TowardLight=BodyTransform.TransformVectorNoScale(FVector(0,1,1)).GetSafeNormal();
    for(auto* Mesh:Meshes)
    {
        if(!Mesh || Mesh->ComponentHasTag(TEXT("WowPortraitPreserveMaterial"))) continue;
        const auto* Skel=Cast<USkeletalMeshComponent>(Mesh);
        auto* Asset=Skel?Skel->GetSkeletalMeshAsset():nullptr;
        const auto* Sections=Asset?Cast<UWowPortraitCameraData>(Asset->GetAssetUserDataOfClass(UWowPortraitCameraData::StaticClass())):nullptr;
        for(int32 Slot=0;Slot<Mesh->GetNumMaterials();++Slot)
        {
            if(Sections && Sections->PreserveMaterialSlots.Contains(Slot)) continue;
            auto* Original=Mesh->GetMaterial(Slot);
            UTexture* Texture=nullptr;
            if(!Original || Original->GetMaterial()!=CharacterMaterial.Get() ||
                !Original->GetTextureParameterValue(FMaterialParameterInfo(TEXT("BaseTexture")),Texture) || !Texture) continue;
            auto* Material=UMaterialInstanceDynamic::Create(PortraitMaterial.Get(),Mesh);
            Material->SetTextureParameterValue(TEXT("BaseTexture"),Texture);
            Material->SetVectorParameterValue(TEXT("TowardLight"),FLinearColor(TowardLight.X,TowardLight.Y,TowardLight.Z));
            Mesh->SetMaterial(Slot,Material);
        }
    }
}
}

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
    bool Captured=false;
    TWeakObjectPtr<AActor> PortraitSource;
    FString PortraitArt;
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
        S.Handle=Handle; S.bPortrait=Portrait;
        if(Portrait) S.Preview=MakeUnique<FWowUiDressUpPreview>();
        S.Target.Reset(NewObject<UTextureRenderTarget2D>());
        S.Target->RenderTargetFormat=RTF_RGBA16f;
        if(Portrait) S.Target->TargetGamma=1.f;
        S.Target->ClearColor=Portrait?FLinearColor::Black:FLinearColor(0,0,0,1);
        S.Target->InitAutoFormat(Portrait?PortraitTextureSize:256,Portrait?PortraitTextureSize:384);
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
    Slot.Image=Image;
    FSlateBrush Brush; Brush.DrawAs=ESlateBrushDrawType::Image;
    Brush.SetResourceObject(Slot.Material.IsValid()?static_cast<UObject*>(Slot.Material.Get()):static_cast<UObject*>(Slot.Target.Get()));
    Brush.ImageSize=FVector2D(Portrait?PortraitTextureSize:256,Portrait?PortraitTextureSize:384);
    Image->SetBrush(Brush); Image->SetRenderOpacity(1); Image->SetColorAndOpacity(FLinearColor::White);
    if(Slot.Guid!=Guid)
    {
        Slot.Guid=Guid; Slot.NextCapture=0; Slot.bLogged=false; Slot.Captured=false; Slot.PortraitArt.Empty();
        if(Portrait)
        {
            Slot.Preview->SetUnit(Guid);
            if(Guid) ShowTemporaryPortrait(Slot);
        }
    }
    Image->SetVisibility(Guid?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
}

void FWowUiModelRenderer::Portrait(const FString& Name,UImage* Image,uint64 Guid)
{
    Configure(Name,Image,Guid,-1,true);
}

bool FWowUiModelRenderer::SetPortraitArt(FSlot& Slot,const FString& Art)
{
    if(Slot.Material.IsValid() && Slot.PortraitArt==Art) return true;
    UTexture2D* Texture=Frames.LoadUITexture(Art);
    if(!Texture) return false;
    if(Slot.Material.IsValid()) Slot.Material->SetTextureParameterValue(TEXT("Image"),Texture);
    else
    {
        auto Brush=Slot.Image->GetBrush();
        Brush.SetResourceObject(Texture);
        Slot.Image->SetBrush(Brush);
    }
    Slot.PortraitArt=Art;
    Slot.Captured=false;
    return true;
}

void FWowUiModelRenderer::ShowTemporaryPortrait(FSlot& Slot)
{
    const auto* Unit=Context.ConnectionManager?Context.ConnectionManager->PacketHandler.FindUiUnit(Slot.Guid):nullptr;
    FString Art=TEXT("Interface\\CharacterFrame\\TemporaryPortrait-Pet");
    if(Unit && Unit->IsPlayer())
    {
        const auto* Race=FDbcStore::Get().ChrRaces().GetById(Unit->GetFieldByte(UnitField::BYTES_0,0));
        Art=Race?FString(TEXT("Interface\\CharacterFrame\\TemporaryPortrait-"))+(Unit->GetFieldByte(UnitField::BYTES_0,2)==1?TEXT("Female-"):TEXT("Male-"))+Race->ClientFileString:TEXT("Interface\\Icons\\INV_Misc_QuestionMark");
    }
    if(!SetPortraitArt(Slot,Art) && Slot.PortraitArt.IsEmpty()) Slot.Target->UpdateResourceImmediate(true);
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
void FWowUiModelRenderer::Tabard(int64 Handle,const FWowGuildEmblem& Value,uint32 Display)
{
    if(auto* S=Slots.Find(FString::Printf(TEXT("Model_%lld"),Handle)); S && (*S)->Preview) {(*S)->Preview->Tabard(Value,Display); (*S)->NextCapture=0;}
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

bool FWowUiModelRenderer::TrackPortraitHead(const FReferenceSkeleton& Skeleton,FName Bone,const FTransform& AnimatedHead,FVector& Position,FVector& Target,
    TConstArrayView<FTransform> StandPose)
{
    const int32 Index=Skeleton.FindBoneIndex(Bone);
    if(Index==INDEX_NONE) return false;
    const TConstArrayView<FTransform> Poses = StandPose.Num()==Skeleton.GetNum() ? StandPose : MakeArrayView(Skeleton.GetRefBonePose());
    FTransform Reference=Poses[Index];
    for(int32 Parent=Skeleton.GetParentIndex(Index); Parent!=INDEX_NONE; Parent=Skeleton.GetParentIndex(Parent)) Reference*=Poses[Parent];
    Position=AnimatedHead.TransformPosition(Reference.InverseTransformPosition(Position));
    Target=AnimatedHead.TransformPosition(Reference.InverseTransformPosition(Target));
    return true;
}

void FWowUiModelRenderer::Tick(UWorld* World)
{
    if(!World || !Context.ResolveActor) return;
    const double Now=FPlatformTime::Seconds();
    for(auto& Pair:Slots)
    {
        auto& S=*Pair.Value;
        if(S.bPortrait && (!S.Image.IsValid() || S.Image->GetVisibility()==ESlateVisibility::Collapsed))
        {
            if(S.Preview->GetActor()) S.Preview->ReleaseActor();
            continue;
        }
        if(S.bPortrait && !Frames.IsRegionOwnerDrawn(Pair.Key)) continue;
        if(S.Handle>=0 && !Frames.IsFrameDrawn(S.Handle)) { if(S.Preview && S.Preview->GetActor()) S.Preview->ReleaseActor(); continue; }
        if(!S.Image.IsValid() || !S.Guid || Now<S.NextCapture) continue;
        if(S.Image->GetVisibility()==ESlateVisibility::Collapsed) continue;
        const auto* Unit=S.bPortrait && Context.ConnectionManager ? Context.ConnectionManager->PacketHandler.FindUiUnit(S.Guid) : nullptr;
        const auto* Display=Unit ? FDbcStore::Get().CreatureDisplayInfo().GetById(Unit->GetDisplayId()) : nullptr;
        if(Display && !Display->PortraitTextureName.IsEmpty() &&
           SetPortraitArt(S,TEXT("Interface\\Icons\\")+Display->PortraitTextureName))
        {
            if(S.Preview->GetActor()) S.Preview->ReleaseActor();
            S.NextCapture=Now+.25;
            continue;
        }
        auto* Actor=Context.ResolveActor(S.Guid);
        if(S.Preview) Actor=S.Preview->Rebuild(World,Context,Mpq,Cache,S.bPortrait)?S.Preview->GetActor():nullptr;
        if(!IsValid(Actor))
        {
            if(S.bPortrait) ShowTemporaryPortrait(S);
            S.Captured=false; S.NextCapture=Now+.2; continue;
        }
        if(S.bPortrait && S.Captured && S.PortraitSource.Get()==Actor) { S.NextCapture=Now+.25; continue; }
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
            if(S.bPortrait)
            {
                // The portrait material supplies the original lighting. Keep it
                // linear until the UI presents it; the world film curve is separate.
                Capture->ShowFlags.SetTonemapper(false);
                Capture->ShowFlags.SetBloom(false);
            }
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
            if(!Body)
                if(auto* Candidate=Cast<USkeletalMeshComponent>(Mesh); Candidate && !Candidate->LeaderPoseComponent.IsValid())
                    Body=Candidate;
        }
        if(!Body) continue;
        if(S.bPortrait && S.PortraitSource.Get()!=Actor)
        {
            // Portraits are textures. Sample the isolated model in its initial idle pose.
            Body->TickAnimation(0.f,false);
            Body->RefreshBoneTransforms();
            Body->bPauseAnims=true;
            Body->SetComponentTickEnabled(false);
            LightPortrait(Meshes,Body->GetComponentTransform());
            S.PortraitSource=Actor;
            S.Captured=false;
            // SetMaterial schedules proxy replacement at the end of this frame.
            // Capturing now can permanently cache an empty or previous unit.
            continue;
        }
        if(S.bPortrait && Meshes.ContainsByPredicate(WaitingForPortraitMesh)) continue;
        if(!S.bPortrait)
        {
            const FVector2D Size=S.Image->GetCachedGeometry().GetLocalSize();
            const int32 Width=FMath::Clamp(FMath::RoundToInt(384*Size.X/FMath::Max(Size.Y,1.)/8)*8,128,1024);
            if(S.Target->SizeX!=Width) S.Target->ResizeTarget(Width,384);
        }
        const auto Bounds=Body->Bounds;
        // Animated M2 culling bounds cover every pose and are much larger than
        // a standing character. Frame its physical body instead of that box.
        float HalfHeight=FMath::Max(float(Bounds.BoxExtent.Z)*.5f,40.f);
        FVector Focus=Bounds.Origin;
        if(const auto* Capsule=Actor->FindComponentByClass<UCapsuleComponent>())
        {
            HalfHeight=Capsule->GetScaledCapsuleHalfHeight();
            Focus=Capsule->GetComponentLocation();
        }
        else if(const auto* Selection=FindObjectFast<UBoxComponent>(Actor,TEXT("UnitSelection")))
        {
            HalfHeight=FMath::Max(float(Selection->Bounds.BoxExtent.Z),40.f);
            Focus=Selection->Bounds.Origin;
        }
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
        FVector Position=Focus+Direction*Distance;
        if(S.bPortrait) if(const auto* View=Cast<UWowPortraitCameraData>(Body->GetSkeletalMeshAsset()->GetAssetUserDataOfClass(UWowPortraitCameraData::StaticClass())); View && View->SquareFovDegrees>0)
        {
            FVector LocalPosition=View->Position,LocalTarget=View->Target;
            if(Body->DoesSocketExist(TEXT("WowPortraitHead")))
            {
                TConstArrayView<FTransform> StandPose;
                if(auto* Anim=Cast<UWowM2AnimInstance>(Body->GetAnimInstance())) StandPose=Anim->GetPortraitStandPose();
                TrackPortraitHead(Body->GetSkeletalMeshAsset()->GetRefSkeleton(),Body->GetSocketBoneName(TEXT("WowPortraitHead")),
                    Body->GetSocketTransform(TEXT("WowPortraitHead"),RTS_Component),LocalPosition,LocalTarget,StandPose);
            }
            Focus=Body->GetComponentTransform().TransformPosition(LocalTarget);
            Position=Body->GetComponentTransform().TransformPosition(LocalPosition);
            Capture->FOVAngle=View->SquareFovDegrees;
        }
        if(!S.bLogged)
        {
            UE_LOG(LogTemp,Log,TEXT("UiModel: %s guid=%llu camera=%s target=%s fov=%.1f headSocket=%d"),
                *Pair.Key,S.Guid,*Position.ToString(),*Focus.ToString(),Capture->FOVAngle,Body->DoesSocketExist(TEXT("WowPortraitHead")));
            S.bLogged=true;
        }
        Capture->SetWorldLocationAndRotation(Position,(Focus-Position).Rotation());
        if(S.Material.IsValid()) S.Material->SetTextureParameterValue(TEXT("Image"),S.Target.Get());
        else if(!S.PortraitArt.IsEmpty())
        {
            auto Brush=S.Image->GetBrush();
            Brush.SetResourceObject(S.Target.Get());
            S.Image->SetBrush(Brush);
        }
        S.PortraitArt.Empty();
        Capture->CaptureScene(); S.Captured=true; S.NextCapture=Now+.1;
    }
}
