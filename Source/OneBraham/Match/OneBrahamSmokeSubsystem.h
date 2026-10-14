// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamSmokeSubsystem.h
// ---------------------------------------------------------------------------
// Smoke as a fact, not a picture.
//
// The smoke cloud a player sees is Niagara (or its fallback), and a picture
// cannot stop anyone seeing through it. This keeps the clouds as numbers (a
// centre, a radius that grows to full over the first seconds, and an end
// time) so anything that asks "can A see B" can be told no: the bots ask it
// before they shoot, which is the whole point of throwing smoke at them.
//
// Registered on every machine from the projectile's detonation, which already
// runs everywhere with the cloud's parameters.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "OneBrahamSmokeSubsystem.generated.h"

UCLASS()
class ONEBRAHAM_API UOneBrahamSmokeSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Records a cloud: where it is, how big it gets, how long it lasts. */
	void AddCloud(const FVector& Centre, float RadiusCm, float LifetimeSeconds);

	/** True when the segment from Start to End passes through a live cloud. */
	bool IsSightBlocked(const FVector& Start, const FVector& End) const;

	/**
	 * The pure test: does the segment pass within Radius of Centre? Clouds are
	 * spheres; a sight line that only grazes the edge (the outer tenth) still
	 * sees, because the edge of real smoke is thin.
	 */
	static bool SegmentHitsCloud(const FVector& Start, const FVector& End, const FVector& Centre, float Radius);

	/** The radius a cloud of full size Radius has reached Age seconds after it popped. */
	static float CloudRadiusAt(float Radius, float AgeSeconds);

	/** Seconds a cloud takes to reach full size. */
	static constexpr float GrowSeconds = 2.5f;

private:
	struct FCloud
	{
		FVector Centre = FVector::ZeroVector;
		float Radius = 0.f;
		double StartTime = 0.0;
		double EndTime = 0.0;
	};

	double Now() const;

	mutable TArray<FCloud> Clouds;
};
