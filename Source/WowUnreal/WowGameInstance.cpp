#include "WowGameInstance.h"
#include "Dom/JsonObject.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"

UWowGameInstance::UWowGameInstance() : WowDataPath(TEXT("")) {}

void UWowGameInstance::Init()
{
    Super::Init();
    FString CommandLineDataPath;
    if (FParse::Value(FCommandLine::Get(), TEXT("-wowdata="), CommandLineDataPath))
    {
        WowDataPath = CommandLineDataPath;
    }
    else
    {
        // Packaged layout: runtime/client-wowunreal/WowUnreal. Honor the same
        // configuration as start-wowunreal.ps1 when launched directly in Explorer.
        const FString RuntimeRoot = FPaths::ConvertRelativePathToFull(
            FPaths::Combine(FPaths::ProjectDir(), TEXT("..")));
        if (WowDataPath.IsEmpty())
        {
            FString Json;
            TSharedPtr<FJsonObject> Settings;
            if (FFileHelper::LoadFileToString(Json, *FPaths::Combine(RuntimeRoot, TEXT("idle-wow.json")))
                && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Settings)
                && Settings.IsValid())
            {
                Settings->TryGetStringField(TEXT("DataPath"), WowDataPath);
                if (!WowDataPath.IsEmpty() && FPaths::IsRelative(WowDataPath))
                {
                    WowDataPath = FPaths::ConvertRelativePathToFull(RuntimeRoot, WowDataPath);
                }
            }
        }
        if (WowDataPath.IsEmpty())
        {
            WowDataPath = FPaths::ConvertRelativePathToFull(
                FPaths::Combine(RuntimeRoot, TEXT("../client-enUS-3.3.5a/Data")));
        }
        // Existing world/data subsystems consume -wowdata directly.
        FCommandLine::Append(*FString::Printf(TEXT(" -wowdata=\"%s\""), *WowDataPath));
    }
    UE_LOG(LogTemp, Log, TEXT("WoW Data Path: %s"), *WowDataPath);
}
