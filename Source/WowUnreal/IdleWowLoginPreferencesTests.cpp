#include "Misc/AutomationTest.h"
#include "WowLoginPreferences.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIdleWowRememberAccountTest, "WowUnreal.Login.RememberAccount",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FIdleWowRememberAccountTest::RunTest(const FString& Parameters)
{
    const FString Directory = FPaths::ProjectSavedDir() / TEXT("Automation/LoginPreferences") / FGuid::NewGuid().ToString();
    IFileManager::Get().MakeDirectory(*Directory, true);
    const FString Path = Directory / TEXT("WowLogin.ini");
    const FString CredentialsPath = Directory / TEXT("WowCredentials.json");
    const FString Credentials = TEXT("[{\"alias\":\"Existing autologin\",\"pw\":\"unchanged-sentinel\"}]");
    ON_SCOPE_EXIT
    {
        IFileManager::Get().Delete(*Path);
        IFileManager::Get().Delete(*CredentialsPath);
        IFileManager::Get().DeleteDirectory(*Directory);
    };
    TestTrue(TEXT("Independent credential fixture is created"), FFileHelper::SaveStringToFile(Credentials, *CredentialsPath));

    FWowLoginPreferences Preferences;
    TestFalse(TEXT("Fresh profile has no saved override"), Preferences.Load(Path));
    Preferences.Server = TEXT("localhost:3730");
    Preferences.AccountName = TEXT("MixedCaseAccount");
    TestTrue(TEXT("Remembered name is saved"), Preferences.Save(Path));

    FWowLoginPreferences Restored;
    TestTrue(TEXT("Saved preference is found"), Restored.Load(Path));
    TestTrue(TEXT("Remember setting survives restart"), Restored.bRememberAccount);
    TestEqual(TEXT("Custom server survives restart"), Restored.Server, Preferences.Server);
    TestEqual(TEXT("Account spelling survives restart"), Restored.AccountName, Preferences.AccountName);

    Preferences.bRememberAccount = false;
    TestTrue(TEXT("Disabling remember is saved"), Preferences.Save(Path));
    TestTrue(TEXT("Disabled setting is loaded"), Restored.Load(Path));
    TestFalse(TEXT("Remember remains off after restart"), Restored.bRememberAccount);
    TestTrue(TEXT("Previously remembered name is cleared"), Restored.AccountName.IsEmpty());
    TestEqual(TEXT("Server remains available without a remembered name"), Restored.Server, Preferences.Server);

    FString SavedText;
    TestTrue(TEXT("Preferences can be inspected"), FFileHelper::LoadFileToString(SavedText, *Path));
    TestFalse(TEXT("Disabled name is removed from disk"), SavedText.Contains(TEXT("MixedCaseAccount")));
    TestFalse(TEXT("Preference file contains no password field"), SavedText.Contains(TEXT("password")));
    FString UnchangedCredentials;
    TestTrue(TEXT("Independent credentials remain readable"), FFileHelper::LoadFileToString(UnchangedCredentials, *CredentialsPath));
    TestEqual(TEXT("Remember settings leave explicit autologin credentials unchanged"), UnchangedCredentials, Credentials);
    return true;
}
#endif
