#include "WowAutoLoginOptions.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

FWowAutoLoginOptions FWowAutoLoginOptions::Parse(const TCHAR* CommandLine)
{
    FWowAutoLoginOptions Result;
    Result.bEnabled = FParse::Param(CommandLine, TEXT("autologin"));
    FParse::Value(CommandLine, TEXT("-account="), Result.AccountAlias);
    FParse::Value(CommandLine, TEXT("-realm="), Result.RealmName);
    FParse::Value(CommandLine, TEXT("-character="), Result.CharacterName);
    return Result;
}

int32 FWowAutoLoginOptions::FindCredential(const TArray<FWowCredential>& Credentials) const
{
    if (!AccountAlias.IsEmpty())
    {
        return Credentials.IndexOfByPredicate([this](const FWowCredential& Credential)
        {
            return Credential.Alias.Equals(AccountAlias, ESearchCase::IgnoreCase);
        });
    }
    const int32 DefaultIndex = Credentials.IndexOfByPredicate([](const FWowCredential& Credential)
    {
        return Credential.bIsDefault;
    });
    return DefaultIndex != INDEX_NONE ? DefaultIndex : (Credentials.IsEmpty() ? INDEX_NONE : 0);
}

int32 FWowAutoLoginOptions::FindRealm(const TArray<FWowRealmInfo>& Realms) const
{
    if (RealmName.IsEmpty()) return Realms.IsEmpty() ? INDEX_NONE : 0;
    return Realms.IndexOfByPredicate([this](const FWowRealmInfo& Realm)
    {
        return Realm.Name.Equals(RealmName, ESearchCase::IgnoreCase);
    });
}

int32 FWowAutoLoginOptions::FindCharacter(const TArray<FWowCharacterInfo>& Characters) const
{
    if (CharacterName.IsEmpty()) return Characters.IsEmpty() ? INDEX_NONE : 0;
    return Characters.IndexOfByPredicate([this](const FWowCharacterInfo& Character)
    {
        return Character.Name.Equals(CharacterName, ESearchCase::IgnoreCase);
    });
}
