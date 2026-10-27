#include "LuaApiRegistry.h"
#include "WowConnectionManager.h"
#include "WowEventSystem.h"
#include "Formats/Dbc/DbcStore.h"

namespace
{
bool Controlled(const FWowEntity& E) {return E.IsPlayer() || (E.GetField(UnitField::FLAGS)&8);}
const FWowEntity* Controller(const FWowLuaContext* C,const FWowEntity* E)
{
    for(int32 I=0;E && E->IsUnit() && I<16;++I)
    {
        if(E->IsPlayer()) return E;
        const uint64 Owner=E->GetField64(UnitField::CHARMEDBY)?E->GetField64(UnitField::CHARMEDBY):E->GetField64(UnitField::SUMMONEDBY);
        if(!Owner || Owner==E->Guid || !C || !C->EntityManager) break;
        E=C->ConnectionManager?C->ConnectionManager->PacketHandler.FindUiUnit(Owner):C->EntityManager->Find(Owner);
    }
    return nullptr;
}
bool Duel(const FWowEntity* A,const FWowEntity* B)
{
    return A && B && A->IsPlayer() && B->IsPlayer() && A->GetField(PlayerField::BASE+8) && B->GetField(PlayerField::BASE+8) &&
        A->GetField64(PlayerField::BASE) && A->GetField64(PlayerField::BASE)==B->GetField64(PlayerField::BASE);
}
const FWowPacketHandler::FReputation* Reputation(const FWowLuaContext* C,uint32 Id,const FDbcStore::FFaction*& Faction)
{
    if(!C || !C->ConnectionManager) return nullptr;
    for(const auto& F:FDbcStore::Get().Factions()) if(F.Id==Id)
    {
        Faction=&F; const auto& Rows=C->ConnectionManager->PacketHandler.Reputations;
        return F.Index>=0 && Rows.IsValidIndex(F.Index)?&Rows[F.Index]:nullptr;
    }
    return nullptr;
}
int32 StandingRank(int32 Value)
{
    static constexpr int32 Lower[]={-42000,-6000,-3000,0,3000,9000,21000,42000};
    int32 Rank=1; while(Rank<8 && Value>=Lower[Rank]) ++Rank; return Rank;
}
}

int32 WowLuaApi::UnitReactionRank(const FWowLuaContext* C,const FWowEntity* A,const FWowEntity* B)
{
    if(!A || !B || !A->IsUnit() || !B->IsUnit()) return 4;
    if(A->Guid==B->Guid) return 5;
    const auto* OwnerA=Controller(C,A); const auto* OwnerB=Controller(C,B);
    if(Controlled(*A) && Controlled(*B))
    {
        if(Duel(OwnerA,OwnerB)) return OwnerA->GetField(PlayerField::BASE+8)==OwnerB->GetField(PlayerField::BASE+8)?5:2;
        if(OwnerA && OwnerB && OwnerA->Guid==OwnerB->Guid) return 5;
        if(C && C->ConnectionManager && C->EntityManager && OwnerA && OwnerB)
        {
            const auto& Group=C->ConnectionManager->PacketHandler.GroupInfo;
            const auto InGroup=[&](uint64 Guid) {return Guid==C->EntityManager->LocalPlayerGuid || Group.FindMember(Guid);};
            if(InGroup(OwnerA->Guid) && InGroup(OwnerB->Guid)) return 5;
        }
        if((A->GetFieldByte(UnitField::BYTES_2,1)&4) && (B->GetFieldByte(UnitField::BYTES_2,1)&4)) return 2;
    }
    const auto& Dbc=FDbcStore::Get().ItemUi();
    const auto* FactionA=Dbc.ReactionTemplate(A->GetField(UnitField::FACTIONTEMPLATE));
    const auto* FactionB=Dbc.ReactionTemplate(B->GetField(UnitField::FACTIONTEMPLATE));
    const bool LocalA=C && C->EntityManager && OwnerA && OwnerA->Guid==C->EntityManager->LocalPlayerGuid;
    const bool LocalB=C && C->EntityManager && OwnerB && OwnerB->Guid==C->EntityManager->LocalPlayerGuid;
    // The active player's attitude is AtWar-based. NPC attitude uses standing.
    const auto* Other=LocalA?FactionB:LocalB?FactionA:nullptr;
    const auto* LocalOwner=LocalA?OwnerA:LocalB?OwnerB:nullptr;
    if(Other && LocalOwner)
    {
        if(LocalB && (Other->Flags&0x1000) && (LocalOwner->GetField(PlayerField::FLAGS)&0x100)) return 2;
        if(C->ConnectionManager) if(const auto* Forced=C->ConnectionManager->PacketHandler.ForcedReactions.Find(Other->Faction)) return *Forced;
        const auto* IgnoreUnit=LocalA?LocalOwner:B;
        if(!(IgnoreUnit->GetField(UnitField::FLAGS_2)&4))
        {
            const FDbcStore::FFaction* Faction=nullptr;
            if(const auto* Rep=Reputation(C,Other->Faction,Faction))
            {
                if((Other->Flags&0x1000) && (LocalOwner->GetField(PlayerField::FLAGS)&0x100)) return 2;
                if(LocalA) return (Rep->Flags&2)?2:5;
                const auto* Player=static_cast<const FWowPlayerEntity*>(LocalOwner);
                return StandingRank(Rep->Standing+Faction->BaseFor(Player->GetRaceId(),Player->GetClassId()));
            }
        }
    }
    const int32 Reaction=Dbc.DefaultReaction(A->GetField(UnitField::FACTIONTEMPLATE),B->GetField(UnitField::FACTIONTEMPLATE));
    return Reaction?Reaction:4;
}

bool WowLuaApi::CanUnitAttack(const FWowLuaContext* C,const FWowEntity* A,const FWowEntity* B)
{
    if(!A || !B || !A->IsUnit() || !B->IsUnit() || A->Guid==B->Guid) return false;
    if(A->IsPlayer() && (A->GetField(PlayerField::FLAGS)&0x80000)) return false;
    if(B->IsPlayer() && (B->GetField(PlayerField::FLAGS)&0x10)) return false;
    const uint32 FlagsA=A->GetField(UnitField::FLAGS),FlagsB=B->GetField(UnitField::FLAGS);
    if(FlagsB&(0x2|0x100000|0x80|0x10000|0x2000000)) return false;
    const bool PlayerA=Controlled(*A),PlayerB=Controlled(*B);
    if((PlayerA && (FlagsB&0x100)) || (!PlayerA && (FlagsB&0x200)) || (PlayerB && (FlagsA&0x100)) || (!PlayerB && (FlagsA&0x200))) return false;
    const int32 Reaction=UnitReactionRank(C,A,B);
    if(!PlayerA && !PlayerB) return Reaction<4 || UnitReactionRank(C,B,A)<4;
    if(Reaction>=5) return false;
    const uint8 PvpA=A->GetFieldByte(UnitField::BYTES_2,1),PvpB=B->GetFieldByte(UnitField::BYTES_2,1);
    if(PlayerA && PlayerB)
    {
        const auto* OwnerA=Controller(C,A); const auto* OwnerB=Controller(C,B);
        if(Duel(OwnerA,OwnerB)) return true;
        if((PvpA&8) || (PvpB&8)) return false;
        if(OwnerA && OwnerB) return (PvpB&1) || ((PvpA&PvpB&4)!=0) || ((PvpA|PvpB)&2);
        return true;
    }
    return !(PlayerA?(PvpB&8):(PvpA&8));
}

FLinearColor WowLuaApi::UnitSelectionColor(const FWowLuaContext* C,const FWowEntity* E)
{
    const auto* Player=C && C->EntityManager?C->EntityManager->GetLocalPlayer():nullptr;
    if(!E || !Player) return FLinearColor(0,0,1,1);
    if(Controlled(*E))
    {
        if(CanUnitAttack(C,E,Player)) return CanUnitAttack(C,Player,E)?FLinearColor(1,0,0,1):FLinearColor(0,0,1,1);
        if(CanUnitAttack(C,Player,E)) return FLinearColor(1,1,0,1);
        const uint8 Pvp=E->GetFieldByte(UnitField::BYTES_2,1);
        if(!(Pvp&1) || (Pvp&8) || (Player->GetFieldByte(UnitField::BYTES_2,1)&8)) return FLinearColor(0,0,1,1);
    }
    const int32 Rank=UnitReactionRank(C,E,Player);
    if(Rank>=5) return FLinearColor(0,1,0,1);
    if(Rank==4) return FLinearColor(1,1,0,1);
    if(Rank==3) return FLinearColor(1,128.f/255,0,1);
    return FLinearColor(1,0,0,1);
}

#if __has_include("lua.h")
extern "C" {
#include "lua.h"
}
void WowLuaApi::UpdateUnitReactions(lua_State* L)
{
    auto* C=GetContext(L); if(!C || !C->EntityManager || !C->EventSystem) return;
    const auto* Player=C->EntityManager->GetLocalPlayer(); if(!Player) return;
    TArray<FString> Tokens={TEXT("player"),TEXT("target"),TEXT("focus"),TEXT("pet")};
    if(C->ConnectionManager)
    {
        const auto& Group=C->ConnectionManager->PacketHandler.GroupInfo;
        const int32 Count=Group.IsRaidGroup()?FMath::Min(Group.Members.Num()+1,40):FMath::Min(Group.Members.Num(),4);
        for(int32 I=1;I<=Count;++I) Tokens.Add(FString(Group.IsRaidGroup()?TEXT("raid"):TEXT("party"))+FString::FromInt(I));
    }
    for(const auto& Token:Tokens)
    {
        lua_pushstring(L,TCHAR_TO_UTF8(*Token)); const auto* E=ResolveUnitToken(L,-1); lua_pop(L,1);
        if(!E) {C->UnitReactionState.Remove(Token); continue;}
        const uint64 State=E->Guid ^ (uint64(UnitReactionRank(C,E,Player))<<48) ^ (uint64(UnitReactionRank(C,Player,E))<<52) ^
            (uint64(CanUnitAttack(C,Player,E))<<56) ^ (uint64(CanUnitAttack(C,E,Player))<<57) ^ (uint64(E->GetFieldByte(UnitField::BYTES_2,1))<<32);
        const auto* Previous=C->UnitReactionState.Find(Token); const bool Changed=Previous && *Previous!=State;
        C->UnitReactionState.Add(Token,State);
        if(Changed) {C->EventSystem->FireEvent(TEXT("UNIT_FACTION"),{Token}); C->EventSystem->FireEvent(TEXT("UNIT_FLAGS"),{Token});}
    }
}
#else
void WowLuaApi::UpdateUnitReactions(lua_State*) {}
#endif
