#include "Misc/AutomationTest.h"
#include "WowStreamingBudget.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIdleWowStreamingBudget,"WowUnreal.Performance.StreamingBudgetBoundaries",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FIdleWowStreamingBudget::RunTest(const FString&)
{
    FWowStreamingBudget B(100);
    TestTrue(TEXT("One indivisible job can make progress after a delayed tick"),B.CanStart(0,101)); B.Complete(0);
    TestFalse(TEXT("A late heavy job yields immediately after completion"),B.CanStart(1,101));
    FWowStreamingBudget T(100); T.Complete(0);
    TestFalse(TEXT("LOD and WDL cannot add a second terrain upload"),T.CanStart(3,100) || T.CanStart(4,100));
    TestTrue(TEXT("Cheap object work can use remaining time"),T.CanStart(1,100.0005));
    TestFalse(TEXT("Time budget stops new work at its boundary"),T.CanStart(2,100.0011));
    FWowStreamingBudget C(100); for(int32 I=0;I<8;++I){TestTrue(TEXT("Count-bounded jobs may progress within time"),C.CanStart(1,100)); C.Complete(1);}
    TestFalse(TEXT("Even zero-time jobs cannot drain an arbitrary queue"),C.CanStart(1,100));
    return true;
}
#endif
