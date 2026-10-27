#pragma once
#include "CoreMinimal.h"
#include "Formats/Dbc/MapUiDbc.h"
struct FWowLuaContext;

struct FWowMapBlob
{
    FString FillTexture,BorderTexture;
    float FillAlpha=128,BorderAlpha=192,BorderScale=1,MergeThreshold=.2f;
    int32 SplinePoints=20;
    bool Smooth=true,Merge=true;
    TSet<uint32> Quests;
    uint32 HoverQuest=0;
    TArray<int32> HoverObjectives;
};
struct FWowMapUiView
{
    uint32 WorldRevision=MAX_uint32,QuestRevision=MAX_uint32,PoiRevision=MAX_uint32,ExplorationHash=0,QuestHash=0;
    TMap<int64,FWowMapBlob> Blobs;
    TArray<uint32> VisibleQuests;
    float IconOverlap=0,IconPush=0;
    TMap<uint32,FVector2D> Icons;
    TMap<uint32,double> ObjectiveNameRequests;
    TArray<int32> PendingArt;
    bool ApplyingArt=false;
};
namespace WowMapUi
{
struct FSelection
{
    const FMapUiDbc::FArea* Area=nullptr;
    const FMapUiDbc::FFloor* Floor=nullptr;
    int32 Continent=0,Zone=0;
    bool Cosmic=false;
};
struct FLandmark {FString Name,Description; uint32 Icon=0,Link=0; bool Graveyard=false; FVector2D Point=FVector2D::ZeroVector;};
FWowMapUiView& View(FWowLuaContext* Context);
TArray<const FMapUiDbc::FArea*> Continents();
TArray<const FMapUiDbc::FArea*> Zones(int32 Continent);
FSelection Selection(FWowLuaContext* Context);
void Select(FWowLuaContext* Context,int32 AreaId,int32 FloorId=-1);
bool SelectZoom(FWowLuaContext* Context,int32 Continent,int32 Zone);
FVector2D Project(FWowLuaContext* Context,uint32 MapId,const FVector& Position,uint32 ActualFloor=0,bool Unclamped=false);
FVector2D PlayerPosition(FWowLuaContext* Context,uint64 Guid);
bool Explored(FWowLuaContext* Context,uint32 AreaId);
TArray<const FMapUiDbc::FOverlay*> Overlays(FWowLuaContext* Context);
TArray<FLandmark> Landmarks(FWowLuaContext* Context);
const FMapUiDbc::FArea* HoverArea(FWowLuaContext* Context,const FVector2D& Point);
FVector4 WorldContinentBounds(const FMapUiDbc::FContinent& Continent);
uint32 CurrentFloor(FWowLuaContext* Context);
struct FQuestShape {uint32 Quest=0; TArray<int32> Objectives; TArray<FVector2D> Points;};
struct FQuestIcon {uint32 Quest=0; int32 LogIndex=0,Objective=-1; bool Complete=false; FVector2D Point=FVector2D(-1,-1);};
TArray<FQuestIcon> QuestIcons(FWowLuaContext* Context);
uint32 QuestDestination(FWowLuaContext* Context,uint32 Quest,uint32& Floor);
TArray<FQuestShape> QuestShapes(FWowLuaContext* Context,const FWowMapBlob& Style);
bool Contains(const TArray<FVector2D>& Points,const FVector2D& Point);
}
