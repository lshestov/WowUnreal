#include "Misc/AutomationTest.h"
#include "Formats/M2Parser.h"
#include "Formats/Dbc/SoundEntriesDbc.h"
#include "WowAudioManager.h"
#include "Mpq/MpqManager.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIdleWowGlueModel,"WowUnreal.Login.WrathSourceModel",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FIdleWowGlueModel::RunTest(const FString&)
{
    FString Path;if(!FParse::Value(FCommandLine::Get(),TEXT("wowdata="),Path)) return false;
    FMpqManager Mpq;if(!Mpq.Initialize(Path)) return false;TArray<uint8> M2,Skin;
    const FString Base=TEXT("Interface\\Glues\\Models\\UI_MainMenu_Northrend\\UI_MainMenu_Northrend");
    if(!TestTrue(TEXT("Original MPQ scene and skin"),Mpq.ReadFile(Base+TEXT(".m2"),M2) && Mpq.ReadFile(Base+TEXT("00.skin"),Skin))) return false;
    const auto Model=FM2Parser::Parse(M2,Skin);
    TestEqual(TEXT("Authored sequence zero duration"),Model.Animations[0].Length,66667u);
    TestEqual(TEXT("All scene render passes"),Model.RenderPasses.Num(),64);
    TestEqual(TEXT("All original particle emitters"),Model.ParticleEmitters.Num(),40);
    TestEqual(TEXT("Original source camera"),Model.Cameras.Num(),1);
    if(Model.Cameras.IsEmpty()) return false;const auto& C=Model.Cameras[0];
    TestEqual(TEXT("Camera shake keys retained"),C.PositionTrack.Sequences[0].Keys.Num(),93);
    bool Animated=false;for(const auto& K:C.PositionTrack.Sequences[0].Keys) Animated|=!K.Value.Equals(C.PositionTrack.Sequences[0].Keys[0].Value,.000001);
    TestTrue(TEXT("Source camera is animated"),Animated);
    const auto& S=C.PositionTrack.Sequences[0];const int32 I=20;const float T=(S.Times[I]+S.Times[I+1])*.5f;
    TestTrue(TEXT("Linear camera keys use the value inside each spline triple"),C.PositionTrack.Sample(T,Model.GlobalSequenceDurations,FVector::ZeroVector).Equals((S.Keys[I].Value+S.Keys[I+1].Value)*.5,.000001));
    TestTrue(TEXT("Authored camera roll is retained"),FMath::IsNearlyEqual(C.RollTrack.Sample(0,{},0.f),2*PI,.00001f));
    TestEqual(TEXT("All source sound events"),Model.Events.Num(),4);
    const uint32 Stamps[]={7000,10533,15067,26233};
    for(int32 E=0;E<Model.Events.Num();++E){TestEqual(TEXT("Sound event token"),Model.Events[E].Token,FString(TEXT("$DSO")));TestEqual(TEXT("Sound event ID"),Model.Events[E].Data,uint32(14143+E));TestEqual(TEXT("Sound timestamp"),Model.Events[E].Times[0][0],Stamps[E]);}
    int32 Layered=0,Environment=0;for(const auto& P:Model.RenderPasses){Layered+=P.TextureCount==2;Environment+=P.TexCoord>2 || P.SecondaryTexCoord>2;TestTrue(TEXT("Source combiner supported"),P.Combiner<=1 && (P.SecondaryCombiner==1 || P.SecondaryCombiner==4));}
    TestTrue(TEXT("Cloud masks and environment reflection layers retained"),Layered>10 && Environment>0);
    for(const auto& Texture:Model.TexturePaths){TArray<uint8> Raw;TestTrue(TEXT("Every source BLP is readable"),Mpq.ReadFile(Texture,Raw) && Raw.Num()>100);}
    auto Broken=M2;uint32 Invalid=0xffffffff;FMemory::Memcpy(Broken.GetData()+276,&Invalid,4);
    TestTrue(TEXT("Invalid camera table is rejected safely"),FM2Parser::Parse(Broken,Skin).Cameras.IsEmpty());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIdleWowGlueAudio,"WowUnreal.Login.OriginalMp3AndModelSounds",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FIdleWowGlueAudio::RunTest(const FString&)
{
    FString Path;if(!FParse::Value(FCommandLine::Get(),TEXT("wowdata="),Path)) return false;
    FMpqManager Mpq;if(!Mpq.Initialize(Path)) return false;TArray<uint8> Raw;if(!Mpq.ReadFile(TEXT("DBFilesClient\\SoundEntries.dbc"),Raw)) return false;
    FDbcParser Parser;if(!Parser.Parse(Raw)) return false;FSoundEntriesDbc Entries;if(!Entries.Load(Parser)) return false;
    for(uint32 Id:{12765u,14143u,14144u,14145u,14146u})
    {
        const auto* E=Entries.GetById(Id);if(!TestNotNull(TEXT("Glue sound resolved through original DBC"),E)) return false;
        if(!TestTrue(TEXT("Source sound bytes read from MPQ"),Mpq.ReadFile(E->DirectoryBase+TEXT("\\")+E->FileDataID[0],Raw))) return false;
        const auto D=AWowAudioManager::DecodeSound(Raw);if(!TestTrue(TEXT("Source format decodes to PCM"),D.Pcm.IsValid())) return false;
        bool Nonzero=false;for(const uint8 B:*D.Pcm) if(B){Nonzero=true;break;}TestTrue(TEXT("Decoded audio has nonzero samples"),Nonzero);
        TestTrue(TEXT("Valid PCM channel/frame alignment"),D.Channels>0 && D.Pcm->Num()%(D.Channels*2)==0);
        if(Id==12765){TestEqual(TEXT("Original MP3 rate"),D.Rate,48000u);TestEqual(TEXT("Original MP3 stereo"),D.Channels,uint16(2));TestTrue(TEXT("Full title track decoded, not only its first frame"),float(D.Pcm->Num())/(D.Rate*D.Channels*2)>540);}
    }
    TestFalse(TEXT("Truncated sound rejected"),AWowAudioManager::DecodeSound({0,1,2,3}).Pcm.IsValid());
    return true;
}
#endif
