#include "WowGameplayController.h"
#include "WowConnectionManager.h"
#include "WowGameUI.h"
#include "WowUIManager.h"
#include "WowInventoryManager.h"
#include "WowLuaVM.h"
#include "WowFrameManager.h"
#include "WowWorldCollision.h"
#include "WowSkyManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "RenderTimer.h"
#include "DynamicRHI.h"
#include "Coord/WowCoordinate.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "UnrealClient.h"

// Explicit development probe. It invokes the same controller/UI callbacks as
// ordinary input, waits for server state, captures rendered frames and exits.
// It never creates characters, grants items or modifies server databases.
struct FWowGameplayProbe
{
    TSharedPtr<FJsonObject> Spec;
    TArray<TSharedPtr<FJsonValue>> Steps, Results;
    FString Output, Failure;
    int32 Index=0;
    double Started=FPlatformTime::Seconds(), StepStarted=0;
    bool bInvoked=false, bFinished=false;
    double FailureExitAt=0;
    uint32 LastEquippedItem=0;
    TWeakObjectPtr<UPrimitiveComponent> Avoiding;
    float AvoidSide=1;
    TMap<FString,TArray<double>> Profile;

    explicit FWowGameplayProbe(const FString& Path)
    {
        FString Text;
        if (!FFileHelper::LoadFileToString(Text,*Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Spec) || !Spec.IsValid())
        { Failure=TEXT("Invalid probe specification"); return; }
        const TArray<TSharedPtr<FJsonValue>>* Values=nullptr;
        if (!Spec->TryGetArrayField(TEXT("steps"),Values)) { Failure=TEXT("Missing steps"); return; }
        Steps=*Values; Output=Spec->GetStringField(TEXT("output"));
        if (Output.IsEmpty() || !IFileManager::Get().MakeDirectory(*Output,true)) Failure=TEXT("Invalid output directory");
    }

    void Finish(AWowGameplayController& Controller,bool Passed)
    {
        if (bFinished) return;
        bFinished=true;
        if (auto* Character=Cast<ACharacter>(Controller.GetPawn())) Character->GetCharacterMovement()->StopMovementImmediately();
        Controller.StopAutoAttack();
        const auto Report=MakeShared<FJsonObject>();
        Report->SetBoolField(TEXT("passed"),Passed); Report->SetStringField(TEXT("failure"),Failure);
        Report->SetNumberField(TEXT("completedSteps"),Index); Report->SetArrayField(TEXT("steps"),Results);
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
        if (Passed) FPlatformMisc::RequestExitWithStatus(false,0);
        else
        {
            if (!Output.IsEmpty()) FScreenshotRequest::RequestScreenshot(Output/TEXT("failure.png"),true,false);
            FailureExitAt=FPlatformTime::Seconds()+2;
        }
    }

    uint64 FindEntry(AWowGameplayController& C,uint32 Entry)
    {
        double Closest=DBL_MAX; uint64 Guid=0;
        for (const auto& Pair : C.SpawnedEntityActors)
        {
            const auto* Entity=C.ConnectionManager->PacketHandler.EntityManager.Find(Pair.Key);
            if (!Entity || Entity->Entry!=Entry || !Entity->IsUnit() || Entity->GetHealth()<=0 || !IsValid(Pair.Value)) continue;
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
        if (bFinished)
        {
            if (FailureExitAt>0 && FPlatformTime::Seconds()>=FailureExitAt) FPlatformMisc::RequestExitWithStatus(false,3);
            return;
        }
        const double Now=FPlatformTime::Seconds();
        if (!Failure.IsEmpty()) { Finish(C,false); return; }
        if (Now-Started>600) { Failure=TEXT("Overall timeout"); Finish(C,false); return; }
        auto* Connection=C.ConnectionManager.Get();
        if (!Connection || !C.GameUI || !C.UIManager || !C.GetPawn() || !Connection->PacketHandler.EntityManager.GetLocalPlayer()) return;
        if (Connection->GetCharacterName()!=Spec->GetStringField(TEXT("character"))) { Failure=TEXT("Verification character mismatch"); Finish(C,false); return; }
        if (Index>=Steps.Num()) { Finish(C,true); return; }
        const auto Step=Steps[Index]->AsObject();
        const FString Op=Step->GetStringField(TEXT("op"));
        double Timeout=60; Step->TryGetNumberField(TEXT("timeout"),Timeout);
        if (StepStarted==0) { StepStarted=Now; UE_LOG(LogTemp,Log,TEXT("GameplayProbe: step %d %s"),Index,*Op); }
        if (Now-StepStarted>Timeout) { Failure=FString::Printf(TEXT("Step %d timed out: %s"),Index,*Op); Finish(C,false); return; }
        auto& Handler=Connection->PacketHandler;
        bool Done=false;
        if (Op==TEXT("wait")) Done=Now-StepStarted>=Step->GetNumberField(TEXT("seconds"));
        else if (Op==TEXT("time"))
        {
            if (auto* Sky=Cast<AWowSkyManager>(UGameplayStatics::GetActorOfClass(C.GetWorld(),AWowSkyManager::StaticClass())))
            { Sky->TimeOfDay=FMath::Clamp(float(Step->GetNumberField(TEXT("minutes"))),0.f,1439.9f); Sky->TimeSpeed=0; Done=true; }
        }
        else if (Op==TEXT("profile"))
        {
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
            Done=Now-StepStarted>=FMath::Clamp(Step->GetNumberField(TEXT("seconds")),1.0,30.0);
        }
        else if (Op==TEXT("select_entry"))
        {
            const uint64 Guid=FindEntry(C,Step->GetIntegerField(TEXT("entry")));
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
        else if (Op==TEXT("jump")) { if (auto* Character=Cast<ACharacter>(C.GetPawn())) { Character->Jump(); Done=true; } }
        else if (Op==TEXT("release_jump")) { if (auto* Character=Cast<ACharacter>(C.GetPawn())) { Character->StopJumping(); Done=true; } }
        else if (Op==TEXT("wait_landed")) { const auto* Character=Cast<ACharacter>(C.GetPawn()); Done=Character && Character->GetCharacterMovement()->IsMovingOnGround() && Now-StepStarted>.5; }
        else if (Op==TEXT("wait_menu")) Done=(Handler.GossipMenu.Guid==C.TargetGuid && (Handler.GossipMenu.Quests.Num() || Handler.GossipMenu.Options.Num())) || (Handler.QuestDetails.QuestGiverGuid==C.TargetGuid && C.GameUI->GetShownQuestId()!=0);
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
        else if (Op==TEXT("wait_loot_empty")) Done=Handler.Loot.Guid && !Handler.Loot.Gold && Handler.Loot.Items.IsEmpty();
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
        else if (Op==TEXT("bags")) { C.UIManager->ToggleBagWindow(); C.UIManager->ToggleCharacterPanel(); Done=true; }
        else if (Op==TEXT("lua")) { Done=C.UIManager->GetLuaVM()->ExecuteString(Step->GetStringField(TEXT("code")),TEXT("=GameplayProbe")); if (!Done) { Failure=TEXT("Lua assertion failed"); Finish(C,false); return; } }
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
