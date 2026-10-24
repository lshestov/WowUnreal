#include "Misc/AutomationTest.h"
#include "WowAutoLoginOptions.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIdleWowAutoLoginArguments, "WowUnreal.Login.AutoLogin.Arguments",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FIdleWowAutoLoginArguments::RunTest(const FString& Parameters)
{
    const auto Options = FWowAutoLoginOptions::Parse(
        TEXT("-autologin -account=\"Local Test\" -realm=\"Local Realm\" -character=Tester"));
    TestTrue(TEXT("Automatic login is explicit"), Options.bEnabled);
    TestEqual(TEXT("Quoted account alias"), Options.AccountAlias, FString(TEXT("Local Test")));
    TestEqual(TEXT("Quoted realm"), Options.RealmName, FString(TEXT("Local Realm")));
    TestEqual(TEXT("Character name"), Options.CharacterName, FString(TEXT("Tester")));
    TestFalse(TEXT("A selector alone does not start a connection"),
        FWowAutoLoginOptions::Parse(TEXT("-character=Tester")).bEnabled);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIdleWowAutoLoginAccounts, "WowUnreal.Login.AutoLogin.AccountSelection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FIdleWowAutoLoginAccounts::RunTest(const FString& Parameters)
{
    FWowAutoLoginOptions Options;
    TArray<FWowCredential> Credentials;
    TestEqual(TEXT("Missing credential store has no account"), Options.FindCredential(Credentials), INDEX_NONE);
    FWowCredential First;
    First.Alias = TEXT("First");
    Credentials.Add(First);
    FWowCredential Default;
    Default.Alias = TEXT("Default");
    Default.bIsDefault = true;
    Credentials.Add(Default);
    TestEqual(TEXT("Default profile takes precedence over list order"), Options.FindCredential(Credentials), 1);
    Options.AccountAlias = TEXT("FIRST");
    TestEqual(TEXT("Explicit alias overrides the default, ignoring case"), Options.FindCredential(Credentials), 0);
    Options.AccountAlias = TEXT("Missing");
    TestEqual(TEXT("An unknown alias never logs into another account"), Options.FindCredential(Credentials), INDEX_NONE);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIdleWowAutoLoginDestinations, "WowUnreal.Login.AutoLogin.DestinationSelection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FIdleWowAutoLoginDestinations::RunTest(const FString& Parameters)
{
    FWowAutoLoginOptions Options;
    TArray<FWowRealmInfo> Realms;
    TArray<FWowCharacterInfo> Characters;
    TestEqual(TEXT("Empty realms do not connect"), Options.FindRealm(Realms), INDEX_NONE);
    TestEqual(TEXT("Empty characters do not create or enter a character"), Options.FindCharacter(Characters), INDEX_NONE);
    FWowRealmInfo FirstRealm, SecondRealm;
    FirstRealm.Name = TEXT("First"); SecondRealm.Name = TEXT("Second");
    Realms = {FirstRealm, SecondRealm};
    FWowCharacterInfo FirstCharacter, SecondCharacter;
    FirstCharacter.Name = TEXT("First"); SecondCharacter.Name = TEXT("Tester");
    Characters = {FirstCharacter, SecondCharacter};
    TestEqual(TEXT("Unspecified realm uses the first available realm"), Options.FindRealm(Realms), 0);
    TestEqual(TEXT("Unspecified character uses the first existing character"), Options.FindCharacter(Characters), 0);
    Options.RealmName = TEXT("SECOND"); Options.CharacterName = TEXT("tester");
    TestEqual(TEXT("Requested realm is selected by name"), Options.FindRealm(Realms), 1);
    TestEqual(TEXT("Requested character is selected by name"), Options.FindCharacter(Characters), 1);
    Options.RealmName = TEXT("Missing"); Options.CharacterName = TEXT("Missing");
    TestEqual(TEXT("Missing realm does not silently select another"), Options.FindRealm(Realms), INDEX_NONE);
    TestEqual(TEXT("Missing character does not silently select another"), Options.FindCharacter(Characters), INDEX_NONE);
    return true;
}
#endif
