#pragma once
#include "CoreMinimal.h"

/** Build 12340 guild tabard indices; artwork is read from the user's MPQs. */
struct FWowGuildEmblem
{
    uint32 Style=0,Color=0,BorderStyle=0,BorderColor=0,Background=0;
    bool Valid() const {return Style<170 && Color<17 && BorderStyle<10 && BorderColor<17 && Background<51;}
    bool operator==(const FWowGuildEmblem& B) const {return Style==B.Style && Color==B.Color && BorderStyle==B.BorderStyle && BorderColor==B.BorderColor && Background==B.Background;}
    uint32 Hash() const {uint32 H=Style; for(uint32 V:{Color,BorderStyle,BorderColor,Background}) H=HashCombineFast(H,V); return H;}
    TArray<FString> Textures(bool Upper) const
    {
        const TCHAR* Region=Upper?TEXT("TU"):TEXT("TL");
        return {FString::Printf(TEXT("Textures\\GuildEmblems\\Background_%02u_%s_U.blp"),Background,Region),
            FString::Printf(TEXT("Textures\\GuildEmblems\\Emblem_%02u_%02u_%s_U.blp"),Style,Color,Region),
            FString::Printf(TEXT("Textures\\GuildEmblems\\Border_%02u_%02u_%s_U.blp"),BorderStyle,BorderColor,Region)};
    }
};
