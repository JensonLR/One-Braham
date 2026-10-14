// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamAimAssist.h
// ---------------------------------------------------------------------------
// Aim assist for a thumbstick. Two effects, both only while the player is on a
// pad, and both scaled by Settings -> Controls -> Aim assist strength:
//
//   SLOWDOWN   When the crosshair is over an enemy, the right stick turns more
//              slowly, so a thumb that overshoots by a few degrees stays on.
//
//   FOLLOW     When an enemy is near the crosshair and moving across it, the
//              view drifts a fraction of the way with them. It only acts while
//              the player is moving or turning: a player who lets go of the pad
//              is never steered.
//
// There is no snap to target on aiming down sights. It is the part of console
// aim assist PC players resent most, and the follow already covers the case it
// exists for.
//
// The cone around each enemy is a world-space radius turned into an angle, so
// it shrinks with distance the way the body does, with an angular floor and
// ceiling: a target across the atrium still gets a usable cone, and one at
// arm's length does not grab the whole screen.
//
// Evaluate is pure (view and targets in, look scale and follow out) so the
// automation tests drive it without a world. The player controller gathers the
// targets, checks sight lines (walls and smoke), and applies the result.

#pragma once

#include "CoreMinimal.h"

struct ONEBRAHAM_API FOneBrahamAimAssistTarget
{
	/** The point assist aims for: upper chest. */
	FVector Location = FVector::ZeroVector;
	FVector Velocity = FVector::ZeroVector;
};

struct ONEBRAHAM_API FOneBrahamAimAssistResult
{
	/** Multiply right-stick turning by this: 1 = no slowdown. */
	float LookScale = 1.f;

	/** Rotation to add to the view this frame, degrees. Zero when follow does not apply. */
	FRotator Follow = FRotator::ZeroRotator;

	/** Which target in the input array was chosen, or INDEX_NONE. */
	int32 TargetIndex = INDEX_NONE;

	/** 0 at the edge of the cone, 1 dead on the target. */
	float Closeness01 = 0.f;
};

struct ONEBRAHAM_API FOneBrahamAimAssist
{
	/**
	 * Picks the target nearest the crosshair (relative to its own cone) and
	 * returns the slowdown and follow for it. Targets in Excluded are skipped:
	 * the controller uses that to drop one whose sight line turned out blocked.
	 */
	static FOneBrahamAimAssistResult Evaluate(const FVector& ViewLocation, const FRotator& ViewRotation, const FVector& ViewVelocity,
		TConstArrayView<FOneBrahamAimAssistTarget> Targets, float Strength01, bool bAimingDownSights, float DeltaSeconds,
		TConstArrayView<int32> Excluded = TConstArrayView<int32>());

	/** The cone half-angle, degrees, around a target this far away. */
	static float ConeHalfAngleDegrees(float DistanceCm, bool bAimingDownSights);

	/** Beyond this, a target gets no assist at all. */
	static float MaxRangeCm(bool bAimingDownSights) { return bAimingDownSights ? 6000.f : 3500.f; }

	/** The body radius the cone is built from. */
	static constexpr float TargetRadiusCm = 90.f;
	static constexpr float MinConeDegrees = 1.2f;
	static constexpr float MaxConeDegrees = 7.f;

	/** The most the stick slows down at full strength, dead on target. */
	static constexpr float MaxSlowdownHip = 0.4f;
	static constexpr float MaxSlowdownADS = 0.55f;

	/** The share of the target's angular motion the view follows at full strength. */
	static constexpr float FollowShareHip = 0.25f;
	static constexpr float FollowShareADS = 0.4f;

	/** Follow never turns the view faster than this, degrees per second. */
	static constexpr float MaxFollowDegreesPerSecond = 25.f;
};
