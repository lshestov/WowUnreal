#pragma once
#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"
#include "Styling/SlateTypes.h"
#include "UObject/StrongObjectPtr.h"

class FMpqManager;
class FWowLuaVM;
class UTexture2D;
class UMaterialInstanceDynamic;
struct FWowCreationLight {FVector Direction=FVector::ZeroVector;FLinearColor Ambient=FLinearColor::Black,Diffuse=FLinearColor::Black;float DiffuseIntensity=0;};

/** Original GlueXML artwork and localized strings, loaded from the user's MPQ. */
class FWowCharacterCreationArt
{
public:
    explicit FWowCharacterCreationArt(FMpqManager* Mpq);
    ~FWowCharacterCreationArt();
    const FSlateBrush* Brush(const FString& Path,FVector2f Min=FVector2f(0,0),FVector2f Max=FVector2f(1,1),ESlateBrushMirrorType::Type Mirror=ESlateBrushMirrorType::NoMirror,bool Additive=false);
    const FButtonStyle* Button(const FString& Base,bool Panel=false);
    const FButtonStyle* ScrollArrow(bool Up);
    const FScrollBarStyle* ScrollBar();
    const FSlateBrush* Icon(const FString& Path,const FString& Table,const FString& Token);
    FString String(const FString& Key) const;
    FSlateFontInfo Font(int32 Size,bool Title=false) const;
    FSlateFontInfo EditFont(int32 Size) const;
    TArray<FWowCreationLight> Lights(const FString& Token);
private:
    FMpqManager* Mpq=nullptr;
    TMap<FString,TStrongObjectPtr<UTexture2D>> Textures;
    TMap<FString,TStrongObjectPtr<UMaterialInstanceDynamic>> Materials;
    TMap<FString,TSharedPtr<FSlateBrush>> Brushes;
    TMap<FString,TSharedPtr<FButtonStyle>> Buttons;
    TSharedPtr<FScrollBarStyle> ScrollStyle;
    TUniquePtr<FWowLuaVM> Strings;
    FString FontPath,TitleFontPath,EditFontPath;
};
