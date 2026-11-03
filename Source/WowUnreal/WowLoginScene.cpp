#include "WowLoginScene.h"
#include "WowWorldManager.h"
#include "WowAudioManager.h"
#include "WowDoodadManager.h"
#include "WowM2Lighting.h"
#include "WowM2EmitterComponent.h"
#include "Formats/M2Types.h"
#include "Mpq/MpqManager.h"
#include "Coord/WowCoordinate.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Internationalization/Regex.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "HAL/IConsoleManager.h"

AWowLoginScene::AWowLoginScene()
{
    PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.TickGroup=TG_PostUpdateWork;
    PrimaryActorTick.TickInterval=1.f/60;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
    Capture=CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("Capture"));Capture->SetupAttachment(RootComponent);
    Capture->CaptureSource=SCS_FinalColorLDR;Capture->bCaptureEveryFrame=false;Capture->bCaptureOnMovement=false;
    Capture->PrimitiveRenderMode=ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
    Capture->ShowFlags.SetFog(false);Capture->ShowFlags.SetAtmosphere(false);Capture->ShowFlags.SetVolumetricFog(false);
    Capture->ShowFlags.SetMotionBlur(false);Capture->ShowFlags.SetTonemapper(false);Capture->ShowFlags.SetBloom(false);
    auto& P=Capture->PostProcessSettings;P.bOverride_AutoExposureMethod=true;P.AutoExposureMethod=AEM_Manual;
    P.bOverride_AutoExposureBias=true;P.AutoExposureBias=0;P.bOverride_AutoExposureApplyPhysicalCameraExposure=true;P.AutoExposureApplyPhysicalCameraExposure=false;
}
bool AWowLoginScene::Setup(AWowWorldManager* Manager,AWowAudioManager* Audio)
{
    if(!Manager || !Manager->GetMpqManager()) return false;
    TArray<uint8> Bytes;if(!Manager->GetMpqManager()->ReadFile(TEXT("Interface\\GlueXML\\AccountLogin.lua"),Bytes)) return false;
    FUTF8ToTCHAR Text(reinterpret_cast<const ANSICHAR*>(Bytes.GetData()),Bytes.Num());
    // Use the non-streaming-trial branch of the user's original Glue script.
    FRegexMatcher Match(FRegexPattern(TEXT("else[\\s\\S]*?AccountLogin:SetModel\\(\"([^\"]+)\"\\)")),FString(Text.Length(),Text.Get()));
    if(!Match.FindNext()) return false;const FString Path=Match.GetCaptureGroup(1).Replace(TEXT("\\\\"),TEXT("\\"));
    Model=FWowDoodadManager::GetOrParseM2(Path,Manager->GetMpqManager());
    if(!Model || Model->Cameras.IsEmpty()) return false;
    SetActorLocation(FVector(0,0,-1000000));AudioManager=Audio;
    Target=NewObject<UTextureRenderTarget2D>(this);Target->ClearColor=FLinearColor::Black;Target->TargetGamma=1;
    // Original FFX uses a clamped 8-bit display-space render target. This is
    // separate from the linear character-preview captures.
    Target->InitCustomFormat(1024,768,PF_B8G8R8A8,true);Target->UpdateResourceImmediate(true);Capture->TextureTarget=Target;
    auto Quarter=[&](){auto* RT=NewObject<UTextureRenderTarget2D>(this);RT->TargetGamma=1;RT->InitCustomFormat(256,192,PF_B8G8R8A8,true);RT->UpdateResourceImmediate(true);return RT;};
    QuarterA=Quarter();QuarterB=Quarter();
    auto Material=[&](const TCHAR* Path){auto* Parent=LoadObject<UMaterialInterface>(nullptr,Path);return Parent?UMaterialInstanceDynamic::Create(Parent,this):nullptr;};
    Box4=Material(TEXT("/Game/Materials/M_WowGlueBox4"));GaussH=Material(TEXT("/Game/Materials/M_WowGlueGauss4"));GaussV=Material(TEXT("/Game/Materials/M_WowGlueGauss4"));Composite=Material(TEXT("/Game/Materials/M_WowGlueComposite"));
    if(!Box4 || !GaussH || !GaussV || !Composite) return false;
    Box4->SetTextureParameterValue(TEXT("Source"),Target);GaussH->SetTextureParameterValue(TEXT("Source"),QuarterA);GaussV->SetTextureParameterValue(TEXT("Source"),QuarterB);
    Composite->SetTextureParameterValue(TEXT("Source"),Target);Composite->SetTextureParameterValue(TEXT("Blur"),QuarterA);
    TArray<uint8> Xml;if(!Manager->GetMpqManager()->ReadFile(TEXT("Interface\\GlueXML\\AccountLogin.xml"),Xml)) return false;
    const FUTF8ToTCHAR XmlText(reinterpret_cast<const ANSICHAR*>(Xml.GetData()),Xml.Num());
    FRegexMatcher Glow(FRegexPattern(TEXT("<ModelFFX[^>]*name=\"AccountLogin\"[^>]*glow=\"([0-9.]+)\"")),FString(XmlText.Length(),XmlText.Get()));
    if(!Glow.FindNext()) return false;
    SourceGlow=uint8(FMath::Clamp(FCString::Atof(*Glow.GetCaptureGroup(1)),0.f,1.f)*255)/255.f;
    Renderer=NewObject<UWowM2EmitterComponent>(this);AddInstanceComponent(Renderer);Renderer->SetupAttachment(RootComponent);Renderer->RegisterComponent();
    Renderer->ConfigureGlueScene();
    Renderer->SetComponentTickInterval(1.f/60);
    UpdateCamera();
    if(!Renderer->Initialize(Model,Manager->GetMpqManager(),Manager->GetAssetCache(),60,true)) return false;
    Capture->ShowOnlyActorComponents(this);PrimaryActorTick.AddPrerequisite(Renderer,Renderer->PrimaryComponentTick);
    Started=FPlatformTime::Seconds();UpdateCamera();Capture->CaptureScene();ApplyGlow();
    UE_LOG(LogTemp,Log,TEXT("LOGIN_SCENE_SOURCE: %s sequence=0 camera=0 duration=%u bones=%d passes=%d emitters=%d events=%d"),*Path,
        Model->Animations.IsEmpty()?0:Model->Animations[0].Length,Model->Bones.Num(),Renderer->GetGeometryCount(),Renderer->GetEmitterCount(),Model->Events.Num());
    return true;
}
float AWowLoginScene::GetAgeMs() const {return Started>0?float((FPlatformTime::Seconds()-Started)*1000):0;}
FVector AWowLoginScene::GetCameraPosition() const {return Capture->GetRelativeLocation();}
void AWowLoginScene::SetViewAspect(float Aspect)
{
    if(!Target || !FMath::IsFinite(Aspect) || Aspect<=0) return;
    const int32 Width=FMath::Clamp(FMath::RoundToInt(768*Aspect),256,2560);
    if(Target->SizeX!=Width){Target->ResizeTarget(Width,768);QuarterA->ResizeTarget(Width/4,192);QuarterB->ResizeTarget(Width/4,192);UpdateCamera();}
}
void AWowLoginScene::UpdateCamera()
{
    if(!Model || Model->Cameras.IsEmpty() || !Target) return;
    const auto& C=Model->Cameras[0];const float Age=GetAgeMs();
    const float Time=Model->Animations.Num() && Model->Animations[0].Length?FMath::Fmod(Age,float(Model->Animations[0].Length)):Age;
    const FVector P=FWowCoordinate::ModelToUE(C.PositionPivot+C.PositionTrack.Sample(Time,Model->GlobalSequenceDurations,FVector::ZeroVector));
    const FVector T=FWowCoordinate::ModelToUE(C.TargetPivot+C.TargetTrack.Sample(Time,Model->GlobalSequenceDurations,FVector::ZeroVector));
    FRotator Rotation=(T-P).Rotation();Rotation.Roll=-FMath::RadiansToDegrees(C.RollTrack.Sample(Time,Model->GlobalSequenceDurations,0.f));
    const float Aspect=float(Target->SizeX)/Target->SizeY,Vertical=C.Fov/FMath::Sqrt(Aspect*Aspect+1);
    Capture->FOVAngle=FMath::RadiansToDegrees(2*FMath::Atan(FMath::Tan(Vertical*.5f)*Aspect));
    Capture->bOverride_CustomNearClippingPlane=true;Capture->CustomNearClippingPlane=FMath::Max(.1f,C.NearClip*FWowCoordinate::SCALE);
    Capture->SetRelativeLocationAndRotation(P,Rotation);
    if(Renderer)
    {
        Renderer->SetCaptureView(Capture->GetComponentLocation(),Capture->GetComponentRotation());
        // AccountLogin retains the M2 directional light; unlike character
        // creation GlueParent does not replace it with RaceLights.
        for(int32 I=0;I<Model->Lights.Num();++I) if(Model->Lights[I].Type==0)
        {const auto Light=FWowM2Lighting::Sample(*Model,I,Age,Age);Renderer->SetGlueLighting(Light.Ambient,Light.Diffuse,Light.Direction);break;}
    }
}
void AWowLoginScene::Tick(float Delta)
{
    Super::Tick(Delta);if(!Renderer || !Model) return;
    Renderer->ExtendEmission(FPlatformTime::Seconds()+60);UpdateCamera();
    const float Age=GetAgeMs(),Length=Model->Animations.Num()?Model->Animations[0].Length:0;
    if(AudioManager.IsValid() && Length>0)
        for(const auto& E:Model->Events) if(E.Token==TEXT("$DSO") && E.Times.Num())
            for(const uint32 Stamp:E.Times[0])
            {
                const int32 First=FMath::Max(0,FMath::FloorToInt((LastEventMs-Stamp)/Length)+1);
                const float At=First*Length+Stamp;
                if(At>LastEventMs && At<=Age) AudioManager->PlayGlueEffect(E.Data);
            }
    LastEventMs=Age;Capture->CaptureScene();ApplyGlow();
}
void AWowLoginScene::ApplyGlow()
{
    if(!Composite) return;
    const auto* Bloom=IConsoleManager::Get().FindConsoleVariable(TEXT("r.BloomQuality"));
    const bool Enabled=!Bloom || Bloom->GetInt()>0;
    Composite->SetScalarParameterValue(TEXT("Glow"),Enabled?SourceGlow:0);
    if(!Enabled) return;
    Box4->SetVectorParameterValue(TEXT("Step"),FLinearColor(1.f/Target->SizeX,1.f/Target->SizeY,0,0));
    GaussH->SetVectorParameterValue(TEXT("Step"),FLinearColor(1.f/QuarterA->SizeX,0,0,0));
    GaussV->SetVectorParameterValue(TEXT("Step"),FLinearColor(0,1.f/QuarterA->SizeY,0,0));
    UKismetRenderingLibrary::DrawMaterialToRenderTarget(this,QuarterA,Box4);
    UKismetRenderingLibrary::DrawMaterialToRenderTarget(this,QuarterB,GaussH);
    UKismetRenderingLibrary::DrawMaterialToRenderTarget(this,QuarterA,GaussV);
}
void AWowLoginScene::EndPlay(const EEndPlayReason::Type Reason)
{
    if(AudioManager.IsValid()) AudioManager->StopGlueEffects();
    if(Renderer){PrimaryActorTick.RemovePrerequisite(Renderer,Renderer->PrimaryComponentTick);Renderer->DestroyComponent();}Renderer=nullptr;
    Capture->ClearShowOnlyComponents();Capture->TextureTarget=nullptr;Target=nullptr;QuarterA=nullptr;QuarterB=nullptr;
    Box4=nullptr;GaussH=nullptr;GaussV=nullptr;Composite=nullptr;Model.Reset();AudioManager.Reset();
    Super::EndPlay(Reason);
}
