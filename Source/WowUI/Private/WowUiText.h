#pragma once
#include "CoreMinimal.h"

inline FString WowQuestText(FString Text,const FString& Name,uint8 ClassId,const FString& Race,bool Female)
{
    static const TCHAR* Classes[]={TEXT(""),TEXT("warrior"),TEXT("paladin"),TEXT("hunter"),TEXT("rogue"),TEXT("priest"),TEXT("death knight"),TEXT("shaman"),TEXT("mage"),TEXT("warlock"),TEXT(""),TEXT("druid")};
    Text.ReplaceInline(TEXT("$B"),TEXT("\n"),ESearchCase::IgnoreCase);
    Text.ReplaceInline(TEXT("$N"),*Name,ESearchCase::IgnoreCase);
    if(ClassId<UE_ARRAY_COUNT(Classes)) Text.ReplaceInline(TEXT("$C"),Classes[ClassId],ESearchCase::IgnoreCase);
    Text.ReplaceInline(TEXT("$R"),*Race.ToLower(),ESearchCase::IgnoreCase);
    for(int32 Start=Text.Find(TEXT("$G"),ESearchCase::IgnoreCase);Start>=0;Start=Text.Find(TEXT("$G"),ESearchCase::IgnoreCase))
    {
        const int32 Colon=Text.Find(TEXT(":"),ESearchCase::CaseSensitive,ESearchDir::FromStart,Start+2),End=Text.Find(TEXT(";"),ESearchCase::CaseSensitive,ESearchDir::FromStart,Start+2);
        if(Colon<0 || End<Colon) break;
        Text=Text.Left(Start)+(Female?Text.Mid(Colon+1,End-Colon-1):Text.Mid(Start+2,Colon-Start-2))+Text.Mid(End+1);
    }
    return Text;
}

/** Strip WoW control codes while retaining hyperlink labels and escaped pipes. */
inline FString WowUiPlainText(const FString& Text)
{
    FString Out;
    for(int32 I=0; I<Text.Len(); ++I)
    {
        if(Text[I]!='|' || I+1>=Text.Len()) { Out.AppendChar(Text[I]); continue; }
        const TCHAR Code=Text[I+1];
        if(Code=='c' && I+9<Text.Len()) { I+=9; continue; }
        if(Code=='r' || Code=='h') { ++I; continue; }
        if(Code=='H' || Code=='T')
        {
            const int32 End=Text.Find(Code=='H'?TEXT("|h"):TEXT("|t"),ESearchCase::CaseSensitive,ESearchDir::FromStart,I+2);
            if(End>=0) { I=End+1; continue; }
        }
        if(Code=='|') { Out.AppendChar('|'); ++I; continue; }
        if(Code=='n') { Out.AppendChar('\n'); ++I; continue; }
        Out.AppendChar(Text[I]);
    }
    return Out;
}
