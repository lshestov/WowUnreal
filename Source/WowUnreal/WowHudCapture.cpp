#include "WowGameplayController.h"
#include "WowConnectionManager.h"
#include "WowUIManager.h"
#include "WowFrameManager.h"
#include "WowLuaVM.h"
#include "Components/Image.h"
#include "Components/ProgressBar.h"
#include "Misc/App.h"

void AWowGameplayController::LogHudCapture() const
{
    if (!UIManager || !UIManager->GetFrameManager()) return;
    auto* Frames = UIManager->GetFrameManager();
    UE_LOG(LogTemp, Log, TEXT("HudFrameTime: %.3f ms"), FApp::GetDeltaTime()*1000.0);
    const auto* LocalEntity = ConnectionManager ? ConnectionManager->PacketHandler.EntityManager.GetLocalPlayer() : nullptr;
    if (LocalEntity) UE_LOG(LogTemp, Log, TEXT("HudCapture: health=%d/%d powerType=%u power=%d/%d form=%u"),
        LocalEntity->GetHealth(), LocalEntity->GetMaxHealth(), LocalEntity->GetPowerTypeId(), LocalEntity->GetPower(LocalEntity->GetPowerTypeId()),
        LocalEntity->GetMaxPower(LocalEntity->GetPowerTypeId()), LocalEntity->GetFieldByte(UnitField::BYTES_2, 3));
    for (const TCHAR* Name : {TEXT("PlayerFrame"), TEXT("PlayerFrameHealthBar"), TEXT("PlayerFrameManaBar"),
        TEXT("MainMenuBar"), TEXT("ActionButton1"), TEXT("BonusActionBarFrame"), TEXT("BonusActionButton1"), TEXT("CastingBarFrame"), TEXT("ChatFrame1"), TEXT("WatchFrame")})
    {
        const auto Handle = Frames->FindFrame(Name);
        const auto Rect = Frames->GetLayoutRect(Handle);
        const auto* Bar = Frames->GetStatusBarWidget(Handle);
        UE_LOG(LogTemp, Log, TEXT("HudFrame: %s shown=%d rect=(%.1f,%.1f,%.1f,%.1f) scale=%.3f fill=%.3f"),
            Name, Frames->IsFrameVisible(Handle), Rect.X, Rect.Y, Rect.Z, Rect.W,
            Frames->GetEffectiveFrameScale(Handle), Bar ? Bar->GetPercent() : -1.f);
    }
    for (const TCHAR* Prefix : {TEXT("ActionButton"), TEXT("BonusActionButton")})
        for (int32 I = 1; I <= 12; ++I)
        {
            const FString Name = FString::Printf(TEXT("%s%dIcon"), Prefix, I);
            const auto* Image = Frames->GetNamedTexture(Name);
            if (Image && Image->GetBrush().GetResourceObject())
                UE_LOG(LogTemp, Log, TEXT("HudIcon: %s texture=%s opacity=%.2f"), *Name,
                    *GetNameSafe(Image->GetBrush().GetResourceObject()), Image->GetRenderOpacity());
        }
    for (int64 Handle = 1; Handle <= Frames->GetFrameCount(); ++Handle)
    {
        const auto* Def = Frames->GetFrameDef(Handle);
        if (!Def || Def->bVirtual || Def->Type != EWowFrameType::StatusBar || !Frames->IsFrameVisible(Handle)) continue;
        bool Visible = true;
        for (int64 Ancestor = Frames->GetParentHandle(Handle); Ancestor >= 0; Ancestor = Frames->GetParentHandle(Ancestor))
            if (!Frames->IsFrameVisible(Ancestor)) { Visible = false; break; }
        if (Visible)
        {
            const auto Rect = Frames->GetLayoutRect(Handle);
            UE_LOG(LogTemp, Log, TEXT("HudVisibleBar: %s rect=(%.1f,%.1f,%.1f,%.1f)"), *Def->Name, Rect.X, Rect.Y, Rect.Z, Rect.W);
        }
    }
    if (auto* Lua = UIManager->GetLuaVM()) Lua->ExecuteString(TEXT(R"LUA(
        print("HudLua", PlayerFrame and PlayerFrame.unit,
            PlayerFrameManaBar and PlayerFrameManaBar.unit,
            PlayerFrameHealthBar and PlayerFrameHealthBar:GetValue(),
            PlayerFrameManaBar and PlayerFrameManaBar:GetValue(), GetBonusBarOffset())
    )LUA"), TEXT("=HudCapture"));
}
