#pragma once
#include "CoreMinimal.h"

/** WoW editor APIs index UTF-8 bytes; Slate indexes UTF-16 code units. */
namespace WowEditOffsets
{
inline int32 CharacterIndex(const FString& Text,int32 Bytes)
{
    const FTCHARToUTF8 Utf8(*Text); int32 At=FMath::Clamp(Bytes,0,Utf8.Length());
    while(At>0 && At<Utf8.Length() && (uint8(Utf8.Get()[At])&0xc0)==0x80) --At;
    return FUTF8ToTCHAR(Utf8.Get(),At).Length();
}
inline int32 ByteIndex(const FString& Text,int32 Characters)
{
    int32 At=FMath::Clamp(Characters,0,Text.Len());
    if(At>0 && At<Text.Len() && uint32(Text[At-1])>=0xd800 && uint32(Text[At-1])<=0xdbff && uint32(Text[At])>=0xdc00 && uint32(Text[At])<=0xdfff) --At;
    return FTCHARToUTF8(*Text.Left(At)).Length();
}
inline int32 CodePoints(const FString& Text,int32 Bytes=-1)
{
    const FTCHARToUTF8 Utf8(*Text); int32 End=Bytes<0?Utf8.Length():FMath::Clamp(Bytes,0,Utf8.Length());
    while(End>0 && End<Utf8.Length() && (uint8(Utf8.Get()[End])&0xc0)==0x80) --End;
    int32 Count=0; for(int32 I=0;I<End;++I) if((uint8(Utf8.Get()[I])&0xc0)!=0x80) ++Count;
    return Count;
}
}
