#include "WowFrameManager.h"
#include "WowPlacedFrameLayout.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "String/LexFromString.h"
#include "Misc/Paths.h"

namespace
{
const TCHAR* const Points[]={TEXT("TOPLEFT"),TEXT("TOP"),TEXT("TOPRIGHT"),TEXT("LEFT"),TEXT("CENTER"),TEXT("RIGHT"),TEXT("BOTTOMLEFT"),TEXT("BOTTOM"),TEXT("BOTTOMRIGHT")};
}

FWowAnchor FWowFrameManager::GetNearestScreenAnchor(int64 Handle)
{
    const auto Rect=GetLayoutRect(Handle);
    const auto Screen=GetLayoutRect(FindFrame(TEXT("UIParent")));
    const auto A=WowPlacedFrameLayout::Nearest(Rect.X,Rect.Y,Rect.Z,Rect.W,Screen.Z,Screen.W,GetEffectiveFrameScale(Handle));
    return {static_cast<EWowAnchorPoint>(A.Point),TEXT("UIParent"),static_cast<EWowAnchorPoint>(A.Point),float(A.X),float(A.Y)};
}

void FWowFrameManager::LoadUserPlacedFrames(const FString& Path)
{
    // Bind the path to this UI instance: a later character identity change must
    // never redirect a retiring VM's save into the next character's directory.
    UserPlacedLayoutPath=Path;
    UpdateViewportLayout();
    FString Text;
    if(!FFileHelper::LoadFileToString(Text,*Path)) return;
    TArray<FString> Lines;
    Text.ParseIntoArrayLines(Lines);
    FString Name;
    int32 Level=-1,Point=0,X=0,Y=0,W=0,H=0;
    bool Valid=true;
    const auto Place=[&]
    {
        const int64 Handle=FindFrame(Name);
        auto* Def=GetMutableFrameDef(Handle);
        if(!Valid || !Def || Def->bAnonymous || Def->bVirtual || Name==TEXT("UIParent")) return;
        if(Level>=0) SetFrameLevel(Handle,Level);
        if(Def->bMovable)
        {
            const auto P=static_cast<EWowAnchorPoint>(Point);
            SetFrameAnchors(Handle,{{P,TEXT("UIParent"),P,float(X),float(Y)}});
            Def->bUserPlaced=true;
        }
        if(Def->bResizable && W>=0 && H>=0)
        {
            SetFrameSize(Handle,W,H);
            Def->bUserPlaced=true;
        }
    };
    for(const FString& Line:Lines)
    {
        FString Key,Value;
        if(!Line.Split(TEXT(":"),&Key,&Value)) continue;
        Key.TrimStartAndEndInline(); Value.TrimStartAndEndInline();
        if(Key.Equals(TEXT("Frame"),ESearchCase::IgnoreCase))
        {
            Place(); Name=Value; Level=-1; Point=X=Y=W=H=0; Valid=true;
        }
        else if(Key.Equals(TEXT("Anchor"),ESearchCase::IgnoreCase))
        {
            bool Found=false;
            for(int32 I=0;I<9;++I) if(Value.Equals(Points[I],ESearchCase::IgnoreCase)) {Point=I; Found=true; break;}
            Valid&=Found;
        }
        else
        {
            int32* Target=Key.Equals(TEXT("FrameLevel"),ESearchCase::IgnoreCase)?&Level:
                Key.Equals(TEXT("X"),ESearchCase::IgnoreCase)?&X:Key.Equals(TEXT("Y"),ESearchCase::IgnoreCase)?&Y:
                Key.Equals(TEXT("W"),ESearchCase::IgnoreCase)?&W:Key.Equals(TEXT("H"),ESearchCase::IgnoreCase)?&H:nullptr;
            if(Target) Valid&=LexTryParseString(*Target,*Value);
        }
    }
    Place();
    // Startup event handlers may immediately measure siblings anchored to a
    // restored window; discard the pre-load dependency rectangles as well.
    FrameRects.Reset(); bLayoutDirty=true;
}

void FWowFrameManager::SaveUserPlacedFrames()
{
    if(UserPlacedLayoutPath.IsEmpty()) return; // Never overwrite layout during bootstrap or a glue VM save.
    if(MovingFrame>=0) StopFrameMovement(MovingFrame);
    FString Text=TEXT("Version: 1\n");
    TArray<int64> Handles;
    Frames.GetKeys(Handles); Handles.Sort();
    for(int64 Handle:Handles)
    {
        const auto& Def=Frames[Handle].Def;
        if(!Def.bUserPlaced || Def.bDontSavePosition || (!Def.bMovable && !Def.bResizable) || Def.bAnonymous || Def.bVirtual || Def.Name.IsEmpty() || Def.Name==TEXT("UIParent")) continue;
        const auto Rect=GetLayoutRect(Handle);
        const double Scale=GetEffectiveFrameScale(Handle);
        if(Rect.ContainsNaN() || Rect.Z<=0 || Rect.W<=0 || Scale<=0) continue;
        const auto A=GetNearestScreenAnchor(Handle);
        Text+=FString::Printf(TEXT("Frame: %s\nFrameLevel: %d\nAnchor: %s\nX: %d\nY: %d\nW: %d\nH: %d\n"),
            *Def.Name,Def.FrameLevel,Points[static_cast<int32>(A.Point)],WowPlacedFrameLayout::Round(A.OffsetX),WowPlacedFrameLayout::Round(A.OffsetY),
            WowPlacedFrameLayout::Round(Rect.Z/Scale),WowPlacedFrameLayout::Round(Rect.W/Scale));
    }
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(UserPlacedLayoutPath),true);
    // Replace only after the complete file is written successfully.
    const FString Temporary=UserPlacedLayoutPath+TEXT(".tmp");
    if(!FFileHelper::SaveStringToFile(Text,*Temporary,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM) ||
        !IFileManager::Get().Move(*UserPlacedLayoutPath,*Temporary,true,true))
        UE_LOG(LogTemp,Warning,TEXT("Could not save user-placed frames to %s"),*UserPlacedLayoutPath);
}
