#include "WowLoginPreferences.h"

#include "HAL/FileManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"

namespace
{
FString LoginPreferencesPath(const FString& Filename)
{
    return Filename.IsEmpty() ? FPaths::ProjectSavedDir() / TEXT("Config/WowLogin.ini") : Filename;
}
}

bool FWowLoginPreferences::Load(const FString& Filename)
{
    FConfigFile Config;
    Config.Read(LoginPreferencesPath(Filename));
    bool Remember = true;
    if (!Config.GetBool(TEXT("Login"), TEXT("RememberAccountName"), Remember)) return false;

    FWowLoginPreferences Saved;
    Saved.bRememberAccount = Remember;
    Config.GetString(TEXT("Login"), TEXT("Server"), Saved.Server);
    if (Saved.Server.IsEmpty()) Saved.Server = TEXT("127.0.0.1:3724");
    if (Remember) Config.GetString(TEXT("Login"), TEXT("AccountName"), Saved.AccountName);
    *this = MoveTemp(Saved);
    return true;
}

bool FWowLoginPreferences::Save(const FString& Filename) const
{
    const FString Path = LoginPreferencesPath(Filename);
    const FString Directory = FPaths::GetPath(Path);
    if (!Directory.IsEmpty() && !IFileManager::Get().MakeDirectory(*Directory, true)) return false;

    // A fresh config removes an older remembered name when the checkbox is off.
    // Passwords belong only to the separately managed autologin credential store.
    FConfigFile Config;
    Config.SetBool(TEXT("Login"), TEXT("RememberAccountName"), bRememberAccount);
    Config.SetString(TEXT("Login"), TEXT("Server"), *Server);
    if (bRememberAccount) Config.SetString(TEXT("Login"), TEXT("AccountName"), *AccountName);
    return Config.Write(Path, false);
}
