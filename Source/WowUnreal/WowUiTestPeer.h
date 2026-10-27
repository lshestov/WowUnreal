#pragma once
#include "CoreMinimal.h"
#include "WowConnectionManager.h"
#include "UObject/StrongObjectPtr.h"

/** Optional native test partner. Only a coordinator-owned QA account is accepted. */
class FWowUiTestPeer
{
public:
    bool Start(const FString& Fixture,uint64 MainGuid,bool CleanupOnly=false);
    void Tick();
    void Stop();
    bool Ready() const;
    bool Complete() const { return Finished; }
    const FString& Error() const { return Failure; }
    bool Command(const FString& Action,int32 Slot=0,uint32 Value=0);
    UWowConnectionManager* Get() const { return Client.Get(); }
    uint64 Guid() const { return CharacterGuid; }
private:
    TStrongObjectPtr<UWowConnectionManager> Client;
    FString Path,Token,Character,Failure,LastPhase;
    uint64 MasterGuid=0,CharacterGuid=0;
    bool Creating=false,Entering=false,Stopping=false,Deleting=false,Finished=false,Cleanup=false,WorldReady=false;
    double LastPing=0,StopAt=0;
    bool Walking=false,WalkComplete=false;
    int32 WalkRequest=0;
    FVector WalkOrigin=FVector::ZeroVector;
    double WalkAt=0,WalkDistance=0;
    void Status(const FString& Phase);
    void Fail(const TCHAR* Message);
};
