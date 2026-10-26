#include "WowConnectionManager.h"
#include "Net/WowWorldSocket.h"

namespace
{
template<typename T> void Append(TArray<uint8>& B,T Value) { const int32 At=B.AddUninitialized(sizeof(T)); FMemory::Memcpy(B.GetData()+At,&Value,sizeof(T)); }
void Text(TArray<uint8>& B,const FString& Value) { FTCHARToUTF8 Utf8(*Value); B.Append(reinterpret_cast<const uint8*>(Utf8.Get()),Utf8.Length()); B.Add(0); }
FWowMailMessage* Mail(FWowPacketHandler& H,uint32 Id) { return H.MailInbox.FindByPredicate([Id](const auto& M){return M.MessageId==Id;}); }
}
bool UWowConnectionManager::FindCarriedItemPosition(uint64 Guid,uint8& Bag,uint8& Slot) const
{
    const auto* P=PacketHandler.EntityManager.GetLocalPlayer(); if(!P || !Guid) return false;
    for(uint8 I=0;I<16;++I) if(P->GetBackpackItemGuid(I)==Guid) { Bag=255; Slot=I+23; return true; }
    for(uint8 I=1;I<=4;++I) if(const auto* B=PacketHandler.EntityManager.FindContainer(P->GetBagGuid(I)))
        for(int32 N=0;N<B->GetNumSlots();++N) if(B->GetItemGuidAtSlot(N)==Guid) {Bag=I+18; Slot=uint8(N); return true;}
    return false;
}
const FWowItemEntity* UWowConnectionManager::GetOutgoingMailItem(int32 Index) const
{
    if(Index<0 || Index>=12) return nullptr;
    const uint64 Guid=PacketHandler.MailComposer.Items[Index]; uint8 Bag=0,Slot=0;
    return FindCarriedItemPosition(Guid,Bag,Slot)?PacketHandler.EntityManager.FindItem(Guid):nullptr;
}
bool UWowConnectionManager::IsItemInOutgoingMail(uint64 Guid) const
{
    if(!Guid) return false;
    for(uint64 Item:PacketHandler.MailComposer.Items) if(Item==Guid) return true;
    return PacketHandler.MailComposer.Pending.Index>=0 && PacketHandler.MailComposer.Pending.SourceGuid==Guid;
}
void UWowConnectionManager::ClearOutgoingMail(bool Force)
{
    auto& C=PacketHandler.MailComposer; if(C.Sending && !Force) return;
    const bool WasLocked=C.LockIndex>=0,Showing=C.Showing;
    C=FWowMailComposer(); C.Showing=Showing; PacketHandler.OnMailSendInfoChanged.Broadcast(); OnCursorChanged.Broadcast();
    if(WasLocked) PacketHandler.OnMailUnlockItems.Broadcast();
}
void UWowConnectionManager::OpenMail(uint64 Guid)
{
    if(!Guid || State!=EWowSessionState::WorldInGame) return;
    if(PacketHandler.CurrentMailboxGuid!=Guid) ClearOutgoingMail();
    PacketHandler.CurrentMailboxGuid=Guid; PacketHandler.NextMailQuery=0;
}
void UWowConnectionManager::CloseMail()
{
    PacketHandler.CurrentMailboxGuid=0; PacketHandler.AutoLootMailId=0; PacketHandler.MailComposer.Showing=false; ClearOutgoingMail(); QueryNextMailTime();
}
bool UWowConnectionManager::ClickOutgoingMailAttachment(int32 Index,bool Remove)
{
    auto& C=PacketHandler.MailComposer;
    if(!PacketHandler.CurrentMailboxGuid || C.Sending || C.Pending.Index>=0 || C.LockIndex>=0) return false;
    if(Index<0) { for(int32 I=0;I<12;++I) if(!C.Items[I]) {Index=I; break;} }
    if(Index<0 || Index>=12) return RejectOutgoingMailItem(TEXT("ERR_MAIL_INVALID_ATTACHMENT_SLOT"));
    const uint64 Previous=C.Items[Index];
    if(Remove) { C.Items[Index]=0; PacketHandler.OnMailSendInfoChanged.Broadcast(); OnCursorChanged.Broadcast(); return true; }
    const auto* Held=GetCursorItem(); const uint64 Guid=Held?Held->Guid:0;
    if(Guid && Guid==Previous) { ClearCursorPayload(); return true; }
    if(Guid)
    {
        if(IsItemInOutgoingMail(Guid)) return false;
        RequestItemTemplate(Held->Entry); const auto* T=PacketHandler.ItemTemplates.Find(Held->Entry); if(!T) return false;
        if(T->Class==12 || T->Bonding==4) return RejectOutgoingMailItem(TEXT("ERR_MAIL_QUEST_ITEM"));
        if((Held->GetField(ItemField::FLAGS)&1) && !(T->Flags&0x08000000)) return RejectOutgoingMailItem(TEXT("ERR_MAIL_BOUND_ITEM"));
        if(T->Flags&2) return RejectOutgoingMailItem(TEXT("ERR_MAIL_CONJURED_ITEM"));
        if((T->Flags&0x10000) || Held->GetField(ItemField::DURATION)) return RejectOutgoingMailItem(TEXT("ERR_MAIL_LIMITED_DURATION_ITEM"));
        if(const auto* Bag=PacketHandler.EntityManager.FindContainer(Guid)) for(int32 I=0;I<Bag->GetNumSlots();++I) if(Bag->GetItemGuidAtSlot(I)) return RejectOutgoingMailItem(TEXT("ERR_MAIL_BAG"));
        uint8 Bag=0,Slot=0;
        const bool Carried=FindCarriedItemPosition(Guid,Bag,Slot);
        if(!Carried && (!CursorPayload.bFromInventorySlot || CursorPayload.SourceInventorySlot>=23)) return false;
        if(CursorPayload.SplitCount || !Carried)
        {
            const auto* P=PacketHandler.EntityManager.GetLocalPlayer(); bool Found=false; uint8 DestinationBag=255,DestinationSlot=0;
            for(int32 I=0;P && I<16;++I) if(!P->GetBackpackItemGuid(uint8(I))) {DestinationSlot=uint8(I+23); Found=true; break;}
            for(uint8 I=1;P && !Found && I<=4;++I)
            {
                const uint64 BagGuid=P->GetBagGuid(I); if(BagGuid==Guid) continue;
                if(const auto* B=PacketHandler.EntityManager.FindContainer(BagGuid)) for(int32 N=0;N<B->GetNumSlots();++N)
                    if(!B->GetItemGuidAtSlot(N)) {DestinationBag=I+18; DestinationSlot=uint8(N); Found=true; break;}
            }
            if(!Found) return RejectOutgoingMailItem(TEXT("ERR_INV_FULL"));
            auto& Pending=C.Pending; Pending.Index=Index; Pending.Bag=DestinationBag; Pending.Slot=DestinationSlot;
            Pending.Guid=CursorPayload.SplitCount?0:Guid; Pending.SourceGuid=Guid; Pending.PreviousGuid=Previous;
            Pending.Entry=Held->Entry; Pending.SourceCount=uint32(Held->GetStackCount()); Pending.Count=CursorPayload.SplitCount?CursorPayload.SplitCount:Pending.SourceCount; Pending.Deadline=FPlatformTime::Seconds()+10;
            if(CursorPayload.SplitCount) SendSplitItem(CursorPayload.SourceBag,CursorPayload.SourceSlot,DestinationBag,DestinationSlot,CursorPayload.SplitCount);
            else SendSwapItem(255,CursorPayload.SourceInventorySlot,DestinationBag,DestinationSlot);
            ClearCursorPayload(); PacketHandler.OnMailSendInfoChanged.Broadcast(); return true;
        }
        C.Items[Index]=Guid; ClearCursorPayload();
    }
    else C.Items[Index]=0;
    if(Previous)
    {
        uint8 Bag=0,Slot=0;
        if(FindCarriedItemPosition(Previous,Bag,Slot)) if(const auto* Item=PacketHandler.EntityManager.FindItem(Previous)) PickupBagItemCursor(Bag,Slot,Item->Entry);
    }
    PacketHandler.OnMailSendInfoChanged.Broadcast(); OnCursorChanged.Broadcast(); CheckOutgoingMailRefund(Index); return true;
}
void UWowConnectionManager::QueryNextMailTime()
{
    if(!WorldSocket || State!=EWowSessionState::WorldInGame || PacketHandler.MailStatusPending) return;
    PacketHandler.MailStatusPending=true; WorldSocket->SendPacket(0x0284,{});
}
void UWowConnectionManager::ResetMailState()
{
    PacketHandler.CurrentMailboxGuid=0; PacketHandler.MailInbox.Empty(); PacketHandler.MailInboxTotalCount=0;
    PacketHandler.MailComposer=FWowMailComposer(); PacketHandler.RecentMailSenders.Empty();
    PacketHandler.RequestedMailNames.Empty(); PacketHandler.RequestedMailCreatures.Empty();
    PacketHandler.PendingMailAction=-1; PacketHandler.PendingMailId=PacketHandler.PendingMailItem=PacketHandler.AutoLootMailId=0;
    PacketHandler.MailListPending=PacketHandler.HasUnreadMail=PacketHandler.MailStatusPending=false; PacketHandler.NextMailQuery=PacketHandler.NextMailStatusQuery=0;
}

bool UWowConnectionManager::SendMail(const FString& Receiver,const FString& Subject,const FString& Body)
{
    auto& H=PacketHandler; auto& C=H.MailComposer;
    if(!WorldSocket || State!=EWowSessionState::WorldInGame || !H.CurrentMailboxGuid || !C.Stationery || H.PendingMailAction>=0 || C.Pending.Index>=0 || C.LockIndex>=0 || C.Sending || Receiver.IsEmpty() || Receiver.Len()>64 || Subject.IsEmpty() || Subject.Len()>255 || Body.Len()>8000 || (C.Money && C.COD)) return false;
    TArray<uint8> Data; Append(Data,H.CurrentMailboxGuid); Text(Data,Receiver); Text(Data,Subject); Text(Data,Body);
    uint8 Count=0;
    for(int32 I=0;I<12;++I) if(C.Items[I]) { if(!GetOutgoingMailItem(I)) return false; ++Count; }
    if(C.COD && !Count) return false;
    Append(Data,C.Stationery); Append(Data,Count?C.Package:0u); Append(Data,Count);
    for(int32 I=0;I<12;++I) if(C.Items[I]) { Append(Data,uint8(I)); Append(Data,C.Items[I]); }
    Append(Data,C.Money); Append(Data,C.COD); Append(Data,uint64(0)); Append(Data,uint8(0));
    H.PendingMailAction=0; H.PendingMailId=H.PendingMailItem=0; C.Sending=true;
    WorldSocket->SendPacket(0x0238,Data); H.OnMailSendInfoChanged.Broadcast(); return true;
}
bool UWowConnectionManager::SendMailAction(uint32 Id,uint32 Action,uint32 Attachment)
{
    auto& H=PacketHandler; auto* M=Mail(H,Id);
    if(!WorldSocket || State!=EWowSessionState::WorldInGame || !H.CurrentMailboxGuid || H.PendingMailAction>=0 || !M) return false;
    uint32 Opcode=0;
    switch(Action)
    {
    case 1: if(!M->Money) return false; Opcode=0x0245; break;
    case 2: if(!M->Attachments.ContainsByPredicate([Attachment](const auto& A){return A.ItemGuidLow==Attachment;})) return false; Opcode=0x0246; break;
    case 3: if(M->MessageType || !M->SenderGuid || (M->Attachments.IsEmpty() && !M->Money) || (M->Checked&0x20A)) return false; Opcode=0x0248; break;
    case 4: if(!M->MessageType && !(M->Checked&0x20A) && (!M->Attachments.IsEmpty() || M->Money)) return false; Opcode=0x0249; break;
    case 5: if((M->Body.IsEmpty() && !M->MailTemplateId) || (M->Checked&4)) return false; Opcode=0x024A; break;
    default: return false;
    }
    TArray<uint8> Data; Append(Data,H.CurrentMailboxGuid); Append(Data,Id);
    if(Action==2) Append(Data,Attachment); else if(Action==3) Append(Data,M->SenderGuid); else if(Action==4) Append(Data,uint32(0));
    H.PendingMailAction=int32(Action); H.PendingMailId=Id; H.PendingMailItem=Attachment; WorldSocket->SendPacket(Opcode,Data); return true;
}
bool UWowConnectionManager::MarkMailRead(uint32 Id)
{
    auto* M=Mail(PacketHandler,Id);
    if(!WorldSocket || State!=EWowSessionState::WorldInGame || !PacketHandler.CurrentMailboxGuid || !M || (M->Checked&1)) return false;
    TArray<uint8> Data; Append(Data,PacketHandler.CurrentMailboxGuid); Append(Data,Id); WorldSocket->SendPacket(0x0247,Data);
    M->Checked|=1; QueryNextMailTime(); return true;
}
void UWowConnectionManager::StartMailAutoLoot(uint32 Id)
{
    const auto* M=Mail(PacketHandler,Id); if(!M || M->COD || PacketHandler.PendingMailAction>=0) return;
    PacketHandler.AutoLootMailId=Id; MarkMailRead(Id); TickMailState();
}
void UWowConnectionManager::TickMailState()
{
    if(State!=EWowSessionState::WorldInGame) return;
    auto& H=PacketHandler; auto& C=H.MailComposer; const double Now=FPlatformTime::Seconds();
    if(Now>=H.NextMailStatusQuery && !H.MailStatusPending) QueryNextMailTime();
    if(C.Pending.Index>=0)
    {
        const auto Pending=C.Pending; const auto* Item=ResolveCursorSource(Pending.Bag,Pending.Slot); const auto* Source=H.EntityManager.FindItem(Pending.SourceGuid);
        const bool SourceMatches=Pending.Guid || (Source && Pending.SourceCount>Pending.Count && uint32(Source->GetStackCount())==Pending.SourceCount-Pending.Count);
        if(SourceMatches && Item && Item->Entry==Pending.Entry && uint32(Item->GetStackCount())==Pending.Count && (Pending.Guid?Item->Guid==Pending.Guid:Item->Guid!=Pending.SourceGuid))
        {
            C.Items[Pending.Index]=Item->Guid; C.Pending=FWowMailComposer::FPendingAttachment();
            uint8 Bag=0,Slot=0;
            if(FindCarriedItemPosition(Pending.PreviousGuid,Bag,Slot)) if(const auto* Old=H.EntityManager.FindItem(Pending.PreviousGuid)) PickupBagItemCursor(Bag,Slot,Old->Entry);
            H.OnMailSendInfoChanged.Broadcast(); OnCursorChanged.Broadcast();
            CheckOutgoingMailRefund(Pending.Index);
        }
        else if(Now>=Pending.Deadline) { C.Pending=FWowMailComposer::FPendingAttachment(); H.OnMailSendInfoChanged.Broadcast(); OnCursorChanged.Broadcast(); }
    }
    if(!C.Sending)
    {
        bool Changed=false; for(int32 I=0;I<12;++I) if(C.Items[I] && !GetOutgoingMailItem(I)) { C.Items[I]=0; Changed=true; }
        if(Changed) { H.OnMailSendInfoChanged.Broadcast(); OnCursorChanged.Broadcast(); }
    }
    if(C.LockIndex>=0 && !C.LockNotified) if(const auto* Item=GetOutgoingMailItem(C.LockIndex); Item && H.ItemTemplates.Contains(Item->Entry))
    { C.LockNotified=true; H.OnMailLockItems.Broadcast(C.LockIndex+1,Item->Guid); }
    if(C.LockIndex>=0 && !GetOutgoingMailItem(C.LockIndex)) RespondOutgoingMailLock(C.LockIndex,false);
    if(H.AutoLootMailId && H.PendingMailAction<0 && H.CurrentMailboxGuid)
    {
        auto* M=Mail(H,H.AutoLootMailId);
        if(!M || M->COD) H.AutoLootMailId=0;
        else if(M->Money) SendMailAction(M->MessageId,1);
        else if(!M->Attachments.IsEmpty()) SendMailAction(M->MessageId,2,M->Attachments[0].ItemGuidLow);
        else if(M->MessageType==2 || (M->Checked&8) || (M->Body.IsEmpty() && !M->MailTemplateId)) SendMailAction(M->MessageId,4);
        else H.AutoLootMailId=0;
    }
}
void FWowPacketHandler::HandleMailResult(FPacketReader& R)
{
    if(!R.CanRead(12)) return;
    FWowMailResult Result; Result.Id=R.ReadU32(); Result.Action=R.ReadU32(); Result.Error=R.ReadU32();
    if(Result.Error==1) { if(!R.CanRead(4)) return; Result.EquipError=R.ReadU32(); }
    else if(Result.Action==2) { if(!R.CanRead(8)) return; Result.ItemGuid=R.ReadU32(); Result.Count=R.ReadU32(); }
    if(int32(Result.Action)!=PendingMailAction || Result.Id!=PendingMailId) return;
    Result.Index=MailInbox.IndexOfByPredicate([&](const auto& M){return M.MessageId==Result.Id;})+1;
    PendingMailAction=-1; PendingMailId=PendingMailItem=0;
    if(!Result.Error)
    {
        if(Result.Action==0) { const bool Showing=MailComposer.Showing; MailComposer=FWowMailComposer(); MailComposer.Showing=Showing; }
        else if(Result.Action==3 || Result.Action==4)
        {
            const int32 Removed=MailInbox.RemoveAll([&](const auto& M){return M.MessageId==Result.Id;});
            MailInboxTotalCount-=FMath::Min(MailInboxTotalCount,uint32(Removed));
        }
        else if(auto* M=Mail(*this,Result.Id))
        {
            if(Result.Action==1) M->Money=0;
            else if(Result.Action==2) { M->Attachments.RemoveAll([&](const auto& A){return A.ItemGuidLow==Result.ItemGuid;}); M->COD=0; }
            else if(Result.Action==5) M->Checked|=4;
            if((Result.Action==1 || Result.Action==2) && !M->Money && M->Attachments.IsEmpty() && (M->MessageType==2 || (M->Checked&8) || (M->Body.IsEmpty() && !M->MailTemplateId))) AutoLootMailId=Result.Id;
        }
    }
    else { MailComposer.Sending=false; AutoLootMailId=0; }
    NextMailQuery=0; NextMailStatusQuery=0;
    OnMailResult.Broadcast(Result); OnMailSendInfoChanged.Broadcast(); OnMailListReceived.Broadcast(MailInbox);
}
void FWowPacketHandler::HandleNextMailTime(FPacketReader& R)
{
    if(!R.CanRead(8)) return;
    const float Time=R.ReadFloat(); const uint32 Count=R.ReadU32(); if(Count>255 || !R.CanRead(Count*24)) return;
    TArray<FWowRecentMailSender> Senders;
    for(uint32 I=0;I<Count;++I) { FWowRecentMailSender S; S.Guid=R.ReadU64(); S.Entry=R.ReadU32(); S.Type=R.ReadU32(); S.Stationery=R.ReadU32(); S.TimeLeft=R.ReadFloat(); Senders.Add(S); }
    RecentMailSenders=MoveTemp(Senders); HasUnreadMail=Time>=0; MailStatusPending=false;
    NextMailStatusQuery=Time>0?FPlatformTime::Seconds()+Time:DBL_MAX; OnPendingMailChanged.Broadcast();
}
void FWowPacketHandler::HandleReceivedMail(FPacketReader& R)
{
    if(!R.CanRead(4)) return;
    R.ReadU32(); HasUnreadMail=true; NextMailStatusQuery=0; MailStatusPending=false; OnPendingMailChanged.Broadcast();
}

void UWowConnectionManager::CheckOutgoingMailRefund(int32 Index)
{
    const auto* Item=GetOutgoingMailItem(Index); if(!Item || !(Item->GetField(ItemField::FLAGS)&0x1000)) return;
    for(int32 I:{0,2,3,6}) if(Item->GetField(ItemField::ENCHANTMENT_1_1+I*3)) return;
    RequestItemRefundInfo(Item->Guid); RequestItemTemplate(Item->Entry);
    if(PacketHandler.ItemRefunds.Contains(Item->Guid) && PacketHandler.PlayedTimeReceived>0 && PacketHandler.ItemRefundSeconds(Item->Guid)<=0) return;
    auto& C=PacketHandler.MailComposer; C.LockIndex=Index; C.LockNotified=PacketHandler.ItemTemplates.Contains(Item->Entry);
    if(C.LockNotified) PacketHandler.OnMailLockItems.Broadcast(Index+1,Item->Guid);
}
void UWowConnectionManager::RespondOutgoingMailLock(int32 Index,bool Keep)
{
    auto& C=PacketHandler.MailComposer; if(Index<0 || Index>=12 || C.LockIndex!=Index) return;
    C.LockIndex=-1; C.LockNotified=false; if(!Keep) C.Items[Index]=0;
    PacketHandler.OnMailSendInfoChanged.Broadcast(); OnCursorChanged.Broadcast(); PacketHandler.OnMailUnlockItems.Broadcast();
}

bool UWowConnectionManager::RejectOutgoingMailItem(const TCHAR* Key)
{
    PacketHandler.OnMailClientError.Broadcast(FString(Key)); return false;
}
