#include "WowCharacterCreationTestGameMode.h"
#include "WowCharacterPreview.h"
#include "WowCharacterCreateWidget.h"
#include "WowCharacterBuilder.h"
#include "WowAnimationController.h"
#include "Formats/Dbc/DbcStore.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonSerializer.h"
#include "UnrealClient.h"
#include "UObject/UObjectIterator.h"
#include "HAL/PlatformMemory.h"
#include "Formats/M2Types.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/Material.h"
#include "ProceduralMeshComponent.h"

AWowCharacterCreationTestGameMode::AWowCharacterCreationTestGameMode()
{ PrimaryActorTick.bCanEverTick=true; HUDClass=nullptr; }
void AWowCharacterCreationTestGameMode::SetupTestScene(UWorld*,AWowWorldManager*)
{
    Started=FPlatformTime::Seconds(); FString Spec,Text; TSharedPtr<FJsonObject> Root;
    FParse::Value(FCommandLine::Get(),TEXT("creationsteps="),Spec); FParse::Value(FCommandLine::Get(),TEXT("creationoutput="),Output);
    if(Output.IsEmpty() || !FFileHelper::LoadFileToString(Text,*Spec) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Root))
    {Finish(false,TEXT("Missing capture specification"));return;}
    const TArray<TSharedPtr<FJsonValue>>* Rows=nullptr;
    if(!Root->TryGetArrayField(TEXT("cases"),Rows) || Rows->IsEmpty()) {Finish(false,TEXT("Empty cases"));return;}
    Cases=*Rows;
}
void AWowCharacterCreationTestGameMode::OpenScreen()
{
    Preview=GetWorld()->SpawnActor<AWowCharacterPreview>(); Preview->Setup(CachedWorldManager,1024,768); Preview->EnableCreationScene();
    Widget=SNew(SWowCharacterCreateWidget); Widget->SetPreview(Preview);
    GEngine->GameViewport->AddViewportWidgetContent(Widget.ToSharedRef(),100);
    Widget->Tick(FGeometry(),0,0);
}
void AWowCharacterCreationTestGameMode::CloseScreen()
{
    if(CalibrationCard) CalibrationCard->DestroyComponent(); CalibrationCard=nullptr;
    if(Widget && GEngine && GEngine->GameViewport) GEngine->GameViewport->RemoveViewportWidgetContent(Widget.ToSharedRef());
    Widget.Reset(); if(Preview) Preview->Destroy(); Preview=nullptr;
}
void AWowCharacterCreationTestGameMode::StartCase()
{
    if(CalibrationCard) CalibrationCard->DestroyComponent(); CalibrationCard=nullptr;
    if(++Index>=Cases.Num()) {CloseScreen(); Phase=4; Changed=FPlatformTime::Seconds();return;}
    const auto C=Cases[Index]->AsObject(); bool Reopen=false; C->TryGetBoolField(TEXT("reopen"),Reopen);
    if(Reopen || !Widget) {CloseScreen();OpenScreen();}
    FWowCharacterAppearance A; A.Race=C->GetIntegerField(TEXT("race")); A.Class=C->GetIntegerField(TEXT("class")); A.Gender=C->GetIntegerField(TEXT("gender"));
    Widget->SetAppearance(A); A=Widget->GetAppearance();
    double Option=-1;
    if(C->TryGetNumberField(TEXT("option"),Option) && Option>=0 && Option<5)
    {
        const auto Before=Widget->GetAppearance();
        for(int32 I=0;I<3;++I) Widget->PressStockControl(FName(*FString::Printf(TEXT("Appearance_%d_1"),int32(Option))));
        A=Widget->GetAppearance();
        if(A==Before) {Finish(false,TEXT("Stock appearance button did not apply its selection"));return;}
    }
    bool IdentityControls=false;C->TryGetBoolField(TEXT("identityControls"),IdentityControls);
    if(IdentityControls)
    {
        Widget->SetAppearance(FWowCharacterAppearance());
        Widget->PressStockControl(TEXT("Race_11"));Widget->PressStockControl(TEXT("Class_7"));Widget->PressStockControl(TEXT("Gender_1"));
        A=Widget->GetAppearance();
        if(A.Race!=11 || A.Class!=7 || A.Gender!=1) {Finish(false,TEXT("Stock race/class/gender buttons did not apply"));return;}
    }
    bool Last=false; C->TryGetBoolField(TEXT("last"),Last);
    if(Last)
    {
        auto Pick=[&](EWowAppearanceOption O,uint8& V) {const auto Values=Widget->GetOptions(O); if(Values.Num()) V=Values.Last(); Widget->SetAppearance(A); A=Widget->GetAppearance();};
        Pick(EWowAppearanceOption::Skin,A.Skin);Pick(EWowAppearanceOption::Face,A.Face);
        Pick(EWowAppearanceOption::HairStyle,A.HairStyle);Pick(EWowAppearanceOption::HairColor,A.HairColor);Pick(EWowAppearanceOption::FacialHair,A.FacialHair);
        Widget->SetAppearance(A);
    }
    double Zoom=0,Yaw=0; C->TryGetNumberField(TEXT("zoom"),Zoom); C->TryGetNumberField(TEXT("yaw"),Yaw);
    Preview->ResetCamera(); Preview->Zoom(Zoom); Preview->Rotate(Yaw);
    Phase=1; Changed=FPlatformTime::Seconds(); Bones.Reset();
}
void AWowCharacterCreationTestGameMode::Tick(float DT)
{
    Super::Tick(DT); if(Finished) return; const double Now=FPlatformTime::Seconds();
    if(Now-Started>500) {Finish(false,TEXT("Capture timeout"));return;}
    if(!FDbcStore::Get().IsDeferredLoaded()) return;
    if(Index<0) {StartCase();return;}
    if(Phase==1)
    {
        if(!Widget->IsAppearanceValid()) {Finish(false,TEXT("Invalid original appearance"));return;}
        if(!Preview->IsReady()) {if(Now-Changed>20) Finish(false,TEXT("No rendered model"));return;}
        if(Now-Changed<1) return;
        bool Calibrate=false; Cases[Index]->AsObject()->TryGetBoolField(TEXT("calibrate"),Calibrate);
        if(Calibrate && !CalibrationCard)
        {
            CalibrationCard=NewObject<UProceduralMeshComponent>(Preview); Preview->AddInstanceComponent(CalibrationCard);
            CalibrationCard->SetupAttachment(Preview->CaptureComponent); CalibrationCard->RegisterComponent();
            CalibrationCard->SetCollisionEnabled(ECollisionEnabled::NoCollision); CalibrationCard->SetCastShadow(false);
            auto* Material=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Materials/M_WowM2Opaque"));
            int32 Section=0;
            for(const float Gray:{.05f,.214041f,.5f,1.f})
            {
                const float Y=-8.f+Section*4.f;
                const TArray<FVector> Positions={FVector(40,Y,9),FVector(40,Y+4,9),FVector(40,Y+4,13),FVector(40,Y,13)};
                CalibrationCard->CreateMeshSection_LinearColor(Section,Positions,{0,1,2,0,2,3},{},{FVector2D(0),FVector2D(0),FVector2D(0),FVector2D(0)},
                    {FLinearColor(Gray,Gray,Gray),FLinearColor(Gray,Gray,Gray),FLinearColor(Gray,Gray,Gray),FLinearColor(Gray,Gray,Gray)},{},false,false);
                CalibrationCard->SetMaterial(Section++,Material);
            }
            Preview->CaptureComponent->ShowOnlyComponent(CalibrationCard);
        }
        bool Collection=false;Cases[Index]->AsObject()->TryGetBoolField(TEXT("collect"),Collection);
        if(Collection) CollectGarbage(RF_NoFlags,true);
        auto* Animation=FWowCharacterBuilder::GetAnimationController(Preview->GetCharacterActor());
        if(auto* Mesh=Animation?Animation->GetModelMesh():nullptr) Bones=Mesh->GetComponentSpaceTransforms();
        else {Finish(false,TEXT("Missing skeletal body"));return;}
        ObservedAnimation=false;Phase=2;Changed=Now;return;
    }
    if(Phase==2)
    {
        auto* Animation=FWowCharacterBuilder::GetAnimationController(Preview->GetCharacterActor());
        if(auto* Mesh=Animation?Animation->GetModelMesh():nullptr)
        {
            const auto& Sample=Mesh->GetComponentSpaceTransforms();
            for(int32 I=0;I<FMath::Min(Bones.Num(),Sample.Num());++I)
                if(!Bones[I].Equals(Sample[I],.0001f)) {ObservedAnimation=true;break;}
        }
        if(Now-Changed<1) return;
        const auto A=Widget->GetAppearance(); const auto& C=Preview->GetShownCharacter();
        if(A.Race!=C.Race || A.Gender!=C.Gender || A.Class!=C.Class || A.Skin!=C.Skin || A.Face!=C.Face || A.HairStyle!=C.HairStyle || A.HairColor!=C.HairColor || A.FacialHair!=C.FacialHair)
        {Finish(false,TEXT("Stale rendered selection"));return;}
        auto* Actor=Preview->GetCharacterActor();
        const bool Animated=ObservedAnimation;
        if(!Animated || !FWowCharacterBuilder::GetAnimationController(Actor)) {Finish(false,TEXT("Idle pose did not animate"));return;}
        int32 Actors=0,Rigs=0; for(TActorIterator<AWowCharacterPreview> It(GetWorld());It;++It) if(!It->IsActorBeingDestroyed()) ++Rigs;
        for(TActorIterator<AActor> It(GetWorld());It;++It) if(!It->IsActorBeingDestroyed() && It->FindComponentByClass<USkeletalMeshComponent>()) ++Actors;
        if(Rigs!=1 || Actors!=1) {Finish(false,TEXT("Stale model/preview actors retained"));return;}
        int32 ExpectedPoints=0;
        if(Preview->SceneModel) for(const auto& Light:Preview->SceneModel->Lights) if(Light.Type==1) ++ExpectedPoints;
        if(Preview->ScenePointLights.Num()!=FMath::Min(ExpectedPoints,4)) {Finish(false,TEXT("Missing original scene point lights"));return;}
        for(const auto& Light:Preview->ScenePointLights)
            if(Light->bUseInverseSquaredFalloff || Light->LightFalloffExponent<=0 || Light->LightFalloffExponent>.001f)
            {Finish(false,TEXT("Original falloff replaced by engine inverse-square attenuation"));return;}
        if(Preview->CaptureComponent->ViewLightingChannels.GetMaskForStruct()!=2) {Finish(false,TEXT("Preview light/view isolation changed"));return;}
        auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("name"),Cases[Index]->AsObject()->GetStringField(TEXT("name")));
        Row->SetNumberField(TEXT("race"),A.Race);Row->SetNumberField(TEXT("class"),A.Class);Row->SetNumberField(TEXT("gender"),A.Gender);
        Row->SetNumberField(TEXT("skin"),A.Skin);Row->SetNumberField(TEXT("face"),A.Face);Row->SetNumberField(TEXT("hairStyle"),A.HairStyle);Row->SetNumberField(TEXT("hairColor"),A.HairColor);Row->SetNumberField(TEXT("facialHair"),A.FacialHair);
        Row->SetBoolField(TEXT("animated"),Animated);Row->SetNumberField(TEXT("liveModels"),Actors);Row->SetNumberField(TEXT("revision"),Preview->GetRevision());
        double Option=-1;Cases[Index]->AsObject()->TryGetNumberField(TEXT("option"),Option);
        Row->SetNumberField(TEXT("stockAppearanceOption"),Option);
        bool IdentityControls=false;Cases[Index]->AsObject()->TryGetBoolField(TEXT("identityControls"),IdentityControls);
        Row->SetBoolField(TEXT("stockIdentityControlsInvoked"),IdentityControls);
        Row->SetStringField(TEXT("sceneAmbient"),Preview->SceneAmbient.ToString());
        Row->SetNumberField(TEXT("viewLightMask"),Preview->CaptureComponent->ViewLightingChannels.GetMaskForStruct());
        if(CalibrationCard)
        {
            const FIntPoint Size=GEngine->GameViewport->Viewport->GetSizeXY();
            const double Projection=Size.X/(80.*FMath::Tan(FMath::DegreesToRadians(Preview->CaptureComponent->FOVAngle*.5f)));
            TArray<TSharedPtr<FJsonValue>> Samples;
            for(int32 I=0;I<4;++I)
            {
                const int32 Expected[]={63,128,188,255}; auto Sample=MakeShared<FJsonObject>();
                Sample->SetNumberField(TEXT("x"),Size.X*.5+(-6+I*4)*Projection);
                Sample->SetNumberField(TEXT("y"),Size.Y*.5-11*Projection);
                Sample->SetNumberField(TEXT("expected"),Expected[I]); Samples.Add(MakeShared<FJsonValueObject>(Sample));
            }
            Row->SetArrayField(TEXT("grayCardSamples"),Samples);
        }
        const auto& Flags=Preview->CaptureComponent->ShowFlags;
        Row->SetStringField(TEXT("lightingFlags"),FString::Printf(TEXT("lighting=%d direct=%d deferred=%d point=%d directional=%d function=%d shadows=%d"),Flags.Lighting,Flags.DirectLighting,Flags.DeferredLighting,Flags.PointLights,Flags.DirectionalLights,Flags.LightFunctions,Flags.DynamicShadows));
        TArray<TSharedPtr<FJsonValue>> Lights;
        for(const auto& Light:Preview->ScenePointLights)
            Lights.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("%s intensity=%g radius=%g visible=%d inverse=%d exponent=%g affectsWorld=%d"),*Light->GetRelativeLocation().ToString(),Light->Intensity,Light->AttenuationRadius,Light->IsVisible(),Light->bUseInverseSquaredFalloff,Light->LightFalloffExponent,Light->bAffectsWorld)));
        Row->SetArrayField(TEXT("scenePointLights"),Lights);
        if(Animation)
            if(auto* Mesh=Animation->GetModelMesh())
                for(int32 I=0;I<Mesh->GetNumMaterials();++I)
                    if(auto* Material=Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(I)))
                    {
                        FLinearColor Ambient;
                        Row->SetBoolField(TEXT("ambientParameterPresent"),Material->GetVectorParameterValue(TEXT("WowInteriorAmbient"),Ambient));
                        Row->SetStringField(TEXT("materialAmbient"),Ambient.ToString());
                        Row->SetNumberField(TEXT("materialShadingModels"),Material->GetMaterial()->GetShadingModels().GetShadingModelField());
                    }
        Results.Add(MakeShared<FJsonValueObject>(Row));
        const FString File=Output/(Row->GetStringField(TEXT("name"))+TEXT(".png")); FScreenshotRequest::RequestScreenshot(File,true,false);
        Phase=3;Changed=Now;return;
    }
    if(Phase==3 && Now-Changed>.4 && !FScreenshotRequest::IsScreenshotRequested()) {StartCase();return;}
    if(Phase==4)
    {
        if(Now-Changed<.2) return;
        CollectGarbage(RF_NoFlags,true);
        int32 Rigs=0,Models=0;
        for(TActorIterator<AWowCharacterPreview> It(GetWorld());It;++It) ++Rigs;
        for(TActorIterator<AActor> It(GetWorld());It;++It) if(It->FindComponentByClass<USkeletalMeshComponent>()) ++Models;
        if(Rigs || Models) {Finish(false,TEXT("Destroyed screen retained a preview actor"));return;}
        if(Stress>=12)
        {
            // A finite repeated-screen plateau, not a claim about unlimited play.
            if(Retained.Num()>4)
            {
                uint64 Min=MAX_uint64,Max=0;
                for(int32 I=4;I<Retained.Num();++I) {Min=FMath::Min(Min,Retained[I]);Max=FMath::Max(Max,Retained[I]);}
                if(Max-Min>96ull*1024*1024) {Finish(false,TEXT("Post-collection memory grew beyond 96 MiB"));return;}
            }
            Finish(true);return;
        }
        Retained.Add(FPlatformMemory::GetStats().UsedPhysical);
        OpenScreen();
        // Twenty queued switches in one frame: only the final model may build.
        for(uint8 Race:FDbcStore::Get().CharacterCreation().Races()) for(uint8 Gender:{uint8(0),uint8(1)})
        {FWowCharacterAppearance A;A.Race=Race;A.Class=1;A.Gender=Gender; if(!FDbcStore::Get().CharacterCreation().Allows(Race,1)) A.Class=2;Widget->SetAppearance(A);}
        Phase=5;Changed=Now;return;
    }
    if(Phase==5 && Now-Changed>.3 && Preview->IsReady()) {CloseScreen();++Stress;Phase=4;Changed=Now;}
}
void AWowCharacterCreationTestGameMode::Finish(bool Passed,const FString& Failure)
{
    if(Finished) return;Finished=true;CloseScreen();
    auto Root=MakeShared<FJsonObject>();Root->SetBoolField(TEXT("passed"),Passed);Root->SetStringField(TEXT("failure"),Failure);
    Root->SetArrayField(TEXT("cases"),Results);Root->SetNumberField(TEXT("screenCycles"),Stress);
    TArray<TSharedPtr<FJsonValue>> Memory;for(uint64 Value:Retained) Memory.Add(MakeShared<FJsonValueNumber>(double(Value)/(1024*1024)));
    Root->SetArrayField(TEXT("retainedMiB"),Memory);FString Text;FJsonSerializer::Serialize(Root,TJsonWriterFactory<>::Create(&Text));
    FFileHelper::SaveStringToFile(Text,*(Output/TEXT("runtime.json")));
    FPlatformMisc::RequestExitWithStatus(false,Passed?0:3);
}
void AWowCharacterCreationTestGameMode::EndPlay(const EEndPlayReason::Type Reason) {CloseScreen();Super::EndPlay(Reason);}
