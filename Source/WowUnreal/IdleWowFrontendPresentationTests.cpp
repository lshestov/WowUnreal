#include "Misc/AutomationTest.h"
#include "SWowLoadingScreen.h"
#include "WowFrontendCursor.h"
#include "WowGameplayController.h"
#include "WowGameViewportClient.h"
#include "Mpq/MpqManager.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Input/CursorReply.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SNullWidget.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIdleWowDialogCursorTest, "WowUnreal.Input.NativeWidgetCursor",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FIdleWowDialogCursorTest::RunTest(const FString&)
{
    auto* World = UWorld::CreateWorld(EWorldType::Game, false);
    if (!TestNotNull(TEXT("Created the cursor test world"), World)) return false;
    auto& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
    TStrongObjectPtr<UGameInstance> Instance(NewObject<UGameInstance>(GEngine));
    TStrongObjectPtr<UWowGameViewportClient> Viewport(NewObject<UWowGameViewportClient>(GEngine));
    Context.OwningGameInstance = Instance.Get();
    Context.GameViewport = Viewport.Get();
    World->SetGameInstance(Instance.Get());
    Context.SetCurrentWorld(World);
    Viewport->Init(Context, Instance.Get(), false);
    const auto ConfiguredArrow = SNew(SBox);
    Viewport->SetUseSoftwareCursorWidgets(true);
    Viewport->SetSoftwareCursorWidget(EMouseCursor::Default, ConfiguredArrow);
    auto* Controller = World->SpawnActor<AWowGameplayController>();
    // Actor initialization normally registers controllers during map startup.
    // This fixture has no level startup, so register its controller explicitly.
    if (Controller) World->AddController(Controller);
    TestTrue(TEXT("The viewport resolves the current game world"), Viewport->GetWorld() == World);
    TestTrue(TEXT("The world resolves the configured game viewport"), World->GetGameViewport() == Viewport.Get());
    if (!TestNotNull(TEXT("Spawned the gameplay cursor owner"), Controller) ||
        !TestTrue(TEXT("The world resolves the controller driving the cursor"), World->GetFirstPlayerController() == Controller))
    {
        World->DestroyWorld(false);
        GEngine->DestroyWorldContext(World);
        return false;
    }
    Controller->CurrentMouseCursor = EMouseCursor::None;
    for (auto Requested : {EMouseCursor::Default, EMouseCursor::Hand, EMouseCursor::TextEditBeam})
    {
        const auto Mapping = Viewport->MapCursor(nullptr, FCursorReply::Cursor(Requested));
        TestTrue(FString::Printf(TEXT("Native cursor request %d keeps the hardware pointer hidden under the WoW cursor"), int32(Requested)),
            Mapping.IsSet() && Mapping.GetValue() == SNullWidget::NullWidget);
    }
    Controller->CurrentMouseCursor = EMouseCursor::Default;
    const auto Mapping = Viewport->MapCursor(nullptr, FCursorReply::Cursor(EMouseCursor::Default));
    TestTrue(TEXT("Relinquishing the WoW cursor restores the viewport's configured mapping"),
        Mapping.IsSet() && Mapping.GetValue() == ConfiguredArrow);
    Controller->Destroy();
    World->DestroyWorld(false);
    GEngine->DestroyWorldContext(World);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIdleWowFrontendPresentationTest,"WowUnreal.Login.LoadingBarAndCursor",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FIdleWowFrontendPresentationTest::RunTest(const FString&)
{
    FString Path;
    if(!FParse::Value(FCommandLine::Get(),TEXT("wowdata="),Path)) return false;
    FMpqManager Mpq;
    if(!TestTrue(TEXT("User's original client archives are mounted"),Mpq.Initialize(Path))) return false;
    auto Loading=SNew(SWowLoadingScreen).Mpq(&Mpq);
    TestTrue(TEXT("Both original loading-bar textures are usable"),Loading->HasProgressArtwork());
    Loading->SetProgress(-1,TEXT("negative"));TestEqual(TEXT("Loading starts empty"),Loading->GetProgress(),0.f);
    Loading->SetProgress(.5f,TEXT("terrain"));TestEqual(TEXT("Partial progress retained"),Loading->GetProgress(),.5f);
    Loading->SetProgressText(TEXT("Connecting"));TestEqual(TEXT("Status changes do not reset progress"),Loading->GetProgress(),.5f);
    Loading->SetProgress(2,TEXT("finished"));TestEqual(TEXT("Overrun clamps to full"),Loading->GetProgress(),1.f);

    // A menu visit must never leave invisible cursor mappings behind in the
    // game or replace mappings that belonged to another viewport owner.
    TStrongObjectPtr<UGameViewportClient> Viewport(NewObject<UGameViewportClient>(GEngine));
    auto Overlay=SNew(SOverlay);
    Viewport->SetViewportOverlayWidget(nullptr,Overlay);
    auto Previous=SNew(SBox);
    Viewport->SetUseSoftwareCursorWidgets(false);
    Viewport->SetSoftwareCursorWidget(EMouseCursor::Default,Previous);
    {
        FWowFrontendCursor Cursor(&Mpq);
        TestTrue(TEXT("Original glove is loaded"),Cursor.IsReady());
        Cursor.Show(Viewport.Get());Cursor.Show(Viewport.Get());
        TestTrue(TEXT("Frontend cursor owns the viewport"),Cursor.IsActive());
        TestTrue(TEXT("Frontend suppresses OS cursor through viewport mapping"),Viewport->GetIsUsingSoftwareCursorWidgets());
        for(auto Type:{EMouseCursor::Default,EMouseCursor::TextEditBeam,EMouseCursor::Hand})
            TestTrue(TEXT("Arrow, edit beam and hand are hidden under the drawn glove"),Viewport->GetSoftwareCursorWidget(Type)==SNullWidget::NullWidget);
        Cursor.Hide();
        TestFalse(TEXT("World entry relinquishes cursor ownership"),Cursor.IsActive());
        TestTrue(TEXT("Previous arrow mapping survives menu round trip"),Viewport->GetSoftwareCursorWidget(EMouseCursor::Default)==Previous);
        TestFalse(TEXT("Software mode restored"),Viewport->GetIsUsingSoftwareCursorWidgets());
        TestFalse(TEXT("No edit-beam mapping leaks into the world"),Viewport->HasSoftwareCursor(EMouseCursor::TextEditBeam));
        Cursor.Show(Viewport.Get());
    }
    TestTrue(TEXT("Owner destruction restores cursor mapping"),Viewport->GetSoftwareCursorWidget(EMouseCursor::Default)==Previous);
    TestFalse(TEXT("Owner destruction restores cursor mode"),Viewport->GetIsUsingSoftwareCursorWidgets());
    return true;
}
#endif
