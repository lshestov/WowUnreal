#pragma once
#include "CoreMinimal.h"
#include "Formats/M2Types.h"

struct WOWWORLD_API FWowM2LightSample
{
    FVector Position=FVector::ZeroVector,Direction=FVector::DownVector;
    FLinearColor Diffuse=FLinearColor::Black,Ambient=FLinearColor::Black;
    float Start=0,End=0,Radius=0;
    bool Visible=false;
};
struct WOWWORLD_API FWowM2Lighting
{
    static FWowM2LightSample Sample(const FM2Data& Model,int32 Light,float AgeMs,float GlobalMs);
    static float Attenuation(float Distance,float Start,float End);
};
