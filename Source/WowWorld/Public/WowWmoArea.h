#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Formats/WmoTypes.h"
#include "WowWmoArea.generated.h"

class UPrimitiveComponent;
class UWowWmoAreaComponent;
struct WOWWORLD_API FWowWmoLocation
{
    TWeakObjectPtr<UWowWmoAreaComponent> Component;
    int32 Group=INDEX_NONE;
    uint32 AreaId=0,FloorId=0;
    bool Indoors=false,Minimap=false,WholeMap=false;
    FVector LocalPosition=FVector::ZeroVector;
    FString ZoneName,SubzoneName;
};
struct WOWWORLD_API FWowWmoMapQuad
{
    int32 Group=0,X=0,Y=0;
    FBox Bounds=FBox(ForceInit);
};

/** Runtime WMO identity and room graph, owned by the same actor as its collision.
 * Geometry stays in its existing components; no duplicated meshes or textures. */
UCLASS()
class WOWWORLD_API UWowWmoAreaComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    struct FGroup
    {
        uint32 Id=0,Flags=0;
        uint16 PortalStart=0,PortalCount=0;
        FBox Bounds=FBox(ForceInit);
        TWeakObjectPtr<UPrimitiveComponent> Collision;
    };
    FString Path;
    uint32 RootId=0,NameSet=0;
    FBox Bounds=FBox(ForceInit);
    TArray<FGroup> Groups;
    TArray<FVector> PortalVertices;
    TArray<FWmoRootData::FPortal> Portals;
    TArray<FWmoRootData::FPortalRef> PortalRefs;
    static FWowWmoLocation Locate(UWorld* World,const FVector& Position,uint32 MapId);
    FWowWmoLocation Location(int32 Group,const FVector& Position,uint32 MapId) const;
    FVector ToLocal(const FVector& Position) const;
    FVector ToWorld(const FVector& Position) const;
    TArray<FWowWmoMapQuad> MapQuads(const FWowWmoLocation& Location,float Radius,int32 PortalDepth=99) const;
    static TArray<FWowWmoMapQuad> GroupQuads(const FBox& Bounds,int32 Group);
};
