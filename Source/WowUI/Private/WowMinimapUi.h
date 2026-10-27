#pragma once
#include "CoreMinimal.h"
#include "WowWmoArea.h"
struct FWowLuaContext;

struct FWowMinimapUiView
{
    FString BlipTexture=TEXT("Interface\\Minimap\\ObjectIcons"),PlayerTexture=TEXT("Interface\\Minimap\\MinimapArrow"),StaticArrowTexture;
    FVector2D PlayerSize=FVector2D(12.8,12.8),Ping=FVector2D::ZeroVector;
    uint32 TrackingHash=MAX_uint32,TrackingSpell=0,QuestgiverHash=MAX_uint32;
    uint32 PingRevision=MAX_uint32;
    uint32 MarkerLogHash=MAX_uint32;
    uint32 WmoHash=0;
    FWowWmoLocation Location;
    int32 PortalDepth=99;
    double NextQuestgiverQuery=0,PingAt=0;
    bool Rotates=false,OwnsTooltip=false;
    FString CommonKey;
};
namespace WowMinimapUi
{
struct FTracking {uint32 Spell=0,Type=0,Flags=0,ClassMask=0; FString Key,Name,Icon;};
struct FBlip {uint64 Guid=0; uint8 Icon=8; FVector Position=FVector::ZeroVector; FString Name;};
FWowMinimapUiView& View(FWowLuaContext* C);
TArray<FTracking> Tracking(FWowLuaContext* C);
uint32 TrackingSpell(FWowLuaContext* C);
int32 CommonTracking(FWowLuaContext* C);
TArray<FBlip> Blips(FWowLuaContext* C);
float Radius(FWowLuaContext* C);
float Radius(FWowLuaContext* C,bool Indoors);
FWowWmoLocation Location(FWowLuaContext* C);
FVector2D Offset(FWowLuaContext* C,const FVector& Center,const FVector& Position,float Radius,float Facing);
FVector Position(FWowLuaContext* C,uint64 Guid);
}
