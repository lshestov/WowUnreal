#include "Misc/AutomationTest.h"
#include "WowRenderSettings.h"
#include "WowM2EmitterComponent.h"
#include "Formats/M2Types.h"
#include "WowLuaVM.h"
#include "WowConnectionManager.h"
#include "../LuaApi/LuaApiRegistry.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_DEV_AUTOMATION_TESTS && __has_include("lua.h")
namespace
{
struct FGraphicsFixture
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    FWowLuaVM Lua;
    FWowLuaContext Context;
    TStrongObjectPtr<UWowConnectionManager> Connection{NewObject<UWowConnectionManager>()};
    AActor* Actor=nullptr;
    FGraphicsFixture()
    {
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        Actor=World->SpawnActor<AActor>();
        Lua.Initialize(false); WowLuaApi::RegisterAll(Lua.GetState());
        Context.ConnectionManager=Connection.Get(); Context.ResolveActor=[this](uint64){return Actor;};
        WowLuaApi::SetContext(Lua.GetState(),&Context);
    }
    ~FGraphicsFixture()
    {
        WowLuaApi::ClearContext(Lua.GetState()); Lua.Shutdown();
        World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGraphicsNativeSettings,"WowUnreal.Graphics.NativeSettings",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FGraphicsNativeSettings::RunTest(const FString&)
{
    FGraphicsFixture F; auto* Settings=F.World->GetSubsystem<UWowRenderSettings>();
    if(!TestNotNull(TEXT("World owns native render settings"),Settings)) return false;
    auto* Existing=NewObject<UInstancedStaticMeshComponent>(F.Actor); Existing->RegisterComponent();
    Settings->RegisterDoodad(Existing,2);
    TestTrue(TEXT("Live native values take precedence over a stale saved preference"),F.Lua.ExecuteString(TEXT("IdleWowUIState={cvar_particledensity='0.1',cvar_environmentdetail='0.5'}; assert(tonumber(GetCVar('particleDensity'))==1 and tonumber(GetCVar('environmentDetail'))==1)")));
    TestTrue(TEXT("Stock SetCVar reaches the native minimum"),F.Lua.ExecuteString(TEXT("SetCVar('particleDensity',0.1); SetCVar('environmentDetail',0.5)")));
    TestEqual(TEXT("Particle renderer reads the native minimum"),Settings->GetParticleDensity(),.1f);
    TestEqual(TEXT("Existing instances update immediately"),Existing->InstanceEndCullDistance,10000);
    auto* Late=NewObject<UInstancedStaticMeshComponent>(F.Actor); Late->RegisterComponent(); Settings->RegisterDoodad(Late,2);
    TestEqual(TEXT("New streaming batches inherit the current preference"),Late->InstanceEndCullDistance,10000);
    TestTrue(TEXT("Native maximum applies without changing the render shadow flag"),F.Lua.ExecuteString(TEXT("SetCVar('particleDensity',1); SetCVar('environmentDetail',1.5)")));
    TestEqual(TEXT("Existing instance maximum"),Existing->InstanceEndCullDistance,30000);
    TestEqual(TEXT("New instance maximum"),Late->InstanceEndCullDistance,30000);
    TestTrue(TEXT("Distance culling is active and shadows remain enabled"),!Late->bNeverDistanceCull && Late->CastShadow);
    TestTrue(TEXT("Invalid saved preferences load through native clamps"),F.Lua.ExecuteString(TEXT("IdleWowUIState={cvar_particledensity='-5',cvar_environmentdetail='900'}")));
    WowLuaApi::ApplySavedUiSettings(F.Lua.GetState());
    TestEqual(TEXT("Legacy density clamps at load"),Settings->GetParticleDensity(),.1f);
    TestEqual(TEXT("Legacy detail clamps at load"),Settings->GetEnvironmentDetail(),1.5f);
    TestTrue(TEXT("Both stock defaults apply through the same native path"),F.Lua.ExecuteString(TEXT("assert(tonumber(GetCVarDefault('particleDensity'))==1 and tonumber(GetCVarDefault('environmentDetail'))==1); SetCVar('particleDensity',GetCVarDefault('particleDensity')); SetCVar('environmentDetail',GetCVarDefault('environmentDetail'))")));
    TestEqual(TEXT("Default density"),Settings->GetParticleDensity(),1.f);
    TestEqual(TEXT("Default distance"),Existing->InstanceEndCullDistance,20000);
    TestTrue(TEXT("Saved endpoints are ready for another world"),F.Lua.ExecuteString(TEXT("SetCVar('particleDensity',0.1); SetCVar('environmentDetail',0.5)")));
    {
        FGraphicsFixture Next; auto* NextSettings=Next.World->GetSubsystem<UWowRenderSettings>();
        TestEqual(TEXT("A fresh world starts with independent defaults"),NextSettings->GetParticleDensity(),1.f);
        TestTrue(TEXT("Restored SavedVariables parse in a fresh VM"),Next.Lua.ExecuteString(TEXT("IdleWowUIState={cvar_particledensity='0.1',cvar_environmentdetail='0.5'}")));
        WowLuaApi::ApplySavedUiSettings(Next.Lua.GetState());
        TestEqual(TEXT("A fresh world's renderer receives saved density"),NextSettings->GetParticleDensity(),.1f);
        TestEqual(TEXT("A fresh world's renderer receives saved distance"),NextSettings->GetEnvironmentDetail(),.5f);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGraphicsParticleDensity,"WowUnreal.Graphics.ParticleDensity",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FGraphicsParticleDensity::RunTest(const FString&)
{
    FGraphicsFixture F; auto* Settings=F.World->GetSubsystem<UWowRenderSettings>();
    auto Model=MakeShared<FM2Data>(); auto& E=Model->ParticleEmitters.AddDefaulted_GetRef();
    E.EmitterType=1; E.RateTrack.Sequences.AddDefaulted(); E.RateTrack.Sequences[0].Times={0}; E.RateTrack.Sequences[0].Values={100};
    E.LifeTrack.Sequences.AddDefaulted(); E.LifeTrack.Sequences[0].Times={0}; E.LifeTrack.Sequences[0].Values={5};
    const auto MakeEmitter=[&]() {
        auto* C=NewObject<UWowM2EmitterComponent>(F.Actor); C->RegisterComponent();
        C->Model=Model; C->ActiveEmitters={0}; C->Accumulators={0}; C->PerEmitterCapacity=128; C->Random.Initialize(12340); return C;
    };
    auto* Low=MakeEmitter(); Settings->SetParticleDensity(.1f);
    for(int32 I=0;I<10;++I) Low->AdvanceParticles(.1f,I*100);
    TestEqual(TEXT("Minimum emits ten live authored particles"),Low->GetParticleCount(),10);
    auto* High=MakeEmitter(); Settings->SetParticleDensity(1);
    for(int32 I=0;I<10;++I) High->AdvanceParticles(.1f,I*100);
    TestEqual(TEXT("Maximum emits one hundred with the same rate and time"),High->GetParticleCount(),100);
    Settings->SetParticleDensity(.1f); High->AdvanceParticles(.1f,1000);
    TestEqual(TEXT("Changing a live effect preserves its existing particles"),High->GetParticleCount(),101);
    High->StopEmitting(); High->AdvanceParticles(.1f,1100);
    TestEqual(TEXT("Stop does not emit or erase the tail"),High->GetParticleCount(),101);
    High->AdvanceParticles(6,7100);
    TestEqual(TEXT("Existing particles expire with their authored lifetime"),High->GetParticleCount(),0);
    auto* Glue=MakeEmitter(); Glue->ConfigureGlueScene();
    for(int32 I=0;I<10;++I) Glue->AdvanceParticles(.1f,I*100);
    TestEqual(TEXT("World effects preference preserves the separate login scene"),Glue->GetParticleCount(),100);
    return true;
}
#endif
