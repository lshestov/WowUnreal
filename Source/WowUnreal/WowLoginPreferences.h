#pragma once

#include "CoreMinimal.h"

/** Login-form preferences, kept separate from explicit autologin credentials. */
struct FWowLoginPreferences
{
    bool bRememberAccount = true;
    FString Server = TEXT("127.0.0.1:3724");
    FString AccountName;

    /** Returns false when the file has no saved login preference. */
    bool Load(const FString& Filename = FString());
    /** Saves only the server, checkbox state and (when enabled) account name. */
    bool Save(const FString& Filename = FString()) const;
};
