#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "GameFramework/Actor.h"
#include "Components/SceneComponent.h"
#include "WowWorldCollision.h"
#include "WowWmoArea.h"
#include "Coord/WowCoordinate.h"
#include "Formats/Dbc/DbcStore.h"
#include "Formats/BlpParser.h"
#include "ProceduralMeshComponent.h"
#include "Formats/WmoTypes.h"
#include "Formats/WmoParser.h"
#include "Formats/M2Types.h"
#include "Formats/M2Parser.h"
#include "Mpq/MpqManager.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
UWorld* CollisionWorld()
{
    auto* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    return World;
}
USceneComponent* CollisionRoot(UWorld* World)
{
    auto* Actor = World->SpawnActor<AActor>();
    auto* Root = NewObject<USceneComponent>(Actor);
    Actor->SetRootComponent(Root); Root->SetMobility(EComponentMobility::Static); Root->RegisterComponent();
    return Root;
}
void Floor(USceneComponent* Root, float Z, FName Name)
{
    FWowWorldCollision::Create(Root, Name, {FVector(-500,-500,Z), FVector(500,-500,Z), FVector(500,500,Z), FVector(-500,500,Z)}, {0,1,2,0,2,3});
}
void DestroyCollisionWorld(UWorld* World) { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIdleWowCollisionSurfaces, "WowUnreal.Collision.RuntimeFloorAndWalls",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FIdleWowCollisionSurfaces::RunTest(const FString& Parameters)
{
    UWorld* World = CollisionWorld();
    auto* Root = CollisionRoot(World);
    Floor(Root, 0, TEXT("Terrain")); Floor(Root, 130, TEXT("IndoorFloor"));
    FWowWorldCollision::Create(Root, TEXT("Wall"), {FVector(200,800,0), FVector(200,1200,0), FVector(200,1200,300), FVector(200,800,300)}, {0,1,2,0,2,3});
    const double Started = FPlatformTime::Seconds();
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, World, Root, Started]()
    {
        FHitResult Hit;
        const bool Ready = FWowWorldCollision::FindFloor(World, FVector(0,0,180), nullptr, Hit);
        if (!Ready && FPlatformTime::Seconds() - Started < 5) return false;
        TestTrue(TEXT("Asynchronously cooked world floor exists"), Ready);
        if (Ready) TestTrue(TEXT("NPC resolves nearby floor below its server height"), FMath::IsNearlyEqual(Hit.ImpactPoint.Z,130.0,0.1));
        TestTrue(TEXT("Capsule placement finds room floor instead of terrain underneath it"),
            FWowWorldCollision::FindFloor(World, FVector::ZeroVector, nullptr, Hit, 200, 1000, 35, 88) && FMath::IsNearlyEqual(Hit.ImpactPoint.Z,130.0,0.1));
        const FCollisionObjectQueryParams Objects(ECC_WorldStatic);
        TestTrue(TEXT("Wall blocks a player-sized capsule sweep"), World->SweepSingleByObjectType(Hit,
            FVector(0,1000,100), FVector(400,1000,100), FQuat::Identity, Objects, FCollisionShape::MakeCapsule(35,88)));
        TestTrue(TEXT("Wall contact is before passing through it"), Hit.ImpactPoint.X < 205);
        FWowWorldCollision::DestroyChildren(Root);
        TestEqual(TEXT("Unloading render parent also removes hidden collision children"), Root->GetAttachChildren().Num(), 0);
        DestroyCollisionWorld(World);
        return true;
    }));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIdleWowWmoCollisionFlags, "WowUnreal.Collision.WmoMaterialFlags",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FIdleWowWmoCollisionFlags::RunTest(const FString& Parameters)
{
    UWorld* World = CollisionWorld();
    auto* Root = CollisionRoot(World);
    FWmoGroupData Group;
    for (int32 Face = 0; Face < 4; ++Face)
    {
        const int32 Base = Group.Vertices.Num();
        Group.Vertices.Append({FVector(Face*3,0,1), FVector(Face*3+1,0,1), FVector(Face*3,1,1)});
        Group.Indices.Append({uint16(Base),uint16(Base+1),uint16(Base+2)});
    }
    Group.TriangleMaterials = {{0x20,0}, {0x24,0}, {0,0xFF}, {0x08,0}};
    FWowWorldCollision::AddWmo(Root, TEXT("FilteredWmo"), Group);
    const double Started = FPlatformTime::Seconds();
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, World, Started]()
    {
        FHitResult Hit;
        const bool Ready = FWowWorldCollision::FindFloor(World, FVector(20,20,150), nullptr, Hit);
        if (!Ready && FPlatformTime::Seconds() - Started < 5) return false;
        TestTrue(TEXT("Ordinary render face supplies collision"), Ready);
        TestFalse(TEXT("Detail-only triangles do not create invisible blockers"), FWowWorldCollision::FindFloor(World, FVector(20,320,150), nullptr, Hit));
        TestTrue(TEXT("Material 255 collision-only face is retained"), FWowWorldCollision::FindFloor(World, FVector(20,620,150), nullptr, Hit));
        TestTrue(TEXT("Explicit collision flag is retained"), FWowWorldCollision::FindFloor(World, FVector(20,920,150), nullptr, Hit));
        DestroyCollisionWorld(World);
        return true;
    }));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIdleWowArchitectureCollision, "WowUnreal.Collision.MpqArchitecture",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FIdleWowArchitectureCollision::RunTest(const FString& Parameters)
{
    FString DataPath;
    if (!FParse::Value(FCommandLine::Get(), TEXT("wowdata="), DataPath)) { AddError(TEXT("Supply -wowdata=<existing Data>")); return false; }
    FMpqManager Mpq;
    if (!TestTrue(TEXT("Local MPQ opens"), Mpq.Initialize(DataPath))) return false;
    const FString Abbey = TEXT("WORLD\\WMO\\AZEROTH\\BUILDINGS\\NSABBEY\\NSABBEY");
    TArray<uint8> Raw;
    Mpq.ReadFile(Abbey + TEXT(".WMO"), Raw);
    const auto RootData = FWmoParser::ParseRoot(Raw);
    if (!TestTrue(TEXT("Actual abbey root is valid"), RootData.bIsValid)) return false;
    UWorld* World = CollisionWorld();
    auto* Root = CollisionRoot(World);
    TOptional<FVector> FloorPoint, WallPoint, WallNormal, RampPoint;
    int32 CollisionFaces = 0;
    double FloorArea=0;
    double LowestRampZ=DBL_MAX;
    for (uint32 GroupIndex = 0; GroupIndex < RootData.NumGroups; ++GroupIndex)
    {
        Raw.Reset();
        Mpq.ReadFile(FString::Printf(TEXT("%s_%03u.WMO"), *Abbey, GroupIndex), Raw);
        const auto Group = FWmoParser::ParseGroup(Raw);
        TestEqual(TEXT("MOPY covers all MOVI triangles"), Group.TriangleMaterials.Num(), Group.Indices.Num()/3);
        FWowWorldCollision::AddWmo(Root, *FString::Printf(TEXT("Abbey_%u"), GroupIndex), Group);
        for (int32 I = 0; I+2 < Group.Indices.Num(); I+=3)
        {
            if (!Group.HasCollision(I/3)) continue;
            ++CollisionFaces;
            FVector Points[3];
            for (int32 Corner=0;Corner<3;++Corner) {const auto& V=Group.Vertices[Group.Indices[I+Corner]];Points[Corner]=FVector(V.Y,V.X,V.Z)*100;}
            const FVector Cross=FVector::CrossProduct(Points[1]-Points[0],Points[2]-Points[0]);
            if (Cross.Size()<10000) continue;
            const FVector Normal=Cross.GetSafeNormal(), Center=(Points[0]+Points[1]+Points[2])/3;
            const double Perimeter=(Points[1]-Points[0]).Size()+(Points[2]-Points[1]).Size()+(Points[0]-Points[2]).Size();
            // A capsule needs a broad horizontal face, not an arbitrary narrow
            // decoration triangle whose centroid can be inside adjacent geometry.
            if (Normal.Z<-.999 && Cross.Size()>FloorArea && Cross.Size()/Perimeter>100)
            { FloorPoint=Center; FloorArea=Cross.Size(); }
            if (Normal.Z<-.85 && Normal.Z>-.94 && Cross.Size()/Perimeter>75 && Center.Z<LowestRampZ)
            { RampPoint=Center; LowestRampZ=Center.Z; }
            if (!WallPoint.IsSet() && FMath::Abs(Normal.Z)<0.05) {WallPoint=Center;WallNormal=Normal;}
        }
    }
    TestTrue(TEXT("Real abbey collision faces found"), CollisionFaces>100);
    TestTrue(TEXT("Floor, stair ramp and wall test points found"), FloorPoint.IsSet() && WallPoint.IsSet() && RampPoint.IsSet());
    if (!FloorPoint.IsSet() || !WallPoint.IsSet() || !RampPoint.IsSet()) {DestroyCollisionWorld(World);return false;}
    TArray<uint8> M2, Skin;
    Mpq.ReadFile(TEXT("WORLD\\GENERIC\\PASSIVEDOODADS\\FURNITURE\\CONTAINERS\\CRATE03.M2"), M2);
    Mpq.ReadFile(TEXT("WORLD\\GENERIC\\PASSIVEDOODADS\\FURNITURE\\CONTAINERS\\CRATE0300.skin"), Skin);
    const auto Crate=FM2Parser::Parse(M2,Skin);
    TestTrue(TEXT("M2 bounding collision is parsed independently of rendering"), !Crate.CollisionVertices.IsEmpty() && !Crate.CollisionIndices.IsEmpty());
    TArray<FVector> CrateVertices; TArray<int32> CrateIndices;
    FWowWorldCollision::AppendM2(Crate,FTransform(FVector(20000,0,0)),CrateVertices,CrateIndices);
    FWowWorldCollision::Create(Root,TEXT("CrateBounds"),CrateVertices,CrateIndices);
    FBox CrateBox(ForceInit); for (const auto& Vertex:CrateVertices) CrateBox+=Vertex;
    const FVector CrateTop(CrateBox.GetCenter().X,CrateBox.GetCenter().Y,CrateBox.Max.Z);
    const double Started=FPlatformTime::Seconds();
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this,World,FloorPoint,WallPoint,WallNormal,RampPoint,CrateTop,Started]()
    {
        FHitResult Hit;
        const FCollisionObjectQueryParams Objects(ECC_WorldStatic);
        FCollisionQueryParams Query;Query.bTraceComplex=true;
        const bool FloorReady=World->LineTraceSingleByObjectType(Hit,FloorPoint.GetValue()+FVector(0,0,5),FloorPoint.GetValue()-FVector(0,0,5),Objects,Query);
        if (!FloorReady && FPlatformTime::Seconds()-Started<10) return false;
        TestTrue(TEXT("Actual abbey floor blocks a downward trace"),FloorReady);
        const bool CapsuleFloor=World->SweepSingleByObjectType(Hit,RampPoint.GetValue()+FVector(0,0,150),RampPoint.GetValue()+FVector(0,0,30),
            FQuat::Identity,Objects,FCollisionShape::MakeCapsule(35,88),Query);
        AddInfo(FString::Printf(TEXT("Abbey stair capsule: sample=%s hit=%d normalZ=%.4f penetrating=%d"),*RampPoint.GetValue().ToString(),CapsuleFloor,Hit.ImpactNormal.Z,Hit.bStartPenetrating));
        TestTrue(TEXT("Abbey stair ramp has a walkable nonpenetrating capsule impact"),CapsuleFloor && !Hit.bStartPenetrating && Hit.ImpactNormal.Z>.8);
        const bool CrateFloor=World->SweepSingleByObjectType(Hit,CrateTop+FVector(0,0,100),CrateTop-FVector(0,0,10),
            FQuat::Identity,Objects,FCollisionShape::MakeCapsule(10,20),Query);
        TestTrue(TEXT("M2 bounding top is walkable from above"),CrateFloor && !Hit.bStartPenetrating && Hit.ImpactNormal.Z>.8);
        TestTrue(TEXT("Actual abbey wall blocks a capsule"),World->SweepSingleByObjectType(Hit,
            WallPoint.GetValue()+WallNormal.GetValue()*100,WallPoint.GetValue()-WallNormal.GetValue()*100,FQuat::Identity,Objects,FCollisionShape::MakeCapsule(10,20),Query));
        DestroyCollisionWorld(World);return true;
    }));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIdleWowWmoAreas,"WowUnreal.Collision.WmoRoomsAndMinimap",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FIdleWowWmoAreas::RunTest(const FString&)
{
    FString Path; if(!FParse::Value(FCommandLine::Get(),TEXT("wowdata="),Path)) return false; FMpqManager Mpq; if(!Mpq.Initialize(Path)) return false;
    auto& Dbc=FDbcStore::Get(); if(!Dbc.IsLoaded()) Dbc.LoadAll(Mpq); if(!Dbc.IsDeferredLoaded()) Dbc.LoadDeferred(Mpq);
    TArray<uint8> Raw; const FString Base=TEXT("World\\Wmo\\Azeroth\\Buildings\\NSabbey\\NSabbey"); if(!Mpq.ReadFile(Base+TEXT(".wmo"),Raw)) return false;
    const auto RootData=FWmoParser::ParseRoot(Raw); TestTrue(TEXT("Actual root identity and portal graph are retained"),RootData.RootId==59 && RootData.NumGroups==14 && RootData.Portals.Num()>0 && RootData.PortalVertices.Num()>0 && RootData.PortalRefs.Num()>0);
    auto* World=CollisionWorld(); auto* Root=CollisionRoot(World); auto* Actor=Root->GetOwner();
    Actor->SetActorLocation(FWowCoordinate::WowToUE(-8897,-178.296875,80.0673218)); Actor->SetActorRotation(FWowCoordinate::WowRotationToUE(0,-201.5,0));
    auto* Meta=NewObject<UWowWmoAreaComponent>(Actor); Actor->AddInstanceComponent(Meta); Meta->RegisterComponent();
    Meta->RootId=RootData.RootId; Meta->NameSet=1; Meta->Bounds=RootData.BoundingBox; Meta->PortalVertices=RootData.PortalVertices; Meta->Portals=RootData.Portals; Meta->PortalRefs=RootData.PortalRefs; Meta->Groups.SetNum(RootData.NumGroups);
    FVector Point=FVector::ZeroVector; double FloorZ=MAX_dbl;
    for(uint32 I=0;I<RootData.NumGroups;++I)
    {
        if(!Mpq.ReadFile(Base+FString::Printf(TEXT("_%03u.wmo"),I),Raw)) continue; const auto G=FWmoParser::ParseGroup(Raw);
        auto& R=Meta->Groups[I]; R.Id=G.GroupId; R.Flags=G.Flags; R.Bounds=G.BoundingBox; R.PortalStart=G.PortalStart; R.PortalCount=G.PortalCount;
        if(I) continue;
        TestTrue(TEXT("Main hall identity and portal range are actual MOGP fields"),G.GroupId==1934 && G.PortalCount>0 && int32(G.PortalStart)+G.PortalCount<=RootData.PortalRefs.Num());
        R.Collision=FWowWorldCollision::AddWmo(Root,TEXT("WmoCollision_0"),G);
        for(int32 T=0;T+2<G.Indices.Num();T+=3)
        {
            if(!G.HasCollision(T/3)) continue; const auto A=G.Vertices[G.Indices[T]],B=G.Vertices[G.Indices[T+1]],C=G.Vertices[G.Indices[T+2]],Cross=FVector::CrossProduct(B-A,C-A),Mid=(A+B+C)/3;
            const double Perimeter=(B-A).Size()+(C-A).Size()+(C-B).Size();
            if(Cross.GetSafeNormal().Z>.99 && Cross.Size()/FMath::Max(.001,Perimeter)>.5 && Mid.Z<FloorZ) {Point=Mid; FloorZ=Mid.Z;}
        }
    }
    if(!TestTrue(TEXT("A broad real main-hall floor was found"),FloorZ<MAX_dbl)) {DestroyCollisionWorld(World); return false;}
    const FVector Position=Meta->ToWorld(Point)+FVector(0,0,88); const auto Room=Meta->Location(0,Position,0);
    TestTrue(TEXT("Raw model transforms round-trip through placed UE coordinates"),Meta->ToLocal(Meta->ToWorld(Point)).Equals(Point,.001));
    TestTrue(TEXT("Placement name set resolves the actual Northshire room and area"),Room.AreaId==24 && Room.Indoors && Room.Minimap && Room.ZoneName==TEXT("Northshire Abbey") && Room.SubzoneName==TEXT("Main Hall"));
    TestTrue(TEXT("Exterior groups do not activate an indoor minimap"),!Meta->Location(4,Position,0).Minimap);
    const auto CurrentOnly=Meta->MapQuads(Room,150,0),Connected=Meta->MapQuads(Room,150,99); TSet<int32> Rooms; for(const auto& Q:Connected) {Rooms.Add(Q.Group); TestFalse(TEXT("Portal traversal cannot draw exterior groups through an interior door"),Meta->Groups[Q.Group].Flags&8);}
    TestTrue(TEXT("Portal limit and graph traversal preserve current-room priority"),CurrentOnly.Num()==1 && CurrentOnly[0].Group==0 && Rooms.Num()>1 && Connected.Last().Group==0);
    TArray<uint8> ArtBytes; Mpq.ReadFile(TEXT("Textures\\Minimap\\8d5e281bd5678bdfb6e6e18717d4b3ab.blp"),ArtBytes); const auto Art=FBlpParser::Parse(ArtBytes);
    TestTrue(TEXT("Generated half-yard quads match the real 128px room tile and two-texel border"),Art.bIsValid && Art.Width==128 && CurrentOnly.Num()==1 && FMath::IsNearlyEqual(CurrentOnly[0].Bounds.GetSize().X,Art.Width*.5+2));
    const double Started=FPlatformTime::Seconds();
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this,World,Meta,Position,Started]()
    {
        const auto Located=UWowWmoAreaComponent::Locate(World,Position,0);
        if(!Located.Component.IsValid() && FPlatformTime::Seconds()-Started<8) return false;
        TestTrue(TEXT("Asynchronously cooked collision resolves the owning room and area"),Located.Component.Get()==Meta && Located.Group==0 && Located.AreaId==24 && Located.Indoors);
        TestFalse(TEXT("Nearby outdoor terrain cannot be classified by a building's broad box"),UWowWmoAreaComponent::Locate(World,Position+FVector(30000,0,0),0).Component.IsValid());
        DestroyCollisionWorld(World); return true;
    }));
    return true;
}
#endif
