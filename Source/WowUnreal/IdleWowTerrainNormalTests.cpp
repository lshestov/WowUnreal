#include "Misc/AutomationTest.h"
#include "Formats/AdtParser.h"
#include "Mpq/MpqManager.h"
#include "WowTerrainMeshBuilder.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIdleWowTerrainWorldNormals, "WowUnreal.Rendering.TerrainWorldNormals",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FIdleWowTerrainWorldNormals::RunTest(const FString&)
{
    FString Path;
    FMpqManager Mpq;
    TArray<uint8> Bytes;
    if (!FParse::Value(FCommandLine::Get(), TEXT("wowdata="), Path) || !Mpq.Initialize(Path) ||
        !Mpq.ReadFile(TEXT("World\\Maps\\Azeroth\\Azeroth_32_48.adt"), Bytes))
    {
        AddError(TEXT("Supply build 12340 data for the Northshire terrain tile"));
        return false;
    }
    const auto Tile = MakeUnique<FAdtData>(FAdtParser::Parse(Bytes));
    if (!TestTrue(TEXT("Northshire ADT loaded"), Tile->bIsValid)) return false;

    // The field under the trees slopes in both world axes. Check its authored
    // normal against the tangent plane from independent MCVT heights.
    const auto& Chunk = Tile->Chunks[10 * 16 + 1];
    const auto Mesh = FTerrainMeshBuilder::BuildChunkMesh(Chunk, 32, 48);
    const auto Lod = FTerrainMeshBuilder::BuildChunkMeshLOD1(Chunk, 32, 48);
    constexpr int32 Vertex = 2 * 17 + 7;
    constexpr int32 LodVertex = 2 * 9 + 7;
    const auto North = Mesh.Vertices[Vertex - 17] - Mesh.Vertices[Vertex + 17];
    const auto East = Mesh.Vertices[Vertex + 1] - Mesh.Vertices[Vertex - 1];
    const auto GeometricNormal = FVector::CrossProduct(North, East).GetSafeNormal();

    TestTrue(TEXT("Authored MCNR bytes match the north/west/up axes"),
        (Chunk.Normals[Vertex] * 127.).Equals(FVector(-7., -28., 123.), .001));
    TestTrue(TEXT("LOD 0 lighting follows the terrain slope"),
        FVector::DotProduct(GeometricNormal, Mesh.Normals[Vertex].GetSafeNormal()) > .998);
    TestTrue(TEXT("LOD 1 keeps the shared vertex normal"), Mesh.Normals[Vertex].Equals(Lod.Normals[LodVertex]));
    return true;
}
#endif
