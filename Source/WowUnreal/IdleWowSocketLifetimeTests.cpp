#include "Misc/AutomationTest.h"
#include "HAL/PlatformTime.h"
#include "Net/WowWorldSocket.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIdleWowWorldSocketLifetimeTest, "WowUnreal.Network.QueuedPacketSocketLifetime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FIdleWowWorldSocketLifetimeTest::RunTest(const FString& Parameters)
{
    auto CancelledCalls = MakeShared<int32>(0);
    auto LiveCalls = MakeShared<int32>(0);
    TSharedPtr<FWowWorldSocket, ESPMode::ThreadSafe> Removed = MakeShared<FWowWorldSocket, ESPMode::ThreadSafe>();
    Removed->CallbackOwner = Removed;
    Removed->OnPacket.BindLambda([CancelledCalls](uint16, const TArray<uint8>&) { ++*CancelledCalls; });
    Removed->DispatchToGameThread([](FWowWorldSocket& Socket) { Socket.OnPacket.ExecuteIfBound(0, {}); });
    Removed.Reset();

    auto Live = MakeShared<FWowWorldSocket, ESPMode::ThreadSafe>();
    Live->CallbackOwner = Live;
    Live->OnPacket.BindLambda([LiveCalls](uint16, const TArray<uint8>&) { ++*LiveCalls; });
    Live->DispatchToGameThread([](FWowWorldSocket& Socket) { Socket.OnPacket.ExecuteIfBound(0, {}); });
    const double Started = FPlatformTime::Seconds();
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, CancelledCalls, LiveCalls, Live, Started]()
    {
        if (FPlatformTime::Seconds() - Started < 0.1) return false;
        TestEqual(TEXT("Queued packets outlive socket destruction without invoking its delegates"), *CancelledCalls, 0);
        TestEqual(TEXT("A live socket still delivers its queued packet"), *LiveCalls, 1);
        return true;
    }));
    return true;
}
#endif
