// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamSpawnRules.h
// ---------------------------------------------------------------------------
// The rules that keep a spawn from being a free kill, as one pure function.
//
// The game mode gathers what it knows about a spawn point - how close the
// nearest enemy is, whether any enemy can see it, whether someone died next to
// it a moment ago, whether the player's own killer is nearby - and this turns
// that into a weight. Zero is a veto. Kept free of the world so the automation
// test (OneBraham.Core.SpawnSafety) can pin every rule without a running match.
//
// Distances are only counted for enemies within one floor of the spawn: a
// concrete slab stops rounds, and a stair is the only way through it.

#pragma once

#include "CoreMinimal.h"

namespace OneBrahamSpawnRules
{
	/** An enemy this close (same floor band) makes a spawn a free kill. */
	constexpr float HardVetoCm = 1200.f;
	/** Within this, a spawn is still unfair and heavily discounted. */
	constexpr float NearPenaltyCm = 3000.f;
	constexpr float NearPenalty = 0.15f;
	/** An enemy with line of sight inside this range vetoes outright. */
	constexpr float SightVetoCm = 2500.f;
	/** Line of sight beyond the veto range, up to this, is near-veto. */
	constexpr float SightCheckCm = 5000.f;
	constexpr float SightPenalty = 0.05f;
	/** A death this recent and this close marks a camped area. */
	constexpr float RecentDeathCm = 2000.f;
	constexpr float RecentDeathSeconds = 15.f;
	constexpr float RecentDeathPenalty = 0.1f;
	/** Respawning where your own killer can reach you again is a veto. */
	constexpr float KillerVetoCm = 4000.f;
	/** Floors either side of the spawn whose enemies count at all. */
	constexpr int32 FloorBand = 1;

	/** What the game mode found out about one spawn point. Distances are
	 *  TNumericLimits<float>::Max() when there is nothing to measure. */
	struct FThreat
	{
		float NearestEnemyCm = TNumericLimits<float>::Max();
		/** Nearest enemy that has an unobstructed line to the spawn. */
		float NearestSightingEnemyCm = TNumericLimits<float>::Max();
		float NearestRecentDeathCm = TNumericLimits<float>::Max();
		/** The requesting player's last killer, if alive and in the floor band. */
		float KillerCm = TNumericLimits<float>::Max();
		bool bKillerHasSight = false;
	};

	/** The weight multiplier for a spawn: 1 is untouched, 0 is a veto. */
	inline float ThreatMultiplier(const FThreat& Threat)
	{
		if (Threat.NearestEnemyCm < HardVetoCm
			|| Threat.NearestSightingEnemyCm < SightVetoCm
			|| Threat.KillerCm < KillerVetoCm
			|| Threat.bKillerHasSight)
		{
			return 0.f;
		}

		float Multiplier = 1.f;

		if (Threat.NearestEnemyCm < NearPenaltyCm)
		{
			Multiplier *= NearPenalty;
		}
		if (Threat.NearestSightingEnemyCm < SightCheckCm)
		{
			Multiplier *= SightPenalty;
		}
		if (Threat.NearestRecentDeathCm < RecentDeathCm)
		{
			Multiplier *= RecentDeathPenalty;
		}

		return Multiplier;
	}

	/** How safe a spawn is when every spawn is vetoed and one must still be
	 *  chosen: the furthest from anything that could shoot it. */
	inline float FallbackSafety(const FThreat& Threat)
	{
		const float Seen = Threat.NearestSightingEnemyCm < TNumericLimits<float>::Max()
			? Threat.NearestSightingEnemyCm * 0.5f : TNumericLimits<float>::Max();
		return FMath::Min3(Threat.NearestEnemyCm, Seen, Threat.KillerCm);
	}

	/** Whether an enemy on EnemyFloor can threaten a spawn on SpawnFloor. */
	inline bool IsInFloorBand(int32 SpawnFloor, int32 EnemyFloor)
	{
		return FMath::Abs(SpawnFloor - EnemyFloor) <= FloorBand;
	}
}
