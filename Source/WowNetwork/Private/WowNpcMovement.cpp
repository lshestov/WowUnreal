#include "WowEntity.h"

float FWowMovementInfo::GetSplineSpeed() const
{
    if (!bHasActiveSpline || SplineDuration == 0) return 0;
    double Length = 0;
    FVector Previous = SplineStartPosition;
    for (const auto& Point : SplineWaypoints) { Length += FVector::Distance(Previous, Point.Position); Previous = Point.Position; }
    return Length * 1000.0 / SplineDuration;
}

void FWowMovementInfo::AdvanceSpline(float DeltaSeconds, const FVector* FacingTargetPosition)
{
    if (!bHasActiveSpline || SplineWaypoints.IsEmpty() || SplineDuration == 0) return;
    const double Length = GetSplineSpeed() * SplineDuration * 0.001;
    SplineElapsed += FMath::Max(0.0f, DeltaSeconds) * 1000;
    const bool bCyclic = (SplineFlags & 0x00080000) != 0;
    const bool bFinished = !bCyclic && SplineElapsed >= SplineDuration;
    if (bCyclic) SplineElapsed = FMath::Fmod(SplineElapsed, float(SplineDuration));
    double Remaining = Length * FMath::Clamp(double(SplineElapsed) / SplineDuration, 0.0, 1.0);
    FVector Previous = SplineStartPosition;
    FVector Direction = FVector::ZeroVector;
    for (int32 Index = 0; Index < SplineWaypoints.Num(); ++Index)
    {
        const FVector Next = SplineWaypoints[Index].Position;
        const double Segment = FVector::Distance(Previous, Next);
        if (Remaining <= Segment || Index == SplineWaypoints.Num() - 1)
        {
            Position = FMath::Lerp(Previous, Next, Segment > UE_SMALL_NUMBER ? FMath::Clamp(Remaining / Segment, 0.0, 1.0) : 1.0);
            Direction = Next - Previous;
            break;
        }
        Remaining -= Segment;
        Previous = Next;
    }
    if (!(SplineFlags & 0x00004000) && !Direction.IsNearlyZero()) Orientation = FMath::Atan2(Direction.Y, Direction.X);
    if (bFinished)
    {
        Position = SplineWaypoints.Last().Position;
        if (SplineMoveType == 4) Orientation = SplineFacingAngle;
        if (SplineMoveType == 2 || (SplineMoveType == 3 && FacingTargetPosition))
        {
            const FVector Facing = (SplineMoveType == 2 ? SplineFacingPoint : *FacingTargetPosition) - Position;
            if (!Facing.IsNearlyZero()) Orientation = FMath::Atan2(Facing.Y, Facing.X);
        }
        bHasActiveSpline = false;
        SplineWaypoints.Reset();
    }
}
