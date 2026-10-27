#include "WowPacketHandler.h"
#include "Misc/DateTime.h"
namespace
{
bool Packed(FPacketReader& R,uint64& Value)
{
    if(!R.CanRead(1)) return false; const uint8 Mask=R.ReadU8(); Value=0;
    for(int I=0;I<8;++I) if(Mask&(1<<I)) {if(!R.CanRead(1)) return false; Value|=uint64(R.ReadU8())<<(I*8);} return true;
}
bool Date(uint32 Value)
{return FDateTime::Validate(2000+int32(Value>>24),int32((Value>>20)&15)+1,int32((Value>>14)&63)+1,int32((Value>>6)&31),int32(Value&63),0,0);}
bool Progress(FPacketReader& R,FWowAchievementState::FProgress& P,uint64 Owner)
{
    if(!Packed(R,P.Counter) || !Packed(R,P.Player) || !P.Player || (Owner && Owner!=P.Player) || !R.CanRead(16)) return false;
    P.Flags=R.ReadU32(); P.Date=R.ReadU32(); P.Elapsed=R.ReadU32(); P.CreationElapsed=R.ReadU32(); P.At=FPlatformTime::Seconds();
    return P.Flags<=1 && Date(P.Date);
}
bool Snapshot(FPacketReader& R,FWowAchievementState::FSnapshot& Out,uint64 Owner)
{
    bool End=false;
    while(R.CanRead(4))
    {
        const uint32 Id=R.ReadU32(); if(Id==MAX_uint32) {End=true; break;}
        if(!Id || !R.CanRead(4) || Out.Completed.Num()>=10000 || Out.Completed.Contains(Id)) return false;
        const uint32 When=R.ReadU32(); if(!Date(When)) return false; Out.Completed.Add(Id,When);
    }
    if(!End) return false; End=false;
    while(R.CanRead(4))
    {
        const uint32 Id=R.ReadU32(); if(Id==MAX_uint32) {End=true; break;}
        FWowAchievementState::FProgress P; if(!Id || Out.Progress.Num()>=20000 || Out.Progress.Contains(Id) || !Progress(R,P,Owner)) return false;
        Out.Progress.Add(Id,P);
    }
    if(!End || R.Remaining()) return false; Out.Ready=true; return true;
}
}
void FWowPacketHandler::RegisterAchievementHandlers()
{
    Handlers.Add(0x47D,&FWowPacketHandler::HandleAllAchievements); Handlers.Add(0x46C,&FWowPacketHandler::HandleInspectAchievements);
    Handlers.Add(0x468,&FWowPacketHandler::HandleAchievementEarned); Handlers.Add(0x46A,&FWowPacketHandler::HandleAchievementCriteria);
    Handlers.Add(0x49F,&FWowPacketHandler::HandleAchievementDeleted); Handlers.Add(0x49E,&FWowPacketHandler::HandleCriteriaDeleted);
}
void FWowPacketHandler::ResetAchievementState()
{
    FWowAchievementState Fresh; Fresh.Revision=Achievements.Revision+1; Fresh.DataRevision=Achievements.DataRevision+1;
    Fresh.ComparisonRevision=Achievements.ComparisonRevision+1; Achievements=MoveTemp(Fresh);
}
void FWowPacketHandler::HandleAllAchievements(FPacketReader& R)
{
    FWowAchievementState::FSnapshot S; if(!Snapshot(R,S,EntityManager.LocalPlayerGuid)) return;
    Achievements.Own=MoveTemp(S); ++Achievements.Revision; ++Achievements.DataRevision;
}
void FWowPacketHandler::HandleInspectAchievements(FPacketReader& R)
{
    uint64 Guid=0; if(!Packed(R,Guid) || !Guid || Guid!=Achievements.CompareGuid) return;
    FWowAchievementState::FSnapshot S; if(!Snapshot(R,S,Guid)) return;
    Achievements.Comparison=MoveTemp(S); ++Achievements.ComparisonRevision;
}
void FWowPacketHandler::HandleAchievementEarned(FPacketReader& R)
{
    uint64 Guid=0; if(!Packed(R,Guid) || !R.CanRead(12)) return;
    const uint32 Id=R.ReadU32(),When=R.ReadU32(); R.ReadU32();
    if(!Id || Guid!=EntityManager.LocalPlayerGuid || !Guid || !Date(When) || R.Remaining()) return;
    const bool New=!Achievements.Own.Completed.Contains(Id); Achievements.Own.Completed.Add(Id,When); ++Achievements.Revision;
    if(New) {if(Achievements.Earned.Num()>=256) Achievements.Earned.RemoveAt(0); Achievements.Earned.Add(Id);}
}
void FWowPacketHandler::HandleAchievementCriteria(FPacketReader& R)
{
    if(!R.CanRead(4)) return; const uint32 Id=R.ReadU32(); FWowAchievementState::FProgress P;
    if(!Id || !Progress(R,P,EntityManager.LocalPlayerGuid) || P.Player!=EntityManager.LocalPlayerGuid || R.Remaining()) return;
    Achievements.Own.Progress.Add(Id,P); ++Achievements.Revision;
}
void FWowPacketHandler::HandleAchievementDeleted(FPacketReader& R)
{if(R.Remaining()!=4) return; const uint32 Id=R.ReadU32(); if(!Id) return; Achievements.Own.Completed.Remove(Id); Achievements.Earned.Remove(Id); ++Achievements.Revision;}
void FWowPacketHandler::HandleCriteriaDeleted(FPacketReader& R)
{if(R.Remaining()!=4) return; const uint32 Id=R.ReadU32(); if(!Id) return; Achievements.Own.Progress.Remove(Id); ++Achievements.Revision;}
