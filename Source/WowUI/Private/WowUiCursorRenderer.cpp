#include "WowUiCursorRenderer.h"
#include "WowFrameManager.h"
#include "WowConnectionManager.h"
#include "LuaApi/LuaApiRegistry.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"

#if __has_include("lua.h")
extern "C" {
#include "lua.h"
#include "lauxlib.h"
}
namespace
{
FString CursorPath(FString Name)
{
    if(Name.Contains(TEXT("\\")) || Name.Contains(TEXT("/"))) return Name;
    Name=Name.ToUpper(); Name.RemoveFromEnd(TEXT("_CURSOR"));
    const bool Unable=Name.RemoveFromStart(TEXT("UNABLE")) || Name.RemoveFromEnd(TEXT("_ERROR"));
    static const TMap<FString,FString> Names={{TEXT("POINT"),TEXT("Point")},{TEXT("BUY"),TEXT("Buy")},{TEXT("SELL"),TEXT("Buy")},{TEXT("REPAIR"),TEXT("Repair")},{TEXT("REPAIRNPC"),TEXT("RepairNPC")},{TEXT("INSPECT"),TEXT("Inspect")},{TEXT("CAST"),TEXT("Cast")},{TEXT("ATTACK"),TEXT("Attack")},{TEXT("SPEAK"),TEXT("Speak")},{TEXT("INTERACT"),TEXT("Interact")},{TEXT("LOOT"),TEXT("LootAll")},{TEXT("LOOTALL"),TEXT("LootAll")},{TEXT("PICKUP"),TEXT("Pickup")},{TEXT("TAXI"),TEXT("Taxi")},{TEXT("TRAINER"),TEXT("Trainer")},{TEXT("MINE"),TEXT("Mine")},{TEXT("SKIN"),TEXT("Skin")},{TEXT("GATHERHERBS"),TEXT("GatherHerbs")},{TEXT("PICKLOCK"),TEXT("PickLock")},{TEXT("MAIL"),TEXT("Mail")},{TEXT("ITEM"),TEXT("Item")},{TEXT("INNKEEPER"),TEXT("Innkeeper")},{TEXT("QUEST"),TEXT("Quest")},{TEXT("QUESTREPEATABLE"),TEXT("QuestRepeatable")},{TEXT("QUESTTURNIN"),TEXT("QuestTurnIn")}};
    const auto* File=Names.Find(Name); return File?FString(TEXT("Interface\\Cursor\\"))+(Unable?TEXT("Unable"):TEXT(""))+*File:FString();
}
const TCHAR* WorldCursor(const FWowLuaContext& Context)
{
    const auto* Unit = Context.WorldMouseoverGuid && Context.EntityManager
        ? Context.EntityManager->FindUnit(Context.WorldMouseoverGuid) : nullptr;
    if (!Unit) return TEXT("Interface\\Cursor\\Point");
    if (Unit->IsDead()) return Unit->IsLootable() ? TEXT("Interface\\Cursor\\LootAll") : TEXT("Interface\\Cursor\\Point");
    if (WowLuaApi::CanUnitAttack(&Context, Context.EntityManager->GetLocalPlayer(), Unit)) return TEXT("Interface\\Cursor\\Attack");
    if (Unit->IsQuestGiver() && Context.ConnectionManager)
    {
        const uint8 Status = Context.ConnectionManager->PacketHandler.QuestGiverStatuses.FindRef(Unit->Guid);
        if (Status == 8 || Status == 2) return TEXT("Interface\\Cursor\\Quest");
        if (Status == 7 || Status == 4) return TEXT("Interface\\Cursor\\QuestRepeatable");
        if (Status == 10 || Status == 9 || Status == 6) return TEXT("Interface\\Cursor\\QuestTurnIn");
    }
    if (Unit->IsFlightMaster()) return TEXT("Interface\\Cursor\\Taxi");
    if (Unit->IsVendor()) return TEXT("Interface\\Cursor\\Buy");
    if (Unit->IsInteractable()) return TEXT("Interface\\Cursor\\Speak");
    return TEXT("Interface\\Cursor\\Point");
}
FString ApiIcon(lua_State* L,const char* Api,int32 Id,int32 ResultIndex=1,int32 Returns=1)
{
    const int32 Top=lua_gettop(L); lua_getglobal(L,Api); lua_pushinteger(L,Id);
    FString Value;
    if(lua_isfunction(L,Top+1) && lua_pcall(L,1,Returns,0)==0 && lua_isstring(L,Top+ResultIndex)) Value=UTF8_TO_TCHAR(lua_tostring(L,Top+ResultIndex));
    lua_settop(L,Top); return Value;
}
void Register(lua_State* L,const char* Name,lua_CFunction Fn) { lua_pushcfunction(L,Fn); lua_setglobal(L,Name); }
}
void WowLuaApi::RegisterCursorApi(lua_State* L)
{
    Register(L,"SetCursor",[](lua_State* S)->int { if(auto* C=GetContext(S)) C->CursorTexture=lua_isstring(S,1)?CursorPath(UTF8_TO_TCHAR(lua_tostring(S,1))):FString(); lua_pushboolean(S,1); return 1; });
    Register(L,"ResetCursor",[](lua_State* S)->int { if(auto* C=GetContext(S)) C->CursorTexture.Empty(); return 0; });
    Register(L,"ShowCursor",[](lua_State* S)->int { if(auto* C=GetContext(S)) C->CursorHidden=false; return 0; });
    Register(L,"HideCursor",[](lua_State* S)->int { if(auto* C=GetContext(S)) C->CursorHidden=true; return 0; });
    Register(L,"ShowInspectCursor",[](lua_State* S)->int { if(auto* C=GetContext(S)) C->CursorTexture=CursorPath(TEXT("INSPECT")); return 0; });
    Register(L,"ShowMerchantSellCursor",[](lua_State* S)->int { if(auto* C=GetContext(S)) C->CursorTexture=CursorPath(TEXT("BUY")); return 0; });
    Register(L,"ShowBuybackSellCursor",[](lua_State* S)->int { if(auto* C=GetContext(S)) C->CursorTexture=CursorPath(TEXT("BUY")); return 0; });
    Register(L,"ShowContainerSellCursor",[](lua_State* S)->int { if(auto* C=GetContext(S)) C->CursorTexture=CursorPath(TEXT("BUY")); return 0; });
}
void FWowUiCursorRenderer::Tick(lua_State* L)
{
    auto* Connection=Context.ConnectionManager; FString Path,Type,Detail; int32 Id=0; uint32 Count=0; bool Held=false;
    if(Connection && Connection->GetCursorInfo(Type,Id,Detail))
    {
        Held=true;
        if(Type==TEXT("item")) { Path=WowLuaApi::ItemIconForUi(&Context,Id); Count=Connection->GetCursorItemCount(); }
        else if(Type==TEXT("merchant")) Path=WowLuaApi::ItemIconForUi(&Context,Connection->GetCursorMerchantItemId());
        else if(Type==TEXT("money")) Path=ApiIcon(L,"GetCoinIcon",Id);
        else if(Type==TEXT("action")) Path=ApiIcon(L,"GetActionTexture",Id);
        else if(Type==TEXT("spell")) Path=ApiIcon(L,"GetSpellTexture",Id);
        else if(Type==TEXT("macro")) Path=ApiIcon(L,"GetMacroInfo",Id,2,3);
        if(Path.IsEmpty()) Path=TEXT("Interface\\Icons\\INV_Misc_QuestionMark");
    }
    else if(Connection && Connection->HasPendingItemSpell()) Path=TEXT("Interface\\Cursor\\Cast");
    else if(Connection && Connection->IsRepairMode()) Path=TEXT("Interface\\Cursor\\Repair");
    else Path=Context.CursorTexture.IsEmpty() ? WorldCursor(Context) : Context.CursorTexture;
    const FVector2D Pointer=Frames.GetPointerPosition();
    if(Context.CursorHidden || Pointer.X<0 || Pointer.Y<0)
    {
        Context.SoftwareCursorVisible=false;
        if(Handle>=0 && Frames.IsFrameVisible(Handle)) Frames.SetFrameVisible(Handle,false);
        return;
    }
    if(Handle<0)
    {
        FWowFrameDef Def; Def.Name=TEXT("IdleWowCursor"); Def.Parent=TEXT("UIParent"); Def.Width=32; Def.Height=32; Def.Strata=EWowFrameStrata::TOOLTIP; Def.FrameLevel=1000; Def.bEnableMouse=false;
        FWowAnchor Origin; Origin.Point=Origin.RelativePoint=EWowAnchorPoint::TOPLEFT; Origin.RelativeTo=TEXT("UIParent"); Def.Anchors.Add(Origin);
        FWowLayer Layer; FWowTextureElement Icon; Icon.Name=TEXT("IdleWowCursorIcon"); Icon.bSetAllPoints=true; Layer.Textures.Add(Icon);
        FWowFontStringElement Number; Number.Name=TEXT("IdleWowCursorCount"); Number.Inherits=TEXT("NumberFontNormal"); Number.Width=32; Number.Height=16; Number.JustifyH=TEXT("RIGHT"); Number.bHasJustifyH=true;
        FWowAnchor A; A.Point=A.RelativePoint=EWowAnchorPoint::BOTTOMRIGHT; Number.Anchors.Add(A); Layer.FontStrings.Add(Number); Def.Layers.Add(Layer);
        Handle=Frames.CreateFrame(Def);
    }
    // A pointer move must not invalidate the layout of every stock frame.
    const float ParentScale=Frames.GetEffectiveFrameScale(Frames.FindFrame(TEXT("UIParent")));
    const FVector2D Position=(Pointer/(FMath::Max(.001f,Frames.GetScreenScale()*ParentScale))+
        (Held?FVector2D(-16,-16):FVector2D::ZeroVector))*Frames.GetUIScale();
    if(auto* Widget=Frames.GetWidgetForHandle(Handle))
    {
        // WoW cursor art is 32 physical pixels. Dragged inventory icons
        // still use the action bar's scale and are centred on the pointer.
        const float Scale=Held?1.f:1.f/FMath::Max(.001f,Frames.GetScreenScale()*ParentScale);
        if(!Widget->GetRenderTransform().Scale.Equals(FVector2D(Scale),.001f)) Widget->SetRenderScale(FVector2D(Scale));
        if(!Position.Equals(PreviousPosition,.01f)) { Widget->SetRenderTranslation(Position); PreviousPosition=Position; }
    }
    if(Path!=PreviousTexture) if(auto* Image=Frames.GetNamedTexture(TEXT("IdleWowCursorIcon")))
    {
        auto* Texture=Frames.LoadUITexture(Path);
        if(!Texture && Path!=TEXT("Interface\\Cursor\\Point")) Texture=Frames.LoadUITexture(TEXT("Interface\\Cursor\\Point"));
        FSlateBrush Brush; Brush.DrawAs=ESlateBrushDrawType::Image; Brush.SetResourceObject(Texture); Brush.ImageSize=FVector2D(32,32);
        Image->SetBrush(Brush); Image->SetRenderOpacity(Texture?1:0);
        Image->SetVisibility(ESlateVisibility::HitTestInvisible); PreviousTexture=Path;
    }
    const auto* Image=Frames.GetNamedTexture(TEXT("IdleWowCursorIcon"));
    Context.SoftwareCursorVisible=Image && Image->GetBrush().GetResourceObject();
    if(Context.SoftwareCursorVisible!=Frames.IsFrameVisible(Handle)) Frames.SetFrameVisible(Handle,Context.SoftwareCursorVisible);
    if(auto* Text=Frames.GetNamedFontString(TEXT("IdleWowCursorCount")))
    {
        Text->SetVisibility(ESlateVisibility::HitTestInvisible);
        Text->SetText(Count>1?FText::AsNumber(Count):FText::GetEmpty());
    }
}
#else
void WowLuaApi::RegisterCursorApi(lua_State*) {}
void FWowUiCursorRenderer::Tick(lua_State*) {}
#endif
