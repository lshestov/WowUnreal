#include "WowCombatTextUi.h"
#include "LuaApi/LuaApiRegistry.h"
#include "WowCombatLog.h"
#include "WowConnectionManager.h"
#include "WowEventSystem.h"
#include "WowFrameManager.h"
#include "WowFontManager.h"
#include "Formats/Dbc/DbcStore.h"
#include "Components/TextBlock.h"
#include "Components/BoxComponent.h"
#include "Components/MeshComponent.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"

struct FWowCombatTextView
{
    struct FNumber
    {
        uint64 Guid=0;
        FString Text;
        FLinearColor Color=FLinearColor::White;
        bool Critical=false;
        double Started=0;
        int64 Handle=-1;
        FVector Position=FVector::ZeroVector;
        bool HasPosition=false;
        bool Refresh=true,Drawn=false;
    };
    FString ActiveUnit=TEXT("player");
    TArray<FNumber> Numbers;
    uint64 Incoming=0,Outgoing=0;
};

#if __has_include("lua.h")
extern "C" {
#include "lua.h"
#include "lauxlib.h"
}
namespace
{
FWowCombatTextView* View(lua_State* L)
{
    auto* C=WowLuaApi::GetContext(L); if(!C) return nullptr;
    if(!C->CombatText) C->CombatText=MakeShared<FWowCombatTextView>();
    return C->CombatText.Get();
}
bool Enabled(lua_State* L,const char* Name)
{
    const int Top=lua_gettop(L); lua_getglobal(L,"GetCVarBool"); lua_pushstring(L,Name);
    const bool Result=lua_pcall(L,1,1,0)==0 && lua_toboolean(L,-1); lua_settop(L,Top); return Result;
}
double Number(const FWowCombatEntry& E,int32 I) {return E.Values.IsValidIndex(I)?E.Values[I].Number:0;}
FString Localized(lua_State* L,const FString& Key)
{
    lua_getglobal(L,TCHAR_TO_UTF8(*Key)); const FString Text=lua_isstring(L,-1)?UTF8_TO_TCHAR(lua_tostring(L,-1)):*Key; lua_pop(L,1); return Text;
}
void Incoming(lua_State* L,FWowCombatTextView& V,const FString& Type,const TArray<FWowCombatValue>& Values)
{
    auto* C=WowLuaApi::GetContext(L); if(!C || !C->EventSystem || !Enabled(L,"enableCombatText")) return;
    ++V.Incoming;
    C->EventSystem->FireEventTyped(TEXT("COMBAT_TEXT_UPDATE"),[&](lua_State* S)
    {
        lua_pushstring(S,TCHAR_TO_UTF8(*Type));
        for(const auto& Value:Values)
            if(Value.Kind==FWowCombatValue::EKind::String) lua_pushstring(S,TCHAR_TO_UTF8(*Value.String));
            else if(Value.Kind==FWowCombatValue::EKind::Nil) lua_pushnil(S);
            else lua_pushnumber(S,Value.Number);
        return 1+Values.Num();
    });
}
void WorldNumber(lua_State* L,FWowCombatTextView& V,const FWowCombatEntry& E,const FString& Text,const FLinearColor& Color,bool Critical)
{
    const double Now=FPlatformTime::Seconds();
    auto* Slot=V.Numbers.FindByPredicate([&](const auto& N){return !N.Guid || Now-N.Started>=1.6;});
    if(!Slot && V.Numbers.Num()<32) Slot=&V.Numbers.AddDefaulted_GetRef();
    if(!Slot) { Slot=&V.Numbers[0]; for(auto& N:V.Numbers) if(N.Started<Slot->Started) Slot=&N; }
    Slot->Guid=E.Destination; Slot->Text=Text; Slot->Color=Color; Slot->Critical=Critical;
    Slot->Started=Now; Slot->HasPosition=false; Slot->Refresh=true; Slot->Drawn=false; ++V.Outgoing;
}
}
void FWowCombatTextUi::RegisterApi(lua_State* L)
{
    lua_pushcfunction(L,[](lua_State* S)->int
    {
        const FString Token=UTF8_TO_TCHAR(luaL_checkstring(S,1));
        if(auto* V=View(S)) V->ActiveUnit=Token;
        return 0;
    });
    lua_setglobal(L,"CombatTextSetActiveUnit");
}

void FWowCombatTextUi::Dispatch(lua_State* L,const FWowCombatEntry& E)
{
    auto* C=WowLuaApi::GetContext(L); auto* V=View(L);
    if(!C || !C->EntityManager || !V) return;
    const uint64 Player=C->EntityManager->LocalPlayerGuid; if(!Player) return;
    const int Top=lua_gettop(L); lua_pushstring(L,TCHAR_TO_UTF8(*V->ActiveUnit));
    const auto* Active=WowLuaApi::ResolveUnitToken(L,Top+1); const uint64 ActiveGuid=Active?Active->Guid:Player; lua_settop(L,Top);
    const bool Self=E.Destination==ActiveGuid;
    const bool Melee=E.Event.StartsWith(TEXT("SWING_"));
    const bool Environment=E.Event==TEXT("ENVIRONMENTAL_DAMAGE");
    const bool Damage=E.Event.EndsWith(TEXT("_DAMAGE")) || E.Event==TEXT("DAMAGE_SHIELD") || E.Event==TEXT("DAMAGE_SPLIT");
    const bool Heal=E.Event==TEXT("SPELL_HEAL") || E.Event==TEXT("SPELL_PERIODIC_HEAL");
    const bool Miss=E.Event.EndsWith(TEXT("_MISSED"));
    const bool Periodic=E.Event.Contains(TEXT("PERIODIC"));
    const int Offset=Environment?1:0;
    const int32 Amount=int32(Number(E,Offset));
    const bool Critical=Number(E,Heal?3:Offset+6)!=0;
    using FValue=FWowCombatValue;
    static const TCHAR* Powers[]={TEXT("MANA"),TEXT("RAGE"),TEXT("FOCUS"),TEXT("ENERGY"),TEXT("HAPPINESS"),TEXT("RUNES"),TEXT("RUNIC_POWER")};
    const int32 Power=Number(E,1);
    if(E.Source==ActiveGuid && E.Event.EndsWith(TEXT("_LEECH")) && Number(E,2)>0 && Power>=0 && Power<UE_ARRAY_COUNT(Powers))
        Incoming(L,*V,Periodic?TEXT("PERIODIC_ENERGIZE"):TEXT("ENERGIZE"),{FValue::N(Number(E,2)),FValue::S(Powers[Power])});
    if(Self)
    {
        if(Damage)
        {
            const FString Prefix=E.Event==TEXT("SPELL_DAMAGE")?TEXT("SPELL_"):TEXT("");
            // Reduction events carry damage, then reduced amount, exactly as the stock Lua expects.
            if(Number(E,Offset+3)>0) Incoming(L,*V,Prefix+TEXT("RESIST"),{FValue::N(Amount),FValue::N(Number(E,Offset+3))});
            else if(Number(E,Offset+4)>0) Incoming(L,*V,Prefix+TEXT("BLOCK"),{FValue::N(Amount),FValue::N(Number(E,Offset+4))});
            else if(Number(E,Offset+5)>0) Incoming(L,*V,Prefix+TEXT("ABSORB"),{FValue::N(Amount),FValue::N(Number(E,Offset+5))});
            else if(Amount>0) Incoming(L,*V,Prefix+(Critical?TEXT("DAMAGE_CRIT"):TEXT("DAMAGE")),{FValue::N(Amount)});
        }
        else if(Miss && !E.Values.IsEmpty()) Incoming(L,*V,(E.Event==TEXT("SPELL_MISSED")?TEXT("SPELL_"):TEXT(""))+E.Values[0].String,{});
        else if(Heal && (Amount>0 || Number(E,2)>0))
        {
            FString Name;
            if(C->ConnectionManager) { auto& H=C->ConnectionManager->PacketHandler; Name=H.PlayerNameCache.FindRef(E.Source); if(Name.IsEmpty()) if(const auto* Unit=C->EntityManager->Find(E.Source)) Name=H.CreatureNameCache.FindRef(Unit->Entry); }
            FString Type=Periodic?TEXT("PERIODIC_HEAL"):Critical?TEXT("HEAL_CRIT"):TEXT("HEAL");
            TArray<FValue> Args={FValue::S(Name),FValue::N(Amount)};
            if(Number(E,2)>0) { Type+=TEXT("_ABSORB"); Args.Add(FValue::N(Number(E,2))); }
            Incoming(L,*V,Type,Args);
        }
        else if(E.Event.EndsWith(TEXT("_ENERGIZE")) || E.Event.EndsWith(TEXT("_DRAIN")) || E.Event.EndsWith(TEXT("_LEECH")))
        {
            const bool Energize=E.Event.EndsWith(TEXT("_ENERGIZE"));
            if(Power>=0 && Power<UE_ARRAY_COUNT(Powers)) Incoming(L,*V,Periodic && Energize?TEXT("PERIODIC_ENERGIZE"):TEXT("ENERGIZE"),{FValue::N(Energize?Amount:-Amount),FValue::S(Powers[Power])});
        }
        else if(E.Event==TEXT("SPELL_AURA_APPLIED") || E.Event==TEXT("SPELL_AURA_REMOVED"))
            if(const auto* Spell=FDbcStore::Get().Spells().GetById(E.Spell))
                Incoming(L,*V,FString(E.Event.EndsWith(TEXT("APPLIED"))?TEXT("SPELL_AURA_START"):TEXT("SPELL_AURA_END"))+(E.Values.Num() && E.Values[0].String==TEXT("DEBUFF")?TEXT("_HARMFUL"):TEXT("")),{FValue::S(Spell->SpellName)});
        return;
    }
    // Only the player's own attacks and owned pets create overhead numbers.
    const auto* Source=C->EntityManager->Find(E.Source);
    const bool Owned=E.Source==Player || (Source && (Source->GetField64(UnitField::SUMMONEDBY)==Player || Source->GetField64(UnitField::CHARMEDBY)==Player));
    if(!Owned || !E.Destination) return;
    const auto* Spell=FDbcStore::Get().Spells().GetById(E.Spell);
    const bool White=Melee || (Spell && (Spell->AttributesEx3&0x8000));
    if(Heal)
    {
        if(Amount>0 && E.Source==Player && Enabled(L,"CombatHealing")) WorldNumber(L,*V,E,TEXT("+")+FString::FromInt(Amount),FLinearColor(.1f,1,.1f),Critical);
    }
    else if((Damage || Miss) && Enabled(L,"CombatDamage") && (!Periodic || Enabled(L,"CombatLogPeriodicSpells")) &&
        (E.Source==Player || Enabled(L,White?"PetMeleeDamage":"PetSpellDamage")))
    {
        const FLinearColor Color=White?(E.Source==Player?FLinearColor::White:FLinearColor::FromSRGBColor(FColor(166,166,166))):FLinearColor::FromSRGBColor(FColor(255,222,0));
        if(Damage && Amount>0) WorldNumber(L,*V,E,FString::FromInt(Amount),Color,Critical);
        else if(Miss && !E.Values.IsEmpty()) WorldNumber(L,*V,E,Localized(L,E.Values[0].String),Color,false);
    }
}

void FWowCombatTextUi::Tick(lua_State* L,UWorld* World)
{
    auto* C=WowLuaApi::GetContext(L); auto* V=View(L);
    if(!C || !C->FrameManager || !V || !World) return;
    auto& Frames=*C->FrameManager; auto* PC=World->GetFirstPlayerController(); if(!PC) return;
    const double Now=FPlatformTime::Seconds();
    for(int32 Index=0;Index<V->Numbers.Num();++Index)
    {
        auto& N=V->Numbers[Index]; const float Age=Now-N.Started; N.Drawn=false;
        if(!N.Guid || Age>=1.6f) { if(N.Handle>=0 && Frames.IsFrameVisible(N.Handle)) Frames.SetFrameVisible(N.Handle,false); N.Guid=0; continue; }
        if(C->ResolveActor) if(auto* Actor=C->ResolveActor(N.Guid))
        {
            // Actor bounds include attachments and inflated animated M2 bounds.
            // Use the same authored body box as unit names and hit selection.
            const auto* Selection=FindObjectFast<UBoxComponent>(Actor,TEXT("UnitSelection"));
            const auto* Mesh=FindObjectFast<UMeshComponent>(Actor,TEXT("CharacterMesh"));
            if(Selection || Mesh)
            {
                const auto& Bounds=Selection?Selection->Bounds:Mesh->Bounds;
                N.Position=Bounds.Origin+FVector(0,0,Bounds.BoxExtent.Z+14); N.HasPosition=true;
            }
        }
        FVector2D Pixel;
        if(!N.HasPosition || !PC->ProjectWorldLocationToScreen(N.Position,Pixel,true)) { if(N.Handle>=0 && Frames.IsFrameVisible(N.Handle)) Frames.SetFrameVisible(N.Handle,false); continue; }
        const FString Name=FString::Printf(TEXT("IdleWowWorldCombatText%d"),Index+1);
        if(N.Handle<0)
        {
            FWowFrameDef Def; Def.Name=Name; Def.Parent=TEXT("UIParent"); Def.Width=200; Def.Height=60; Def.Strata=EWowFrameStrata::BACKGROUND; Def.FrameLevel=1; Def.bEnableMouse=false;
            FWowAnchor A; A.Point=A.RelativePoint=EWowAnchorPoint::TOPLEFT; A.RelativeTo=TEXT("UIParent"); Def.Anchors.Add(A);
            FWowLayer Layer; FWowFontStringElement Text; Text.Name=Name+TEXT("Text"); Text.Inherits=TEXT("CombatTextFont"); Text.Width=200; Text.Height=60;
            Text.JustifyH=TEXT("CENTER"); Text.bHasJustifyH=true; A.RelativeTo=Name; Text.Anchors.Add(A); Layer.FontStrings.Add(Text); Def.Layers.Add(Layer);
            N.Handle=Frames.CreateFrame(Def);
        }
        if(N.Refresh) if(auto* Label=Frames.GetNamedFontString(Name+TEXT("Text")))
        {
            Label->SetText(FText::FromString(N.Text)); Label->SetColorAndOpacity(N.Color);
            if(auto* Fonts=Frames.GetFontManager())
            {
                // Fonts.xml defines DAMAGE_TEXT_FONT separately from the incoming CombatTextFont.
                FString FontName=Localized(L,TEXT("DAMAGE_TEXT_FONT")); if(FontName==TEXT("DAMAGE_TEXT_FONT")) FontName=TEXT("Fonts\\FRIZQT__.TTF");
                auto Font=Fonts->GetFont(FontName,(N.Critical?30:22)*Frames.GetUIScale()); Font.OutlineSettings.OutlineSize=1; Label->SetFont(Font);
            }
            Label->SetVisibility(ESlateVisibility::HitTestInvisible);
            N.Refresh=false;
        }
        if(!Frames.IsFrameVisible(N.Handle)) Frames.SetFrameVisible(N.Handle,true);
        if(auto* Widget=Frames.GetWidgetForHandle(N.Handle))
        {
            const float Scale=Frames.GetUIScale(); const float ScreenScale=FMath::Max(.001f,Frames.GetScreenScale());
            Widget->SetRenderTranslation((Pixel/ScreenScale-FVector2D(100,60+Age*50))*Scale);
            Widget->SetRenderOpacity(FMath::Clamp((1.6f-Age)/.45f,0.f,1.f));
            N.Drawn=Frames.IsFrameDrawn(N.Handle);
        }
    }
}
void FWowCombatTextUi::GetCounts(lua_State* L,int32& Active,uint64& IncomingCount,uint64& OutgoingCount)
{
    Active=0; IncomingCount=OutgoingCount=0;
    if(const auto* V=View(L)) { IncomingCount=V->Incoming; OutgoingCount=V->Outgoing; for(const auto& N:V->Numbers) if(N.Guid && N.Drawn && FPlatformTime::Seconds()-N.Started<1.6) ++Active; }
}
#else
void FWowCombatTextUi::RegisterApi(lua_State*) {}
void FWowCombatTextUi::Dispatch(lua_State*,const FWowCombatEntry&) {}
void FWowCombatTextUi::Tick(lua_State*,UWorld*) {}
void FWowCombatTextUi::GetCounts(lua_State*,int32& Active,uint64& Incoming,uint64& Outgoing) {Active=0; Incoming=Outgoing=0;}
#endif
