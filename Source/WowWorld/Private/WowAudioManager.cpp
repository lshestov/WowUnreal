#include "WowAudioManager.h"
#include "Mpq/MpqManager.h"
#include "Formats/Dbc/DbcStore.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundWave.h"
#include "Kismet/GameplayStatics.h"
#include "WowPcmSoundWave.h"

DEFINE_LOG_CATEGORY_STATIC(LogWowAudio, Log, All);

AWowAudioManager::AWowAudioManager()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickInterval = 0.05f; // 20Hz for smooth crossfade
}

void AWowAudioManager::BeginPlay()
{
    Super::BeginPlay();

    // Create two music audio components for crossfading (A/B pattern)
    MusicComponentA = NewObject<UAudioComponent>(this, TEXT("MusicA"));
    MusicComponentA->SetupAttachment(GetRootComponent());
    MusicComponentA->bAutoActivate = false;
    MusicComponentA->bIsUISound = true; // Non-positional
    MusicComponentA->RegisterComponent();

    MusicComponentB = NewObject<UAudioComponent>(this, TEXT("MusicB"));
    MusicComponentB->SetupAttachment(GetRootComponent());
    MusicComponentB->bAutoActivate = false;
    MusicComponentB->bIsUISound = true;
    MusicComponentB->RegisterComponent();

    AmbienceComponent = NewObject<UAudioComponent>(this, TEXT("Ambience"));
    AmbienceComponent->SetupAttachment(GetRootComponent());
    AmbienceComponent->bAutoActivate = false;
    AmbienceComponent->bIsUISound = true;
    AmbienceComponent->RegisterComponent();

    UE_LOG(LogWowAudio, Log, TEXT("Audio manager initialized"));
}

void AWowAudioManager::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    // Handle crossfade
    if (CrossfadeProgress < 1.0f)
    {
        CrossfadeProgress = FMath::Min(CrossfadeProgress + DeltaTime / CrossfadeDuration, 1.0f);

        float FadeIn = CrossfadeProgress;
        float FadeOut = 1.0f - CrossfadeProgress;

        UAudioComponent* ActiveComp = bMusicAActive ? MusicComponentA : MusicComponentB;
        UAudioComponent* FadingComp = bMusicAActive ? MusicComponentB : MusicComponentA;

        const float Volume=bSoundEnabled && bMusicEnabled?MusicVolume*MasterVolume:0;
        if (ActiveComp) ActiveComp->SetVolumeMultiplier(FadeIn * Volume);
        if (FadingComp) FadingComp->SetVolumeMultiplier(FadeOut * Volume);

        if (CrossfadeProgress >= 1.0f && FadingComp)
        {
            FadingComp->Stop();
        }
    }
}

bool AWowAudioManager::GetUiOption(const FString& Name,FString& Value) const
{
    if(Name==TEXT("sound_mastervolume")) Value=FString::SanitizeFloat(MasterVolume);
    else if(Name==TEXT("sound_musicvolume")) Value=FString::SanitizeFloat(MusicVolume);
    else if(Name==TEXT("sound_ambiencevolume")) Value=FString::SanitizeFloat(AmbienceVolume);
    else if(Name==TEXT("sound_sfxvolume")) Value=FString::SanitizeFloat(EffectsVolume);
    else if(Name==TEXT("sound_enableall")) Value=bSoundEnabled?TEXT("1"):TEXT("0");
    else if(Name==TEXT("sound_enablemusic")) Value=bMusicEnabled?TEXT("1"):TEXT("0");
    else if(Name==TEXT("sound_enableambience")) Value=bAmbienceEnabled?TEXT("1"):TEXT("0");
    else if(Name==TEXT("sound_enablesfx")) Value=bEffectsEnabled?TEXT("1"):TEXT("0");
    else return false;
    return true;
}
bool AWowAudioManager::SetUiOption(const FString& Name,const FString& Value)
{
    const float Volume=FMath::Clamp(FCString::Atof(*Value),0.f,1.f); const bool Enabled=Value.ToBool() || FCString::Atof(*Value)!=0;
    if(Name==TEXT("sound_mastervolume")) MasterVolume=Volume;
    else if(Name==TEXT("sound_musicvolume")) MusicVolume=Volume;
    else if(Name==TEXT("sound_ambiencevolume")) AmbienceVolume=Volume;
    else if(Name==TEXT("sound_sfxvolume")) EffectsVolume=Volume;
    else if(Name==TEXT("sound_enableall")) bSoundEnabled=Enabled;
    else if(Name==TEXT("sound_enablemusic")) bMusicEnabled=Enabled;
    else if(Name==TEXT("sound_enableambience")) bAmbienceEnabled=Enabled;
    else if(Name==TEXT("sound_enablesfx")) bEffectsEnabled=Enabled;
    else return false;
    ApplyVolumes(); return true;
}
void AWowAudioManager::ApplyVolumes()
{
    const float Volume=bSoundEnabled && bMusicEnabled?MusicVolume*MasterVolume:0;
    auto* Active=bMusicAActive?MusicComponentA.Get():MusicComponentB.Get(); auto* Fading=bMusicAActive?MusicComponentB.Get():MusicComponentA.Get();
    if(Active) Active->SetVolumeMultiplier(CrossfadeProgress*Volume);
    if(Fading) Fading->SetVolumeMultiplier((1-CrossfadeProgress)*Volume);
    if(AmbienceComponent) AmbienceComponent->SetVolumeMultiplier(bSoundEnabled && bAmbienceEnabled?AmbienceVolume*MasterVolume:0);
}

void AWowAudioManager::SetCurrentZone(uint32 ZoneId, uint32 AreaId)
{
    if (ZoneId == CurrentZoneId && AreaId == CurrentAreaId)
    {
        return;
    }

    CurrentZoneId = ZoneId;
    CurrentAreaId = AreaId;

    // Look up zone music from AreaTable.dbc
    const FAreaTableDbcEntry* AreaEntry = FDbcStore::Get().AreaTable().GetById(AreaId);
    if (!AreaEntry)
    {
        AreaEntry = FDbcStore::Get().AreaTable().GetById(ZoneId);
    }

    if (AreaEntry)
    {
        if (AreaEntry->ZoneMusicID != 0 && AreaEntry->ZoneMusicID != PendingZoneMusicId)
        {
            PendingZoneMusicId = AreaEntry->ZoneMusicID;
            UpdateMusic();
        }

        if (AreaEntry->AmbienceID != 0 && AreaEntry->AmbienceID != PendingAmbienceId)
        {
            PendingAmbienceId = AreaEntry->AmbienceID;
            UpdateAmbience();
        }
    }

    UE_LOG(LogWowAudio, Log, TEXT("Zone changed: zone=%d area=%d musicId=%d ambienceId=%d"),
        ZoneId, AreaId, PendingZoneMusicId, PendingAmbienceId);
}

void AWowAudioManager::UpdateMusic()
{
    if (!Mpq || PendingZoneMusicId == 0) return;

    const FZoneMusicDbcEntry* ZoneMusic = FDbcStore::Get().ZoneMusic().GetById(PendingZoneMusicId);
    if (!ZoneMusic) return;

    // Pick day or night variant
    uint32 SoundId = bIsDaytime ? ZoneMusic->SoundDayID : ZoneMusic->SoundNightID;
    if (SoundId == 0) SoundId = ZoneMusic->SoundDayID; // Fallback to day

    FString FilePath = GetSoundFilePath(SoundId);
    if (FilePath.IsEmpty())
    {
        UE_LOG(LogWowAudio, Warning, TEXT("No music file for ZoneMusic %d (SoundEntry %d)"),
            PendingZoneMusicId, SoundId);
        return;
    }

    USoundWave* Sound = LoadSoundFromMpq(FilePath,true);
    if (!Sound)
    {
        UE_LOG(LogWowAudio, Warning, TEXT("Failed to load music: %s"), *FilePath);
        return;
    }

    // Start crossfade — switch active component
    bMusicAActive = !bMusicAActive;
    UAudioComponent* NewActive = bMusicAActive ? MusicComponentA : MusicComponentB;
    NewActive->SetSound(Sound);
    NewActive->SetVolumeMultiplier(0.0f); // Start silent, crossfade will ramp up
    NewActive->Play();
    CrossfadeProgress = 0.0f;

    UE_LOG(LogWowAudio, Log, TEXT("Playing music: %s (ZoneMusic %d, %s)"),
        *FilePath, PendingZoneMusicId, bIsDaytime ? TEXT("day") : TEXT("night"));
}

void AWowAudioManager::UpdateAmbience()
{
    if (!Mpq || PendingAmbienceId == 0) return;

    const FSoundAmbienceDbcEntry* Ambience = FDbcStore::Get().SoundAmbience().GetById(PendingAmbienceId);
    if (!Ambience) return;

    uint32 SoundId = bIsDaytime ? Ambience->DayAmbience : Ambience->NightAmbience;
    if (SoundId == 0) SoundId = Ambience->DayAmbience;
    if (SoundId == 0) return;

    FString FilePath = GetSoundFilePath(SoundId);
    if (FilePath.IsEmpty()) return;

    USoundWave* Sound = LoadSoundFromMpq(FilePath,true);
    if (!Sound) return;

    if (AmbienceComponent)
    {
        AmbienceComponent->Stop();
        AmbienceComponent->SetSound(Sound);
        AmbienceComponent->SetVolumeMultiplier(bSoundEnabled && bAmbienceEnabled?AmbienceVolume * MasterVolume:0);
        AmbienceComponent->Play();

        UE_LOG(LogWowAudio, Log, TEXT("Playing ambience: %s (AmbienceId %d, %s)"),
            *FilePath, PendingAmbienceId, bIsDaytime ? TEXT("day") : TEXT("night"));
    }
}

FString AWowAudioManager::GetSoundFilePath(uint32 SoundEntryId)
{
    const FSoundEntriesDbcEntry* SoundEntry = FDbcStore::Get().SoundEntries().GetById(SoundEntryId);
    if (!SoundEntry) return FString();

    // Pick a random file from the entry's file list
    TArray<FString> ValidFiles;
    for (int32 i = 0; i < 10; i++)
    {
        if (!SoundEntry->FileDataID[i].IsEmpty())
        {
            ValidFiles.Add(SoundEntry->FileDataID[i]);
        }
    }

    if (ValidFiles.Num() == 0) return FString();

    FString FileName = ValidFiles[FMath::RandRange(0, ValidFiles.Num() - 1)];
    FString DirBase = SoundEntry->DirectoryBase;

    // Build full path
    if (DirBase.IsEmpty())
    {
        return FString::Printf(TEXT("Sound\\%s"), *FileName);
    }
    return FString::Printf(TEXT("%s\\%s"), *DirBase, *FileName);
}

void AWowAudioManager::PlaySoundEffect(uint32 SoundEntryId)
{
    if (!Mpq) return;

    FString FilePath = GetSoundFilePath(SoundEntryId);
    if (FilePath.IsEmpty()) return;

    USoundWave* Sound = LoadSoundFromMpq(FilePath);
    if (!Sound) return;

    if(bSoundEnabled && bEffectsEnabled)
        if(auto* Component=UGameplayStatics::SpawnSound2D(this,Sound,MasterVolume*EffectsVolume))
        {
            // A procedural source supplies full audio quanta, including trailing
            // silence. Stop finite effects explicitly so silent sources cannot
            // occupy mixer channels indefinitely.
            FTimerHandle Handle; TWeakObjectPtr<UAudioComponent> Weak=Component;
            GetWorldTimerManager().SetTimer(Handle,[Weak](){if(Weak.IsValid()) Weak->Stop();},Sound->Duration+.05f,false);
        }
}

USoundWave* AWowAudioManager::LoadSoundFromMpq(const FString& FilePath,bool bLoop)
{
    if(!Mpq) return nullptr;
    if(!SoundCache.Contains(FilePath))
    {
        TArray<uint8> Raw;
        if(!Mpq->ReadFile(FilePath,Raw) || Raw.Num()<44) return nullptr;
        if(FMemory::Memcmp(Raw.GetData(),"RIFF",4) || FMemory::Memcmp(Raw.GetData()+8,"WAVE",4)) return nullptr;
        FDecodedSound Decoded;
        uint16 Format=0,Bits=0;
        int32 DataOffset=0,DataSize=0;
        for(int64 Offset=12;Offset+8<=Raw.Num();)
        {
            const auto* Chunk=Raw.GetData()+Offset; uint32 Size=0; FMemory::Memcpy(&Size,Chunk+4,4);
            if(Offset+8+Size>Raw.Num()) return nullptr;
            if(!FMemory::Memcmp(Chunk,"fmt ",4) && Size>=16)
            {
                FMemory::Memcpy(&Format,Chunk+8,2); FMemory::Memcpy(&Decoded.Channels,Chunk+10,2);
                FMemory::Memcpy(&Decoded.Rate,Chunk+12,4); FMemory::Memcpy(&Bits,Chunk+22,2);
            }
            if(!FMemory::Memcmp(Chunk,"data",4)) { DataOffset=int32(Offset+8); DataSize=Size; }
            Offset+=8+int64(Size)+(Size&1);
        }
        if(Format!=1 || (Bits!=8 && Bits!=16) || Decoded.Channels<1 || Decoded.Channels>2 || !Decoded.Rate || !DataSize) return nullptr;
        auto Pcm=MakeShared<TArray<uint8>,ESPMode::ThreadSafe>();
        if(Bits==16) Pcm->Append(Raw.GetData()+DataOffset,DataSize);
        else
        {
            Pcm->SetNumUninitialized(DataSize*2);
            for(int32 I=0;I<DataSize;++I) { const int16 Sample=(int32(Raw[DataOffset+I])-128)*256; FMemory::Memcpy(Pcm->GetData()+I*2,&Sample,2); }
        }
        if(Pcm->Num()%(Decoded.Channels*2)) return nullptr;
        Decoded.Pcm=Pcm; SoundCache.Add(FilePath,MoveTemp(Decoded));
    }
    const auto& Decoded=SoundCache[FilePath];
    auto* Wave=NewObject<UWowPcmSoundWave>(this);
    Wave->Pcm=Decoded.Pcm; Wave->bRepeat=bLoop; Wave->SetSampleRate(Decoded.Rate); Wave->NumChannels=Decoded.Channels;
    Wave->Duration=bLoop?INDEFINITELY_LOOPING_DURATION:float(Decoded.Pcm->Num())/(Decoded.Rate*Decoded.Channels*2);
    Wave->SoundGroup=ESoundGroup::SOUNDGROUP_UI;
    UE_LOG(LogWowAudio,Verbose,TEXT("Runtime PCM: %s, %d bytes, %u Hz"),*FilePath,Decoded.Pcm->Num(),Decoded.Rate);
    return Wave;
}
