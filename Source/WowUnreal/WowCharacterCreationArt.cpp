#include "WowCharacterCreationArt.h"
#include "WowLuaVM.h"
#include "Mpq/MpqManager.h"
#include "Formats/BlpParser.h"
#include "WowTextureFactory.h"
#include "Engine/Texture2D.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Styling/CoreStyle.h"
#include "Fonts/CompositeFont.h"
#include "Materials/MaterialInstanceDynamic.h"

FWowCharacterCreationArt::FWowCharacterCreationArt(FMpqManager* InMpq):Mpq(InMpq)
{
    Strings=MakeUnique<FWowLuaVM>(); Strings->Initialize(false);
    TArray<uint8> Data;
    if(Mpq && Mpq->ReadFile(TEXT("Interface\\GlueXML\\GlueStrings.lua"),Data)) Strings->ExecuteBuffer(Data,TEXT("@GlueStrings.lua"));
    if(Mpq && Mpq->ReadFile(TEXT("Interface\\GlueXML\\CharacterCreate.lua"),Data)) Strings->ExecuteBuffer(Data,TEXT("@CharacterCreate.lua"));
    if(Mpq && Mpq->ReadFile(TEXT("Interface\\GlueXML\\GlueParent.lua"),Data)) Strings->ExecuteBuffer(Data,TEXT("@GlueParent.lua"));
    auto Extract=[&](const TCHAR* Name)
    {
        const FString Path=FPaths::ProjectSavedDir()/TEXT("Fonts")/Name;
        if(Mpq && Mpq->ReadFile(FString(TEXT("Fonts\\"))+Name,Data))
        {IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path),true);FFileHelper::SaveArrayToFile(Data,*Path);return Path;}
        return FString();
    };
    FontPath=Extract(TEXT("FRIZQT__.TTF")); TitleFontPath=Extract(TEXT("MORPHEUS.TTF"));
}
FWowCharacterCreationArt::~FWowCharacterCreationArt()=default;
const FSlateBrush* FWowCharacterCreationArt::Brush(const FString& Path,FVector2f Min,FVector2f Max,ESlateBrushMirrorType::Type Mirror,bool Additive)
{
    const FString Key=FString::Printf(TEXT("%s:%g,%g,%g,%g:%d:%d"),*Path,Min.X,Min.Y,Max.X,Max.Y,int32(Mirror),int32(Additive));
    if(const auto* Found=Brushes.Find(Key)) return Found->Get();
    TSharedPtr<FSlateBrush> B=MakeShared<FSlateBrush>(); B->DrawAs=ESlateBrushDrawType::Image;B->Mirroring=Mirror;
    auto* Texture=Textures.Find(Path);
    if(!Texture)
    {
        TArray<uint8> Bytes;
        if(Mpq && Mpq->ReadFile(Path+TEXT(".blp"),Bytes))
        {
            const auto Blp=FBlpParser::Parse(Bytes);
            if(Blp.bIsValid) Texture=&Textures.Add(Path,TStrongObjectPtr<UTexture2D>(FWowTextureFactory::CreateTexture(Blp,MakeUniqueObjectName(GetTransientPackage(),UTexture2D::StaticClass(),TEXT("GlueTexture")).ToString())));
        }
    }
    if(Texture && Texture->IsValid())
    {
        B->SetResourceObject(Texture->Get());B->ImageSize=FVector2D(Texture->Get()->GetSizeX(),Texture->Get()->GetSizeY());
        if(Additive)
        {
            auto* Material=Materials.Find(Key);
            if(!Material)
                if(auto* Parent=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Materials/M_WowUiRegionAdd.M_WowUiRegionAdd")))
                {
                    auto* MID=UMaterialInstanceDynamic::Create(Parent,GetTransientPackage());
                    MID->SetTextureParameterValue(TEXT("Image"),Texture->Get());
                    MID->SetVectorParameterValue(TEXT("UVTL"),FLinearColor(Min.X,Min.Y,0,0));MID->SetVectorParameterValue(TEXT("UVBL"),FLinearColor(Min.X,Max.Y,0,0));
                    MID->SetVectorParameterValue(TEXT("UVTR"),FLinearColor(Max.X,Min.Y,0,0));MID->SetVectorParameterValue(TEXT("UVBR"),FLinearColor(Max.X,Max.Y,0,0));
                    Material=&Materials.Add(Key,TStrongObjectPtr<UMaterialInstanceDynamic>(MID));
                }
            if(Material) B->SetResourceObject(Material->Get());
        }
    }
    if(!Additive) B->SetUVRegion(FBox2f(Min,Max)); Brushes.Add(Key,B); return B.Get();
}
const FButtonStyle* FWowCharacterCreationArt::Button(const FString& Base,bool Panel)
{
    if(const auto* Found=Buttons.Find(Base)) return Found->Get();
    TSharedPtr<FButtonStyle> Style=MakeShared<FButtonStyle>();
    const FVector2f Max=Panel?FVector2f(.578125f,.75f):FVector2f(1,1);
    Style->SetNormal(*Brush(Base+TEXT("-Up"),FVector2f(0,0),Max));
    Style->SetPressed(*Brush(Base+TEXT("-Down"),FVector2f(0,0),Max));
    Style->SetHovered(*Brush(Base+TEXT("-Up"),FVector2f(0,0),Max));
    Style->SetDisabled(*Brush(Base+TEXT("-Up"),FVector2f(0,0),Max));
    Style->SetNormalPadding(FMargin(0));Style->SetPressedPadding(FMargin(1,1,0,0));
    Buttons.Add(Base,Style);return Style.Get();
}
FString FWowCharacterCreationArt::String(const FString& Key) const
{return Strings?Strings->GetGlobalString(Key):FString();}
const FButtonStyle* FWowCharacterCreationArt::ScrollArrow(bool Up)
{
    const FString Base=FString(TEXT("Interface\\Buttons\\UI-ScrollBar-Scroll"))+(Up?TEXT("UpButton"):TEXT("DownButton"));
    if(const auto* Found=Buttons.Find(Base)) return Found->Get();
    TSharedPtr<FButtonStyle> Style=MakeShared<FButtonStyle>();const FVector2f Min(.23f,.23f),Max(.77f,.77f);
    Style->SetNormal(*Brush(Base+TEXT("-Up"),Min,Max));Style->SetHovered(*Brush(Base+TEXT("-Up"),Min,Max));
    Style->SetPressed(*Brush(Base+TEXT("-Down"),Min,Max));Style->SetDisabled(*Brush(Base+TEXT("-Disabled"),Min,Max));
    Style->SetNormalPadding(FMargin(0));Style->SetPressedPadding(FMargin(0));Buttons.Add(Base,Style);return Style.Get();
}
const FScrollBarStyle* FWowCharacterCreationArt::ScrollBar()
{
    if(!ScrollStyle)
    {
        ScrollStyle=MakeShared<FScrollBarStyle>();FSlateBrush Empty;Empty.DrawAs=ESlateBrushDrawType::NoDrawType;
        ScrollStyle->SetVerticalBackgroundImage(Empty).SetHorizontalBackgroundImage(Empty)
            .SetVerticalTopSlotImage(Empty).SetVerticalBottomSlotImage(Empty).SetHorizontalTopSlotImage(Empty).SetHorizontalBottomSlotImage(Empty);
        const auto* Knob=Brush(TEXT("Interface\\Buttons\\UI-ScrollBar-Knob"),FVector2f(.2f,.125f),FVector2f(.8f,.875f));
        ScrollStyle->SetNormalThumbImage(*Knob).SetHoveredThumbImage(*Knob).SetDraggedThumbImage(*Knob);
    }
    return ScrollStyle.Get();
}
const FSlateBrush* FWowCharacterCreationArt::Icon(const FString& Path,const FString& Table,const FString& Token)
{
    const FString Key=Table+TEXT(":")+Token+TEXT(":")+Path;
    if(const auto* Found=Brushes.Find(Key)) return Found->Get();
    if(Strings) Strings->ExecuteString(FString::Printf(TEXT("__glueCoords = table.concat(%s['%s'] or {0,1,0,1}, ',')"),*Table,*Token),TEXT("=GlueIcon"));
    TArray<FString> Values;String(TEXT("__glueCoords")).ParseIntoArray(Values,TEXT(","));
    const auto* B=Values.Num()==4?Brush(Path,FVector2f(FCString::Atof(*Values[0]),FCString::Atof(*Values[2])),FVector2f(FCString::Atof(*Values[1]),FCString::Atof(*Values[3]))):Brush(Path);
    Brushes.Add(Key,MakeShared<FSlateBrush>(*B));return Brushes[Key].Get();
}
FSlateFontInfo FWowCharacterCreationArt::Font(int32 Size,bool Title) const
{
    const FString& Path=Title?TitleFontPath:FontPath;
    if(Path.IsEmpty()) return FCoreStyle::GetDefaultFontStyle("Regular",Size);
    FSlateFontInfo Info; Info.Size=FMath::Max(1,FMath::RoundToInt(Size*.75f));
    auto Composite=MakeShared<FCompositeFont>();FTypefaceEntry Face;
    Face.Name=TEXT("Glue");Face.Font=FFontData(Path,EFontHinting::Default,EFontLoadingPolicy::LazyLoad);
    Composite->DefaultTypeface.Fonts.Add(Face);Composite->FallbackTypeface.Typeface=FCoreStyle::GetDefaultFont()->DefaultTypeface;
    Info.CompositeFont=Composite;Info.TypefaceFontName=Face.Name;return Info;
}
TArray<FWowCreationLight> FWowCharacterCreationArt::Lights(const FString& Token)
{
    TArray<FWowCreationLight> Result;
    if(!Strings) return Result;
    for(int32 I=1;I<=4;++I)
    {
        Strings->ExecuteString(FString::Printf(TEXT("__glueLight = RaceLights['%s'] and RaceLights['%s'][%d]; __glueCoords = __glueLight and table.concat(__glueLight, ',') or ''"),*Token,*Token,I),TEXT("=GlueLight"));
        TArray<FString> V;String(TEXT("__glueCoords")).ParseIntoArray(V,TEXT(","));
        if(V.Num()!=13 || FCString::Atof(*V[0])!=1) continue;
        FWowCreationLight L;L.Direction=FVector(FCString::Atof(*V[2]),FCString::Atof(*V[3]),FCString::Atof(*V[4]));
        L.Ambient=FLinearColor(FCString::Atof(*V[6]),FCString::Atof(*V[7]),FCString::Atof(*V[8]))*FCString::Atof(*V[5]);
        L.Diffuse=FLinearColor(FCString::Atof(*V[10]),FCString::Atof(*V[11]),FCString::Atof(*V[12]));L.DiffuseIntensity=FCString::Atof(*V[9]);
        Result.Add(L);
    }
    return Result;
}
