// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamBotRules.h
// ---------------------------------------------------------------------------
// The rules that decide how many bots a match gets and where they go, as pure
// functions.
//
// WHY BOTS FILL SQUADS
//   ASCENT is three squads of four and FIRE DRILL is six a side. A solo session
//   or a half-full lobby used to start with one squad in an empty tower: no
//   contact, no contest for the Skydeck, nothing to test the extraction flow
//   against. Bots now take every slot no human holds when the match starts, so
//   the building always has three squads (or two full teams) in it. A human who
//   joins later takes a bot's place rather than being turned away as a
//   spectator.
//
// HOW MANY
//   The match settings decide whether a mode fills by default (ASCENT and FIRE
//   DRILL do; NIGHT SHIFT, the co-op beta, does not). The travel option
//   `?Bots=N` overrides it: N caps the total, and `?Bots=0` turns them off.
//   Bots never exceed the empty slots - they fill a squad, they do not
//   overcrowd it.
//
// Kept free of the world so OneBraham.Bots.* can pin every rule without a
// running match; the game mode and AOneBrahamBotController are the only
// callers.

#pragma once

#include "CoreMinimal.h"
#include "Core/OneBrahamTypes.h"

class UObject;

namespace OneBrahamBotRules
{
	/** "Fill every empty slot": no cap on the number of bots. */
	constexpr int32 FillAllSlots = INDEX_NONE;

	/** Hard ceiling, whatever the options say: the tower is tuned for 16. */
	constexpr int32 MaxBotsPerMatch = 15;

	/** A bot that has moved less than this... */
	constexpr float StallDistanceCm = 150.f;
	/** ...in this long, while it wants to be going somewhere, is stuck. */
	constexpr float StallSeconds = 5.f;

	/** Close enough to the objective to stop travelling and hold the area. */
	constexpr float ObjectiveArrivalCm = 700.f;

	/** True when a mode puts bots in empty slots without being asked. */
	inline bool ModeFillsWithBotsByDefault(EOneBrahamMatchMode Mode)
	{
		return Mode == EOneBrahamMatchMode::Ascent || Mode == EOneBrahamMatchMode::FireDrill;
	}

	/**
	 * The bot limit a match runs with.
	 *
	 * bOptionGiven / OptionValue are the `?Bots=N` travel option (or `-Bots=N` on
	 * the command line); bSettingsFill is the match settings' answer when there
	 * is no option. Returns FillAllSlots, 0 (no bots) or a cap.
	 */
	inline int32 ResolveBotLimit(bool bOptionGiven, int32 OptionValue, bool bSettingsFill, int32 SettingsMaxBots = FillAllSlots)
	{
		if (bOptionGiven)
		{
			return FMath::Clamp(OptionValue, 0, MaxBotsPerMatch);
		}

		if (!bSettingsFill)
		{
			return 0;
		}

		return SettingsMaxBots < 0 ? FillAllSlots : FMath::Min(SettingsMaxBots, MaxBotsPerMatch);
	}

	/**
	 * How many bots each squad gets.
	 *
	 * Squads are filled one bot at a time, always into the squad with the fewest
	 * members (humans and bots alike; ties to the lower index), until every squad
	 * is full or the limit is spent. So a capped fill spreads out - a solo player
	 * with `?Bots=2` gets one opponent in each of the other two squads rather than
	 * two in one - and an uncapped fill tops every squad up to SquadSize.
	 *
	 * HumansPerSquad may be shorter than SquadCount (missing squads have nobody);
	 * a squad already over SquadSize gets nothing and takes nothing.
	 */
	inline TArray<int32> ComputeBotsPerSquad(int32 SquadCount, int32 SquadSize, const TArray<int32>& HumansPerSquad, int32 BotLimit)
	{
		TArray<int32> Bots;
		Bots.Init(0, FMath::Max(0, SquadCount));

		if (SquadCount <= 0 || SquadSize <= 0 || BotLimit == 0)
		{
			return Bots;
		}

		int32 Remaining = BotLimit < 0 ? MaxBotsPerMatch : FMath::Min(BotLimit, MaxBotsPerMatch);

		while (Remaining > 0)
		{
			int32 Best = INDEX_NONE;
			int32 BestMembers = TNumericLimits<int32>::Max();

			for (int32 Squad = 0; Squad < SquadCount; ++Squad)
			{
				const int32 Humans = HumansPerSquad.IsValidIndex(Squad) ? HumansPerSquad[Squad] : 0;
				const int32 Members = Humans + Bots[Squad];
				if (Members < SquadSize && Members < BestMembers)
				{
					BestMembers = Members;
					Best = Squad;
				}
			}

			if (Best == INDEX_NONE)
			{
				break;
			}

			++Bots[Best];
			--Remaining;
		}

		return Bots;
	}

	/**
	 * A human is joining and every squad is full: which squad gives up a bot.
	 * The one with the most bots (it is the least "real"), then the one with the
	 * fewest humans, then the lower index. INDEX_NONE when there are no bots.
	 */
	inline int32 PickSquadToYieldBot(const TArray<int32>& HumansPerSquad, const TArray<int32>& BotsPerSquad)
	{
		int32 Best = INDEX_NONE;
		for (int32 Squad = 0; Squad < BotsPerSquad.Num(); ++Squad)
		{
			if (BotsPerSquad[Squad] <= 0)
			{
				continue;
			}

			if (Best == INDEX_NONE)
			{
				Best = Squad;
				continue;
			}

			const int32 Humans = HumansPerSquad.IsValidIndex(Squad) ? HumansPerSquad[Squad] : 0;
			const int32 BestHumans = HumansPerSquad.IsValidIndex(Best) ? HumansPerSquad[Best] : 0;

			if (BotsPerSquad[Squad] > BotsPerSquad[Best]
				|| (BotsPerSquad[Squad] == BotsPerSquad[Best] && Humans < BestHumans))
			{
				Best = Squad;
			}
		}
		return Best;
	}

	/**
	 * Skill for the Nth bot of a squad: a spread around the middle so a squad is
	 * not four copies of one player. Practice-grade on purpose (Docs/06 s5);
	 * tuning against play data is M5.
	 */
	inline float SkillForBot(int32 OrdinalInSquad)
	{
		static constexpr float Spread[] = { 0.45f, 0.55f, 0.35f, 0.6f, 0.4f, 0.5f, 0.3f, 0.65f };
		return Spread[FMath::Abs(OrdinalInSquad) % UE_ARRAY_COUNT(Spread)];
	}

	/**
	 * Which of the mode's objective points a bot heads for. ASCENT has one (the
	 * Skydeck); FIRE DRILL spreads a team over its control points so a team does
	 * not walk to one point as a conga line.
	 */
	inline int32 PickObjectiveIndex(int32 ObjectiveCount, int32 SquadId, int32 OrdinalInSquad)
	{
		if (ObjectiveCount <= 0)
		{
			return INDEX_NONE;
		}
		return (FMath::Abs(SquadId) + FMath::Abs(OrdinalInSquad)) % ObjectiveCount;
	}

	/** True when a bot that wants to travel has not covered StallDistanceCm in StallSeconds. */
	inline bool IsStalled(float MovedCm, float SecondsSinceCheck)
	{
		return SecondsSinceCheck >= StallSeconds && MovedCm < StallDistanceCm;
	}

	/**
	 * A match with bots in it is played for its humans: true when at least one
	 * human holds a squad slot, none of them is still in the fight (alive or
	 * downed), there are bots still in it to play on without them (with none,
	 * the squads' "no survivors" rule decides), and no extraction
	 * is under way. Bots cannot extract, so without this a solo player who died
	 * watched them until the purge or the clock.
	 */
	inline bool ShouldEndWithNoHumansLeft(int32 HumansInSquads, int32 HumansInFight, int32 Bots, bool bExtractionActive)
	{
		return Bots > 0 && HumansInSquads > 0 && HumansInFight == 0 && !bExtractionActive;
	}

	/** "BOT Arnold (Alpha)" style display name: the ordinal keeps names unique. */
	ONEBRAHAM_API FString MakeBotName(int32 GlobalOrdinal);

	/**
	 * Reads the optional bot fields of a match settings asset by reflection:
	 * `bFillWithBots` (bool) and `MaxBots` (int, -1 = fill every slot). A field
	 * the class does not have leaves the out value untouched, so the caller's
	 * per-mode default stands. Returns true when bFillWithBots was found.
	 */
	ONEBRAHAM_API bool ReadSettingsBotFill(const UObject* Settings, bool& bOutFill, int32& OutMaxBots);
}
