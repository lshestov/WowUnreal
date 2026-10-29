#include "WowFrameManager.h"
#include "WowEventSystem.h"
#include "WowMultiLineEditBox.h"
#include "Components/EditableTextBox.h"
#include "Framework/Application/SlateApplication.h"

bool FWowFrameManager::HasEditFocus(int64 Handle) const
{
    if(const auto* Edit=GetMultiLineEditBoxWidget(Handle)) return Edit->HasEditFocus();
    if(const auto* Edit=GetEditBoxWidget(Handle))
    {const auto Widget=Edit->GetCachedWidget(); return Widget.IsValid() && Widget->HasAnyUserFocusOrFocusedDescendants();}
    return false;
}
void FWowFrameManager::NotifyEditFocus(int64 Handle)
{
    if(Handle>=0) {const auto* Def=GetFrameDef(Handle); if(!Def || Def->Type!=EWowFrameType::EditBox) return;}
    if(Handle==FocusedEditHandle) return;
    const uint32 Revision=++EditFocusRevision; const int64 Previous=FocusedEditHandle; FocusedEditHandle=-1;
    if(EventSystem && Previous>=0) EventSystem->RunFrameScript(Previous,TEXT("OnEditFocusLost"));
    if(EditFocusRevision!=Revision) return;
    FocusedEditHandle=Handle;
    if(EventSystem && Handle>=0) EventSystem->RunFrameScript(Handle,TEXT("OnEditFocusGained"));
}
void FWowFrameManager::SyncEditFocus()
{
    int64 Current=-1;
    for(const auto& Pair:EditBoxWidgets) if(IsFrameHierarchyShown(Pair.Key) && HasEditFocus(Pair.Key)) {Current=Pair.Key; break;}
    if(Current<0) for(const auto& Pair:MultiLineEditBoxWidgets) if(IsFrameHierarchyShown(Pair.Key) && HasEditFocus(Pair.Key)) {Current=Pair.Key; break;}
    NotifyEditFocus(Current);
}
void FWowFrameManager::SetEditFocus(int64 Handle,bool Focus)
{
    if(Focus)
    {
        if(!IsFrameHierarchyShown(Handle)) return;
        if(auto* Edit=GetEditBoxWidget(Handle); Edit && Edit->GetIsEnabled()) Edit->SetKeyboardFocus();
        if(auto* Edit=GetMultiLineEditBoxWidget(Handle); Edit && Edit->GetIsEnabled()) Edit->SetKeyboardFocus();
    }
    else if(HasEditFocus(Handle) && FSlateApplication::IsInitialized()) FSlateApplication::Get().SetAllUserFocusToGameViewport();
    SyncEditFocus();
}

void FWowFrameManager::NotifyEditorTextChanged(int64 Handle,const FString& Value,bool FromUser)
{
    const auto* Previous=EditBoxLastText.Find(Handle); const bool Changed=!Previous || *Previous!=Value;
    EditBoxLastText.Add(Handle,Value);
    if(Changed && EventSystem) EventSystem->RunFrameScript(Handle,TEXT("OnTextChanged"),{FromUser?TEXT("1"):TEXT("0")});
}

int64 FWowFrameManager::GetCharacterCaptureFrame() const
{
    if(!EventSystem) return -1;
    int64 Best=-1; int32 BestZ=MIN_int32;
    for(const auto& Pair:Frames)
    {
        const auto& E=Pair.Value;
        if(!E.bKeyboardEnabled || E.Def.Type==EWowFrameType::EditBox || !IsFrameDrawn(Pair.Key) || !EventSystem->HasFrameScript(Pair.Key,TEXT("OnChar"))) continue;
        const int32 Z=int32(E.Def.Strata)*1000+E.Def.FrameLevel;
        if(Z>BestZ || (Z==BestZ && Pair.Key>Best)) { Best=Pair.Key; BestZ=Z; }
    }
    return Best;
}

bool FWowFrameManager::DispatchCharacter(TCHAR Character)
{
    if(Character<32) return false; // Enter/backspace belong to key handlers.
    for(const auto& Edit:EditBoxWidgets) if(Edit.Value.IsValid() && HasEditFocus(Edit.Key)) return false;
    for(const auto& Edit:MultiLineEditBoxWidgets) if(Edit.Value.IsValid() && Edit.Value->HasEditFocus()) return false;
    const int64 Handle=GetCharacterCaptureFrame();
    return Handle>=0 && EventSystem->RunFrameScript(Handle,TEXT("OnChar"),{FString::Chr(Character)});
}
