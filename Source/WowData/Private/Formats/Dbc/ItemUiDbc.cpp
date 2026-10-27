#include "Formats/Dbc/ItemUiDbc.h"
#include "Formats/DbcParser.h"
#include "Mpq/MpqManager.h"

int32 FItemUiDbc::DefaultReaction(uint32 From,uint32 To) const
{
    if(From && From==To) return 5;
    const auto* A=ReactionTemplates.Find(From); const auto* B=ReactionTemplates.Find(To); if(!A || !B) return 0;
    for(uint32 Id:A->Enemies) if(Id && Id==B->Faction) return 2;
    for(uint32 Id:A->Friends) if(Id && Id==B->Faction) return 5;
    if(A->EnemyGroup&B->Group) return 2;
    if((A->FriendGroup&B->Group) || (B->FriendGroup&A->Group)) return 5;
    return 4;
}

bool FItemUiDbc::Load(FMpqManager& Mpq)
{
    FDbcParser Parser; bool Complete=true;
    const auto Read=[&](const TCHAR* File,uint32 Fields)
    {
        TArray<uint8> Bytes; const bool Valid=Mpq.ReadFile(FString(TEXT("DBFilesClient\\"))+File,Bytes) && Parser.Parse(Bytes) && Parser.GetFieldCount()>=Fields;
        if(!Valid) { UE_LOG(LogTemp,Warning,TEXT("Missing item UI DBC: %s"),File); Complete=false; } return Valid;
    };
    if(Read(TEXT("ItemExtendedCost.dbc"),16))
    {
        ExtendedCosts.Empty(); for(uint32 I=0;I<Parser.GetRecordCount();++I)
        {
            FExtendedCost Cost; Cost.Honor=Parser.GetUInt(I,1); Cost.Arena=Parser.GetUInt(I,2); Cost.Bracket=Parser.GetUInt(I,3);
            for(uint32 N=0;N<5;++N) { Cost.Items[N]=Parser.GetUInt(I,4+N); Cost.Counts[N]=Parser.GetUInt(I,9+N); }
            Cost.Rating=Parser.GetUInt(I,14); Cost.Group=Parser.GetUInt(I,15); ExtendedCosts.Add(Parser.GetUInt(I,0),Cost);
        }
    }
    if(Read(TEXT("ItemClass.dbc"),20)) { Classes.Empty(); for(uint32 I=0;I<Parser.GetRecordCount();++I) Classes.Add(Parser.GetUInt(I,0),Parser.GetString(I,3)); }
    if(Read(TEXT("ItemSubClass.dbc"),44)) { Subclasses.Empty(); for(uint32 I=0;I<Parser.GetRecordCount();++I) Subclasses.Add((uint64(Parser.GetUInt(I,0))<<32)|Parser.GetUInt(I,1),Parser.GetString(I,10)); }
    if(Read(TEXT("DurabilityCosts.dbc"),30))
    {
        DurabilityCosts.Empty(); for(uint32 I=0;I<Parser.GetRecordCount();++I) { TArray<uint32> Values; for(uint32 N=1;N<30;++N) Values.Add(Parser.GetUInt(I,N)); DurabilityCosts.Add(Parser.GetUInt(I,0),MoveTemp(Values)); }
    }
    if(Read(TEXT("DurabilityQuality.dbc"),2)) { DurabilityQuality.Empty(); for(uint32 I=0;I<Parser.GetRecordCount();++I) DurabilityQuality.Add(Parser.GetUInt(I,0),Parser.GetFloat(I,1)); }
    if(Read(TEXT("FactionTemplate.dbc"),14)) { FactionTemplates.Empty(); ReactionTemplates.Empty(); for(uint32 I=0;I<Parser.GetRecordCount();++I) {const uint32 Id=Parser.GetUInt(I,0); FReactionTemplate T; T.Faction=Parser.GetUInt(I,1); T.Flags=Parser.GetUInt(I,2); T.Group=Parser.GetUInt(I,3); T.FriendGroup=Parser.GetUInt(I,4); T.EnemyGroup=Parser.GetUInt(I,5); for(int32 J=0;J<4;++J) {T.Enemies[J]=Parser.GetUInt(I,6+J); T.Friends[J]=Parser.GetUInt(I,10+J);} FactionTemplates.Add(Id,T.Faction); ReactionTemplates.Add(Id,T);} }
    if(Read(TEXT("BankBagSlotPrices.dbc"),2)) { BankBagPrices.Empty(); for(uint32 I=0;I<Parser.GetRecordCount();++I) BankBagPrices.Add(Parser.GetUInt(I,0),Parser.GetUInt(I,1)); }
    if(Read(TEXT("Stationery.dbc"),4)) { MailStationeries.Empty(); for(uint32 I=0;I<Parser.GetRecordCount();++I) { FStationery Row; Row.Id=Parser.GetUInt(I,0); Row.Item=Parser.GetUInt(I,1); Row.Texture=Parser.GetString(I,2); Row.Flags=Parser.GetUInt(I,3); MailStationeries.Add(MoveTemp(Row)); } }
    if(Read(TEXT("Package.dbc"),20)) { MailPackages.Empty(); for(uint32 I=0;I<Parser.GetRecordCount();++I) { FPackage Row; Row.Id=Parser.GetUInt(I,0); Row.Icon=Parser.GetString(I,1); Row.Cost=Parser.GetUInt(I,2); Row.Name=Parser.GetString(I,3); MailPackages.Add(MoveTemp(Row)); } }
    if(Read(TEXT("MailTemplate.dbc"),35))
    {
        MailBodies.Empty(); MailSubjects.Empty();
        for(uint32 I=0;I<Parser.GetRecordCount();++I) { const uint32 Id=Parser.GetUInt(I,0); MailBodies.Add(Id,Parser.GetString(I,18)); MailSubjects.Add(Id,Parser.GetString(I,1)); }
    }
    if(Read(TEXT("SpellItemEnchantment.dbc"),38))
    {
        Enchants.Empty(); for(uint32 I=0;I<Parser.GetRecordCount();++I) { FEnchant Row; Row.Name=Parser.GetString(I,14); Row.Gem=Parser.GetUInt(I,33); Row.Flags=Parser.GetUInt(I,32); Enchants.Add(Parser.GetUInt(I,0),MoveTemp(Row)); }
    }
    if(Read(TEXT("ChatChannels.dbc"),37))
    {
        ChannelDefinitions.Empty(); for(uint32 I=0;I<Parser.GetRecordCount();++I)
        {FChatChannel Row; Row.Id=Parser.GetUInt(I,0); Row.Flags=Parser.GetUInt(I,1); Row.Pattern=Parser.GetString(I,3); Row.Name=Parser.GetString(I,20); if(Row.Id && !Row.Name.IsEmpty()) ChannelDefinitions.Add(MoveTemp(Row));}
    }
    return Complete;
}
uint32 FItemUiDbc::RepairCost(uint32 Level,uint32 Quality,uint32 Class,uint32 Subclass,uint32 Lost,float Discount) const
{
    if(!Lost) return 0;
    const int32 Column=Class==2 && Subclass<21?int32(Subclass):Class==4 && Subclass<8?21+int32(Subclass):-1;
    const auto* Costs=DurabilityCosts.Find(Level); const auto* Modifier=DurabilityQuality.Find((Quality+1)*2);
    if(Column<0 || !Costs || !Costs->IsValidIndex(Column) || !Modifier) return 0;
    const uint32 Base=uint32(FMath::Min(double(Lost)*(*Costs)[Column]*(*Modifier),double(MAX_uint32)));
    const double Discounted=float(Base)*FMath::Clamp(Discount,0.f,1.f);
    return FMath::Max(1u,uint32(FMath::Min(Discounted,double(MAX_uint32))));
}
