#pragma once

#include "CoreMinimal.h"
#include "WowCredentialStore.h"
#include "WowSessionState.h"

/** Selection policy shared by automatic login and its offline tests. */
struct WOWCLIENT_API FWowAutoLoginOptions
{
    bool bEnabled = false;
    FString AccountAlias;
    FString RealmName;
    FString CharacterName;

    static FWowAutoLoginOptions Parse(const TCHAR* CommandLine);
    int32 FindCredential(const TArray<FWowCredential>& Credentials) const;
    int32 FindRealm(const TArray<FWowRealmInfo>& Realms) const;
    int32 FindCharacter(const TArray<FWowCharacterInfo>& Characters) const;
};
