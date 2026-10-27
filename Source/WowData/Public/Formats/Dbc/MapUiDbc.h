#pragma once
#include "CoreMinimal.h"
class FMpqManager;

/** Client map definitions. Physical maps, display continents and dungeon floors
 * have separate identities in the 12340 DBCs. */
class WOWDATA_API FMapUiDbc
{
public:
    struct FArea
    {
        uint32 Id=0,MapId=0,AreaId=0; FString Name;
        float Y1=0,Y2=0,X1=0,X2=0;
        int32 DisplayMapId=-1,DefaultFloor=0,Parent=0;
        uint32 DisplayMap() const {return DisplayMapId<0?MapId:uint32(DisplayMapId);}
        bool HasBounds() const {return X1!=X2 && Y1!=Y2;}
        FVector2D Project(const FVector& P) const {return HasBounds()?FVector2D((Y1-P.Y)/(Y1-Y2),(X1-P.X)/(X1-X2)):FVector2D(-1,-1);}
    };
    struct FOverlay
    {
        uint32 Id=0,MapAreaId=0,Areas[4]={}; FString Name;
        int32 Width=0,Height=0,X=0,Y=0,MapPointX=0,MapPointY=0,Top=0,Left=0,Bottom=0,Right=0;
    };
    struct FContinent {uint32 Id=0,MapId=0,WorldMap=0; int32 Left=0,Right=0,Top=0,Bottom=0; FVector2D Offset=FVector2D::ZeroVector; float Scale=1;};
    struct FFloor
    {
        uint32 Id=0,MapId=0,Index=0,Parent=0; float MinX=0,MaxX=0,MinY=0,MaxY=0;
        FVector2D Project(const FVector& P) const {return MinX!=MaxX && MinY!=MaxY?FVector2D((MaxX-P.Y)/(MaxX-MinX),(MaxY-P.X)/(MaxY-MinY)):FVector2D(-1,-1);}
    };
    struct FTransform {uint32 MapId=0,NewMap=0,Floor=0; FVector2D Min=FVector2D::ZeroVector,Max=FVector2D::ZeroVector,Offset=FVector2D::ZeroVector;};
    struct FLandmark {uint32 Id=0,Importance=0,Icons[9]={},Faction=0,MapId=0,Flags=0,AreaId=0,WorldState=0,Link=0; FVector Position=FVector::ZeroVector; FString Name,Description;};
    struct FFloorChunk {uint32 MapId=0,GroupId=0,Floor=0; float MinZ=0;};
    struct FWmoArea {uint32 Root=0,NameSet=0,Group=0,Flags=0,AreaId=0; FString Name;};
    struct FVehicle {uint32 Flags=0,Locomotion=0;};

    bool Load(FMpqManager& Mpq);
    const TArray<FArea>& Areas() const {return AreaRows;}
    const TArray<FOverlay>& Overlays() const {return OverlayRows;}
    const TArray<FContinent>& Continents() const {return ContinentRows;}
    const TArray<FFloor>& Floors() const {return FloorRows;}
    const TArray<FLandmark>& Landmarks() const {return LandmarkRows;}
    const TArray<FWmoArea>& WmoAreas() const {return WmoAreaRows;}
    const FVehicle* Vehicle(uint32 Id) const {return Vehicles.Find(Id);}
    uint32 ResourceLockMask(uint32 Id) const {return ResourceLocks.FindRef(Id);}
    const FArea* Area(uint32 Id) const;
    const FFloor* Floor(uint32 Id) const;
    const FContinent* Continent(uint32 MapId) const;
    uint32 Transform(uint32 MapId,FVector& Position,uint32& FloorId) const;
    uint32 FloorAt(uint32 MapId,uint32 GroupId,float Z) const;
    uint32 HitArea(uint32 MapId,const FVector2D& Point) const;
private:
    TArray<FArea> AreaRows;
    TArray<FOverlay> OverlayRows;
    TArray<FContinent> ContinentRows;
    TArray<FFloor> FloorRows;
    TArray<FTransform> TransformRows;
    TArray<FLandmark> LandmarkRows;
    TArray<FFloorChunk> FloorChunkRows;
    TArray<FWmoArea> WmoAreaRows;
    TMap<uint32,TArray<uint32>> ZoneMasks;
    TMap<uint32,FVehicle> Vehicles;
    TMap<uint32,uint32> ResourceLocks;
};
