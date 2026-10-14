// Copyright (c) One Braham. All Rights Reserved.

#include "Core/OneBrahamAimAssist.h"

float FOneBrahamAimAssist::ConeHalfAngleDegrees(float DistanceCm, bool bAimingDownSights)
{
	// Down sights the cone is a little wider: the player has committed to the
	// target, and the slower ADS turn rate makes small corrections harder.
	const float Radius = TargetRadiusCm * (bAimingDownSights ? 1.25f : 1.f);
	const float Angle = FMath::RadiansToDegrees(FMath::Atan2(Radius, FMath::Max(DistanceCm, 1.f)));
	return FMath::Clamp(Angle, MinConeDegrees, MaxConeDegrees);
}

FOneBrahamAimAssistResult FOneBrahamAimAssist::Evaluate(const FVector& ViewLocation, const FRotator& ViewRotation, const FVector& ViewVelocity,
	TConstArrayView<FOneBrahamAimAssistTarget> Targets, float Strength01, bool bAimingDownSights, float DeltaSeconds,
	TConstArrayView<int32> Excluded)
{
	FOneBrahamAimAssistResult Result;
	const float Strength = FMath::Clamp(Strength01, 0.f, 1.f);
	if (Strength <= 0.f || Targets.Num() == 0)
	{
		return Result;
	}

	const FVector Forward = ViewRotation.Vector();
	const float MaxRange = MaxRangeCm(bAimingDownSights);

	// The target nearest the crosshair relative to its own cone: a far target
	// half a degree off beats a near one three degrees off.
	float BestNormalised = 1.f;
	for (int32 Index = 0; Index < Targets.Num(); ++Index)
	{
		if (Excluded.Contains(Index))
		{
			continue;
		}

		const FVector ToTarget = Targets[Index].Location - ViewLocation;
		const float Distance = ToTarget.Size();
		if (Distance < KINDA_SMALL_NUMBER || Distance > MaxRange)
		{
			continue;
		}

		const float Dot = FVector::DotProduct(Forward, ToTarget / Distance);
		const float OffsetDegrees = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Dot, -1.f, 1.f)));
		const float Normalised = OffsetDegrees / ConeHalfAngleDegrees(Distance, bAimingDownSights);
		if (Normalised < BestNormalised)
		{
			BestNormalised = Normalised;
			Result.TargetIndex = Index;
		}
	}

	if (Result.TargetIndex == INDEX_NONE)
	{
		return Result;
	}

	Result.Closeness01 = 1.f - BestNormalised;

	const float MaxSlowdown = bAimingDownSights ? MaxSlowdownADS : MaxSlowdownHip;
	Result.LookScale = 1.f - MaxSlowdown * Strength * Result.Closeness01;

	// Follow: where the target will be relative to the player one frame on,
	// turned into the rotation that keeps the crosshair the same distance off
	// it, and a share of that applied.
	if (DeltaSeconds > 0.f)
	{
		const FOneBrahamAimAssistTarget& Target = Targets[Result.TargetIndex];
		const FVector Now = Target.Location - ViewLocation;
		const FVector Next = Now + (Target.Velocity - ViewVelocity) * DeltaSeconds;

		FRotator Delta = (Next.Rotation() - Now.Rotation()).GetNormalized();
		Delta.Roll = 0.f;

		const float Share = (bAimingDownSights ? FollowShareADS : FollowShareHip) * Strength * Result.Closeness01;
		const float Cap = MaxFollowDegreesPerSecond * DeltaSeconds;
		Result.Follow.Yaw = FMath::Clamp(Delta.Yaw * Share, -Cap, Cap);
		Result.Follow.Pitch = FMath::Clamp(Delta.Pitch * Share, -Cap, Cap);
	}

	return Result;
}
