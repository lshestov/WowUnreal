#include "WowGameUI.h"
#include "WowUiText.h"
#include "Formats/Dbc/DbcStore.h"
#include "Widgets/SWowLootWindow.h"
#include "Widgets/SWowTradeWindow.h"
#include "Widgets/SWowVendorWindow.h"
#include "Widgets/SWowQuestDialog.h"
#include "WowConnectionManager.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Layout/SDPIScaler.h"
#include "Engine/UserInterfaceSettings.h"

DEFINE_LOG_CATEGORY_STATIC(LogWowGameUI, Log, All);

static FWowQuestDetails FormatQuest(const FWowQuestDetails& Input,UWowConnectionManager* Connection)
{
    auto Quest=Input; const auto* P=Connection?Connection->PacketHandler.EntityManager.GetLocalPlayer():nullptr;
    const auto* Race=P?FDbcStore::Get().ChrRaces().GetById(P->GetRaceId()):nullptr;
    for(auto* Text:{&Quest.Title,&Quest.Details,&Quest.Objectives})
        *Text=WowQuestText(*Text,Connection?Connection->GetCharacterName():FString(),P?P->GetClassId():0,Race?Race->Name:FString(),P && P->GetGenderId()==1);
    return Quest;
}
UWowGameUI::UWowGameUI()
{
    PrimaryComponentTick.bCanEverTick = false;
}

bool UWowGameUI::CloseActiveWindow()
{
    if(TradeWindow.IsValid() && TradeWindow->GetVisibility().IsVisible()) { OnCancelTrade(); return true; }
    if(VendorWindow.IsValid() && VendorWindow->GetVisibility().IsVisible()) { OnCloseVendor(); return true; }
    if(LootWindow.IsValid() && LootWindow->GetVisibility().IsVisible()) { OnCloseLoot(); return true; }
    if(QuestDialog.IsValid() && QuestDialog->GetVisibility().IsVisible()) { OnQuestDecline(); return true; }
    return false;
}

void UWowGameUI::BeginPlay()
{
    Super::BeginPlay();
    CreateWidgets();
}

void UWowGameUI::EndPlay(const EEndPlayReason::Type Reason)
{
    RemoveWidgetsFromViewport(); Super::EndPlay(Reason);
}

void UWowGameUI::SetConnectionManager(UWowConnectionManager* InConnectionManager)
{
    ConnectionManager = InConnectionManager;
}

void UWowGameUI::ShowLootWindow(uint64 LootGuid, uint8 LootType, uint32 Gold, const TArray<FWowLootItem>& Items)
{
    CreateWidgets();
    if (LootWindow.IsValid())
    {
        auto DisplayItems=Items;
        if (ConnectionManager) for (auto& Item : DisplayItems)
        {
            ConnectionManager->RequestItemTemplate(Item.ItemId);
            if (const auto* Template=ConnectionManager->PacketHandler.ItemTemplates.Find(Item.ItemId)) { Item.Name=Template->Name; Item.Quality=Template->Quality; }
        }
        LootWindow->UpdateLoot(LootGuid, LootType, Gold, DisplayItems);
        UE_LOG(LogWowGameUI, Log, TEXT("Showing loot window"));
    }
}

void UWowGameUI::HideLootWindow()
{
    if (LootWindow.IsValid())
    {
        LootWindow->CloseLoot();
        UE_LOG(LogWowGameUI, Log, TEXT("Hiding loot window"));
    }
}

void UWowGameUI::ShowVendorWindow(uint64 VendorGuid, const TArray<FWowVendorItem>& Items)
{
    CreateWidgets();
    if (VendorWindow.IsValid())
    {
        VendorWindow->UpdateVendor(VendorGuid, Items);
        UE_LOG(LogWowGameUI, Log, TEXT("Showing vendor window"));
    }
}

void UWowGameUI::UpdateVendorPlayerInventory(const TArray<FWowItem>& Items)
{
    if (VendorWindow.IsValid())
    {
        VendorWindow->UpdatePlayerInventory(Items);
        UE_LOG(LogWowGameUI, Log, TEXT("Updated vendor player inventory"));
    }
}

void UWowGameUI::HideVendorWindow()
{
    if (VendorWindow.IsValid())
    {
        VendorWindow->CloseVendor();
        UE_LOG(LogWowGameUI, Log, TEXT("Hiding vendor window"));
    }
}

void UWowGameUI::ShowQuestDialog(const FWowQuestDetails& QuestDetails)
{
    CreateWidgets();
    if (QuestDialog.IsValid())
    {
        QuestDialog->ShowQuestDetails(FormatQuest(QuestDetails,ConnectionManager));
        UE_LOG(LogWowGameUI, Log, TEXT("Showing quest dialog"));
    }
}

void UWowGameUI::ShowQuestRewardDialog(const FWowQuestDetails& QuestDetails)
{
    CreateWidgets();
    if (QuestDialog.IsValid())
    {
        QuestDialog->ShowQuestReward(FormatQuest(QuestDetails,ConnectionManager));
        UE_LOG(LogWowGameUI, Log, TEXT("Showing quest reward dialog"));
    }
}

void UWowGameUI::HideQuestDialog()
{
    if (QuestDialog.IsValid())
    {
        QuestDialog->CloseDialog();
        UE_LOG(LogWowGameUI, Log, TEXT("Hiding quest dialog"));
    }
}

void UWowGameUI::ShowGossip(const FWowGossipMenu& Menu)
{
    CreateWidgets(); QuestDialog->ShowGossip(Menu);
}

void UWowGameUI::SelectGossipQuest(uint32 Id)
{
    if (!ConnectionManager) return;
    const auto& Menu=ConnectionManager->PacketHandler.GossipMenu;
    const auto* Quest=Menu.Quests.FindByPredicate([Id](const auto& Q){return Q.Id==Id;});
    if (!Quest) return;
    if (Quest->Icon==4) ConnectionManager->SendQuestComplete(Menu.Guid,Id);
    else ConnectionManager->SendQuestQuery(Menu.Guid,Id);
}

void UWowGameUI::SelectGossipOption(uint32 Id)
{
    if (!ConnectionManager) return;
    const auto& Menu=ConnectionManager->PacketHandler.GossipMenu;
    const auto* Option=Menu.Options.FindByPredicate([Id](const auto& O){return O.Id==Id;});
    if (Option && !Option->bCoded && !Option->Money && Option->Confirmation.IsEmpty())
        ConnectionManager->SendGossipSelect(Menu.Guid,Menu.MenuId,Id);
}

void UWowGameUI::AcceptQuest() { if (QuestDialog.IsValid()) QuestDialog->AcceptCurrentQuest(); }
void UWowGameUI::CompleteQuest() { if (QuestDialog.IsValid()) QuestDialog->CompleteCurrentQuest(); }
uint32 UWowGameUI::GetShownQuestId() const { return QuestDialog.IsValid()?QuestDialog->GetCurrentQuestId():0; }
void UWowGameUI::LootAll() { OnLootAll(); }
void UWowGameUI::CloseLoot() { OnCloseLoot(); HideLootWindow(); }

bool UWowGameUI::IsPointerOverWindow() const
{
    if (!FSlateApplication::IsInitialized()) return false;
    const auto Pointer=FSlateApplication::Get().GetCursorPos();
    for (const TSharedPtr<SWidget>& Widget : {TSharedPtr<SWidget>(QuestDialog),TSharedPtr<SWidget>(LootWindow),TSharedPtr<SWidget>(VendorWindow),TSharedPtr<SWidget>(TradeWindow)})
        if (Widget.IsValid() && Widget->GetVisibility().IsVisible() && Widget->GetCachedGeometry().IsUnderLocation(Pointer)) return true;
    return false;
}

void UWowGameUI::ShowTradeWindow(const FWowTradeState& TradeState)
{
    CreateWidgets();
    if (TradeWindow.IsValid())
    {
        TradeWindow->UpdateTrade(TradeState);
        UE_LOG(LogWowGameUI, Log, TEXT("Showing trade window"));
    }
}

void UWowGameUI::HideTradeWindow()
{
    if (TradeWindow.IsValid())
    {
        TradeWindow->CloseTrade();
        UE_LOG(LogWowGameUI, Log, TEXT("Hiding trade window"));
    }
}

void UWowGameUI::CreateWidgets()
{
    if (bWidgetsCreated) return;

    // Create loot window
    LootWindow = SNew(SWowLootWindow)
        .OnLootItem_UObject(this, &UWowGameUI::OnLootItem)
        .OnLootAll_UObject(this, &UWowGameUI::OnLootAll)
        .OnCloseLoot_UObject(this, &UWowGameUI::OnCloseLoot);

    // Create vendor window
    VendorWindow = SNew(SWowVendorWindow)
        .OnBuyItem_UObject(this, &UWowGameUI::OnBuyItem)
        .OnSellItem_UObject(this, &UWowGameUI::OnSellItem)
        .OnCloseVendor_UObject(this, &UWowGameUI::OnCloseVendor);

    // Create quest dialog
    QuestDialog = SNew(SWowQuestDialog)
        .OnQuestAccept_UObject(this, &UWowGameUI::OnQuestAccept)
        .OnQuestComplete_UObject(this, &UWowGameUI::OnQuestComplete)
        .OnQuestDecline_UObject(this, &UWowGameUI::OnQuestDecline)
        .OnGossipQuest_UObject(this,&UWowGameUI::SelectGossipQuest)
        .OnGossipOption_UObject(this,&UWowGameUI::SelectGossipOption);

    TradeWindow = SNew(SWowTradeWindow)
        .OnTradeAccept_UObject(this, &UWowGameUI::OnAcceptTrade)
        .OnTradeCancel_UObject(this, &UWowGameUI::OnCancelTrade);

    AddWidgetsToViewport();
    bWidgetsCreated = true;

    UE_LOG(LogWowGameUI, Log, TEXT("Created WoW game UI widgets"));
}

void UWowGameUI::AddWidgetsToViewport()
{
    if (!GEngine || !GEngine->GameViewport || WindowRoot.IsValid()) return;
    WindowRoot=SNew(SDPIScaler)
        .Visibility(EVisibility::SelfHitTestInvisible)
        .DPIScale_Lambda([]()->float
        {
            FVector2D Viewport;
            if (!GEngine || !GEngine->GameViewport) return 1.f;
            GEngine->GameViewport->GetViewportSize(Viewport);
            if (Viewport.Y<100) return 1.f;
            const float Dpi=GetDefault<UUserInterfaceSettings>()->GetDPIScaleBasedOnSize(FIntPoint(Viewport.X,Viewport.Y));
            return Viewport.Y/768.f/FMath::Max(Dpi,.01f);
        })
        [
            SNew(SConstraintCanvas).Visibility(EVisibility::SelfHitTestInvisible)
            +SConstraintCanvas::Slot().Anchors(FAnchors(.5f,.35f)).Alignment(FVector2D::ZeroVector).Offset(FMargin(-200,-100,400,300))[LootWindow.ToSharedRef()]
            +SConstraintCanvas::Slot().Anchors(FAnchors(.2f,.5f)).Alignment(FVector2D::ZeroVector).Offset(FMargin(-150,-200,300,400))[VendorWindow.ToSharedRef()]
            +SConstraintCanvas::Slot().Anchors(FAnchors(.5f,.5f)).Alignment(FVector2D::ZeroVector).Offset(FMargin(-250,-200,500,400))[QuestDialog.ToSharedRef()]
            +SConstraintCanvas::Slot().Anchors(FAnchors(.5f,.45f)).Alignment(FVector2D::ZeroVector).Offset(FMargin(-240,-170,480,340))[TradeWindow.ToSharedRef()]
        ];
    GEngine->GameViewport->AddViewportWidgetContent(WindowRoot.ToSharedRef(),90);
}

void UWowGameUI::RemoveWidgetsFromViewport()
{
    if (GEngine && GEngine->GameViewport && WindowRoot.IsValid())
        GEngine->GameViewport->RemoveViewportWidgetContent(WindowRoot.ToSharedRef());
    WindowRoot.Reset(); bWidgetsCreated=false;
}

void UWowGameUI::OnLootItem(int32 LootSlot)
{
    if (ConnectionManager)
    {
        ConnectionManager->SendLootItem(LootSlot);
        UE_LOG(LogWowGameUI, Log, TEXT("Looting item slot %d"), LootSlot);
    }
}

void UWowGameUI::OnLootAll()
{
    if (ConnectionManager)
    {
        const auto Items=ConnectionManager->PacketHandler.Loot.Items;
        ConnectionManager->SendLootMoney();
        for (const auto& Item : Items)
            if (Item.CanLoot()) ConnectionManager->SendLootItem(Item.Index);
        UE_LOG(LogWowGameUI, Log, TEXT("Loot all clicked"));
    }
}

void UWowGameUI::OnCloseLoot()
{
    UE_LOG(LogWowGameUI, Log, TEXT("Loot window closed by user"));
    if (ConnectionManager && ConnectionManager->PacketHandler.Loot.Guid)
        ConnectionManager->SendLootRelease(ConnectionManager->PacketHandler.Loot.Guid);
}

void UWowGameUI::OnBuyItem(uint64 VendorGuid, uint32 ItemId, int32 Count)
{
    if (ConnectionManager)
    {
        ConnectionManager->SendBuyItem(VendorGuid, ItemId, Count);
        UE_LOG(LogWowGameUI, Log, TEXT("Buying item %u (count=%d) from vendor %llu"), ItemId, Count, VendorGuid);
    }
}

void UWowGameUI::OnAcceptTrade(bool bBeginTrade)
{
    if (!ConnectionManager)
    {
        return;
    }

    if (bBeginTrade)
    {
        ConnectionManager->SendBeginTrade();
        UE_LOG(LogWowGameUI, Log, TEXT("Accepting incoming trade request"));
    }
    else
    {
        ConnectionManager->SendAcceptTrade();
        UE_LOG(LogWowGameUI, Log, TEXT("Accepting current trade contents"));
    }
}

void UWowGameUI::OnCancelTrade()
{
    if (ConnectionManager)
    {
        ConnectionManager->SendCancelTrade();
        UE_LOG(LogWowGameUI, Log, TEXT("Canceling trade"));
    }
}

void UWowGameUI::OnSellItem(uint64 VendorGuid, uint64 ItemGuid, uint8 Count)
{
    if (ConnectionManager)
    {
        ConnectionManager->SendSellItem(VendorGuid, ItemGuid, Count);
        UE_LOG(LogWowGameUI, Log, TEXT("Selling item %llu (count=%d) to vendor %llu"), ItemGuid, Count, VendorGuid);
    }
}

void UWowGameUI::OnCloseVendor()
{
    UE_LOG(LogWowGameUI, Log, TEXT("Vendor window closed by user"));
    // The vendor window is already hidden by the widget itself
}

void UWowGameUI::OnQuestAccept(uint64 QuestGiverGuid, uint32 QuestId)
{
    if (ConnectionManager)
    {
        if (!ConnectionManager->PacketHandler.QuestLog.ContainsByPredicate([QuestId](const auto& Q){return Q.QuestId==QuestId;}))
            ConnectionManager->SendQuestAccept(QuestGiverGuid, QuestId);
        UE_LOG(LogWowGameUI, Log, TEXT("Accepting quest %u from questgiver %llu"), QuestId, QuestGiverGuid);
    }
}

void UWowGameUI::OnQuestComplete(uint64 QuestGiverGuid, uint32 QuestId, int32 RewardChoice)
{
    if (ConnectionManager)
    {
        ConnectionManager->SendQuestChooseReward(QuestGiverGuid, QuestId, RewardChoice);
        UE_LOG(LogWowGameUI, Log, TEXT("Completing quest %u with reward choice %d from questgiver %llu"), QuestId, RewardChoice, QuestGiverGuid);
    }
}

void UWowGameUI::OnQuestDecline()
{
    UE_LOG(LogWowGameUI, Log, TEXT("Quest declined by user"));
    // No packet needs to be sent for quest decline - just close the dialog
}
