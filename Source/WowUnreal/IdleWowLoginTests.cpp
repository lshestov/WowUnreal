#include "Misc/AutomationTest.h"
#include "WowLoginWidget.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIdleWowLoginEndpointTest, "WowUnreal.Login.AuthEndpoint",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FIdleWowLoginEndpointTest::RunTest(const FString& Parameters)
{
    TSharedRef<SWowLoginWidget> Widget = SNew(SWowLoginWidget);
    int32 Submissions = 0;
    FString SubmittedHost;
    int32 SubmittedPort = 0;
    Widget->OnLoginSubmit.BindLambda([&](const FString& Host, int32 Port, const FString&, const FString&)
    {
        ++Submissions;
        SubmittedHost = Host;
        SubmittedPort = Port;
    });

    // A cleared server field should use the project's actual auth endpoint.
    Widget->SetCredentials(TEXT(""), TEXT("test"), TEXT("test"));
    Widget->AutoSubmit();
    TestEqual(TEXT("Empty field submits once"), Submissions, 1);
    TestEqual(TEXT("Default host"), SubmittedHost, FString(TEXT("127.0.0.1")));
    TestEqual(TEXT("Default auth port"), SubmittedPort, 3724);

    for (const TCHAR* Address : {TEXT("127.0.0.1:8085"), TEXT("localhost:8085"), TEXT("127.0.0.1:0"), TEXT("127.0.0.1:65536")})
    {
        Widget->SetCredentials(Address, TEXT("test"), TEXT("test"));
        Widget->AutoSubmit();
    }
    TestEqual(TEXT("World and invalid ports never send an auth packet"), Submissions, 1);

    Widget->SetCredentials(TEXT("127.0.0.1:3725"), TEXT("test"), TEXT("test"));
    Widget->AutoSubmit();
    TestEqual(TEXT("Custom auth port remains supported"), Submissions, 2);
    TestEqual(TEXT("Custom auth port"), SubmittedPort, 3725);
    Widget->OnLoginSubmit.Unbind();
    return true;
}
#endif
