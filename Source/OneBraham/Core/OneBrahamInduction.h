// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamInduction.h
// ---------------------------------------------------------------------------
// A new starter's induction: the three minutes on the ground floor that teach a
// first-time player everything ASCENT asks of them, one prompt at a time, by
// doing it - move, look, sprint, crouch, find a keycard, badge through the
// gates, ride up, fire, reload. Each step finishes the moment the player has
// done the thing, and none can trap anyone: a step with no progress moves on
// by itself.
//
// Pure: UOneBrahamInductionComponent feeds it a snapshot every frame and shows
// the prompt; the tests drive it directly.

#pragma once

#include "CoreMinimal.h"

enum class EOneBrahamInductionStep : uint8
{
	Move,
	Look,
	Sprint,
	Crouch,
	Keycard,
	Gates,
	Climb,
	Fire,
	Reload,
	Done
};

/** What the player has done so far, as the component measures it. */
struct FOneBrahamInductionSnapshot
{
	float DistanceWalkedCm = 0.f;
	float YawTurnedDeg = 0.f;
	bool bSprinting = false;
	bool bCrouched = false;
	bool bHasKeycard = false;
	bool bPastGates = false;
	int32 Floor = 0;
	int32 ShotsFired = 0;
	bool bReloading = false;
};

struct FOneBrahamInduction
{
	/** Seconds a step waits for the player before moving on anyway. */
	static constexpr float StepTimeoutSeconds = 45.f;

	EOneBrahamInductionStep Step = EOneBrahamInductionStep::Move;
	float StepSeconds = 0.f;

	/** Baselines taken when a step starts, so each step measures its own progress. */
	FOneBrahamInductionSnapshot Baseline;

	bool IsDone() const { return Step == EOneBrahamInductionStep::Done; }

	/** True when the snapshot shows the current step done. */
	bool IsStepComplete(const FOneBrahamInductionSnapshot& Now) const
	{
		switch (Step)
		{
		case EOneBrahamInductionStep::Move:		return Now.DistanceWalkedCm - Baseline.DistanceWalkedCm >= 400.f;
		case EOneBrahamInductionStep::Look:		return Now.YawTurnedDeg - Baseline.YawTurnedDeg >= 120.f;
		case EOneBrahamInductionStep::Sprint:	return Now.bSprinting;
		case EOneBrahamInductionStep::Crouch:	return Now.bCrouched;
		case EOneBrahamInductionStep::Keycard:	return Now.bHasKeycard;
		case EOneBrahamInductionStep::Gates:	return Now.bPastGates || Now.Floor >= 1;
		case EOneBrahamInductionStep::Climb:	return Now.Floor >= 1;
		case EOneBrahamInductionStep::Fire:		return Now.ShotsFired - Baseline.ShotsFired >= 3;
		case EOneBrahamInductionStep::Reload:	return Now.bReloading;
		default:								return false;
		}
	}

	/** Advances by one frame. Returns true when the step changed. */
	bool Tick(const FOneBrahamInductionSnapshot& Now, float DeltaSeconds)
	{
		if (IsDone())
		{
			return false;
		}
		StepSeconds += DeltaSeconds;
		if (IsStepComplete(Now) || StepSeconds >= StepTimeoutSeconds)
		{
			Step = static_cast<EOneBrahamInductionStep>(static_cast<uint8>(Step) + 1);
			StepSeconds = 0.f;
			Baseline = Now;
			return true;
		}
		return false;
	}

	/** The prompt for the current step: what to do, and how. */
	FText GetPrompt() const
	{
		switch (Step)
		{
		case EOneBrahamInductionStep::Move:
			return NSLOCTEXT("OneBraham", "Induction_Move", "INDUCTION  ·  Walk in from the street  -  W A S D");
		case EOneBrahamInductionStep::Look:
			return NSLOCTEXT("OneBraham", "Induction_Look", "Look around the lobby  -  move the mouse");
		case EOneBrahamInductionStep::Sprint:
			return NSLOCTEXT("OneBraham", "Induction_Sprint", "Sprint  -  hold SHIFT while moving");
		case EOneBrahamInductionStep::Crouch:
			return NSLOCTEXT("OneBraham", "Induction_Crouch", "Crouch behind cover  -  C");
		case EOneBrahamInductionStep::Keycard:
			return NSLOCTEXT("OneBraham", "Induction_Keycard", "Find a staff keycard  -  look around reception, then E to pick it up");
		case EOneBrahamInductionStep::Gates:
			return NSLOCTEXT("OneBraham", "Induction_Gates", "Badge through the card gates, right of reception");
		case EOneBrahamInductionStep::Climb:
			return NSLOCTEXT("OneBraham", "Induction_Climb", "Take the escalators up to level 1  -  right after the gates, then left");
		case EOneBrahamInductionStep::Fire:
			return NSLOCTEXT("OneBraham", "Induction_Fire", "Fire your weapon  -  LEFT MOUSE  ·  aim with RIGHT MOUSE");
		case EOneBrahamInductionStep::Reload:
			return NSLOCTEXT("OneBraham", "Induction_Reload", "Reload  -  R");
		default:
			return NSLOCTEXT("OneBraham", "Induction_Done",
				"Induction complete. The Skydeck is on 15. Hold it for thirty seconds. Good luck.");
		}
	}
};
