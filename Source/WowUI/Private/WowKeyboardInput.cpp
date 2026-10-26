#include "WowFrameManager.h"
#include "WowEventSystem.h"
#include "WowMultiLineEditBox.h"
#include "Components/EditableTextBox.h"

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
    for(const auto& Edit:EditBoxWidgets) if(Edit.Value.IsValid() && Edit.Value->HasKeyboardFocus()) return false;
    for(const auto& Edit:MultiLineEditBoxWidgets) if(Edit.Value.IsValid() && Edit.Value->HasEditFocus()) return false;
    const int64 Handle=GetCharacterCaptureFrame();
    return Handle>=0 && EventSystem->RunFrameScript(Handle,TEXT("OnChar"),{FString::Chr(Character)});
}
