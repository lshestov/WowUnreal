#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/SkeletalMesh.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "WowPlayerCharacter.h"
#include "WowSkeletalMeshBuilder.h"
#include "WowAnimationController.h"
#include "WowM2AnimInstance.h"
#include "Formats/M2Types.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIdleWowRuntimeAnimations, "WowUnreal.Animation.RuntimePlayback",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FIdleWowRuntimeAnimations::RunTest(const FString& Parameters)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto Data = MakeShared<FM2Data>();
    // Original bone order intentionally differs from UE's parent-first order.
    Data->Bones.SetNum(3);
    Data->Bones[0].ParentBone = 2;
    Data->Bones[0].PivotPoint = FVector(1, 0, 0);
    Data->Bones[1].ParentBone = 0;
    Data->Bones[2].ParentBone = -1;
    Data->Vertices.SetNum(3);
    Data->Vertices[1].Position = FVector(1, 0, 0);
    Data->Vertices[2].Position = FVector(0, 0, 1);
    for (auto& V : Data->Vertices) { V.BoneWeights[0] = 255; V.Normal = FVector(0, 1, 0); }
    Data->Indices = {0, 1, 2};
    Data->BoundingBox = FBox(FVector::ZeroVector, FVector(1, 1, 1));
    for (int32 Id : {0, 4, 5, 25, 37, 38, 39, 40, 16, 109})
    {
        FM2AnimationData Clip;
        Clip.AnimationId = Id;
        Clip.Duration = Id == 16 ? 1 : 1000;
        Clip.MoveSpeed = Id == 5 ? 7.0f : Id == 4 ? 2.5f : 0;
        Clip.BoneTracks.SetNum(3);
        auto& Track = Clip.BoneTracks[0];
        Track.TransTimestamps = {0, 100, 1000};
        Track.TransValues = {FVector::ZeroVector, FVector(10, 0, 0), FVector::ZeroVector};
        Track.RotTimestamps = {0, 1000};
        Track.RotValues = {FQuat::Identity, FQuat(FVector::UpVector, PI / 2)};
        Data->AnimationTracks.Add(MoveTemp(Clip));
    }
    auto* Skeleton = FWowSkeletalMeshBuilder::CreateSkeleton(*Data, TEXT("AnimationTest"));
    auto* Mesh = FWowSkeletalMeshBuilder::CreateSkeletalMesh(*Data, Skeleton, TEXT("AnimationTest"), nullptr, nullptr, nullptr, nullptr, true);
    auto* Player = World->SpawnActor<AWowPlayerCharacter>();
    Player->GetMesh()->SetSkeletalMesh(Mesh);
    auto* Controller = NewObject<UWowAnimationController>(Player);
    Controller->InitializeRuntime(Player->GetMesh(), Data);
    auto* Instance = Controller->GetRuntimeInstance();
    if (!TestNotNull(TEXT("Native animation instance, without editor sequence generation"), Instance))
    {
        World->DestroyWorld(false); GEngine->DestroyWorldContext(World); return false;
    }
    const int32 Bone = Mesh->GetRefSkeleton().FindBoneIndex(FWowSkeletalMeshBuilder::GetBoneName(0));
    Instance->NativeUpdateAnimation(0.55f);
    TArray<FTransform> Pose;
    Instance->SampleLocalPose(Pose);
    TestTrue(TEXT("Nonuniform timestamps are interpolated in milliseconds, with mapped local pivot"),
        FMath::IsNearlyEqual(Pose[Bone].GetTranslation().Y, 600.0, 0.01));
    TestTrue(TEXT("Quaternion basis reflection is normalized"), Pose[Bone].GetRotation().IsNormalized());
    TestTrue(TEXT("Rotation follows elapsed time, rather than key index"),
        FMath::IsNearlyEqual(Pose[Bone].GetRotation().GetAngle(), 0.55 * PI / 2, 0.001)
        || FMath::IsNearlyEqual(Pose[Bone].GetRotation().GetAngle(), 2 * PI - 0.55 * PI / 2, 0.001));
    TestTrue(TEXT("Missing track keeps the child reference transform"), Pose[2].Equals(Mesh->GetRefSkeleton().GetRefBonePose()[2]));
    TestFalse(TEXT("Missing animation is reported, allowing a real fallback"), Instance->PlayM2Animation(65535, true));
    Instance->NativeUpdateAnimation(1.0f);
    TestTrue(TEXT("Loop wraps by clip duration"), FMath::IsNearlyEqual(Instance->GetM2Time(), 0.55f, 0.0001f));
    Instance->PlayM2Animation(5, false);
    TArray<FTransform> Blended;
    Instance->SampleLocalPose(Blended);
    TestTrue(TEXT("State transition starts at the previous pose"), Blended[Bone].Equals(Pose[Bone], 0.01));
    Instance->NativeUpdateAnimation(2.0f);
    TestEqual(TEXT("One-shot clamps at final frame"), Instance->GetM2Time(), 1.0f);

    auto* Movement = Player->GetCharacterMovement();
    Movement->SetMovementMode(MOVE_Walking);
    Movement->Velocity = FVector(700, 0, 0);
    Controller->UpdateLocalPlayerState(Player, false);
    TestEqual(TEXT("Normal movement uses Run"), Instance->GetM2AnimationId(), 5);
    Controller->UpdateLocalPlayerState(Player, true);
    TestEqual(TEXT("Walking toggle uses Walk"), Instance->GetM2AnimationId(), 4);
    Movement->Velocity = FVector::ZeroVector;
    Controller->UpdateLocalPlayerState(Player, false);
    TestEqual(TEXT("Stopping returns to Stand"), Instance->GetM2AnimationId(), 0);
    Movement->SetMovementMode(MOVE_Falling);
    Movement->Velocity = FVector(0, 0, 400);
    Controller->UpdateLocalPlayerState(Player, false);
    TestEqual(TEXT("Jump starts with takeoff"), Instance->GetM2AnimationId(), 37);
    TestFalse(TEXT("Takeoff does not lock out falling or landing"), Controller->IsPlayingOneShot());
    Movement->Velocity.Z = -100;
    Controller->UpdateLocalPlayerState(Player, false);
    TestEqual(TEXT("Descending switches to falling before the takeoff clip ends"), Instance->GetM2AnimationId(), 40);
    Movement->SetMovementMode(MOVE_Walking);
    Movement->Velocity = FVector::ZeroVector;
    Controller->UpdateLocalPlayerState(Player, false);
    TestEqual(TEXT("Landing plays the landing clip"), Instance->GetM2AnimationId(), 39);
    TestTrue(TEXT("Combat fallback finds actual unarmed animation"), Controller->PlayAttackAnimation());
    TestEqual(TEXT("Unavailable 1H clip does not pretend to play idle"), Instance->GetM2AnimationId(), 16);
    TestTrue(TEXT("Dance can use the same native runtime path"), Controller->PlayAnimationById(EWowAnimId::Dance));

    // Exercise the actual UE evaluation proxy and component bone transforms.
    Player->GetMesh()->TickAnimation(0.25f, false);
    Player->GetMesh()->RefreshBoneTransforms();
    const FTransform Evaluated = Player->GetMesh()->GetBoneSpaceTransforms()[Bone];
    TestFalse(TEXT("Mesh receives the animated pose through the native proxy"),
        Evaluated.Equals(Mesh->GetRefSkeleton().GetRefBonePose()[Bone], 0.001));

    auto* Other = World->SpawnActor<AWowPlayerCharacter>();
    Other->GetMesh()->SetSkeletalMesh(Mesh);
    auto* Copied = NewObject<UWowAnimationController>(Other);
    Copied->InitializeFromController(Other->GetMesh(), Controller);
    TestTrue(TEXT("Player controller transfer preserves runtime clips"), Copied->PlayAnimationById(EWowAnimId::Run));
    Copied->GetRuntimeInstance()->SetM2MovementSpeed(3.5f, 1.0f);
    TestEqual(TEXT("Gait rate follows actual travel speed"), Copied->GetRuntimeInstance()->GetM2PlayRate(), 0.5f);
    FWowMovementInfo Travel;
    Travel.MoveFlags = WowMovementFlags::FORWARD;
    Copied->UpdateAnimationState(Travel, true);
    TestEqual(TEXT("A moving combat NPC runs instead of sliding in a ready stance"), Copied->GetRuntimeInstance()->GetM2AnimationId(), 5);
    Travel.MoveFlags = 0;
    Copied->UpdateAnimationState(Travel, true);
    TestEqual(TEXT("Combat ready is used after stopping"), Copied->GetRuntimeInstance()->GetM2AnimationId(), 25);
    Copied->UpdateAnimationState(Travel, false);
    TestNotEqual(TEXT("Each character owns an independent playback instance"), Copied->GetRuntimeInstance(), Instance);
    Other->SetupAnimationController(Copied);
    Other->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    Other->GetCharacterMovement()->Velocity = FVector::ZeroVector;
    Copied->PlayAttackAnimation();
    const double Started = FPlatformTime::Seconds();
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, World, Other, Copied, Started]()
    {
        if (Copied->IsPlayingOneShot() && FPlatformTime::Seconds() - Started < 2.0) return false;
        Copied->UpdateLocalPlayerState(Other);
        TestEqual(TEXT("One-shot completion returns to idle even when movement state stayed idle"),
            Copied->GetRuntimeInstance()->GetM2AnimationId(), 0);
        World->DestroyWorld(false);
        GEngine->DestroyWorldContext(World);
        return true;
    }));
    return true;
}
#endif
