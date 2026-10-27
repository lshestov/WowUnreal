#include "WowGameplayController.h"
#include "WowConnectionManager.h"
#include "WowUiTestPeer.h"
#include "WowGameUI.h"
#include "WowUIManager.h"
#include "WowInventoryManager.h"
#include "WowLuaVM.h"
#include "WowFrameManager.h"
#include "WowWorldCollision.h"
#include "WowWorldManager.h"
#include "Formats/Dbc/DbcStore.h"
#include "WowSkyManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "RenderTimer.h"
#include "DynamicRHI.h"
#include "Coord/WowCoordinate.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/CanvasPanelSlot.h"
#include "Engine/World.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "UnrealClient.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/IInputProcessor.h"

// Native probe callbacks drive the test. Consume physical input in this test
// process so typing or moving the mouse in other apps cannot steer its player.
class FWowProbeInputGuard final : public IInputProcessor
{
public:
    bool SyntheticText=false;
    void Tick(float,FSlateApplication&,TSharedRef<ICursor>) override {}
    bool HandleKeyDownEvent(FSlateApplication&,const FKeyEvent&) override { return !SyntheticText; }
    bool HandleKeyUpEvent(FSlateApplication&,const FKeyEvent&) override { return !SyntheticText; }
    bool HandleAnalogInputEvent(FSlateApplication&,const FAnalogInputEvent&) override { return true; }
    bool HandleMouseMoveEvent(FSlateApplication&,const FPointerEvent&) override { return true; }
    bool HandleMouseButtonDownEvent(FSlateApplication&,const FPointerEvent&) override { return true; }
    bool HandleMouseButtonUpEvent(FSlateApplication&,const FPointerEvent&) override { return true; }
    bool HandleMouseButtonDoubleClickEvent(FSlateApplication&,const FPointerEvent&) override { return true; }
    bool HandleMouseWheelOrGestureEvent(FSlateApplication&,const FPointerEvent&,const FPointerEvent*) override { return true; }
};

// Explicit development probe. It invokes the same controller/UI callbacks as
// ordinary input, waits for server state, captures rendered frames and exits.
// The opt-in QA peer can create/delete its own coordinator-owned character.
// Database setup and restoration are performed by the external coordinator.
struct FWowGameplayProbe
{
    TSharedPtr<FJsonObject> Spec;
    TArray<TSharedPtr<FJsonValue>> Steps, Results;
    FString Output, Failure;
    int32 Index=0;
    double Started=FPlatformTime::Seconds(), StepStarted=0;
    bool bInvoked=false, bFinished=false;
    bool bVerifiedCharacter=false;
    bool bJumpRequested=false,bJumpObserved=false;
    double FailureExitAt=0;
    uint32 LastEquippedItem=0;
    TWeakObjectPtr<UPrimitiveComponent> Avoiding;
    float AvoidSide=1;
    TMap<FString,TArray<double>> Profile;
    TSharedPtr<FJsonObject> Inspections=MakeShared<FJsonObject>();
    TSharedPtr<FWowProbeInputGuard> InputGuard;
    TUniquePtr<FWowUiTestPeer> Peer;
    TSharedPtr<FJsonObject> FinalReport;
    int32 PeerExitCode=-1;

    explicit FWowGameplayProbe(const FString& Path)
    {
        if(FSlateApplication::IsInitialized()) { InputGuard=MakeShared<FWowProbeInputGuard>(); FSlateApplication::Get().RegisterInputPreProcessor(InputGuard,0); }
        FString Text;
        if (!FFileHelper::LoadFileToString(Text,*Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Spec) || !Spec.IsValid())
        { Failure=TEXT("Invalid probe specification"); return; }
        const TArray<TSharedPtr<FJsonValue>>* Values=nullptr;
        if (!Spec->TryGetArrayField(TEXT("steps"),Values)) { Failure=TEXT("Missing steps"); return; }
        Steps=*Values; Output=Spec->GetStringField(TEXT("output"));
        if (Output.IsEmpty() || !IFileManager::Get().MakeDirectory(*Output,true)) Failure=TEXT("Invalid output directory");
    }
    ~FWowGameplayProbe() { if(InputGuard && FSlateApplication::IsInitialized()) FSlateApplication::Get().UnregisterInputPreProcessor(InputGuard); }

    void Finish(AWowGameplayController& Controller,bool Passed)
    {
        if (bFinished) return;
        bFinished=true;
        if (auto* Character=Cast<ACharacter>(Controller.GetPawn())) Character->GetCharacterMovement()->StopMovementImmediately();
        Controller.StopAutoAttack();
        const auto Report=MakeShared<FJsonObject>();
        Report->SetBoolField(TEXT("passed"),Passed); Report->SetStringField(TEXT("failure"),Failure);
        Report->SetNumberField(TEXT("completedSteps"),Index); Report->SetArrayField(TEXT("steps"),Results);
        Report->SetObjectField(TEXT("ui"),Inspections);
        Report->SetBoolField(TEXT("jumpObserved"),bJumpObserved);
        Report->SetStringField(TEXT("character"),Controller.ConnectionManager?Controller.ConnectionManager->GetCharacterName():TEXT(""));
        if (const auto* Character=Cast<ACharacter>(Controller.GetPawn()))
        {
            const auto Position=FWowCoordinate::UEToWow(Character->GetActorLocation());
            Report->SetNumberField(TEXT("x"),Position.X); Report->SetNumberField(TEXT("y"),Position.Y); Report->SetNumberField(TEXT("z"),Position.Z);
            const auto* Movement=Character->GetCharacterMovement();
            Report->SetNumberField(TEXT("floorNormalZ"),Movement->CurrentFloor.HitResult.ImpactNormal.Z);
            Report->SetBoolField(TEXT("walkableFloor"),Movement->CurrentFloor.IsWalkableFloor());
            FHitResult Hit; FCollisionQueryParams Query(SCENE_QUERY_STAT(ProbeBlocker),false,Character);
            const auto* Capsule=Character->GetCapsuleComponent();
            if (Controller.GetWorld()->SweepSingleByChannel(Hit,Character->GetActorLocation(),Character->GetActorLocation()+Character->GetActorForwardVector()*100,
                FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(),Capsule->GetScaledCapsuleHalfHeight()),Query))
            {
                Report->SetStringField(TEXT("blocker"),GetNameSafe(Hit.GetComponent()));
                Report->SetNumberField(TEXT("blockerNormalZ"),Hit.ImpactNormal.Z);
                Report->SetNumberField(TEXT("blockerShapeNormalZ"),Hit.Normal.Z);
                Report->SetBoolField(TEXT("blockerAllowsStepUp"),Hit.GetComponent() && Hit.GetComponent()->CanCharacterStepUp(const_cast<ACharacter*>(Character)));
                Report->SetBoolField(TEXT("startPenetrating"),Hit.bStartPenetrating);
            }
        }
        FString Text; FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Text));
        if (!Output.IsEmpty()) FFileHelper::SaveStringToFile(Text,*(Output/TEXT("probe.json")));
        UE_LOG(LogTemp,Log,TEXT("GameplayProbe: %s step=%d reason=%s"),Passed?TEXT("PASS"):TEXT("FAIL"),Index,*Failure);
        if(Peer)
        {
            FinalReport=Report; PeerExitCode=Passed?0:3; Peer->Stop();
            if(!Passed && !Output.IsEmpty()) FScreenshotRequest::RequestScreenshot(Output/TEXT("failure.png"),true,false);
            FailureExitAt=FPlatformTime::Seconds()+(Passed?0:2); return;
        }
        if (Passed) FPlatformMisc::RequestExitWithStatus(false,0);
        else
        {
            if (!Output.IsEmpty()) FScreenshotRequest::RequestScreenshot(Output/TEXT("failure.png"),true,false);
            FailureExitAt=FPlatformTime::Seconds()+2;
        }
    }

    uint64 FindEntry(AWowGameplayController& C,uint32 Entry,bool IncludeDead=false)
    {
        double Closest=DBL_MAX; uint64 Guid=0;
        for (const auto& Pair : C.SpawnedEntityActors)
        {
            const auto* Entity=C.ConnectionManager->PacketHandler.EntityManager.Find(Pair.Key);
            if (!Entity || Entity->Entry!=Entry || !Entity->IsUnit() || (!IncludeDead && Entity->GetHealth()<=0) || !IsValid(Pair.Value)) continue;
            const double Distance=FVector::DistSquared(C.GetPawn()->GetActorLocation(),Pair.Value->GetActorLocation());
            if (Distance<Closest) { Closest=Distance; Guid=Pair.Key; }
        }
        return Guid;
    }

    bool Walk(AWowGameplayController& C,const FVector& Point,float Range,bool AvoidObstacles=true)
    {
        APawn* Pawn=C.GetPawn();
        FVector Delta=Point-Pawn->GetActorLocation(); Delta.Z=0;
        if (!Delta.IsNearlyZero()) { const FRotator Facing=Delta.Rotation(); Pawn->SetActorRotation(Facing); C.SetControlRotation(Facing); }
        if (Delta.Size()<=Range*100)
        {
            if (auto* Character=Cast<ACharacter>(Pawn)) Character->GetCharacterMovement()->StopMovementImmediately();
            return true;
        }
        FVector Direction=Delta.GetSafeNormal();
        if (auto* Character=Cast<ACharacter>(Pawn); Character && AvoidObstacles)
        {
            // Drive normal movement around nearby trunks/walls instead of
            // treating a straight test route as a path through solid objects.
            const auto* Capsule=Character->GetCapsuleComponent();
            FCollisionQueryParams Query(SCENE_QUERY_STAT(GameplayProbeWalk),false,Character);
            if (const auto* TargetActor=C.SpawnedEntityActors.Find(C.TargetGuid); TargetActor && IsValid(*TargetActor)) Query.AddIgnoredActor(TargetActor->Get());
            FHitResult Hit;
            const float Radius=Capsule->GetScaledCapsuleRadius()+5;
            const float Step=Character->GetCharacterMovement()->MaxStepHeight;
            const float HalfHeight=FMath::Max(Radius,(Capsule->GetScaledCapsuleHalfHeight()*2-Step-4)*.5f);
            const FVector Start=Pawn->GetActorLocation()+FVector(0,0,Step*.5f);
            const auto Shape=FCollisionShape::MakeCapsule(Radius,HalfHeight);
            if (C.GetWorld()->SweepSingleByChannel(Hit,Start,Start+Direction*180,FQuat::Identity,ECC_Pawn,Shape,Query)
                && Hit.ImpactNormal.Z<.5f)
            {
                FVector Tangent=FVector::CrossProduct(FVector::UpVector,Hit.ImpactNormal).GetSafeNormal2D();
                if (Avoiding.Get()!=Hit.GetComponent()) { Avoiding=Hit.GetComponent(); AvoidSide=FVector::DotProduct(Tangent,Direction)>=0?1.f:-1.f; }
                Direction=(Tangent*AvoidSide+Direction*.15f).GetSafeNormal2D();
            }
            else Avoiding.Reset();
        }
        Pawn->AddMovementInput(Direction,1); return false;
    }

    void Tick(AWowGameplayController& C)
    {
        if(Peer) Peer->Tick();
        if (bFinished)
        {
            if(Peer && PeerExitCode>=0)
            {
                if(!Peer->Complete() || FPlatformTime::Seconds()<FailureExitAt) return;
                FinalReport->SetBoolField(TEXT("peerCleanup"),Peer->Error().IsEmpty());
                if(!Peer->Error().IsEmpty()) {FinalReport->SetBoolField(TEXT("passed"),false); FinalReport->SetStringField(TEXT("failure"),Peer->Error()); PeerExitCode=3;}
                FString Text; FJsonSerializer::Serialize(FinalReport.ToSharedRef(),TJsonWriterFactory<>::Create(&Text)); FFileHelper::SaveStringToFile(Text,*(Output/TEXT("probe.json")));
                FPlatformMisc::RequestExitWithStatus(false,uint8(PeerExitCode)); return;
            }
            if (FailureExitAt>0 && FPlatformTime::Seconds()>=FailureExitAt) FPlatformMisc::RequestExitWithStatus(false,3);
            return;
        }
        const double Now=FPlatformTime::Seconds();
        if(Peer && !Peer->Error().IsEmpty()) Failure=Peer->Error();
        if (!Failure.IsEmpty()) { Finish(C,false); return; }
        if (Now-Started>600) { Failure=TEXT("Overall timeout"); Finish(C,false); return; }
        auto* Connection=C.ConnectionManager.Get();
        if (!Connection || !C.GameUI || !C.UIManager || !C.GetPawn()) return;
        if(!bVerifiedCharacter)
        {
            if(!Connection->PacketHandler.EntityManager.GetLocalPlayer()) return;
            if (Connection->GetCharacterName()!=Spec->GetStringField(TEXT("character"))) { Failure=TEXT("Verification character mismatch"); Finish(C,false); return; }
            bVerifiedCharacter=true;
        }
        if (bJumpRequested) if (const auto* Character=Cast<ACharacter>(C.GetPawn()); Character && Character->GetCharacterMovement()->IsFalling()) bJumpObserved=true;
        if (Index>=Steps.Num()) { Finish(C,true); return; }
        const auto Step=Steps[Index]->AsObject();
        const FString Op=Step->GetStringField(TEXT("op"));
        double Timeout=60; Step->TryGetNumberField(TEXT("timeout"),Timeout);
        if (StepStarted==0) { StepStarted=Now; UE_LOG(LogTemp,Log,TEXT("GameplayProbe: step %d %s"),Index,*Op); }
        if (Now-StepStarted>Timeout) { Failure=FString::Printf(TEXT("Step %d timed out: %s"),Index,*Op); Finish(C,false); return; }
        auto& Handler=Connection->PacketHandler;
        bool Done=false;
        if(Op==TEXT("peer_start") || Op==TEXT("peer_cleanup"))
        {
            if(!Peer)
            {
                Peer=MakeUnique<FWowUiTestPeer>();
                if(!Peer->Start(Spec->GetStringField(TEXT("peerFixture")),Handler.EntityManager.LocalPlayerGuid,Op==TEXT("peer_cleanup"))) {Failure=TEXT("Invalid coordinator-owned QA peer fixture"); Finish(C,false); return;}
            }
            if(Op==TEXT("peer_cleanup")) Peer->Stop();
            Done=Op==TEXT("peer_cleanup")?Peer->Complete():Peer->Ready();
            if(Done && Op==TEXT("peer_start"))
            {
                Connection->SendNameQuery(int64(Peer->Guid()));
                C.UIManager->GetLuaVM()->ExecuteString(FString::Printf(TEXT("QAUiPeerGuid=%llu; QAUiPeerName='%s'"),Peer->Guid(),*Peer->Get()->GetCharacterName()));
            }
        }
        else if(Op==TEXT("peer_target")) {if(Peer && Peer->Ready() && Handler.EntityManager.Find(Peer->Guid())) {C.SetTarget(Peer->Guid()); Done=true;}}
        else if(Op==TEXT("peer_trade") || Op==TEXT("peer_command"))
        {
            if(Peer) {double Slot=0,Value=0; Step->TryGetNumberField(TEXT("slot"),Slot); Step->TryGetNumberField(TEXT("value"),Value); Done=Peer->Command(Step->GetStringField(TEXT("action")),int32(Slot),uint32(Value));}
        }
        else if(Op==TEXT("peer_assert"))
        {
            if(!Peer || !Peer->Ready()) {Failure=TEXT("QA peer is not ready"); Finish(C,false); return;}
            const auto* N=Peer->Get(); const FString Field=Step->GetStringField(TEXT("field")); int64 Value=-1;
            if(Field==TEXT("money")) {if(const auto* P=N->PacketHandler.EntityManager.GetLocalPlayer()) Value=P->GetCoinage();}
            else if(Field==TEXT("trade_open")) Value=N->PacketHandler.CurrentTrade.bTradeOpen?1:0;
            else if(Field==TEXT("accepted")) Value=N->PacketHandler.CurrentTrade.bLocalAccepted?1:0;
            else if(Field==TEXT("item_count")) {Value=0; const uint32 Entry=Step->GetIntegerField(TEXT("entry")); for(const auto* Item:N->PacketHandler.EntityManager.GetInventoryItems()) if(Item->Entry==Entry) Value+=Item->GetStackCount();}
            if(Value!=int64(Step->GetNumberField(TEXT("value")))) {Failure=TEXT("QA peer assertion differs from server state"); Finish(C,false); return;} Done=true;
        }
        else if (Op==TEXT("wait")) Done=Now-StepStarted>=Step->GetNumberField(TEXT("seconds"));
        else if(Op==TEXT("wait_character_select")) Done=Connection->GetState()==EWowSessionState::WorldHaveCharList;
        else if(Op==TEXT("enter_character"))
        {
            for(const auto& Character:Connection->GetCachedCharacters()) if(Character.Name==Spec->GetStringField(TEXT("character"))) { Connection->EnterWorld(Character.Guid); Done=true; break; }
        }
        else if(Op==TEXT("wait_world"))
        {
            const auto* Manager=Cast<AWowWorldManager>(UGameplayStatics::GetActorOfClass(&C,AWowWorldManager::StaticClass()));
            Done=Connection->GetState()==EWowSessionState::WorldInGame && Handler.EntityManager.GetLocalPlayer()
                && Manager && Manager->IsInitialLoadComplete() && !C.bDeferSpawnTeleport && !C.bHasDeferredSpawn;
        }
        else if (Op==TEXT("time"))
        {
            if (auto* Sky=Cast<AWowSkyManager>(UGameplayStatics::GetActorOfClass(C.GetWorld(),AWowSkyManager::StaticClass())))
            { Sky->TimeOfDay=FMath::Clamp(float(Step->GetNumberField(TEXT("minutes"))),0.f,1439.9f); Sky->TimeSpeed=0; Done=true; }
        }
        else if (Op==TEXT("profile"))
        {
            // UE's frame/thread counters describe the preceding frame. The
            // first observation predates this interval (often UI bootstrap).
            if(!bInvoked) {bInvoked=true; return;}
            Profile.FindOrAdd(TEXT("frameMs")).Add(FApp::GetDeltaTime()*1000);
            Profile.FindOrAdd(TEXT("gameThreadMs")).Add(FPlatformTime::ToMilliseconds(GGameThreadTime));
            Profile.FindOrAdd(TEXT("renderThreadMs")).Add(FPlatformTime::ToMilliseconds(GRenderThreadTime));
            Profile.FindOrAdd(TEXT("gpuMs")).Add(FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles()));
            Profile.FindOrAdd(TEXT("uiMs")).Add(C.LastUiMilliseconds);
            Profile.FindOrAdd(TEXT("uiOnUpdateMs")).Add(C.LastOnUpdateMilliseconds);
            Profile.FindOrAdd(TEXT("uiHoverMs")).Add(C.LastHoverMilliseconds);
            Profile.FindOrAdd(TEXT("uiLayoutMs")).Add(C.LastLayoutMilliseconds);
            Profile.FindOrAdd(TEXT("uiWidgetsMs")).Add(C.LastWidgetMilliseconds);
            if (auto* Lua=C.UIManager->GetLuaVM()) Profile.FindOrAdd(TEXT("luaMiB")).Add(Lua->GetMemoryUsage()/(1024.0*1024));
            Done=Now-StepStarted>=FMath::Clamp(Step->GetNumberField(TEXT("seconds")),1.0,60.0);
        }
        else if (Op==TEXT("select_entry"))
        {
            bool IncludeDead=false; Step->TryGetBoolField(TEXT("includeDead"),IncludeDead);
            const uint64 Guid=FindEntry(C,Step->GetIntegerField(TEXT("entry")),IncludeDead);
            if (Guid) { C.SetTarget(Guid); Done=true; }
        }
        else if (Op==TEXT("pick_target"))
        {
            const uint64 Guid=FindEntry(C,Step->GetIntegerField(TEXT("entry")));
            const auto* Actor=C.SpawnedEntityActors.Find(Guid);
            if (Actor && IsValid(*Actor)) for (float Height : {50.f,100.f,150.f})
            {
                FVector2D Point;
                if (C.ProjectWorldLocationToScreen((*Actor)->GetActorLocation()+FVector(0,0,Height),Point))
                {
                    C.TryTargetAtScreen(Point.X,Point.Y);
                    if (C.TargetGuid==Guid) { Done=true; break; }
                }
            }
        }
        else if (Op==TEXT("approach"))
        {
            const auto* Actor=C.SpawnedEntityActors.Find(C.TargetGuid);
            double Range=3; Step->TryGetNumberField(TEXT("range"),Range);
            if (Actor && IsValid(*Actor)) Done=Walk(C,(*Actor)->GetActorLocation(),Range);
        }
        else if (Op==TEXT("assert_position"))
        {
            const auto Values=Step->GetArrayField(TEXT("position"));
            if(Values.Num()!=3 || !C.GetPawn()) {Failure=TEXT("Invalid position assertion"); Finish(C,false); return;}
            const FVector Expected(Values[0]->AsNumber(),Values[1]->AsNumber(),Values[2]->AsNumber());
            FVector Feet=C.GetPawn()->GetActorLocation(); if(const auto* Character=Cast<ACharacter>(C.GetPawn())) Feet.Z-=Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
            double Tolerance=1.5; Step->TryGetNumberField(TEXT("tolerance"),Tolerance); Tolerance=FMath::Clamp(Tolerance,.01,10.);
            const auto* Player=C.ConnectionManager->PacketHandler.EntityManager.GetLocalPlayer();
            Done=Player && FVector::Distance(FWowCoordinate::UEToWow(Feet),Expected)<=Tolerance && FVector::Distance(Player->Movement.Position,Expected)<=Tolerance;
        }
        else if (Op==TEXT("walk"))
        {
            const auto Values=Step->GetArrayField(TEXT("position"));
            bool Direct=false; Step->TryGetBoolField(TEXT("direct"),Direct);
            Done=Walk(C,FWowCoordinate::WowToUE(Values[0]->AsNumber(),Values[1]->AsNumber(),Values[2]->AsNumber()),1.5f,!Direct);
        }
        else if (Op==TEXT("survey"))
        {
            const auto Min=Step->GetArrayField(TEXT("min")), Max=Step->GetArrayField(TEXT("max"));
            const double Spacing=Step->GetNumberField(TEXT("spacing"));
            if (Spacing<.25 || Min.Num()!=3 || Max.Num()!=3 || Max[0]->AsNumber()-Min[0]->AsNumber()>100 || Max[1]->AsNumber()-Min[1]->AsNumber()>100)
            { Failure=TEXT("Invalid bounded floor survey"); Finish(C,false); return; }
            const double Width=Max[0]->AsNumber()-Min[0]->AsNumber(), Height=Max[1]->AsNumber()-Min[1]->AsNumber();
            if (Width<0 || Height<0 || (Width/Spacing+1)*(Height/Spacing+1)>4096)
            { Failure=TEXT("Floor survey exceeds the sample budget"); Finish(C,false); return; }
            TArray<TSharedPtr<FJsonValue>> Points;
            for (double X=Min[0]->AsNumber(); X<=Max[0]->AsNumber(); X+=Spacing)
                for (double Y=Min[1]->AsNumber(); Y<=Max[1]->AsNumber(); Y+=Spacing)
                {
                    FHitResult Hit;
                    const FVector Feet=FWowCoordinate::WowToUE(X,Y,Min[2]->AsNumber());
                    if (!FWowWorldCollision::FindFloor(C.GetWorld(),Feet,C.GetPawn(),Hit,(Max[2]->AsNumber()-Min[2]->AsNumber())*100,500,35,88)) continue;
                    const auto Point=MakeShared<FJsonObject>();
                    Point->SetNumberField(TEXT("x"),X); Point->SetNumberField(TEXT("y"),Y); Point->SetNumberField(TEXT("z"),Hit.ImpactPoint.Z/100);
                    Point->SetStringField(TEXT("surface"),GetNameSafe(Hit.GetComponent()));
                    Point->SetNumberField(TEXT("normalZ"),Hit.ImpactNormal.Z);
                    FCollisionQueryParams Query(SCENE_QUERY_STAT(ProbeSurvey),false,C.GetPawn());
                    const bool Blocked=C.GetWorld()->OverlapBlockingTestByChannel(Hit.ImpactPoint+FVector(0,0,90),FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(35,88),Query);
                    Point->SetBoolField(TEXT("blocked"),Blocked);
                    Points.Add(MakeShared<FJsonValueObject>(Point));
                }
            const auto Report=MakeShared<FJsonObject>(); Report->SetArrayField(TEXT("points"),Points);
            FString Text; FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Text));
            Done=FFileHelper::SaveStringToFile(Text,*(Output/(Step->GetStringField(TEXT("name"))+TEXT(".json"))));
        }
        else if (Op==TEXT("interact")) { C.InteractWithTarget(); Done=true; }
        else if (Op==TEXT("jump")) { if (auto* Character=Cast<ACharacter>(C.GetPawn())) { bJumpRequested=true; bJumpObserved=false; Character->Jump(); Done=true; } }
        else if (Op==TEXT("release_jump")) { if (auto* Character=Cast<ACharacter>(C.GetPawn())) { Character->StopJumping(); Done=true; } }
        else if (Op==TEXT("wait_landed")) { const auto* Character=Cast<ACharacter>(C.GetPawn()); Done=bJumpObserved && Character && Character->GetCharacterMovement()->IsMovingOnGround() && Now-StepStarted>.5; if(Done)bJumpRequested=false; }
        else if(Op==TEXT("wait_floor")) { const auto* Character=Cast<ACharacter>(C.GetPawn()); Done=Character && Character->GetCharacterMovement()->IsMovingOnGround() && Character->GetCharacterMovement()->CurrentFloor.IsWalkableFloor(); }
        else if(Op==TEXT("assert_map")) {const auto* Manager=Cast<AWowWorldManager>(UGameplayStatics::GetActorOfClass(&C,AWowWorldManager::StaticClass())); const uint32 Id=Step->GetIntegerField(TEXT("id")); const auto* Map=FDbcStore::Get().Maps().GetById(Id); if(!Manager || !Map || Handler.CurrentMapId!=Id || Manager->GetMapName()!=Map->InternalName || !Manager->IsInitialLoadComplete()) {Failure=TEXT("Rendered terrain does not match the server map"); Finish(C,false); return;} Done=true;}
        else if (Op==TEXT("wait_menu")) Done=(Handler.GossipMenu.Guid==C.TargetGuid && (Handler.GossipMenu.Quests.Num() || Handler.GossipMenu.Options.Num())) || (Handler.QuestDetails.QuestGiverGuid==C.TargetGuid && C.GameUI->GetShownQuestId()!=0);
        else if(Op==TEXT("gossip_option"))
        {
            const FString Text=Step->GetStringField(TEXT("textContains"));
            if(Handler.GossipMenu.Guid==C.TargetGuid) for(const auto& Option:Handler.GossipMenu.Options) if(!Text.IsEmpty() && Option.Text.Contains(Text,ESearchCase::IgnoreCase))
            {C.GameUI->SelectGossipOption(Option.Id); Done=true; break;}
        }
        else if (Op==TEXT("quest")) { if (C.GameUI->GetShownQuestId()!=uint32(Step->GetIntegerField(TEXT("id")))) C.GameUI->SelectGossipQuest(Step->GetIntegerField(TEXT("id"))); Done=true; }
        else if (Op==TEXT("wait_dialog")) Done=C.GameUI->GetShownQuestId()==uint32(Step->GetIntegerField(TEXT("id")));
        else if (Op==TEXT("accept")) { C.GameUI->AcceptQuest(); Done=true; }
        else if (Op==TEXT("abandon_quest")) { Connection->SendQuestAbandon(Step->GetIntegerField(TEXT("id"))); Done=true; }
        else if (Op==TEXT("complete")) { C.GameUI->CompleteQuest(); Done=true; }
        else if (Op==TEXT("wait_quest")) Done=Handler.QuestLog.ContainsByPredicate([Id=Step->GetIntegerField(TEXT("id"))](const auto& Quest){return Quest.QuestId==uint32(Id);});
        else if (Op==TEXT("wait_quest_removed")) Done=!Handler.QuestLog.ContainsByPredicate([Id=Step->GetIntegerField(TEXT("id"))](const auto& Quest){return Quest.QuestId==uint32(Id);});
        else if (Op==TEXT("attack")) { C.CastSpellFromSlot(0); Done=true; }
        else if (Op==TEXT("click_action"))
        {
            const FString Name=FString::Printf(TEXT("%s%d"),Connection->GetBonusActionOffset()>0?TEXT("BonusActionButton"):TEXT("ActionButton"),Step->GetIntegerField(TEXT("index")));
            auto* Frames=C.UIManager->GetFrameManager(); const auto Handle=Frames->FindFrame(Name);
            if (Handle>=0 && Frames->IsFrameDrawn(Handle))
            {
                const auto Rect=Frames->GetLayoutRect(Handle); const float Scale=Frames->GetScreenScale();
                if (Frames->HitTestFrames((Rect.X+Rect.Z/2)*Scale,(Rect.Y+Rect.W/2)*Scale)!=Handle)
                { Failure=TEXT("Visible action button does not match its click rectangle"); Finish(C,false); return; }
                Frames->DispatchMouseDown(Handle,TEXT("LeftButton"));
                if (Frames->IsFrameClickRegistered(Handle,TEXT("LeftButton"),true)) Frames->DispatchClick(Handle,TEXT("LeftButton"),true);
                Frames->DispatchMouseUp(Handle,TEXT("LeftButton"));
                if (Frames->IsFrameClickRegistered(Handle,TEXT("LeftButton"),false)) Frames->DispatchClick(Handle,TEXT("LeftButton"),false);
                Done=true;
            }
        }
        else if (Op==TEXT("wait_dead"))
        {
            const auto* Target=Handler.EntityManager.Find(C.TargetGuid); Done=Target && Target->GetHealth()==0;
            if (!Done) if (const auto* Actor=C.SpawnedEntityActors.Find(C.TargetGuid); Actor && IsValid(*Actor)) Walk(C,(*Actor)->GetActorLocation(),2.5f);
        }
        else if (Op==TEXT("wait_loot")) Done=Handler.Loot.Guid==C.TargetGuid;
        else if (Op==TEXT("loot_all")) { C.GameUI->LootAll(); Done=true; }
        else if (Op==TEXT("wait_loot_empty")) Done=Handler.Loot.Guid && !Handler.Loot.Gold && !Handler.Loot.Items.ContainsByPredicate([](const auto& I){return !I.bLooted;});
        else if (Op==TEXT("close_loot")) { C.GameUI->CloseLoot(); Done=true; }
        else if (Op==TEXT("close_dialog")) { C.GameUI->HideQuestDialog(); Done=true; }
        else if (Op==TEXT("equip"))
        {
            const auto Inventory=C.UIManager->GetInventoryManager();
            if (Inventory.IsValid()) for (const auto& Item : Inventory->GetBackpackItems())
                if (Item.ItemEntry==uint32(Step->GetIntegerField(TEXT("id")))) { LastEquippedItem=Item.ItemEntry; Inventory->AutoEquipItem(Item.Bag,Item.Slot); Done=true; break; }
        }
        else if (Op==TEXT("wait_equipped"))
        {
            const auto Inventory=C.UIManager->GetInventoryManager();
            Done=Inventory.IsValid() && Inventory->GetEquippedItems().ContainsByPredicate([Id=Step->GetIntegerField(TEXT("id"))](const auto& Item){return Item.ItemEntry==uint32(Id);});
        }
        else if (Op==TEXT("unequip"))
        {
            const auto Inventory=C.UIManager->GetInventoryManager();
            const int32 Slot=Step->GetIntegerField(TEXT("slot"));
            if (Inventory.IsValid() && Slot>=0 && Slot<19)
            {
                Inventory->AutoStoreItem(255,Slot,255); Done=true;
            }
        }
        else if (Op==TEXT("wait_unequipped"))
        {
            const auto Inventory=C.UIManager->GetInventoryManager();
            const int32 Slot=Step->GetIntegerField(TEXT("slot"));
            Done=Inventory.IsValid() && Slot>=0 && Slot<19 && Inventory->GetEquippedItem(Slot)->IsEmpty();
        }
        else if(Op==TEXT("talent_state"))
        {
            const auto& Info=Connection->PacketHandler; const auto* TalentPlayer=Info.EntityManager.GetLocalPlayer();
            bool Valid=TalentPlayer && Info.HasTalentInfo && Info.TalentGroups.Num()>0 && Info.TalentGroups.IsValidIndex(Info.ActiveTalentGroup);
            if(Valid)
            {
                Valid=Info.FreeTalentPoints==TalentPlayer->GetField(PlayerField::CHARACTER_POINTS1);
                for(uint32 I=0;I<6;++I) Valid&=Info.TalentGroups[Info.ActiveTalentGroup].Glyphs[I]==TalentPlayer->GetField(PlayerField::GLYPHS_1+I);
            }
            if(!Valid) { Failure=TEXT("Talent packet state disagrees with player update fields"); Finish(C,false); return; }
            auto Row=MakeShared<FJsonObject>(); Row->SetNumberField(TEXT("groups"),Info.TalentGroups.Num()); Row->SetNumberField(TEXT("active"),Info.ActiveTalentGroup+1);
            Row->SetNumberField(TEXT("freePoints"),Info.FreeTalentPoints); Row->SetNumberField(TEXT("talents"),Info.Talents.Num()); Row->SetNumberField(TEXT("glyphsEnabled"),TalentPlayer->GetField(PlayerField::GLYPHS_ENABLED));
            Inspections->SetObjectField(TEXT("talent-state"),Row); Done=true;
        }
        else if (Op==TEXT("bags")) { C.UIManager->ToggleBagWindow(); C.UIManager->ToggleCharacterPanel(); Done=true; }
        else if (Op==TEXT("lua")) { Done=C.UIManager->GetLuaVM()->ExecuteString(Step->GetStringField(TEXT("code")),TEXT("=GameplayProbe")); if (!Done) { Failure=TEXT("Lua assertion failed"); Finish(C,false); return; } }
        else if(Op==TEXT("drop_action_cursor")) { Done=Connection->DropActionCursor(); if(!Done) { Failure=TEXT("No action is held by the cursor"); Finish(C,false); return; } }
        else if(Op==TEXT("request_delete_cursor")) { Done=C.RequestCursorItemDeletion(); if(!Done) { Failure=TEXT("No valid item is held by the cursor"); Finish(C,false); return; } }
        else if(Op==TEXT("click_link") || Op==TEXT("hover_link"))
        {
            auto* Frames=C.UIManager->GetFrameManager(); const int64 Handle=Frames->FindFrame(Step->GetStringField(TEXT("name"))); FVector2D Point;
            if(Frames->MessageHyperlinkPoint(Handle,Step->GetStringField(TEXT("data")),Point))
            {
                Frames->UpdateMouseHover(Point.X,Point.Y); C.GameplayProbePointer=Point;
                if(Op==TEXT("click_link")) {FString Button=TEXT("LeftButton"); Step->TryGetStringField(TEXT("button"),Button); Frames->DispatchMouseDown(Handle,Button); Frames->DispatchMouseUp(Handle,Button);}
                Done=true;
            }
        }
        else if (Op==TEXT("click_frame") || Op==TEXT("drag_frame") || Op==TEXT("hover_frame") || Op==TEXT("wheel_frame"))
        {
            auto* Frames=C.UIManager->GetFrameManager();
            FString Name,Global;
            if(Step->TryGetStringField(TEXT("nameFromGlobal"),Global)) Name=C.UIManager->GetLuaVM()->GetGlobalString(Global);
            else Name=Step->GetStringField(TEXT("name"));
            const int64 Handle=Frames->FindFrame(Name);
            if(Handle<0 || !Frames->IsFrameDrawn(Handle)) { Failure=FString::Printf(TEXT("UI frame '%s' is not visible"),*Name); Finish(C,false); return; }
            const auto Rect=Frames->GetLayoutRect(Handle); const float Scale=Frames->GetScreenScale();
            const float X=(Rect.X+Rect.Z*.5f)*Scale,Y=(Rect.Y+Rect.W*.5f)*Scale;
            const int64 Hit=Frames->HitTestFrames(X,Y,Op==TEXT("wheel_frame")); bool Allowed=Hit==Handle;
            if(Op==TEXT("wheel_frame")) for(int64 Parent=Hit,Depth=0;Parent>=0 && Depth<64;++Depth,Parent=Frames->GetParentHandle(Parent)) Allowed|=Parent==Handle;
            if(!Allowed) { Failure=FString::Printf(TEXT("UI frame %s is obscured by %s"),*Name,*Frames->GetFrameName(Hit)); Finish(C,false); return; }
            Frames->UpdateMouseHover(X,Y);
            C.GameplayProbePointer=FVector2D(X,Y);
            if(Op==TEXT("hover_frame") || Op==TEXT("wheel_frame"))
            {
                if(Op==TEXT("wheel_frame")) Frames->DispatchMouseWheel(Hit,Step->GetNumberField(TEXT("delta")));
                Done=true;
            }
            else
            {
            FString Button=TEXT("LeftButton"); Step->TryGetStringField(TEXT("button"),Button);
            Frames->DispatchMouseDown(Handle,Button);
            if(Op==TEXT("drag_frame"))
            {
                if(!Frames->GetSliderWidget(Handle) && !Frames->DispatchDragStart(Handle,Button)) { Failure=TEXT("UI drag could not start"); Finish(C,false); return; }
                FString Target; const bool HasDestination=Step->TryGetStringField(TEXT("destination"),Target);
                const int64 Destination=HasDestination?Frames->FindFrame(Target):-1;
                float DX=X,DY=Y;
                if(HasDestination)
                {
                    if(Destination<0 || !Frames->IsFrameDrawn(Destination)) { Failure=TEXT("UI drop destination is not visible"); Finish(C,false); return; }
                    const auto DestRect=Frames->GetLayoutRect(Destination); DX=(DestRect.X+DestRect.Z*.5f)*Scale; DY=(DestRect.Y+DestRect.W*.5f)*Scale;
                    if(Frames->HitTestFrames(DX,DY)!=Destination) { Failure=TEXT("UI drop destination is obscured"); Finish(C,false); return; }
                }
                else { DX+=Step->GetNumberField(TEXT("dx")); DY+=Step->GetNumberField(TEXT("dy")); }
                for(int32 Sample=1;Sample<=4;++Sample) Frames->UpdateMouseHover(FMath::Lerp(X,DX,Sample/4.f),FMath::Lerp(Y,DY,Sample/4.f));
                if(HasDestination) Frames->DispatchReceiveDrag(Destination);
                Frames->DispatchDragStop(Handle,Button);
                Frames->DispatchMouseUp(Handle,Button);
                C.GameplayProbePointer=FVector2D(DX,DY);
            }
            else
            {
                if(Frames->IsFrameClickRegistered(Handle,Button,true)) Frames->DispatchClick(Handle,Button,true);
                Frames->DispatchMouseUp(Handle,Button);
                if(Frames->IsFrameClickRegistered(Handle,Button,false)) Frames->DispatchClick(Handle,Button,false);
            }
            Done=true;
            }
        }
        else if(Op==TEXT("inspect_vendor"))
        {
            const auto& Merchant=Handler.Merchant;
            if(Merchant.Open && Merchant.Guid==C.TargetGuid)
            {
                const int32 Expected=Step->GetIntegerField(TEXT("items"));
                if(Merchant.Items.Num()!=Expected) { Failure=TEXT("Vendor item count differs from server fixture"); Finish(C,false); return; }
                auto Row=MakeShared<FJsonObject>(); Row->SetStringField(TEXT("guid"),FString::Printf(TEXT("%llu"),Merchant.Guid));
                TArray<TSharedPtr<FJsonValue>> Items;
                for(const auto& Item:Merchant.Items)
                {
                    auto Value=MakeShared<FJsonObject>(); Value->SetNumberField(TEXT("slot"),Item.Slot); Value->SetNumberField(TEXT("id"),Item.ItemId); Value->SetNumberField(TEXT("price"),Item.Price); Value->SetNumberField(TEXT("bundle"),Item.BuyCount); Value->SetNumberField(TEXT("stock"),int32(Item.MaxCount));
                    Items.Add(MakeShared<FJsonValueObject>(Value));
                }
                Row->SetArrayField(TEXT("items"),Items); Inspections->SetObjectField(TEXT("merchant"),Row); Done=true;
            }
        }
        else if(Op==TEXT("inspect_frame"))
        {
            const FString Name=Step->GetStringField(TEXT("name")); auto* Frames=C.UIManager->GetFrameManager(); const int64 Handle=Frames->FindFrame(Name);
            const auto Rect=Handle>=0?Frames->GetLayoutRect(Handle):Frames->GetRegionRect(Name);
            auto Row=MakeShared<FJsonObject>(); Row->SetNumberField(TEXT("x"),Rect.X); Row->SetNumberField(TEXT("y"),Rect.Y); Row->SetNumberField(TEXT("width"),Rect.Z); Row->SetNumberField(TEXT("height"),Rect.W);
            Row->SetStringField(TEXT("hit"),Frames->GetFrameName(Frames->HitTestFrames((Rect.X+Rect.Z*.5f)*Frames->GetScreenScale(),(Rect.Y+Rect.W*.5f)*Frames->GetScreenScale())));
            if(const auto* Def=Frames->GetFrameDef(Handle)) { Row->SetNumberField(TEXT("level"),Def->FrameLevel); Row->SetNumberField(TEXT("strata"),int32(Def->Strata)); }
            auto* Text=Handle>=0?Frames->GetPrimaryTextWidget(Handle):Frames->GetNamedFontString(Name);
            if(Text)
            {
                Row->SetStringField(TEXT("text"),Text->GetText().ToString()); Row->SetNumberField(TEXT("fontSize"),Text->GetFont().Size);
                Row->SetNumberField(TEXT("opacity"),Text->GetRenderOpacity()); Row->SetNumberField(TEXT("visibility"),int32(Text->GetVisibility()));
                Row->SetStringField(TEXT("desired"),Text->GetDesiredSize().ToString()); Row->SetStringField(TEXT("color"),Text->GetColorAndOpacity().GetSpecifiedColor().ToString());
                if(const auto* Slot=Cast<UCanvasPanelSlot>(Text->Slot)) { Row->SetStringField(TEXT("slotSize"),Slot->GetSize().ToString()); Row->SetStringField(TEXT("slotPosition"),Slot->GetPosition().ToString()); }
            }
            if(const auto* Image=Frames->GetNamedTexture(Name))
            {
                const auto& Brush=Image->GetBrush(); auto* Resource=Brush.GetResourceObject();
                Row->SetStringField(TEXT("resource"),GetNameSafe(Resource)); Row->SetStringField(TEXT("resourceClass"),Resource?Resource->GetClass()->GetName():TEXT(""));
                Row->SetNumberField(TEXT("opacity"),Image->GetRenderOpacity()); Row->SetNumberField(TEXT("visibility"),int32(Image->GetVisibility()));
                Row->SetNumberField(TEXT("drawAs"),int32(Brush.DrawAs)); Row->SetStringField(TEXT("color"),Image->GetColorAndOpacity().ToString());
                Row->SetStringField(TEXT("geometry"),Image->GetCachedGeometry().GetLocalSize().ToString());
                if(const FBox2D UV=Brush.GetUVRegion(); UV.bIsValid) { Row->SetStringField(TEXT("uvMin"),UV.Min.ToString()); Row->SetStringField(TEXT("uvMax"),UV.Max.ToString()); }
                if(auto* Material=Cast<UMaterialInstanceDynamic>(Resource))
                {
                    Row->SetStringField(TEXT("material"),GetNameSafe(Material->Parent));
                    Resource=Material->K2_GetTextureParameterValue(TEXT("Image")); Row->SetStringField(TEXT("imageTexture"),GetNameSafe(Resource));
                    for(const auto Parameter:{TEXT("Desaturation"),TEXT("OpaqueImage"),TEXT("Circle")}) { float Value=0; Material->GetScalarParameterValue(FMaterialParameterInfo(Parameter),Value); Row->SetNumberField(Parameter,Value); }
                }
                if(const auto* Texture=Cast<UTexture2D>(Resource)) { Row->SetNumberField(TEXT("textureWidth"),Texture->GetSizeX()); Row->SetNumberField(TEXT("textureHeight"),Texture->GetSizeY()); Row->SetBoolField(TEXT("textureResource"),Texture->GetResource()!=nullptr); }
            }
            Inspections->SetObjectField(Name,Row); Done=true;
        }
        else if(Op==TEXT("assert_key_state"))
        {
            const FKey Key(*Step->GetStringField(TEXT("name")));
            if(C.IsInputKeyDown(Key)!=Step->GetBoolField(TEXT("down"))) { Failure=TEXT("Unexpected routed key state"); Finish(C,false); return; } Done=true;
        }
        else if(Op==TEXT("input_text"))
        {
            const auto Focused=FSlateApplication::IsInitialized()?FSlateApplication::Get().GetKeyboardFocusedWidget():nullptr;
            const bool FrameCapture=C.UIManager->GetFrameManager()->GetCharacterCaptureFrame()>=0;
            if(!FrameCapture && (!Focused || !Focused->GetType().ToString().Contains(TEXT("EditableText")))) { Failure=TEXT("Text input requires a focused editor or keyboard-enabled frame"); Finish(C,false); return; }
            TGuardValue<bool> Synthetic(C.bProbeSyntheticKeyboard,true);
            TGuardValue<bool> AllowText(InputGuard->SyntheticText,true);
            auto& Slate=FSlateApplication::Get();
            for(const TCHAR Ch:Step->GetStringField(TEXT("text")))
                if(Ch=='\n')
                {
                    const FKeyEvent Enter(EKeys::Enter,FModifierKeysState(),0,false,0,0);
                    Slate.ProcessKeyDownEvent(Enter); Slate.ProcessKeyUpEvent(Enter);
                }
                else Slate.ProcessKeyCharEvent(FCharacterEvent(Ch,FModifierKeysState(),uint32(0),false));
            Done=true;
        }
        else if(Op==TEXT("input_key"))
        {
            const FKey Key(*Step->GetStringField(TEXT("name"))); if(!Key.IsValid()) { Failure=TEXT("Invalid native input key"); Finish(C,false); return; }
            TGuardValue<bool> Synthetic(C.bProbeSyntheticKeyboard,true);
            C.InputKey(FInputKeyEventArgs::CreateSimulated(Key,Step->GetStringField(TEXT("event"))==TEXT("release")?IE_Released:IE_Pressed,1.f)); Done=true;
        }
        else if(Op==TEXT("key"))
        {
            const FString Key=Step->GetStringField(TEXT("name"));
            if(Key==TEXT("Escape")) C.OnEscapeKey();
            else if(Key==TEXT("B")) C.OnBagKey();
            else if(Key==TEXT("C")) C.OnCharacterKey();
            else if(Key==TEXT("P")) C.OnSpellbookKey();
            else if(Key==TEXT("N")) C.OnTalentKey();
            else if(Key==TEXT("L")) C.OnQuestLogKey();
            else if(Key==TEXT("M")) C.OnToggleMap();
            else if(Key==TEXT("Enter")) C.OnEnterKey();
            else { Failure=TEXT("Unsupported UI probe key"); Finish(C,false); return; }
            Done=true;
        }
        else if (Op==TEXT("capture"))
        {
            const FString Path=Output/(Step->GetStringField(TEXT("name"))+TEXT(".png"));
            if (!bInvoked) { FScreenshotRequest::RequestScreenshot(Path,true,false); bInvoked=true; }
            else Done=IFileManager::Get().FileSize(*Path)>0;
        }
        else { Failure=TEXT("Unknown probe operation: ")+Op; Finish(C,false); return; }
        if (Done)
        {
            const auto Result=MakeShared<FJsonObject>(); Result->SetStringField(TEXT("op"),Op);
            if (Op==TEXT("profile"))
            {
                const auto Metrics=MakeShared<FJsonObject>();
                for (auto& Column:Profile)
                {
                    auto& Values=Column.Value; Values.Sort(); double Sum=0; for (double Value:Values) Sum+=Value;
                    const auto Metric=MakeShared<FJsonObject>();
                    Metric->SetNumberField(TEXT("samples"),Values.Num()); Metric->SetNumberField(TEXT("mean"),Sum/Values.Num());
                    Metric->SetNumberField(TEXT("median"),Values[Values.Num()/2]); Metric->SetNumberField(TEXT("p95"),Values[FMath::FloorToInt((Values.Num()-1)*.95)]);
                    Metric->SetNumberField(TEXT("max"),Values.Last());
                    Metrics->SetObjectField(Column.Key,Metric);
                }
                Result->SetObjectField(TEXT("profile"),Metrics); Profile.Reset();
            }
            Result->SetNumberField(TEXT("seconds"),Now-StepStarted); Result->SetStringField(TEXT("target"),FString::Printf(TEXT("%llu"),C.TargetGuid));
            Result->SetNumberField(TEXT("quests"),Handler.QuestLog.Num());
            if (LastEquippedItem) Result->SetNumberField(TEXT("equippedItem"),LastEquippedItem);
            if (const auto* Target=Handler.EntityManager.Find(C.TargetGuid)) Result->SetNumberField(TEXT("targetHealth"),Target->GetHealth());
            Results.Add(MakeShared<FJsonValueObject>(Result)); ++Index; StepStarted=0; bInvoked=false;
        }
    }
};

void AWowGameplayController::TickGameplayProbe()
{
    if (!bGameplayProbeChecked)
    {
        bGameplayProbeChecked=true; FString Path;
        if (FParse::Value(FCommandLine::Get(),TEXT("gameplayprobe="),Path)) GameplayProbe=MakeShared<FWowGameplayProbe>(Path);
    }
    if (GameplayProbe.IsValid()) GameplayProbe->Tick(*this);
}
