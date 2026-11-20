#include "WowQuestItemTooltip.h"
#include "WowUIManager.h"
#include "LuaApi/LuaApiRegistry.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Widgets/SViewport.h"

void WowQuestItemTooltip::Show(UWowUIManager& UI, uint32 ItemId, const FGeometry& Row)
{
    auto* Viewport = UI.GetWorld() ? UI.GetWorld()->GetGameViewport() : nullptr;
    const auto Widget = Viewport ? Viewport->GetGameViewportWidget() : nullptr;
    if (!UI.GetLuaVM() || !Widget || !ItemId) return;
    const auto& Geometry = Widget->GetCachedGeometry();
    if (Geometry.GetLocalSize().GetMin() <= 0) return;
    const auto Start = Geometry.AbsoluteToLocal(Row.GetAbsolutePositionAtCoordinates(FVector2D::ZeroVector)) / Geometry.GetLocalSize();
    const auto End = Geometry.AbsoluteToLocal(Row.GetAbsolutePositionAtCoordinates(FVector2D(1, 1))) / Geometry.GetLocalSize();

    UI.RaiseItemTooltips();
    // Give FrameXML the geometry of the Slate item as a tooltip owner. Its
    // stock ANCHOR_RIGHT, inventory comparisons and OnHide scripts do the rest.
    UI.GetLuaVM()->ExecuteString(FString::Printf(TEXT(
        "if not GameTooltip or not UIParent then return end "
        "local a = IdleWowQuestItemAnchor or CreateFrame('Frame','IdleWowQuestItemAnchor',UIParent) "
        "a:Hide() a:ClearAllPoints() a:SetPoint('TOPLEFT',UIParent,'TOPLEFT',%.8f * UIParent:GetWidth(),-%.8f * UIParent:GetHeight()) "
        "a:SetSize(%.8f * UIParent:GetWidth(),%.8f * UIParent:GetHeight()) "
        "GameTooltip:SetOwner(a,'ANCHOR_RIGHT')"),
        Start.X, Start.Y, End.X-Start.X, End.Y-Start.Y), TEXT("=QuestItemTooltip"));
    if (auto* Frames = UI.GetFrameManager())
        WowLuaApi::ShowQuestItemTooltip(UI.GetLuaVM()->GetState(),Frames->FindFrame(TEXT("GameTooltip")),ItemId);
}

void WowQuestItemTooltip::Hide(UWowUIManager& UI)
{
    if (auto* VM = UI.GetLuaVM())
        VM->ExecuteString(TEXT("if IdleWowQuestItemAnchor and GameTooltip and GameTooltip:IsOwned(IdleWowQuestItemAnchor) then GameTooltip:Hide() end"),
            TEXT("=QuestItemTooltip"));
}
