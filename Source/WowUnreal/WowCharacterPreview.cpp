#include "WowCharacterPreview.h"
#include "WowWorldManager.h"
#include "WowCharacterBuilder.h"
#include "WowAnimationController.h"
#include "Coord/WowCoordinate.h"
#include "Formats/M2Types.h"
#include "WowDoodadManager.h"
#include "WowM2EmitterComponent.h"
#include "WowM2Lighting.h"
#include "Formats/Dbc/DbcStore.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/PointLightComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"

namespace
{
void AddItem(FWowCharacterBuilder::FCharacterParams& P,uint32 Display,uint8 Type,int32 Slot=INDEX_NONE,bool Hunter=false)
{
    if (!Display || ((Type==15 || Type==25 || Type==26) && !Hunter)) return;
    auto& B=P.BodyEquipment;
    using E=FWowEquipmentManager::EAttachmentPoint;
    switch (Type)
    {
    case 1: P.Equipment.Add({Display,E::Helmet}); break;
    case 3: P.Equipment.Add({Display,E::LeftShoulder}); P.Equipment.Add({Display,E::RightShoulder}); break;
    case 4: B.ShirtDisplayId=Display; break;
    case 5: case 20: B.ChestDisplayId=Display; B.bIsRobeEquipped=Type==20; break;
    case 6: B.BeltDisplayId=Display; break;
    case 7: B.PantsDisplayId=Display; break;
    case 8: B.BootsDisplayId=Display; break;
    case 9: B.BracersDisplayId=Display; break;
    case 10: B.GlovesDisplayId=Display; break;
    case 16: B.CapeDisplayId=Display; break;
    case 19: B.TabardDisplayId=Display; break;
    case 13: case 14: case 15: case 17: case 21: case 22: case 23: case 25: case 26:
    {
        const E Point=Type==14?E::ShieldInHand:(Slot==16 || Type==22 || Type==23 || (Hunter && Type==15))?E::LeftHand:E::RightHand;
        P.Equipment.RemoveAll([Point](const auto& Item){return Item.AttachPoint==Point;});
        P.Equipment.Add({Display,Point});break;
    }
    }
}
}

AWowCharacterPreview::AWowCharacterPreview()
{
    PrimaryActorTick.bCanEverTick=true;
    PrimaryActorTick.TickInterval=1.f/30;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
    CaptureComponent=CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("Capture"));
    CaptureComponent->SetupAttachment(RootComponent);
    CaptureComponent->CaptureSource=SCS_FinalColorLDR;
    CaptureComponent->bCaptureEveryFrame=false;
    CaptureComponent->bCaptureOnMovement=false;
    CaptureComponent->PrimitiveRenderMode=ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
    CaptureComponent->ViewLightingChannels.bViewChannel0=false;
    CaptureComponent->ViewLightingChannels.bViewChannel1=true;
    CaptureComponent->FOVAngle=30;
    CaptureComponent->ShowFlags.SetFog(false);
    CaptureComponent->ShowFlags.SetAtmosphere(false);
    CaptureComponent->ShowFlags.SetVolumetricFog(false);
    CaptureComponent->ShowFlags.SetMotionBlur(false);
    auto& Post=CaptureComponent->PostProcessSettings;
    Post.bOverride_AutoExposureMethod=true; Post.AutoExposureMethod=AEM_Manual;
    Post.bOverride_AutoExposureBias=true; Post.AutoExposureBias=0;
    Post.bOverride_AutoExposureApplyPhysicalCameraExposure=true; Post.AutoExposureApplyPhysicalCameraExposure=false;
    auto Light=[&](const TCHAR* Name,FVector Position,float Intensity,FColor Color)
    {
        auto* L=CreateDefaultSubobject<UPointLightComponent>(Name); L->SetupAttachment(RootComponent);
        L->SetMobility(EComponentMobility::Movable);
        L->SetRelativeLocation(Position); L->SetIntensity(Intensity); L->SetAttenuationRadius(2000);
        L->SetLightColor(Color);L->SetLightingChannels(true,false,false);
        L->ViewLightingChannels.bViewChannel0=false;L->ViewLightingChannels.bViewChannel1=true;return L;
    };
    KeyLight=Light(TEXT("Key"),FVector(-250,-200,350),10000,FColor(255,238,215));
    FillLight=Light(TEXT("Fill"),FVector(-150,250,200),5000,FColor(215,230,255));
    RimLight=Light(TEXT("Rim"),FVector(200,0,300),12000,FColor(190,215,255));
}
void AWowCharacterPreview::SetOriginalLighting(const TArray<FWowCreationLight>& Lights)
{
    KeyLight->SetIntensity(0);FillLight->SetIntensity(0);RimLight->SetIntensity(0);SceneAmbient=FLinearColor::Black;
    for(const auto& Light:OriginalLights) Light->SetIntensity(0);
    FVector Red=FVector::ZeroVector,Green=FVector::ZeroVector,Blue=FVector::ZeroVector,Direction=FVector::ZeroVector;
    FLinearColor Total=FLinearColor::Black;
    for(const auto& Source:Lights)
    {
        SceneAmbient+=Source.Ambient;
        const FVector D=FWowCoordinate::ModelToUE(Source.Direction).GetSafeNormal();
        const FLinearColor C=Source.Diffuse*Source.DiffuseIntensity;
        Red+=D*C.R;Green+=D*C.G;Blue+=D*C.B;Total+=C;
        Direction+=D*(C.R*.212671f+C.G*.71516f+C.B*.072169f);
    }
    // Original CM2Lighting::AddDiffuse / SetupSunlight merges directional
    // lights into one sun and adds the residual energy to ambient lighting.
    Direction=Direction.GetSafeNormal(UE_SMALL_NUMBER,FVector::DownVector);
    const FLinearColor Projected(FVector::DotProduct(Red,Direction),FVector::DotProduct(Green,Direction),FVector::DotProduct(Blue,Direction));
    SceneAmbient+=(Total-Projected)*.25f;
    SceneAmbient.A=1;
    FLinearColor Diffuse=Projected*1.25f-Total*.25f;
    Diffuse.R=FMath::Max(0.f,Diffuse.R);Diffuse.G=FMath::Max(0.f,Diffuse.G);Diffuse.B=FMath::Max(0.f,Diffuse.B);
    if(OriginalLights.IsEmpty())
    {
        auto* Light=NewObject<UDirectionalLightComponent>(this);AddInstanceComponent(Light);Light->SetupAttachment(RootComponent);
        Light->SetMobility(EComponentMobility::Movable);
        Light->SetLightingChannels(true,false,false);Light->SetCastShadows(false);
        Light->ViewLightingChannels.bViewChannel0=false;Light->ViewLightingChannels.bViewChannel1=true;
        Light->RegisterComponent();OriginalLights.Add(Light);
    }
    auto* Light=OriginalLights[0].Get();Light->SetRelativeRotation(Direction.Rotation());
    const float Bright=FMath::Max3(Diffuse.R,Diffuse.G,Diffuse.B);
    Light->SetLightColor(Bright>0?Diffuse/Bright:FLinearColor::Black);Light->SetIntensity(Bright*PI);
}
void AWowCharacterPreview::Setup(AWowWorldManager* Manager,int32 Width,int32 Height)
{
    ClearCharacter(); WorldManager=Manager;
    SetActorLocation(FVector(0,0,-500000));
    RenderTarget=NewObject<UTextureRenderTarget2D>(this);
    RenderTarget->ClearColor=FLinearColor(.018f,.023f,.035f,1);
    RenderTarget->InitAutoFormat(Width,Height); RenderTarget->UpdateResourceImmediate(true);
    CaptureComponent->TextureTarget=RenderTarget;
}
void AWowCharacterPreview::ShowCreation(const FWowCharacterAppearance& A)
{
    FWowCharacterInfo C; C.Race=A.Race; C.Class=A.Class; C.Gender=A.Gender;
    C.Skin=A.Skin; C.Face=A.Face; C.HairStyle=A.HairStyle; C.HairColor=A.HairColor; C.FacialHair=A.FacialHair;
    Request(C,true);
}
void AWowCharacterPreview::ShowCharacter(uint8 Race,uint8 Gender)
{ FWowCharacterInfo C; C.Race=Race; C.Class=1; C.Gender=Gender; Request(C,true); }
void AWowCharacterPreview::ShowCharacter(const FWowCharacterInfo& Character) { Request(Character,false); }
void AWowCharacterPreview::Request(const FWowCharacterInfo& Info,bool Starter)
{
    const bool ChangedModel=Requested.Race!=Info.Race || Requested.Gender!=Info.Gender;
    ClearCharacter(false); Requested=Info; bStarter=Starter; bPending=true; ++Revision;
    if (ChangedModel) ResetCamera();
}
void AWowCharacterPreview::Tick(float DT)
{
    Super::Tick(DT);
    if (bPending && FDbcStore::Get().IsDeferredLoaded()) Rebuild();
    if(SceneRenderer) {SceneRenderer->ExtendEmission(FPlatformTime::Seconds()+60);UpdateSceneAttachment();UpdateSceneLights();}
    if (IsReady()) CaptureNow();
}
void AWowCharacterPreview::Rebuild()
{
    bPending=false;
    if (!WorldManager || !WorldManager->GetMpqManager() || !WorldManager->GetAssetCache()) return;
    const auto& C=Requested;
    if(bCreationScene && !SetupCreationScene()) return;
    FWowCharacterBuilder::FCharacterParams P;
    P.Race=FWowCharacterBuilder::ERace(C.Race); P.Gender=FWowCharacterBuilder::EGender(C.Gender);
    P.Customization={C.Race,C.Gender,C.Skin,C.Face,C.HairStyle,C.HairColor,C.FacialHair};
    P.Customization.ClassId=C.Class;
    P.BodyEquipment.RaceId=C.Race; P.BodyEquipment.Gender=C.Gender;
    if (bStarter)
    {
        const auto* Outfit=FDbcStore::Get().CharStartOutfit().GetByRaceClassGender(C.Race,C.Class,C.Gender);
        if (!Outfit) return;
        for (int32 I=0; I<24; ++I) if (Outfit->ItemDisplayIds[I]>0)
        {
            const int32 Type=Outfit->ItemInventorySlots[I];
            // Original CharacterCreation::Dress hides helmets and equips the
            // hunter's ranged weapon instead of overlapping its melee weapon.
            if(Type==1 || (C.Class==3 && (Type==13 || Type==17 || Type==21 || Type==22))) continue;
            AddItem(P,Outfit->ItemDisplayIds[I],Type,INDEX_NONE,C.Class==3);
        }
    }
    else for (const auto& E:C.Equipment) if (E.DisplayId>0)
    {
        if(C.Class==3 && (E.InventoryType==13 || E.InventoryType==17 || E.InventoryType==21 || E.InventoryType==22)) continue;
        AddItem(P,E.DisplayId,E.InventoryType,E.Slot,C.Class==3);
    }
    CurrentCharacterActor=FWowCharacterBuilder::SpawnCharacterWithEquipment(GetWorld(),WorldManager->GetMpqManager(),
        WorldManager->GetAssetCache(),P,GetActorLocation(),FRotator(0,Yaw,0));
    if (!CurrentCharacterActor) return;
    // An Outer does not retain a UObject. Keep the controller alive for this
    // preview's entire lifetime, including engine garbage collections.
    CurrentAnimationController=FWowCharacterBuilder::GetAnimationController(CurrentCharacterActor);
    CurrentCharacterActor->SetActorEnableCollision(false);
    TInlineComponentArray<UPrimitiveComponent*> Components(CurrentCharacterActor);
    for (auto* Component:Components)
    {
        Component->SetLightingChannels(true,false,false);
        if(bCreationScene)
            for(int32 I=0;I<Component->GetNumMaterials();++I)
                if(auto* MID=Cast<UMaterialInstanceDynamic>(Component->GetMaterial(I))) MID->SetVectorParameterValue(TEXT("WowInteriorAmbient"),SceneAmbient);
        if (auto* Mesh=Cast<USkeletalMeshComponent>(Component)) Mesh->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    }
    CaptureComponent->ShowOnlyActorComponents(CurrentCharacterActor);
    if(SceneActor) CaptureComponent->ShowOnlyActorComponents(SceneActor);
    UpdateSceneAttachment();
    FBox Bounds(ForceInit);
    // M2's authored animation/culling box can be several body heights wide.
    // Frame the source body vertices instead of that conservative volume.
    if (auto* Animation=FWowCharacterBuilder::GetAnimationController(CurrentCharacterActor))
        if (const auto Model=Animation->GetRuntimeModel())
            if (auto* Mesh=CurrentCharacterActor->FindComponentByClass<USkeletalMeshComponent>())
                for (const auto& Vertex:Model->Vertices)
                    Bounds+=Mesh->GetComponentTransform().TransformPosition(FWowCoordinate::ModelToUE(Vertex.Position));
    if (!Bounds.IsValid) Bounds=CurrentCharacterActor->GetComponentsBoundingBox(true);
    Focus=Bounds.GetCenter()-GetActorLocation();
    const FVector Extent=Bounds.GetExtent();
    const float Aspect=float(RenderTarget->SizeX)/RenderTarget->SizeY;
    const float Tan=FMath::Tan(FMath::DegreesToRadians(CaptureComponent->FOVAngle*.5f));
    Distance=FMath::Max(Extent.Z*Aspect,FMath::Max(Extent.X,Extent.Y))/Tan*1.12f+Extent.X;
    PositionCamera();
}
bool AWowCharacterPreview::SetupCreationScene()
{
    uint8 Race=Requested.Race==7?3:Requested.Race==8?2:Requested.Race;
    const auto* R=FDbcStore::Get().ChrRaces().GetById(Race);
    const FString Token=Requested.Class==6?FDbcStore::Get().CharacterCreation().ClassToken(6):R?R->ClientFileString:FString();
    const FString Path=FString::Printf(TEXT("Interface\\Glues\\Models\\UI_%s\\UI_%s.m2"),*Token,*Token);
    if(SceneActor && ScenePath==Path) return true;
    if(SceneRenderer) PrimaryActorTick.RemovePrerequisite(SceneRenderer,SceneRenderer->PrimaryComponentTick);
    if(SceneActor) SceneActor->Destroy();SceneActor=nullptr;SceneRenderer=nullptr;
    for(auto& Light:ScenePointLights) if(Light) Light->DestroyComponent();
    ScenePointLights.Reset();SceneLightIndices.Reset();
    SceneModel=FWowDoodadManager::GetOrParseM2(Path,WorldManager->GetMpqManager());
    if(!SceneModel || SceneModel->Cameras.IsEmpty()) return false;
    SceneActor=GetWorld()->SpawnActor<AActor>();SceneActor->SetRootComponent(NewObject<USceneComponent>(SceneActor));
    SceneActor->GetRootComponent()->RegisterComponent();SceneActor->SetActorLocation(GetActorLocation());
    SceneRenderer=NewObject<UWowM2EmitterComponent>(SceneActor);SceneActor->AddInstanceComponent(SceneRenderer);
    SceneRenderer->SetupAttachment(SceneActor->GetRootComponent());SceneRenderer->RegisterComponent();SceneRenderer->SetComponentTickInterval(1.f/30);
    SceneCamera=FWowCoordinate::ModelToUE(SceneModel->Cameras[0].Position);SceneTarget=FWowCoordinate::ModelToUE(SceneModel->Cameras[0].Target);
    PositionCamera();
    if(!SceneRenderer->Initialize(SceneModel,WorldManager->GetMpqManager(),WorldManager->GetAssetCache(),60)) return false;
    SceneStartedAt=FPlatformTime::Seconds();
    // GlueXML replaces directional lights but retains the scene's authored
    // point lights, including the lights that illuminate Night Elf faces.
    auto* Falloff=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Materials/M_WowM2LightFunction"));
    for(int32 I=0;I<SceneModel->Lights.Num() && ScenePointLights.Num()<4;++I)
        if(SceneModel->Lights[I].Type==1 && Falloff)
        {
            auto* Light=NewObject<UPointLightComponent>(this);AddInstanceComponent(Light);Light->SetupAttachment(RootComponent);
            Light->SetMobility(EComponentMobility::Movable);Light->bUseInverseSquaredFalloff=false;
            // LightData.ush reserves exponent zero for inverse-square lights.
            // Keep the built-in mask flat; the light function supplies falloff.
            Light->LightFalloffExponent=.0001f;
            Light->SetIntensityUnits(ELightUnits::Unitless);Light->SetCastShadows(false);Light->SetLightingChannels(true,false,false);
            Light->ViewLightingChannels.bViewChannel0=false;Light->ViewLightingChannels.bViewChannel1=true;
            Light->SetLightFunctionMaterial(UMaterialInstanceDynamic::Create(Falloff,Light));
            Light->SetLightFunctionFadeDistance(20000);Light->SetLightFunctionDisabledBrightness(0);Light->RegisterComponent();
            ScenePointLights.Add(Light);SceneLightIndices.Add(I);
        }
    UpdateSceneLights();
    PrimaryActorTick.AddPrerequisite(SceneRenderer,SceneRenderer->PrimaryComponentTick);
    ScenePath=Path;
    return true;
}
void AWowCharacterPreview::UpdateSceneLights()
{
    if(!SceneModel) return;
    const float AgeMs=float((FPlatformTime::Seconds()-SceneStartedAt)*1000);
    for(int32 I=0;I<ScenePointLights.Num();++I)
    {
        auto* Light=ScenePointLights[I].Get();
        const auto Sample=FWowM2Lighting::Sample(*SceneModel,SceneLightIndices[I],AgeMs,AgeMs);
        const FVector Position=GetActorLocation()+Sample.Position;
        const float Bright=FMath::Max3(Sample.Diffuse.R,Sample.Diffuse.G,Sample.Diffuse.B);
        Light->SetWorldLocation(Position);Light->SetAttenuationRadius(Sample.Radius*FWowCoordinate::SCALE);
        Light->SetLightColor(Bright>0?Sample.Diffuse/Bright:FLinearColor::Black);Light->SetIntensity(PI*Bright);Light->SetVisibility(Sample.Visible);
        if(auto* Material=Cast<UMaterialInstanceDynamic>(Light->LightFunctionMaterial))
        {
            Material->SetVectorParameterValue(TEXT("SourcePosition"),FLinearColor(Position.X,Position.Y,Position.Z));
            Material->SetScalarParameterValue(TEXT("WorldScale"),FWowCoordinate::SCALE);
            Material->SetScalarParameterValue(TEXT("Start"),Sample.Start);Material->SetScalarParameterValue(TEXT("End"),Sample.End);
            Material->SetScalarParameterValue(TEXT("Radius"),Sample.Radius);Material->SetScalarParameterValue(TEXT("AuthoredRange"),Sample.End>Sample.Start && Sample.End>0?1:0);
        }
    }
}
void AWowCharacterPreview::UpdateSceneAttachment()
{
    if(!CurrentCharacterActor || !SceneModel || !SceneRenderer) return;
    const auto* Attachment=SceneModel->Attachments.FindByPredicate([](const auto& A){return A.Id==0;});
    if(!Attachment) return;
    const FTransform Bone=SceneRenderer->GetModelBoneTransform(Attachment->Bone);
    const FVector Position=Bone.TransformPosition(FWowCoordinate::ModelToUE(Attachment->Position));
    const FQuat Rotation=Bone.GetRotation()*FRotator(0,Yaw-90,0).Quaternion();
    CurrentCharacterActor->SetActorLocationAndRotation(GetActorLocation()+Position,Rotation);
    CurrentCharacterActor->SetActorScale3D(Bone.GetScale3D());
    const FVector Front=(SceneCamera-Position).GetSafeNormal2D();
    const FVector Right=FVector::CrossProduct(FVector::UpVector,Front);
    KeyLight->SetRelativeLocation(Position+Front*250-Right*200+FVector(0,0,350));
    FillLight->SetRelativeLocation(Position+Front*150+Right*250+FVector(0,0,200));
    RimLight->SetRelativeLocation(Position-Front*200+FVector(0,0,300));
}
void AWowCharacterPreview::PositionCamera()
{
    if (!CaptureComponent) return;
    if(bCreationScene && SceneModel && SceneModel->Cameras.Num())
    {
        const FVector Position=SceneCamera;
        const float Aspect=float(RenderTarget->SizeX)/RenderTarget->SizeY;
        // Original GxuXformCreateProjection_SG converts its diagonal-like
        // stored angle to vertical FOV by dividing by sqrt(aspect^2+1).
        const float Vertical=SceneModel->Cameras[0].Fov/FMath::Sqrt(Aspect*Aspect+1);
        CaptureComponent->FOVAngle=FMath::Clamp(FMath::RadiansToDegrees(2*FMath::Atan(FMath::Tan(Vertical*.5f)*Aspect))*ZoomFactor,12.f,110.f);
        CaptureComponent->SetRelativeLocation(Position);
        CaptureComponent->SetRelativeRotation((SceneTarget-Position).Rotation());
        if(SceneRenderer) SceneRenderer->SetCaptureView(CaptureComponent->GetComponentLocation(),CaptureComponent->GetComponentRotation());
        return;
    }
    CaptureComponent->FOVAngle=30;
    const FVector Position=Focus+FVector(-Distance*ZoomFactor,0,Distance*.025f);
    CaptureComponent->SetRelativeLocation(Position);
    CaptureComponent->SetRelativeRotation((Focus-Position).Rotation());
}
void AWowCharacterPreview::SetViewAspect(float Aspect)
{
    if(!bCreationScene || !RenderTarget || !FMath::IsFinite(Aspect) || Aspect<=0) return;
    const int32 Width=FMath::Clamp(FMath::RoundToInt(768*Aspect),256,2048);
    if(RenderTarget->SizeX==Width && RenderTarget->SizeY==768) return;
    RenderTarget->ResizeTarget(Width,768);PositionCamera();
}
void AWowCharacterPreview::Rotate(float Degrees)
{
    Yaw=FMath::UnwindDegrees(Yaw+Degrees);
    if (CurrentCharacterActor) {if(SceneRenderer) UpdateSceneAttachment();else CurrentCharacterActor->SetActorRotation(FRotator(0,Yaw,0));}
}
void AWowCharacterPreview::Zoom(float Steps) { ZoomFactor=FMath::Clamp(ZoomFactor*FMath::Pow(.88f,Steps),.35f,1.6f); PositionCamera(); }
void AWowCharacterPreview::ResetCamera() { Yaw=180; ZoomFactor=1; if (CurrentCharacterActor) CurrentCharacterActor->SetActorRotation(FRotator(0,Yaw,0)); PositionCamera(); }
void AWowCharacterPreview::ClearCharacter(bool ClearScene)
{
    bPending=false;
    if (CaptureComponent) CaptureComponent->ClearShowOnlyComponents();
    if (IsValid(CurrentCharacterActor)) CurrentCharacterActor->Destroy();
    CurrentCharacterActor=nullptr;
    CurrentAnimationController=nullptr;
    if(ClearScene)
    {
        for(auto& Light:ScenePointLights) if(Light) Light->DestroyComponent();
        ScenePointLights.Reset();SceneLightIndices.Reset();
        if(SceneRenderer) PrimaryActorTick.RemovePrerequisite(SceneRenderer,SceneRenderer->PrimaryComponentTick);
        if(SceneActor) SceneActor->Destroy();SceneActor=nullptr;SceneRenderer=nullptr;SceneModel.Reset();ScenePath.Reset();
    }
    if (RenderTarget) RenderTarget->UpdateResourceImmediate(true);
}
void AWowCharacterPreview::CaptureNow()
{
    if (!IsActorBeingDestroyed() && CaptureComponent && RenderTarget && CurrentCharacterActor) CaptureComponent->CaptureScene();
}
void AWowCharacterPreview::EndPlay(const EEndPlayReason::Type Reason)
{
    ClearCharacter(); if (CaptureComponent) CaptureComponent->TextureTarget=nullptr;
    RenderTarget=nullptr; WorldManager=nullptr; Super::EndPlay(Reason);
}
