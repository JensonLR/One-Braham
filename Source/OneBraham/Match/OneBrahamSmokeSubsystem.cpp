// Copyright (c) One Braham. All Rights Reserved.

#include "Match/OneBrahamSmokeSubsystem.h"

#include "Engine/World.h"

double UOneBrahamSmokeSubsystem::Now() const
{
	const UWorld* World = GetWorld();
	return World != nullptr ? World->GetTimeSeconds() : 0.0;
}

void UOneBrahamSmokeSubsystem::AddCloud(const FVector& Centre, float RadiusCm, float LifetimeSeconds)
{
	if (RadiusCm <= 0.f || LifetimeSeconds <= 0.f)
	{
		return;
	}
	FCloud Cloud;
	Cloud.Centre = Centre;
	Cloud.Radius = RadiusCm;
	Cloud.StartTime = Now();
	Cloud.EndTime = Cloud.StartTime + LifetimeSeconds;
	Clouds.Add(Cloud);
}

float UOneBrahamSmokeSubsystem::CloudRadiusAt(float Radius, float AgeSeconds)
{
	if (AgeSeconds <= 0.f)
	{
		return 0.f;
	}
	// Eases out: most of the cloud is there in the first second.
	const float Alpha = FMath::Clamp(AgeSeconds / GrowSeconds, 0.f, 1.f);
	return Radius * (1.f - FMath::Square(1.f - Alpha));
}

bool UOneBrahamSmokeSubsystem::SegmentHitsCloud(const FVector& Start, const FVector& End, const FVector& Centre, float Radius)
{
	if (Radius <= 0.f)
	{
		return false;
	}
	const FVector Closest = FMath::ClosestPointOnSegment(Centre, Start, End);
	return FVector::DistSquared(Closest, Centre) < FMath::Square(Radius * 0.9f);
}

bool UOneBrahamSmokeSubsystem::IsSightBlocked(const FVector& Start, const FVector& End) const
{
	const double Time = Now();
	Clouds.RemoveAll([Time](const FCloud& Cloud) { return Cloud.EndTime <= Time; });

	for (const FCloud& Cloud : Clouds)
	{
		const float Radius = CloudRadiusAt(Cloud.Radius, static_cast<float>(Time - Cloud.StartTime));
		if (SegmentHitsCloud(Start, End, Cloud.Centre, Radius))
		{
			return true;
		}
	}
	return false;
}
