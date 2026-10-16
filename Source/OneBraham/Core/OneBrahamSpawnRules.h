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
//
// It also holds the order blocked spawns are retried in (OrderSpawnAttempts)
// and where an ASCENT late joiner enters (LateJoinFloor); the test
// OneBraham.Core.SpawnFallback pins those.

#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"

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

	// ----------------------------------------------------------------------
	//  Attempt order: what happens when the chosen spawn is blocked
	// ----------------------------------------------------------------------
	// A spawn can be the right choice by every rule above and still fail: a bot
	// is standing on it, or a player who spawned there a moment ago has not
	// moved. Retrying the same point is how a respawn waited forever behind one
	// idle bot, so the game mode ranks every point the mode allows and walks
	// the list, and a point that failed a player goes to the back of that
	// player's list until something else has been tried.

	/** One spawn point as the attempt order sees it. The array the game mode
	 *  passes in is already in the mode's order of preference. */
	struct FSpawnAttempt
	{
		/** Nothing stands in a pawn's capsule there right now. */
		bool bClearNow = true;
		/** The requester's own squad point (ASCENT's plaza corner): standing
		 *  beside a squadmate on it beats a clear point in another squad's corner. */
		bool bSquadPoint = false;
		/** How recently this point failed this requester (higher = more recent),
		 *  or INDEX_NONE if it has not. */
		int32 FailedSerial = INDEX_NONE;
		/** Outside the mode's own floors (ASCENT's floors above the way in,
		 *  FIRE DRILL's floors without a control point): only once standing
		 *  beside a point on the mode's floors has been tried. */
		bool bFallback = false;
	};

	/**
	 * The order to try spawn points in, as indices into Attempts. Five tiers,
	 * the mode's preference kept within each:
	 *   1. on the mode's floors, not failed recently, and clear now (or the
	 *      requester's squad point);
	 *   2. on the mode's floors, not failed recently, but occupied - tried
	 *      beside the point, not on it;
	 *   3. and 4. the same two for the fallback floors - a clear point on
	 *      another floor is not better than a spot beside the right one;
	 *   5. failed recently - the oldest failure first, so a player whose every
	 *      point is blocked rotates through them rather than hammering one.
	 */
	inline void OrderSpawnAttempts(const TArray<FSpawnAttempt>& Attempts, TArray<int32>& OutOrder)
	{
		OutOrder.Reset(Attempts.Num());
		for (int32 Index = 0; Index < Attempts.Num(); ++Index)
		{
			OutOrder.Add(Index);
		}

		auto Tier = [&Attempts](int32 Index)
		{
			const FSpawnAttempt& Attempt = Attempts[Index];
			if (Attempt.FailedSerial != INDEX_NONE)
			{
				return 4;
			}
			return (Attempt.bFallback ? 2 : 0) + ((Attempt.bClearNow || Attempt.bSquadPoint) ? 0 : 1);
		};

		OutOrder.StableSort([&Attempts, &Tier](int32 A, int32 B)
		{
			const int32 TierA = Tier(A);
			const int32 TierB = Tier(B);
			if (TierA != TierB)
			{
				return TierA < TierB;
			}
			return TierA == 4 && Attempts[A].FailedSerial < Attempts[B].FailedSerial;
		});
	}

	/**
	 * ASCENT's floor for a player who joins a match already under way.
	 *
	 * ASCENT is a lobby start (Docs/06): everybody walks in off the plaza, and a
	 * late joiner does too while the plaza is still open - they are behind, as
	 * the last one through the door should be. Once the lockdown has sealed the
	 * plaza or the purge is gassing it, a spawn there is a death or a cage, so
	 * the joiner gets the lowest floor that is neither sealed nor purged: still
	 * the back of the climb, never inside the lockdown.
	 *
	 * FloorsWithSpawns is ascending. INDEX_NONE when every floor is closed.
	 */
	inline int32 LateJoinFloor(const TArray<int32>& FloorsWithSpawns, TFunctionRef<bool(int32)> IsFloorClosed)
	{
		for (const int32 Floor : FloorsWithSpawns)
		{
			if (!IsFloorClosed(Floor))
			{
				return Floor;
			}
		}
		return INDEX_NONE;
	}
}
