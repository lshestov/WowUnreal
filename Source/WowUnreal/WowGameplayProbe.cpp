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
#include "WowSpellMissile.h"
#include "WowPlayerCharacter.h"
#include "WowAnimationController.h"
#include "WowCharacterBuilder.h"
#include "WowM2AnimInstance.h"
#include "WowM2EmitterComponent.h"
#include "WowGraphicsSnapshot.h"
#include "WowLoginScene.h"
#include "WowCharacterPreview.h"
#include "WowAudioManager.h"
#include "ProceduralMeshComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "RenderTimer.h"
#include "RHIStats.h"
#include "HAL/PlatformMemory.h"
#include "DynamicRHI.h"
#include "Coord/WowCoordinate.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextBlock.h"
#include "Components/ProgressBar.h"
#include "Components/Image.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/CanvasPanelSlot.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "Widgets/SViewport.h"
#include "Widgets/Text/STextBlock.h"
#include "Fonts/CompositeFont.h"
#include "Layout/Children.h"
#include "Layout/WidgetPath.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "UObject/GarbageCollection.h"
#include "UnrealClient.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
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

namespace
{
TSharedPtr<SWidget> FindVisibleWidget(TSharedRef<SWidget> Root, TFunctionRef<bool(SWidget&)> Match)
{
    if (!Root->GetVisibility().IsVisible()) return nullptr;
    if (Match(Root.Get())) return Root;
    FChildren* Children = Root->GetChildren();
    for (int32 Index = 0; Children && Index < Children->Num(); ++Index)
        if (auto Found = FindVisibleWidget(Children->GetChildAt(Index), Match)) return Found;
    return nullptr;
}
}

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
    uint64 PendingWorldClick=0;
    bool bWorldClickReleased=false;
    FVector2D SlateClickPoint=FVector2D::ZeroVector;
    TOptional<FVector2D> SavedSlateCursor;
    FRotator BeforeMouseAxis=FRotator::ZeroRotator;
    TMap<uint64,TPair<uint32,uint32>> OfflineCorpseFields;
    uint64 OfflineFacingGuid=0, SavedOfflineNpcTarget=0;
    float SavedOfflineNpcOrientation=0, OfflineNpcRawOrientation=0;
    FVector SavedOfflinePlayerPosition=FVector::ZeroVector;
    FRotator SavedOfflinePlayerRotation=FRotator::ZeroRotator;
    TSet<int32> SwingVariations;
    TArray<TSharedPtr<FJsonValue>> SwingSamples;
    int32 SwingCount=0;
    double NextSwingAt=0;
    bool bSampleSwing=false;
    FString SwingCapture;
    TWeakObjectPtr<ACameraActor> SideCamera;
    TWeakObjectPtr<UBoxComponent> SideTarget;
    FVector SideCameraOffset = FVector::ZeroVector;
    TWeakObjectPtr<UPrimitiveComponent> Avoiding;
    float AvoidSide=1;
    TMap<FString,TArray<double>> Profile;
    double PreviousProfileTime=0;
    TSharedPtr<FJsonObject> Inspections=MakeShared<FJsonObject>();
    TSharedPtr<FWowProbeInputGuard> InputGuard;
    TSharedPtr<FSlateVirtualUserHandle> DialogPointerUser;
    TWeakPtr<SWidget> ExpectedDialogHover;
    TUniquePtr<FWowUiTestPeer> Peers[4];
    FWowUiTestPeer* Peer=nullptr;
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
    ~FWowGameplayProbe()
    {
        RestoreSlateCursor();
        if(InputGuard && FSlateApplication::IsInitialized()) FSlateApplication::Get().UnregisterInputPreProcessor(InputGuard);
    }

    void RestoreSlateCursor()
    {
        if(SavedSlateCursor.IsSet() && FSlateApplication::IsInitialized()) FSlateApplication::Get().SetCursorPos(*SavedSlateCursor);
        SavedSlateCursor.Reset();
    }

    void Finish(AWowGameplayController& Controller,bool Passed)
    {
        if (bFinished) return;
        bFinished=true;
        RestoreSlateCursor();
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
        bool HasPeers=false; for(const auto& TestPeer:Peers) HasPeers|=bool(TestPeer);
        if(HasPeers)
        {
            FinalReport=Report; PeerExitCode=Passed?0:3;
            for(const auto& TestPeer:Peers) if(TestPeer) TestPeer->Stop();
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

    bool RouteWorldClick(AWowGameplayController& C,bool Down)
    {
        auto* Client=C.GetWorld()->GetGameViewport();
        const auto Window=Client?Client->GetWindow():nullptr;
        const auto Viewport=Client?Client->GetGameViewportWidget():nullptr;
        int32 Width=0,Height=0;
        C.GetViewportSize(Width,Height);
        if (!Window || !Viewport || Width<=0 || Height<=0) return false;
        const FVector2D Screen=Viewport->GetCachedGeometry().GetAbsolutePositionAtCoordinates(
            FVector2D(SlateClickPoint.X/Width,SlateClickPoint.Y/Height));
        auto& Slate=FSlateApplication::Get();
        const auto Path=Slate.LocateWindowUnderMouse(Screen,{Window.ToSharedRef()},false,0);
        if (!Path.IsValid()) return false;
        // SceneViewport refreshes its cached position from the desktop cursor
        // when capture ends. Keep both positions together until PlayerInput has
        // dispatched release; restoring earlier would turn this click into a drag.
        if(Down && !SavedSlateCursor.IsSet()) SavedSlateCursor=Slate.GetCursorPos();
        Slate.SetCursorPos(Screen);
        const TSet<FKey> Buttons=Down?TSet<FKey>{EKeys::LeftMouseButton}:TSet<FKey>{};
        const FPointerEvent Event(FSlateApplication::CursorPointerIndex,Screen,Screen,Buttons,EKeys::LeftMouseButton,0,FModifierKeysState());
        UE_LOG(LogTemp,Log,TEXT("GameplayProbe: Slate world click %s path=%s desktop=(%.1f,%.1f)"),Down?TEXT("down"):TEXT("up"),*Path.ToString(),Screen.X,Screen.Y);
        TGuardValue<bool> Synthetic(C.bProbeSyntheticKeyboard,true);
        return (Down?Slate.RoutePointerDownEvent(Path,Event):Slate.RoutePointerUpEvent(Path,Event)).IsEventHandled();
    }

    FString DescribeWorldClick(AWowGameplayController& C) const
    {
        float X=0,Y=0;
        const bool HasPointer=C.GetMousePosition(X,Y);
        const auto& Press=C.LeftWorldPress;
        const FVector2D Desktop=FSlateApplication::Get().GetCursorPos();
        return FString::Printf(TEXT("active=%d dragged=%d picked=%llu start=(%.1f,%.1f) pointer=(%d %.1f,%.1f) desktop=(%.1f,%.1f) view=%s startView=%s ui=%lld native=%d keyDown=%d"),
            Press.Active,Press.Dragged,Press.Guid,Press.Position.X,Press.Position.Y,HasPointer,X,Y,Desktop.X,Desktop.Y,
            *C.GetControlRotation().ToString(),*Press.ViewRotation.ToString(),C.GetUiFrameUnderCursor(),C.IsPointerOverNativeWindow(),C.IsInputKeyDown(EKeys::LeftMouseButton));
    }

    bool ProbeDialog(AWowGameplayController& C, const FJsonObject& Step, const FString& Op)
    {
        auto* Client = C.GetWorld()->GetGameViewport();
        const auto Window = Client ? Client->GetWindow() : nullptr;
        const auto Viewport = Client ? Client->GetGameViewportWidget() : nullptr;
        if (!Window || !Viewport) return false;

        const auto Dialog = FindVisibleWidget(Window.ToSharedRef(), [](SWidget& Widget) {
            return Widget.GetType() == TEXT("SWowQuestDialog");
        });
        if (Op == TEXT("wait_dialog_closed")) return !Dialog;
        if (!Dialog) return false;
        FString Label, Tag;
        Step.TryGetStringField(TEXT("text"), Label);
        Step.TryGetStringField(TEXT("tag"), Tag);
        if (Label.IsEmpty() && Tag.IsEmpty()) return false;
        const bool InspectText = Op == TEXT("wait_dialog_text");
        const auto MatchesText = [&](SWidget& Widget) {
            if (Widget.GetType() != TEXT("STextBlock")) return false;
            const FString Text = static_cast<STextBlock&>(Widget).GetText().ToString();
            return InspectText ? Text.Contains(Label) : Text == Label;
        };
        if (InspectText)
        {
            const auto Found = FindVisibleWidget(Dialog.ToSharedRef(), MatchesText);
            if (!Found) return false;
            FString Face;
            if (!Step.TryGetStringField(TEXT("face"), Face)) return true;
            const auto& Text = static_cast<const STextBlock&>(*Found);
            const auto Font = Text.GetFont();
            const auto* Composite = Font.GetCompositeFont();
            const bool Unhinted = Composite && !Composite->DefaultTypeface.Fonts.IsEmpty() &&
                Composite->DefaultTypeface.Fonts[0].Font.GetHinting() == EFontHinting::None;
            const auto Measured = MakeShared<FJsonObject>();
            Measured->SetStringField(TEXT("face"), Font.TypefaceFontName.ToString());
            Measured->SetNumberField(TEXT("points"), Font.Size);
            Measured->SetNumberField(TEXT("letterSpacing"), Font.LetterSpacing);
            Measured->SetBoolField(TEXT("unhinted"), Unhinted);
            Inspections->SetObjectField(Step.GetStringField(TEXT("label")), Measured);
            if (Font.TypefaceFontName != FName(*Face) || !Unhinted ||
                !FMath::IsNearlyEqual(Font.Size, float(Step.GetNumberField(TEXT("points")))))
            {
                Failure = FString::Printf(TEXT("Unexpected font for '%s': %s %.2f pt, unhinted=%d"),
                    *Label, *Font.TypefaceFontName.ToString(), Font.Size, Unhinted);
                Finish(C,false);
                return false;
            }
            return true;
        }

        const auto Button = FindVisibleWidget(Dialog.ToSharedRef(), [&](SWidget& Widget) {
            return Widget.GetType() == TEXT("SButton") && Widget.IsEnabled() &&
                (Tag.IsEmpty() ? FindVisibleWidget(Widget.AsShared(), MatchesText).IsValid() : Widget.GetTag() == FName(*Tag));
        });
        if (!Button || Button->GetCachedGeometry().GetLocalSize().GetMin() < 1) return false;

        // Hit the actual arranged row. Dispatch through Slate, including its
        // hit-test grid and SButton handlers, exactly as a mouse click would.
        const FVector2D Screen = Button->GetCachedGeometry().GetAbsolutePositionAtCoordinates(FVector2D(.55,.5));
        auto& Slate = FSlateApplication::Get();
        // Give the probe its own pointer so a stationary hardware mouse cannot
        // undo the hovered state between the routed move and the rendered capture.
        if (!DialogPointerUser) DialogPointerUser = Slate.FindOrCreateVirtualUser(0);
        const uint32 User = DialogPointerUser->GetUserIndex();
        const auto Path = Slate.LocateWindowUnderMouse(Screen, {Window.ToSharedRef()}, false, User);
        if (!Path.IsValid() || !Path.ContainsWidget(Button.Get())) return false;
        int32 Width, Height;
        C.GetViewportSize(Width, Height);
        const auto& Geometry = Viewport->GetCachedGeometry();
        const FVector2D LocalSize = Geometry.GetLocalSize();
        if (LocalSize.GetMin() < 1 || Width < 1 || Height < 1) return false;
        const FVector2D Pixel = Geometry.AbsoluteToLocal(Screen) / LocalSize * FVector2D(Width, Height);
        C.GameplayProbePointer = Pixel;
        const TSet<FKey> NoButtons;
        const FPointerEvent Move(User, FSlateApplication::CursorPointerIndex, Screen, Screen, NoButtons,
            EKeys::Invalid, 0, FModifierKeysState());
        Slate.RoutePointerMoveEvent(Path, Move, false);
        if (!Button->IsHovered()) return false;
        bool Click = false;
        Step.TryGetBoolField(TEXT("click"), Click);
        if (Click)
        {
            const TSet<FKey> PressedButtons{EKeys::LeftMouseButton};
            const FPointerEvent Down(User, FSlateApplication::CursorPointerIndex, Screen, Screen,
                PressedButtons, EKeys::LeftMouseButton, 0, FModifierKeysState());
            const FPointerEvent Up(User, FSlateApplication::CursorPointerIndex, Screen, Screen,
                NoButtons, EKeys::LeftMouseButton, 0, FModifierKeysState());
            const bool Pressed = Slate.RoutePointerDownEvent(Path, Down).IsEventHandled();
            const bool Released = Slate.RoutePointerUpEvent(Path, Up).IsEventHandled();
            if (!Pressed || !Released)
            {
                Failure = FString::Printf(TEXT("Dialog button did not handle click: %s"), Tag.IsEmpty() ? *Label : *Tag);
                return false;
            }
        }
        if (Click) ExpectedDialogHover.Reset();
        else ExpectedDialogHover = Button;
        const auto Row = MakeShared<FJsonObject>();
        Row->SetNumberField(TEXT("x"), Pixel.X);
        Row->SetNumberField(TEXT("y"), Pixel.Y);
        Row->SetBoolField(TEXT("click"), Click);
        Inspections->SetObjectField(Step.GetStringField(TEXT("label")), Row);
        return true;
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

    FVector SideViewFocus(const APawn& Pawn, const UBoxComponent& Target) const
    {
        FVector Focus = (Pawn.GetActorLocation() + Target.Bounds.Origin) * .5;
        // Leave space for a quest marker and overhead name above humanoids.
        Focus.Z = FMath::Max(Pawn.GetActorLocation().Z, Target.Bounds.Origin.Z) + 25;
        return Focus;
    }

    bool FrameFromSide(AWowGameplayController& C, AActor& Target, double Distance)
    {
        APawn* Pawn = C.GetPawn();
        SideTarget = FindObjectFast<UBoxComponent>(&Target, TEXT("UnitSelection"));
        if (!Pawn || !SideTarget.IsValid()) return false;
        if (!SideCamera.IsValid()) SideCamera = C.GetWorld()->SpawnActor<ACameraActor>();
        if (!SideCamera.IsValid()) return false;
        const FVector Forward = (Target.GetActorLocation() - Pawn->GetActorLocation()).GetSafeNormal2D();
        const FVector Right = Forward.IsNearlyZero() ? Pawn->GetActorRightVector() : FVector::CrossProduct(FVector::UpVector, Forward);
        const FVector Lift(0, 0, Distance * .20);
        const FVector Focus = SideViewFocus(*Pawn, *SideTarget.Get());
        FCollisionQueryParams Query(SCENE_QUERY_STAT(ProbeSideCamera), true, Pawn);
        Query.AddIgnoredActor(&Target);
        SideCameraOffset = Right * Distance + Lift;
        if (C.GetWorld()->LineTraceTestByChannel(Focus, Focus + SideCameraOffset, ECC_Visibility, Query))
            SideCameraOffset = -Right * Distance + Lift;
        SideCamera->GetCameraComponent()->SetConstraintAspectRatio(false);
        SideCamera->GetCameraComponent()->SetFieldOfView(66);
        SideCamera->SetActorLocationAndRotation(Focus + SideCameraOffset, (-SideCameraOffset).Rotation());
        C.SetViewTarget(SideCamera.Get());
        return true;
    }

    void Tick(AWowGameplayController& C)
    {
        if (SideCamera.IsValid() && C.GetViewTarget() == SideCamera.Get() && SideTarget.IsValid() && C.GetPawn())
            SideCamera->SetActorLocation(SideViewFocus(*C.GetPawn(), *SideTarget.Get()) + SideCameraOffset);
        for(const auto& TestPeer:Peers) if(TestPeer) TestPeer->Tick();
        if (bFinished)
        {
            if(PeerExitCode>=0)
            {
                if(FPlatformTime::Seconds()<FailureExitAt) return;
                FString PeerFailure;
                for(const auto& TestPeer:Peers) if(TestPeer)
                {
                    if(!TestPeer->Complete()) return;
                    if(!TestPeer->Error().IsEmpty()) PeerFailure=TestPeer->Error();
                }
                FinalReport->SetBoolField(TEXT("peerCleanup"),PeerFailure.IsEmpty());
                if(!PeerFailure.IsEmpty()) {FinalReport->SetBoolField(TEXT("passed"),false); FinalReport->SetStringField(TEXT("failure"),PeerFailure); PeerExitCode=3;}
                FString Text; FJsonSerializer::Serialize(FinalReport.ToSharedRef(),TJsonWriterFactory<>::Create(&Text)); FFileHelper::SaveStringToFile(Text,*(Output/TEXT("probe.json")));
                FPlatformMisc::RequestExitWithStatus(false,uint8(PeerExitCode)); return;
            }
            if (FailureExitAt>0 && FPlatformTime::Seconds()>=FailureExitAt) FPlatformMisc::RequestExitWithStatus(false,3);
            return;
        }
        const double Now=FPlatformTime::Seconds();
        for(const auto& TestPeer:Peers) if(TestPeer && !TestPeer->Error().IsEmpty()) Failure=TestPeer->Error();
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
        double PeerIndex=0; Step->TryGetNumberField(TEXT("peer"),PeerIndex);
        if(PeerIndex<0 || PeerIndex>=UE_ARRAY_COUNT(Peers) || FMath::FloorToDouble(PeerIndex)!=PeerIndex) {Failure=TEXT("Invalid QA peer index"); Finish(C,false); return;}
        Peer=Peers[int32(PeerIndex)].Get();
        const FString Op=Step->GetStringField(TEXT("op"));
        double Timeout=60; Step->TryGetNumberField(TEXT("timeout"),Timeout);
        if (StepStarted==0) { StepStarted=Now; UE_LOG(LogTemp,Log,TEXT("GameplayProbe: step %d %s"),Index,*Op); }
        if (Now-StepStarted>Timeout) { Failure=FString::Printf(TEXT("Step %d timed out: %s"),Index,*Op); Finish(C,false); return; }
        auto& Handler=Connection->PacketHandler;
        bool Done=false;
        // These fixtures never alter a connected session or server state.
        if(Op.StartsWith(TEXT("offline_")) && (!C.bOfflineUiPreview || Connection->GetState()!=EWowSessionState::Disconnected))
        {Failure=TEXT("Offline fixture operations require the disconnected UI preview"); Finish(C,false); return;}
        if(Op==TEXT("peer_start") || Op==TEXT("peer_cleanup"))
        {
            if(!Peer)
            {
                FString Fixture;
                if(PeerIndex==0) Spec->TryGetStringField(TEXT("peerFixture"),Fixture);
                else {const TArray<TSharedPtr<FJsonValue>>* Extra=nullptr; if(Spec->TryGetArrayField(TEXT("extraPeerFixtures"),Extra) && Extra->IsValidIndex(int32(PeerIndex)-1)) Fixture=(*Extra)[int32(PeerIndex)-1]->AsString();}
                Peers[int32(PeerIndex)]=MakeUnique<FWowUiTestPeer>(); Peer=Peers[int32(PeerIndex)].Get();
                if(!Peer->Start(Fixture,Handler.EntityManager.LocalPlayerGuid,Op==TEXT("peer_cleanup"))) {Failure=TEXT("Invalid coordinator-owned QA peer fixture"); Finish(C,false); return;}
            }
            if(Op==TEXT("peer_cleanup")) Peer->Stop();
            Done=Op==TEXT("peer_cleanup")?Peer->Complete():Peer->Ready();
            if(Done && Op==TEXT("peer_start"))
            {
                Connection->SendNameQuery(int64(Peer->Guid()));
                C.UIManager->GetLuaVM()->ExecuteString(FString::Printf(TEXT("QAUiPeerGuid=%llu; QAUiPeerName='%s'; QAUiPeerGuids=QAUiPeerGuids or {}; QAUiPeerNames=QAUiPeerNames or {}; QAUiPeerGuids[%d]=QAUiPeerGuid; QAUiPeerNames[%d]=QAUiPeerName"),Peer->Guid(),*Peer->Get()->GetCharacterName(),int32(PeerIndex)+1,int32(PeerIndex)+1));
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
        else if(Op==TEXT("lfg_boot_owned") || Op==TEXT("lfg_finish_owned"))
        {
            TSet<uint64> Owned{Handler.EntityManager.LocalPlayerGuid};
            for(const auto& Candidate:Peers) if(Candidate && Candidate->Ready()) Owned.Add(Candidate->Guid());
            const auto& G=Handler.GroupInfo; const auto* Dungeon=FDbcStore::Get().LfgUi().Dungeon(G.LfgDungeon);
            bool Valid=Owned.Num()==5 && Handler.CurrentMapId==34 && G.IsLfgGroup() && Dungeon && Dungeon->Id==12 && G.MemberCount>=4 && G.MemberCount<=5;
            for(const auto& Member:G.Members) Valid&=Owned.Contains(Member.Guid);
            if(!Valid) {Failure=TEXT("LFG fixture action requires an owned Stockades group"); Finish(C,false); return;}
            if(Op==TEXT("lfg_boot_owned"))
            {
                if(G.MemberCount!=5 || !Peers[0] || !Peers[3] || !Peers[0]->Ready() || !Peers[3]->Ready() || Peers[0]->Get()->PacketHandler.GroupInfo.GroupGuid!=G.GroupGuid || Peers[3]->Get()->PacketHandler.GroupInfo.GroupGuid!=G.GroupGuid || Peers[3]->Guid()>MAX_uint32) {Failure=TEXT("LFG vote fixture membership changed"); Finish(C,false); return;}
                Done=Peers[0]->Command(TEXT("lfg_boot"),0,uint32(Peers[3]->Guid()));
            }
            else
            {
                const auto* Target=Handler.EntityManager.Find(C.TargetGuid); const auto* Player=Handler.EntityManager.GetLocalPlayer();
                if(!Target || !Player || !Target->IsUnit() || Target->IsPlayer() || Target->Entry!=1716 || FVector::DistSquared(Target->Movement.Position,Player->Movement.Position)>900) {Failure=TEXT("LFG completion target is not the owned instance boss"); Finish(C,false); return;}
                // Deal damage through the normal reward path: .die can leave the
                // creature's required player damage unmet and skip encounter credit.
                Connection->SendChatMessage(TEXT(".damage 100000")); Done=true;
            }
        }
        else if(Op==TEXT("arena_finish_owned") || Op==TEXT("arena_damage_owned"))
        {
            const auto ActiveQueue=[](const FWowPacketHandler& H)->const FWowPvpState::FQueue* {for(const auto& Q:H.Pvp.Queues) if(Q.Status==3 && Q.Arena==2) return &Q; return nullptr;};
            const auto* MainQueue=ActiveQueue(Handler);
            TArray<FWowUiTestPeer*> Opponents;
            TSet<uint64> OwnedGuids={Handler.EntityManager.LocalPlayerGuid};
            bool Valid=MainQueue!=nullptr;
            for(const auto& TestPeer:Peers)
            {
                const auto* Queue=TestPeer && TestPeer->Ready()?ActiveQueue(TestPeer->Get()->PacketHandler):nullptr;
                Valid&=Queue && TestPeer->Get()->PacketHandler.CurrentMapId==Handler.CurrentMapId;
                if(TestPeer) OwnedGuids.Add(TestPeer->Guid());
                if(Queue && MainQueue && Queue->Team!=MainQueue->Team) Opponents.Add(TestPeer.Get());
            }
            if(!Valid || OwnedGuids.Num()!=4 || Opponents.Num()!=2) {Failure=TEXT("Arena finish requires exactly four owned QA players on the same map"); Finish(C,false); return;}
            // A real server match result with all four score rows. The guard
            // above excludes every non-fixture character before either kill.
            if(Op==TEXT("arena_damage_owned"))
            {
                if(!Opponents.ContainsByPredicate([&](const auto* Opponent){return Opponent->Guid()==C.TargetGuid;})) {Failure=TEXT("Damage target is not an owned arena opponent"); Finish(C,false); return;}
                Connection->SendChatMessage(TEXT(".damage 20"));
            }
            else for(auto* Opponent:Opponents) {C.SetTarget(Opponent->Guid()); Connection->SendChatMessage(TEXT(".die"));}
            Done=true;
        }
        else if (Op==TEXT("wait")) Done=Now-StepStarted>=Step->GetNumberField(TEXT("seconds"));
        else if(Op==TEXT("collect_garbage")) {CollectGarbage(RF_NoFlags); Done=true;}
        else if(Op==TEXT("move_pointer"))
        {
            int32 Width=0,Height=0; C.GetViewportSize(Width,Height);
            const FVector2D Point(FMath::Clamp(Step->GetNumberField(TEXT("x")),0.,1.)*Width,FMath::Clamp(Step->GetNumberField(TEXT("y")),0.,1.)*Height);
            C.GameplayProbePointer=Point; C.UIManager->GetFrameManager()->UpdateMouseHover(Point.X,Point.Y); Done=true;
        }
        else if(Op==TEXT("mouse_axis"))
        {
            if(!bInvoked)
            {
                BeforeMouseAxis=C.GetControlRotation(); bInvoked=true;
                TGuardValue<bool> Synthetic(C.bProbeSyntheticKeyboard,true);
                C.InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::MouseX,IE_Axis,Step->GetNumberField(TEXT("dx")),1));
                C.InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::MouseY,IE_Axis,Step->GetNumberField(TEXT("dy")),1));
            }
            else if(!C.GetControlRotation().Equals(BeforeMouseAxis,.01f))
            {
                const auto Row=MakeShared<FJsonObject>();
                Row->SetNumberField(TEXT("yawBefore"),BeforeMouseAxis.Yaw); Row->SetNumberField(TEXT("yawAfter"),C.GetControlRotation().Yaw);
                Row->SetNumberField(TEXT("pitchBefore"),BeforeMouseAxis.Pitch); Row->SetNumberField(TEXT("pitchAfter"),C.GetControlRotation().Pitch);
                Inspections->SetObjectField(TEXT("mouseAxis"),Row); Done=true;
            }
        }
        else if(Op==TEXT("offline_heroic_packet"))
        {
            // Visual acknowledgement/error replay only. This never fabricates
            // successful client dispatch; native cast packets have separate tests.
            const FString Phase=Step->GetStringField(TEXT("phase"));
            int32 Counter=0; Step->TryGetNumberField(TEXT("counter"),Counter);
            if(Counter<0 || Counter>255 || !Handler.EntityManager.GetLocalPlayer())
            {Failure=TEXT("Invalid offline Heroic Strike replay identity"); Finish(C,false); return;}
            TArray<uint8> Bytes;
            const auto U32=[&](uint32 Value){Bytes.Append(reinterpret_cast<const uint8*>(&Value),4);};
            const auto Packed=[&](uint64 Guid)
            {
                uint8 Mask=0;
                for(int32 Byte=0;Byte<8;++Byte) if(uint8(Guid>>(Byte*8))) Mask|=1u<<Byte;
                Bytes.Add(Mask);
                for(int32 Byte=0;Byte<8;++Byte) if(Mask&(1u<<Byte)) Bytes.Add(uint8(Guid>>(Byte*8)));
            };
            if(Phase==TEXT("rage"))
            {
                const int32 Rage=Step->GetIntegerField(TEXT("rawRage"));
                if(Rage<0 || Rage>1000) {Failure=TEXT("Invalid offline rage value"); Finish(C,false); return;}
                U32(1); Bytes.Add(0); Packed(Handler.EntityManager.LocalPlayerGuid);
                Bytes.Add(1); U32(1u<<UnitField::POWER2); U32(uint32(Rage));
                Handler.HandlePacket(WowOpcode::SMSG_UPDATE_OBJECT,Bytes);
            }
            else if(Phase==TEXT("start") || Phase==TEXT("go"))
            {
                Packed(Handler.EntityManager.LocalPlayerGuid); Packed(Handler.EntityManager.LocalPlayerGuid);
                Bytes.Add(uint8(Counter)); U32(78); U32(0); U32(0);
                if(Phase==TEXT("go")) {Bytes.Add(0); Bytes.Add(0);}
                U32(0);
                Handler.HandlePacket(Phase==TEXT("start")?WowOpcode::SMSG_SPELL_START:WowOpcode::SMSG_SPELL_GO,Bytes);
            }
            else if(Phase==TEXT("no_power"))
            {
                Bytes.Add(uint8(Counter)); U32(78); Bytes.Add(85);
                Handler.HandlePacket(0x0130,Bytes); // SMSG_CAST_FAILED in build 12340.
            }
            else {Failure=TEXT("Unknown offline Heroic Strike packet phase"); Finish(C,false); return;}
            const auto Row=MakeShared<FJsonObject>();
            Row->SetStringField(TEXT("source"),TEXT("server-packet-replay"));
            Row->SetNumberField(TEXT("queuedSpell"),Handler.LocalQueuedMeleeSpell);
            Row->SetNumberField(TEXT("queuedCounter"),Handler.LocalQueuedMeleeCounter);
            Inspections->SetObjectField(FString::Printf(TEXT("heroicPacket:%s:%d"),*Phase,Counter),Row);
            Done=true;
        }
        else if(Op==TEXT("offline_npc_facing"))
        {
            const FString Phase=Step->GetStringField(TEXT("phase"));
            const uint64 Guid=FindEntry(C,Step->GetIntegerField(TEXT("entry")));
            auto* Entity=Handler.EntityManager.Find(Guid);
            auto* Actor=C.SpawnedEntityActors.FindRef(Guid).Get();
            APawn* Pawn=C.GetPawn();
            if(!Entity || !IsValid(Actor) || !Pawn || Entity->IsPlayer())
            {Failure=TEXT("NPC facing replay requires the disconnected living creature fixture"); Finish(C,false); return;}
            const auto SendTarget=[&](uint64 Target)
            {
                // Original VALUES update: packed object GUID and target low/high.
                TArray<uint8> Bytes;
                const auto U32=[&](uint32 Value){Bytes.Append(reinterpret_cast<const uint8*>(&Value),4);};
                U32(1); Bytes.Add(0);
                uint8 Mask=0;
                for(int32 Byte=0;Byte<8;++Byte) if(uint8(Guid>>(Byte*8))) Mask|=1u<<Byte;
                Bytes.Add(Mask);
                for(int32 Byte=0;Byte<8;++Byte) if(Mask&(1u<<Byte)) Bytes.Add(uint8(Guid>>(Byte*8)));
                Bytes.Add(1); U32((1u<<UnitField::TARGET)|(1u<<(UnitField::TARGET+1)));
                U32(uint32(Target)); U32(uint32(Target>>32));
                Handler.HandlePacket(WowOpcode::SMSG_UPDATE_OBJECT,Bytes);
            };
            if(Phase==TEXT("target"))
            {
                if(auto* Character=Cast<ACharacter>(Pawn)) Character->GetCharacterMovement()->StopMovementImmediately();
                OfflineFacingGuid=Guid; SavedOfflineNpcTarget=Entity->GetField64(UnitField::TARGET);
                SavedOfflineNpcOrientation=Entity->Movement.Orientation;
                SavedOfflinePlayerPosition=Pawn->GetActorLocation(); SavedOfflinePlayerRotation=Pawn->GetActorRotation();
                const FVector TowardPlayer=(Pawn->GetActorLocation()-Actor->GetActorLocation()).GetSafeNormal2D();
                if(TowardPlayer.IsNearlyZero()) {Failure=TEXT("NPC facing fixture overlaps the player"); Finish(C,false); return;}
                OfflineNpcRawOrientation=-FMath::DegreesToRadians(TowardPlayer.Rotation().Yaw+180.f);
                Entity->Movement.Orientation=OfflineNpcRawOrientation;
                SendTarget(Handler.EntityManager.LocalPlayerGuid);
            }
            else
            {
                if(OfflineFacingGuid!=Guid) {Failure=TEXT("NPC facing replay has no saved fixture"); Finish(C,false); return;}
                if(Phase==TEXT("track"))
                {
                    // Move around the creature without another movement
                    // or target packet; the ordinary render tick must follow us.
                    const FVector Offset=SavedOfflinePlayerPosition-Actor->GetActorLocation();
                    FVector Position=Actor->GetActorLocation()+FVector(-Offset.Y,Offset.X,Offset.Z);
                    Position.Z=SavedOfflinePlayerPosition.Z;
                    Pawn->SetActorLocation(Position,false,nullptr,ETeleportType::TeleportPhysics);
                    Pawn->SetActorRotation(FRotator(0,(Actor->GetActorLocation()-Position).Rotation().Yaw,0));
                }
                else if(Phase==TEXT("clear")) SendTarget(0);
                else if(Phase==TEXT("restore"))
                {
                    Entity->Movement.Orientation=SavedOfflineNpcOrientation;
                    Pawn->SetActorLocation(SavedOfflinePlayerPosition,false,nullptr,ETeleportType::TeleportPhysics);
                    Pawn->SetActorRotation(SavedOfflinePlayerRotation);
                    SendTarget(SavedOfflineNpcTarget); OfflineFacingGuid=0;
                }
                else {Failure=TEXT("Unknown NPC facing replay phase"); Finish(C,false); return;}
            }
            C.UpdateEntitySplineMovement(.016f);
            if(Phase==TEXT("restore")) Done=true;
            else
            {
                const FVector Expected=Phase==TEXT("clear")
                    ? FRotator(0,-FMath::RadiansToDegrees(OfflineNpcRawOrientation),0).Vector()
                    : (Pawn->GetActorLocation()-Actor->GetActorLocation()).GetSafeNormal2D();
                const double Dot=FVector::DotProduct(Actor->GetActorForwardVector().GetSafeNormal2D(),Expected);
                const bool TargetValid=Entity->GetField64(UnitField::TARGET)==(Phase==TEXT("clear")?0:Handler.EntityManager.LocalPlayerGuid);
                const bool RawUnchanged=FMath::IsNearlyEqual(Entity->Movement.Orientation,OfflineNpcRawOrientation,.0001f);
                const auto Row=MakeShared<FJsonObject>();
                Row->SetNumberField(TEXT("forwardDot"),Dot); Row->SetNumberField(TEXT("rawOrientation"),Entity->Movement.Orientation);
                Row->SetBoolField(TEXT("rawUnchanged"),RawUnchanged); Row->SetBoolField(TEXT("targetValid"),TargetValid);
                Inspections->SetObjectField(TEXT("npcFacing:")+Phase,Row);
                if(Dot<.999 || !TargetValid || !RawUnchanged)
                {Failure=FString::Printf(TEXT("NPC facing %s failed: dot=%.5f target=%d rawUnchanged=%d"),*Phase,Dot,TargetValid,RawUnchanged); Finish(C,false); return;}
                Done=true;
            }
        }
        else if(Op==TEXT("offline_corpse"))
        {
            const uint64 Guid=FindEntry(C,Step->GetIntegerField(TEXT("entry")),true);
            auto* Entity=Handler.EntityManager.Find(Guid);
            if(!Entity) {Failure=TEXT("Offline corpse fixture was not found"); Finish(C,false); return;}
            if(Step->GetBoolField(TEXT("dead")))
            {
                if(!OfflineCorpseFields.Contains(Guid)) OfflineCorpseFields.Add(Guid,{Entity->GetField(UnitField::HEALTH),Entity->GetField(UnitField::DYNAMIC_FLAGS)});
                Entity->SetField(UnitField::HEALTH,0);
                Entity->SetField(UnitField::DYNAMIC_FLAGS,Entity->GetField(UnitField::DYNAMIC_FLAGS)|WowDynFlags::LOOTABLE);
            }
            else
            {
                const auto* Saved=OfflineCorpseFields.Find(Guid);
                if(!Saved) {Failure=TEXT("No saved offline corpse fixture to restore"); Finish(C,false); return;}
                Entity->SetField(UnitField::HEALTH,Saved->Key); Entity->SetField(UnitField::DYNAMIC_FLAGS,Saved->Value);
                OfflineCorpseFields.Remove(Guid);
            }
            Handler.EntityManager.OnEntityUpdated.Broadcast(*Entity); Done=true;
        }
        else if(Op==TEXT("assert_cursor"))
        {
            const FString Path=TEXT("Interface\\Cursor\\")+Step->GetStringField(TEXT("texture"));
            auto* Frames=C.UIManager->GetFrameManager();
            const auto* Image=Frames->GetNamedTexture(TEXT("IdleWowCursorIcon"));
            auto* Expected=Frames->LoadUITexture(Path);
            Done=Expected && Image && Image->IsVisible() && Image->GetBrush().GetResourceObject()==Expected;
            if(Done) Inspections->SetStringField(FString::Printf(TEXT("cursor:%d"),Index),Path);
        }
        else if(Op==TEXT("wait_character_select")) Done=Connection->GetState()==EWowSessionState::WorldHaveCharList;
        else if(Op==TEXT("assert_lobby_lifecycle"))
        {
            int32 LoginScenes=0,Previews=0,AudioOwners=0;AWowAudioManager* Audio=nullptr;
            for(TActorIterator<AWowLoginScene> It(C.GetWorld());It;++It) ++LoginScenes;
            for(TActorIterator<AWowCharacterPreview> It(C.GetWorld());It;++It) ++Previews;
            for(TActorIterator<AWowAudioManager> It(C.GetWorld());It;++It){++AudioOwners;Audio=*It;}
            Done=LoginScenes==Step->GetIntegerField(TEXT("loginScenes")) && Previews==Step->GetIntegerField(TEXT("previews")) && AudioOwners==1 &&
                Audio && Audio->IsGlueMusicPlaying()==Step->GetBoolField(TEXT("glueMusic")) && Audio->GetGlueEffectCount()==0;
        }
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
            if(!bInvoked) {bInvoked=true; PreviousProfileTime=Now; return;}
            Profile.FindOrAdd(TEXT("frameMs")).Add((Now-PreviousProfileTime)*1000); PreviousProfileTime=Now;
            Profile.FindOrAdd(TEXT("drawCalls")).Add(GNumDrawCallsRHI[0]);
            Profile.FindOrAdd(TEXT("presentMs")).Add(FPlatformTime::ToMilliseconds(GSwapBufferTime));
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
            const TArray<TSharedPtr<FJsonValue>>* Destination=nullptr;
            if(Step->TryGetArrayField(TEXT("position"),Destination) && Destination->Num()==3)
            {
                bool Direct=false; Step->TryGetBoolField(TEXT("direct"),Direct);
                Done=Walk(C,FWowCoordinate::WowToUE((*Destination)[0]->AsNumber(),(*Destination)[1]->AsNumber(),(*Destination)[2]->AsNumber()),1.5f,!Direct);
            }
        }
        else if (Op==TEXT("select_entry"))
        {
            bool IncludeDead=false; Step->TryGetBoolField(TEXT("includeDead"),IncludeDead);
            const uint64 Guid=FindEntry(C,Step->GetIntegerField(TEXT("entry")),IncludeDead);
            if (Guid) { C.SetTarget(Guid); Done=true; }
        }
        else if (Op==TEXT("click_world_unit") || Op==TEXT("click_world_unit_slate") || Op==TEXT("hover_world_unit"))
        {
            const bool ThroughSlate=Op==TEXT("click_world_unit_slate");
            if (PendingWorldClick)
            {
                if (!bWorldClickReleased)
                {
                    if (ThroughSlate)
                    {
                        float X=0,Y=0;
                        const bool SamePixel=C.GetMousePosition(X,Y) && FVector2D::Distance(FVector2D(X,Y),SlateClickPoint)<=2;
                        const bool Released=RouteWorldClick(C,false);
                        if (!SamePixel || !Released)
                        {
                            Failure=FString::Printf(TEXT("Slate click did not reach the expected viewport pixel (wanted %.0f,%.0f actual %.0f,%.0f released=%d)"),
                                SlateClickPoint.X,SlateClickPoint.Y,X,Y,Released); Finish(C,false); return;
                        }
                    }
                    else
                    {
                        TGuardValue<bool> Synthetic(C.bProbeSyntheticKeyboard,true);
                        C.InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::LeftMouseButton,IE_Released,0.f));
                    }
                    // PlayerInput dispatches the queued release on the next tick.
                    // Selection now deliberately happens on release, not press.
                    bWorldClickReleased=true;
                    return;
                }
                Done=C.TargetGuid==PendingWorldClick && uint64(Connection->GetTargetGuid())==PendingWorldClick;
                if (!Done)
                {
                    Failure=FString::Printf(TEXT("Released left mouse input did not select the visible world unit (expected=%llu controller=%llu connection=%llu %s)"),
                        PendingWorldClick,C.TargetGuid,uint64(Connection->GetTargetGuid()),*DescribeWorldClick(C));
                    Finish(C,false); return;
                }
                RestoreSlateCursor();
                PendingWorldClick=0;
                bWorldClickReleased=false;
            }
            else
            {
                const uint32 Entry=Step->GetIntegerField(TEXT("entry"));
                bool IncludeDead=false; Step->TryGetBoolField(TEXT("includeDead"),IncludeDead);
                int32 Width=0,Height=0;
                C.GetViewportSize(Width,Height);
                double Nearest=DBL_MAX;
                uint64 Guid=0;
                FVector2D Pointer;
                for (const auto& Unit:C.SpawnedEntityActors)
                {
                    const auto* Entity=Handler.EntityManager.Find(Unit.Key);
                    if (!Entity || Entity->Entry!=Entry || (!IncludeDead && Entity->GetHealth()<=0) || !IsValid(Unit.Value)) continue;
                    const auto* Body=FindObjectFast<UBoxComponent>(Unit.Value.Get(),TEXT("UnitSelection"));
                    if (!Body) continue;
                    const double Distance=FVector::DistSquared(Body->GetComponentLocation(),C.GetPawn()->GetActorLocation());
                    FVector2D Point;
                    if (Distance>=Nearest || !C.ProjectWorldLocationToScreen(Body->GetComponentLocation(),Point)
                        || Point.X<0 || Point.X>=Width || Point.Y<0 || Point.Y>=Height) continue;
                    if (C.UIManager->GetFrameManager()->HitTestFrames(Point.X,Point.Y)>=0 || C.GetWorldUnitAtScreen(Point.X,Point.Y)!=Unit.Key) continue;
                    Guid=Unit.Key; Pointer=Point; Nearest=Distance;
                }
                if (Guid)
                {
                    C.GameplayProbePointer=Pointer;
                    C.UIManager->GetFrameManager()->UpdateMouseHover(Pointer.X,Pointer.Y);
                    if (Op==TEXT("hover_world_unit")) Done=true;
                    else
                    {
                        if (C.TargetGuid==Guid) { Failure=TEXT("Clear or change the target before verifying a world click"); Finish(C,false); return; }
                        if (ThroughSlate)
                        {
                            // Let the real viewport derive the pointer from the
                            // Slate event. The target setter must read that pixel.
                            C.GameplayProbePointer.Reset();
                            SlateClickPoint=Pointer;
                            if (!RouteWorldClick(C,true)) { Failure=TEXT("Slate could not route the click to the game window"); Finish(C,false); return; }
                        }
                        else
                        {
                            TGuardValue<bool> Synthetic(C.bProbeSyntheticKeyboard,true);
                            C.InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::LeftMouseButton,IE_Pressed,1.f));
                        }
                        PendingWorldClick=Guid;
                        bWorldClickReleased=false;
                    }
                    UE_LOG(LogTemp,Log,TEXT("GameplayProbe: %s entry=%u guid=%llu viewport=(%.0f,%.0f)"),*Op,Entry,Guid,Pointer.X,Pointer.Y);
                }
            }
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
        else if (Op == TEXT("inspect_unit_overhead"))
        {
            const uint32 Entry = Step->GetIntegerField(TEXT("entry"));
            const uint64 Guid = FindEntry(C, Entry);
            const auto* Info = C.UnitOverheads.Find(Guid);
            const auto* Marker = Info ? Info->Marker.Get() : nullptr;
            const bool Visible = Marker && Marker->IsVisible() && Marker->GetStaticMesh();
            const FString Model = Visible ? Info->MarkerModel.ToString() : FString();
            const FString Expected = Step->GetStringField(TEXT("marker"));
            const bool Show = Step->GetBoolField(TEXT("showName"));
            const bool Painted = Info && Info->LastNamePaint && GFrameCounter - Info->LastNamePaint <= 2;
            Done = Info && Info->ShowName == Show && Painted == Show && Model == Expected;
            FString Subtitle;
            if (Done && Step->TryGetStringField(TEXT("subtitle"), Subtitle))
                Done = Info->Subtitle.ToString() == Subtitle;
            if (Done)
            {
                auto State = MakeShared<FJsonObject>();
                State->SetStringField(TEXT("name"), Info->Name.ToString());
                State->SetStringField(TEXT("subtitle"), Info->Subtitle.ToString());
                State->SetBoolField(TEXT("nameVisible"), Info->ShowName);
                State->SetBoolField(TEXT("painted"), Painted);
                if (Painted)
                {
                    const auto& Rect = Info->NamePixelRect;
                    State->SetArrayField(TEXT("namePixelRect"), {MakeShared<FJsonValueNumber>(Rect.X), MakeShared<FJsonValueNumber>(Rect.Y),
                        MakeShared<FJsonValueNumber>(Rect.Z), MakeShared<FJsonValueNumber>(Rect.W)});
                }
                State->SetNumberField(TEXT("status"), Handler.QuestGiverStatuses.FindRef(Guid));
                State->SetStringField(TEXT("model"), Model);
                State->SetNumberField(TEXT("markerPeriod"), Info->MarkerPeriod);
                const auto* Entity = Handler.EntityManager.Find(Guid);
                const auto* Actor = C.SpawnedEntityActors.FindRef(Guid).Get();
                const auto* Body = Actor ? Actor->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
                if (Entity && Body && Info->Selection.IsValid())
                {
                    State->SetNumberField(TEXT("display"), Entity->GetDisplayId());
                    State->SetNumberField(TEXT("serverScale"), Entity->Scale);
                    State->SetNumberField(TEXT("renderedScale"), Body->GetComponentScale().Z);
                    State->SetNumberField(TEXT("heightYards"), Info->Selection->GetScaledBoxExtent().Z * 2 / FWowCoordinate::SCALE);
                }
                Inspections->SetObjectField(Step->GetStringField(TEXT("label")), State);
            }
        }
        else if(Op==TEXT("inspect_target_model"))
        {
            const auto* Unit=Handler.EntityManager.FindUnit(C.TargetGuid);
            const uint32 Display=Step->GetIntegerField(TEXT("display"));
            Done=Unit && Display && C.ConfigureNpcCapture(Display,C.TargetGuid);
            if(Done) C.LogNpcCapture();
        }
        else if(Op==TEXT("frame_camera_target"))
        {
            auto* Pawn=Cast<AWowPlayerCharacter>(C.GetPawn()); const auto* Actor=C.SpawnedEntityActors.Find(C.TargetGuid);
            if(!Pawn || !Pawn->CameraBoom || !Actor || !IsValid(*Actor)) {Failure=TEXT("Camera framing requires a rendered target and player"); Finish(C,false); return;}
            const auto Direction=(*Actor)->GetActorLocation()-Pawn->GetActorLocation();
            double Distance=1000; Step->TryGetNumberField(TEXT("distance"),Distance);
            Distance = FMath::Clamp(Distance,200.,2000.);
            bool SideView=false; Step->TryGetBoolField(TEXT("sideView"),SideView);
            if (SideView) Done = FrameFromSide(C, *Actor->Get(), Distance);
            else
            {
                if (SideCamera.IsValid() && C.GetViewTarget() == SideCamera.Get()) C.SetViewTarget(Pawn);
                C.SetControlRotation(FRotator(-15,Direction.Rotation().Yaw,0)); Pawn->CameraBoom->TargetArmLength=Distance; Done=true;
            }
        }
        else if(Op==TEXT("wait_spell_missile"))
        {
            const uint32 Spell=Step->GetIntegerField(TEXT("spell")),Entry=Step->GetIntegerField(TEXT("targetEntry"));
            for(TActorIterator<AWowSpellMissile> It(C.GetWorld());It;++It)
            {
                auto* Missile=*It; const auto* Target=Handler.EntityManager.Find(Missile->GetTargetGuid());
                bool Reflected=false; Step->TryGetBoolField(TEXT("reflected"),Reflected);
                const bool Owner=Reflected?Missile->GetTargetGuid()==Handler.EntityManager.LocalPlayerGuid && Missile->GetCasterGuid()!=Handler.EntityManager.LocalPlayerGuid:Missile->GetCasterGuid()==Handler.EntityManager.LocalPlayerGuid;
                if(!Missile->IsFlying() || Missile->GetSpellId()!=Spell || !Owner || !Target || Target->Entry!=Entry) continue;
                if(!Reflected && C.TargetGuid==Missile->GetTargetGuid()) {Failure=TEXT("Projectile probe requires selection to have changed"); Finish(C,false); return;}
                const auto State=MakeShared<FJsonObject>(); State->SetStringField(TEXT("targetGuid"),FString::Printf(TEXT("%llu"),Missile->GetTargetGuid()));
                State->SetStringField(TEXT("selectedGuid"),FString::Printf(TEXT("%llu"),C.TargetGuid)); State->SetNumberField(TEXT("spell"),Spell);
                State->SetNumberField(TEXT("travelSeconds"),Missile->GetTravelTime()); Inspections->SetObjectField(Reflected?TEXT("spellReturn"):TEXT("spellMissile"),State); Done=true; break;
            }
        }
        else if(Op==TEXT("place_ground_spell"))
        {
            FVector Position=FVector::ZeroVector; if(auto* Actor=C.SpellActor(C.TargetGuid)) Position=FWowCoordinate::UEToWow(C.SpellAttachment(Actor,19)); else if(const auto* Player=Handler.EntityManager.GetLocalPlayer()) Position=Player->Movement.Position;
            Done=Connection->PlaceGroundSpell(Position);
        }
        else if(Op==TEXT("wait_spell_effect") || Op==TEXT("wait_no_spell_effect"))
        {
            const uint32 Spell=Step->GetIntegerField(TEXT("spell")); const FString Phase=Step->GetStringField(TEXT("phase"));
            const FName SpellTag(*FString::Printf(TEXT("spell:%u"),Spell)),PhaseTag(*FString::Printf(TEXT("phase:%s"),*Phase));
            uint32 Entry = 0;
            Step->TryGetNumberField(TEXT("entry"), Entry);
            const uint64 Guid = Entry ? FindEntry(C, Entry) : 0;
            const auto* Unit = Guid ? Handler.EntityManager.FindUnit(Guid) : nullptr;
            const auto* TargetActor = Guid ? C.SpellActor(Guid) : nullptr;
            bool RequireAura = false;
            Step->TryGetBoolField(TEXT("requireAura"), RequireAura);
            const bool HasAura = Unit && Unit->Auras.ContainsByPredicate([Spell](const FAuraInfo& Aura) {
                return Aura.bActive && Aura.SpellId == Spell;
            });
            int32 Components=0,Particles=0,Geometry=0; TArray<TSharedPtr<FJsonValue>> Models,Ids;
            for(TActorIterator<AActor> It(C.GetWorld());It;++It)
            {
                if (Entry && *It != TargetActor) continue;
                TInlineComponentArray<UWowM2EmitterComponent*> Effects(*It);
                for(auto* Effect:Effects) if(Effect->ComponentHasTag(SpellTag) && Effect->ComponentHasTag(PhaseTag))
                {
                    if(Effect->GetParticleCount()==0 && (!Effect->IsEmitting() || Effect->GetGeometryCount()==0)) continue;
                    ++Components; Particles+=Effect->GetParticleCount(); Geometry+=Effect->GetGeometryCount(); Ids.Add(MakeShared<FJsonValueNumber>(Effect->GetUniqueID()));
                    if(Effect->ComponentTags.Num()) Models.Add(MakeShared<FJsonValueString>(Effect->ComponentTags[0].ToString()));
                }
            }
            double Minimum=1; Step->TryGetNumberField(TEXT("minComponents"),Minimum);
            double MinParticles=0,MinGeometry=0; Step->TryGetNumberField(TEXT("minParticles"),MinParticles); Step->TryGetNumberField(TEXT("minGeometry"),MinGeometry);
            const bool ExpectedEffect = Op==TEXT("wait_no_spell_effect") ? Components==0 : Components>=Minimum && Particles>=MinParticles && Geometry>=MinGeometry;
            Done = ExpectedEffect && (!Entry || TargetActor) && (!RequireAura || HasAura);
            if (Done)
            {
                const auto Row = MakeShared<FJsonObject>();
                Row->SetNumberField(TEXT("components"), Components);
                Row->SetNumberField(TEXT("particles"), Particles);
                Row->SetNumberField(TEXT("geometryPasses"), Geometry);
                Row->SetArrayField(TEXT("models"), Models);
                Row->SetArrayField(TEXT("ids"), Ids);
                if (Unit)
                {
                    Row->SetBoolField(TEXT("serverAuraActive"), HasAura);
                    if (const auto* Viewer = Handler.EntityManager.GetLocalPlayer())
                        Row->SetNumberField(TEXT("viewerAuraVision"), Viewer->GetFieldByte(PlayerField::FIELD_BYTES2, 3));
                }
                FString Label;
                Step->TryGetStringField(TEXT("label"),Label);
                Inspections->SetObjectField(FString::Printf(TEXT("effect:%u:%s:%s"),Spell,*Phase,*Label),Row);
            }
        }
        else if(Op==TEXT("wait_cast_particles"))
        {
            const auto* State=C.AuthoredCastEffects.Find(Handler.EntityManager.LocalPlayerGuid);
            if(State && State->Spell==uint32(Step->GetIntegerField(TEXT("spell"))))
            {
                int32 Count=0,Particles=0; TArray<TSharedPtr<FJsonValue>> Rows;
                double MinimumTextures=0; Step->TryGetNumberField(TEXT("minTextures"),MinimumTextures); bool TexturesValid=true;
                for(const auto& Weak:State->Components) if(auto* Effect=Weak.Get())
                {
                    ++Count; Particles+=Effect->GetParticleCount(); const auto Row=MakeShared<FJsonObject>(); Row->SetStringField(TEXT("bone"),Effect->GetAttachSocketName().ToString());
                    Row->SetNumberField(TEXT("emitters"),Effect->GetEmitterCount()); Row->SetNumberField(TEXT("particles"),Effect->GetParticleCount());
                    Row->SetNumberField(TEXT("renderCpuMs"),Effect->GetRenderMilliseconds());
                    Row->SetStringField(TEXT("location"),Effect->GetComponentLocation().ToString());
                    TArray<USceneComponent*> Children; Effect->GetChildrenComponents(false,Children);
                    for(auto* Child:Children) if(auto* Mesh=Cast<UProceduralMeshComponent>(Child))
                    {
                        TSet<UTexture*> UniqueTextures;
                        for(int32 I=0;I<Mesh->GetNumMaterials();++I) if(auto* Material=Mesh->GetMaterial(I))
                        {
                            UTexture* Texture=nullptr; if(Material->GetTextureParameterValue(FMaterialParameterInfo(TEXT("ParticleTexture")),Texture) && Texture) UniqueTextures.Add(Texture);
                        }
                        Row->SetNumberField(TEXT("distinctTextures"),UniqueTextures.Num()); TexturesValid&=UniqueTextures.Num()>=MinimumTextures;
                        Row->SetBoolField(TEXT("visible"),Mesh->IsVisible() && !Mesh->bHiddenInGame);
                        Row->SetBoolField(TEXT("rendered"),Mesh->WasRecentlyRendered(.5f));
                        Row->SetStringField(TEXT("bounds"),Mesh->Bounds.ToString());
                        if(auto* Material=Mesh->GetMaterial(0))
                        {
                            Row->SetStringField(TEXT("material"),Material->GetPathName()); UTexture* Texture=nullptr;
                            if(Material->GetTextureParameterValue(FMaterialParameterInfo(TEXT("ParticleTexture")),Texture) && Texture) Row->SetStringField(TEXT("texture"),Texture->GetPathName());
                        }
                        if(const auto* Section=Mesh->GetProcMeshSection(0); Section && Section->ProcVertexBuffer.Num())
                        {
                            const auto& Vertex=Section->ProcVertexBuffer[0];
                            Row->SetStringField(TEXT("vertex"),Mesh->GetComponentTransform().TransformPosition(FVector(Vertex.Position)).ToString());
                            Row->SetStringField(TEXT("color"),Vertex.Color.ToString());
                            Row->SetStringField(TEXT("uv"),Vertex.UV0.ToString());
                        }
                    }
                    if(Effect->ComponentTags.Num()) Row->SetStringField(TEXT("model"),Effect->ComponentTags[0].ToString()); Rows.Add(MakeShared<FJsonValueObject>(Row));
                }
                double Minimum=12; Step->TryGetNumberField(TEXT("minParticles"),Minimum);
                if(Count>=2 && Particles>=Minimum && TexturesValid) {Inspections->SetArrayField(TEXT("castParticles"),Rows); Done=true;}
            }
        }
        else if(Op==TEXT("particle_debug_material"))
        {
            FString Mode; Step->TryGetStringField(TEXT("mode"),Mode);
            if(const auto* State=C.AuthoredCastEffects.Find(Handler.EntityManager.LocalPlayerGuid))
                for(const auto& Weak:State->Components) if(auto* Effect=Weak.Get())
                {
                    TArray<USceneComponent*> Children; Effect->GetChildrenComponents(false,Children);
                    for(auto* Child:Children) if(auto* Mesh=Cast<UProceduralMeshComponent>(Child))
                        for(int32 I=0;I<Mesh->GetNumMaterials();++I)
                        {
                            if(Mode==TEXT("white"))
                            {
                                if(auto* Material=Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(I)))
                                {Material->SetTextureParameterValue(TEXT("ParticleTexture"),LoadObject<UTexture>(nullptr,TEXT("/Engine/EngineResources/WhiteSquareTexture"))); Material->SetScalarParameterValue(TEXT("IgnoreAlpha"),1);}
                            }
                            else Mesh->SetMaterial(I,LoadObject<UMaterialInterface>(nullptr,TEXT("/Engine/BasicShapes/BasicShapeMaterial")));
                        }
                }
            Done=true;
        }
        else if(Op==TEXT("wait_no_cast_particles"))
        {
            if(APawn* Pawn=C.GetPawn())
            {
                TArray<UWowM2EmitterComponent*> Components; Pawn->GetComponents(Components);
                Done=!C.AuthoredCastEffects.Contains(Handler.EntityManager.LocalPlayerGuid) && !Components.ContainsByPredicate([](const auto* Effect){return IsValid(Effect);});
            }
        }
        else if (Op==TEXT("start_melee")) { C.StartAutoAttack(); Done=C.bIsAutoAttacking; }
        else if(Op==TEXT("offline_melee_variations"))
        {
            auto* Player=Cast<AWowPlayerCharacter>(C.GetPawn());
            auto* Runtime=Player && Player->AnimationController?Player->AnimationController->GetRuntimeInstance():nullptr;
            const auto* Self=Handler.EntityManager.GetLocalPlayer();
            const auto* Victim=Handler.EntityManager.Find(C.TargetGuid);
            if(!Runtime || !Self || !Victim || Victim->GetHealth()<=0)
            {Failure=TEXT("Offline swing replay requires a rendered player and living target"); Finish(C,false); return;}
            const uint32 Weapon=Self->GetField(PlayerField::VISIBLE_ITEM_BASE+PlayerField::EQUIPMENT_SLOT_MAINHAND*2);
            const int32 Expected=FDbcStore::Get().ItemUi().MeleeSequences(Weapon).Attack;
            if(Expected!=17 && Expected!=18 && Expected!=19)
            {Failure=TEXT("Offline swing replay requires a real equipped melee weapon"); Finish(C,false); return;}
            if(!bInvoked)
            {
                Runtime->SetM2VariationSeed(1234); SwingCount=0; NextSwingAt=0; bSampleSwing=false;
                SwingVariations.Reset(); SwingSamples.Reset(); SwingCapture.Empty(); bInvoked=true;
            }
            if(!SwingCapture.IsEmpty())
            {
                if(IFileManager::Get().FileSize(*SwingCapture)<=0) return;
                SwingCapture.Empty();
            }
            if(bSampleSwing && Runtime->GetM2AnimationId()==Expected && Runtime->GetM2Time()>=Runtime->GetM2Duration()*.35f)
            {
                const int32 Variation=Runtime->GetM2VariationId();
                const auto Row=MakeShared<FJsonObject>(); Row->SetNumberField(TEXT("swing"),SwingCount);
                Row->SetNumberField(TEXT("weapon"),Weapon); Row->SetNumberField(TEXT("clip"),Expected);
                Row->SetNumberField(TEXT("variation"),Variation); Row->SetNumberField(TEXT("seconds"),Runtime->GetM2Time());
                Row->SetNumberField(TEXT("duration"),Runtime->GetM2Duration());
                SwingSamples.Add(MakeShared<FJsonValueObject>(Row));
                if(!SwingVariations.Contains(Variation))
                {
                    SwingCapture=Output/FString::Printf(TEXT("07-attack-variation-%d.png"),Variation);
                    FScreenshotRequest::RequestScreenshot(SwingCapture,true,false);
                    SwingVariations.Add(Variation);
                }
                bSampleSwing=false;
            }
            if(!bSampleSwing && Now>=NextSwingAt && SwingCapture.IsEmpty())
            {
                const int32 Limit=FMath::Clamp(Step->GetIntegerField(TEXT("swings")),2,32);
                if(SwingCount>=Limit)
                {
                    Inspections->SetArrayField(TEXT("meleeSwings"),SwingSamples);
                    Inspections->SetNumberField(TEXT("meleeVariationCount"),SwingVariations.Num());
                    if(SwingVariations.Num()<2) {Failure=TEXT("Repeated offline swings never chose a second original M2 variation"); Finish(C,false); return;}
                    Done=true;
                }
                else
                {
                    Handler.OnAttackerStateUpdate.Broadcast(Self->Guid,C.TargetGuid,0,1);
                    if(Runtime->GetM2AnimationId()!=Expected) {Failure=TEXT("Combat handler chose a different equipped-weapon animation"); Finish(C,false); return;}
                    ++SwingCount; bSampleSwing=true; NextSwingAt=Now+Runtime->GetM2Duration()+.15;
                }
            }
        }
        else if (Op==TEXT("wait_unit_animation"))
        {
            const FString Unit=Step->GetStringField(TEXT("unit"));
            const uint64 Guid=Unit==TEXT("player") ? Handler.EntityManager.LocalPlayerGuid : C.TargetGuid;
            AActor* Actor=C.SpellActor(Guid);
            const auto* Player=Cast<AWowPlayerCharacter>(Actor);
            auto* Animation=Player ? Player->AnimationController.Get() : FWowCharacterBuilder::GetAnimationController(Actor);
            const auto* Runtime=Animation ? Animation->GetRuntimeInstance() : nullptr;
            double Progress=0; Step->TryGetNumberField(TEXT("progress"),Progress);
            const auto* Entity=Handler.EntityManager.Find(Guid);
            Done=Entity && Runtime && Runtime->GetM2AnimationId()==Step->GetIntegerField(TEXT("id")) &&
                Runtime->GetM2Time()>=Runtime->GetM2Duration()*Progress;
            if (Done)
            {
                const auto Row=MakeShared<FJsonObject>();
                Row->SetStringField(TEXT("unit"),Unit); Row->SetNumberField(TEXT("health"),Entity->GetHealth());
                Row->SetNumberField(TEXT("clip"),Runtime->GetM2AnimationId());
                Row->SetNumberField(TEXT("variation"),Runtime->GetM2VariationId());
                Row->SetNumberField(TEXT("seconds"),Runtime->GetM2Time()); Row->SetNumberField(TEXT("duration"),Runtime->GetM2Duration());
                if (Player) Row->SetBoolField(TEXT("grounded"),Player->GetCharacterMovement()->IsMovingOnGround());
                Inspections->SetObjectField(FString::Printf(TEXT("animation:%d"),Index),Row);
            }
        }
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
        else if (Op==TEXT("quest"))
        {
            const uint32 Id = Step->GetIntegerField(TEXT("id"));
            Done = C.GameUI->GetShownQuestId() == Id;
            if (!Done && Handler.GossipMenu.Guid == C.TargetGuid &&
                Handler.GossipMenu.Quests.ContainsByPredicate([Id](const FWowGossipQuest& Quest){ return Quest.Id == Id; }))
            {
                C.GameUI->SelectGossipQuest(Id);
                Done = true;
            }
        }
        else if (Op==TEXT("wait_dialog")) Done=C.GameUI->GetShownQuestId()==uint32(Step->GetIntegerField(TEXT("id")));
        else if (Op==TEXT("dialog_button") || Op==TEXT("wait_dialog_text") || Op==TEXT("wait_dialog_closed")) Done=ProbeDialog(C,*Step,Op);
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
        else if(Op==TEXT("inspect_render_settings")) {Inspections->SetObjectField(TEXT("graphics:")+Step->GetStringField(TEXT("label")),WowGraphicsSnapshot(C.GetWorld(),&C)); Done=true;}
        else if (Op==TEXT("lua")) { Done=C.UIManager->GetLuaVM()->ExecuteString(Step->GetStringField(TEXT("code")),TEXT("=GameplayProbe")); if (!Done) { Failure=TEXT("Lua assertion failed"); Finish(C,false); return; } }
        else if(Op==TEXT("lua_wait"))
        {
            auto* VM=C.UIManager->GetLuaVM();
            if(!VM->ExecuteString(TEXT("QAProbeCondition=tostring(not not (")+Step->GetStringField(TEXT("condition"))+TEXT("))"),TEXT("=GameplayProbeCondition"))) {Failure=TEXT("Lua wait condition failed"); Finish(C,false); return;}
            Done=VM->GetGlobalString(TEXT("QAProbeCondition"))==TEXT("true");
        }
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
            if(const auto* Def=Frames->GetFrameDef(Handle)) { Row->SetNumberField(TEXT("level"),Def->FrameLevel); Row->SetNumberField(TEXT("strata"),int32(Def->Strata)); Row->SetBoolField(TEXT("hidden"),Def->bHidden); Row->SetBoolField(TEXT("hiddenSpecified"),Def->bHiddenSpecified); Row->SetBoolField(TEXT("drawn"),Frames->IsFrameDrawn(Handle)); }
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
            if (const auto* Bar = Frames->GetStatusBarWidget(Handle))
                Row->SetNumberField(TEXT("percent"), Bar->GetPercent());
            if (Name == TEXT("MainMenuExpBar"))
            {
                if (const auto* Player = Handler.EntityManager.GetLocalPlayer())
                {
                    Row->SetNumberField(TEXT("xp"), Player->GetField(PlayerField::XP));
                    Row->SetNumberField(TEXT("xpMax"), Player->GetField(PlayerField::NEXT_LEVEL_XP));
                    Row->SetNumberField(TEXT("playerLevel"), Player->GetLevel());
                }
            }
            FString Label = Name;
            Step->TryGetStringField(TEXT("label"), Label);
            if (Handle<0)
            {
                const UWidget* Region=Text;
                if (!Region) Region=Frames->GetNamedTexture(Name);
                const bool Drawn=Region && Region->IsVisible() && Frames->IsRegionOwnerDrawn(Name) && Frames->IsRegionAnchored(Name);
                Row->SetBoolField(TEXT("drawn"),Drawn);
                bool Expected=false;
                if (Step->TryGetBoolField(TEXT("expectedDrawn"),Expected) && Drawn!=Expected)
                {
                    Failure=FString::Printf(TEXT("Unexpected rendered state for %s"),*Name);
                    Finish(C,false);
                    return;
                }
            }
            Inspections->SetObjectField(Label,Row); Done=true;
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
            bool Slate=false; Step->TryGetBoolField(TEXT("slate"),Slate);
            const bool Release=Step->GetStringField(TEXT("event"))==TEXT("release");
            if(Slate)
            {
                const auto Focused=FSlateApplication::IsInitialized()?FSlateApplication::Get().GetKeyboardFocusedWidget():nullptr;
                if(!Focused || !Focused->GetType().ToString().Contains(TEXT("EditableText"))) {Failure=TEXT("Slate input requires a focused editor"); Finish(C,false); return;}
                TGuardValue<bool> AllowText(InputGuard->SyntheticText,true);
                const FKeyEvent Event(Key,FModifierKeysState(),0,false,0,0);
                if(Release) FSlateApplication::Get().ProcessKeyUpEvent(Event); else FSlateApplication::Get().ProcessKeyDownEvent(Event);
            }
            else C.InputKey(FInputKeyEventArgs::CreateSimulated(Key,Release?IE_Released:IE_Pressed,1.f)); Done=true;
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
            bool RequireDialogHover = false;
            Step->TryGetBoolField(TEXT("requireDialogHover"), RequireDialogHover);
            const auto Hovered = ExpectedDialogHover.Pin();
            if (RequireDialogHover && (!Hovered || !Hovered->IsHovered()))
            {
                Failure = TEXT("Dialog control lost hover before its screenshot");
                Finish(C,false);
                return;
            }
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
                    Metric->SetNumberField(TEXT("p99"),Values[FMath::Clamp(FMath::CeilToInt(Values.Num()*.99)-1,0,Values.Num()-1)]);
                    Metric->SetNumberField(TEXT("above50"),Values.FilterByPredicate([](double Value){return Value>50;}).Num());
                    Metrics->SetObjectField(Column.Key,Metric);
                }
                Result->SetObjectField(TEXT("profile"),Metrics); Profile.Reset();
                const auto Memory=FPlatformMemory::GetStats();
                Result->SetNumberField(TEXT("privateMiB"),Memory.UsedVirtual/1048576.);
                Result->SetNumberField(TEXT("workingSetMiB"),Memory.UsedPhysical/1048576.);
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
