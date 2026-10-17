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
// THE CLIMB
//   How bots get up the tower in ASCENT - which route each squad takes, when
//   it leaves the plaza, how far ahead of its squad a bot may get, when a
//   fight has gone on long enough, when the lockdown makes the climb urgent -
//   is the second half of this file, played by UOneBrahamBotClimbDirector
//   (AI/OneBrahamBotClimbDirector.h).
//
// Kept free of the world so OneBraham.Bots.* can pin every rule without a
// running match; the game mode, AOneBrahamBotController and the climb
// director are the only callers.

#pragma once

#include "CoreMinimal.h"
#include "Core/OneBrahamTypes.h"
#include "OneBraham.h"

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

	// ======================================================================
	//  The climb (ASCENT): routes, departures, the squad, fights, the lockdown
	// ----------------------------------------------------------------------
	//  UOneBrahamBotClimbDirector plays these; they live here so
	//  OneBraham.Bots.Climb* can pin them without a tower.
	//
	//  WHY ROUTES
	//    Every squad's shortest path to the Skydeck went up the spiral: all
	//    three met in its 120 cm floor-1 lobby inside twenty seconds and the
	//    bodies plugged the door for everyone after. The tower has three ways
	//    up from the ground: the fire stair A (-x), the spiral B (+x, left of the
	//    glass lift) and the four escalators to floor 1. Each squad now starts
	//    up its own (AssignOpeningRoutes), and on every floor after that a squad
	//    takes the cheapest usable route: distance, plus whoever is standing in
	//    its doorway and its stairwell above, minus a preference for the
	//    squad's own stair and for staying on the one it is on.
	// ======================================================================

	/** A way up the tower. */
	enum class EClimbRoute : uint8
	{
		None = 0,
		/** The switchback fire stair at -x: lobby door (-440, 600), ground to roof. */
		FireStairA = 1,
		/** The spiral at +x, left of the glass lift: lobby door (440, 600), ground to roof. */
		SpiralB = 2,
		/** The four escalators, ground floor to floor 1 only. */
		Escalators = 3
	};

	inline const TCHAR* ClimbRouteName(EClimbRoute Route)
	{
		switch (Route)
		{
		case EClimbRoute::FireStairA: return TEXT("StairA");
		case EClimbRoute::SpiralB:    return TEXT("SpiralB");
		case EClimbRoute::Escalators: return TEXT("Escalators");
		default:                      return TEXT("none");
		}
	}

	// --- Tower layout (mirrors Tools/Python/onebraham/build_environment.py) --

	/** Stair doors: centre x (mirrored), the door line, and the lobby behind it. */
	constexpr float StairDoorX = 440.f;
	constexpr float StairDoorY = 600.f;
	/** In the 120 cm lobby, at the foot of the flight. */
	constexpr float StairLobbyY = 665.f;
	/** On the cross-hallway in front of the door (the corridor is y 300..560). */
	constexpr float StairApproachY = 470.f;
	/** A stairwell's footprint, lobby included: |x| 190..690, y 590..1250. */
	constexpr float StairwellInnerX = 190.f;
	constexpr float StairwellOuterX = 690.f;
	constexpr float StairwellMinY = 590.f;
	constexpr float StairwellMaxY = 1250.f;

	/** The escalators: four lanes over x -1430..-770, rising north from y -900 to -200. */
	constexpr float EscalatorMinX = -1430.f;
	constexpr float EscalatorMaxX = -770.f;
	constexpr int32 EscalatorLanes = 4;
	/** The passage at their foot, and the gallery at their top on floor 1. */
	constexpr float EscalatorApproachY = -1000.f;
	constexpr float EscalatorFootY = -900.f;
	constexpr float EscalatorTopY = -200.f;
	constexpr float EscalatorArrivalY = -140.f;

	/** Destinations are given at capsule-centre height over the slab. */
	constexpr float ClimbPointHeightCm = 90.f;

	/** World Z of a floor's slab top (floor 1 on the origin, the ground floor a storey below). */
	inline float FloorBaseZ(int32 Floor)
	{
		return OneBrahamConstants::GroundFloorZ + Floor * OneBrahamConstants::FloorHeightCm;
	}

	/** True for the floors a route climbs from: the stairs everywhere below the roof, the escalators from the ground. */
	inline bool RouteClimbsFromFloor(EClimbRoute Route, int32 Floor)
	{
		switch (Route)
		{
		case EClimbRoute::FireStairA:
		case EClimbRoute::SpiralB:
			return Floor >= OneBrahamConstants::FloorPlaza && Floor < OneBrahamConstants::FloorTop;
		case EClimbRoute::Escalators:
			return Floor == OneBrahamConstants::FloorPlaza;
		default:
			return false;
		}
	}

	/** X of a stair's door: A is at -x, B at +x. */
	inline float StairSideX(EClimbRoute Route)
	{
		return Route == EClimbRoute::FireStairA ? -StairDoorX : StairDoorX;
	}

	/** Centre x of escalator lane N (wrapped), so a squad uses all four. */
	inline float EscalatorLaneX(int32 Lane)
	{
		const float LaneWidth = (EscalatorMaxX - EscalatorMinX) / EscalatorLanes;
		return EscalatorMinX + LaneWidth * (0.5f + FMath::Abs(Lane) % EscalatorLanes);
	}

	/**
	 * Where a bot on Floor walks to before it takes Route: in front of a stair
	 * door (spread along the hallway by Slot so a squad does not queue in one
	 * file in the door), or the passage at the escalators' foot.
	 */
	inline FVector RouteEntryPoint(EClimbRoute Route, int32 Floor, int32 Slot)
	{
		const float Z = FloorBaseZ(Floor) + ClimbPointHeightCm;
		if (Route == EClimbRoute::Escalators)
		{
			return FVector(EscalatorLaneX(Slot), EscalatorApproachY, Z);
		}
		const float Spread = ((FMath::Abs(Slot) % 4) - 1.5f) * 45.f;
		return FVector(StairSideX(Route) + Spread, StairApproachY, Z);
	}

	/** Where Route arrives on Floor: the stair lobby, or the escalators' top on floor 1. */
	inline FVector RouteLandingPoint(EClimbRoute Route, int32 Floor, int32 Slot)
	{
		const float Z = FloorBaseZ(Floor) + ClimbPointHeightCm;
		if (Route == EClimbRoute::Escalators)
		{
			return FVector(EscalatorLaneX(Slot), EscalatorArrivalY, FloorBaseZ(OneBrahamConstants::FloorLobby) + ClimbPointHeightCm);
		}
		return FVector(StairSideX(Route), StairLobbyY, Z);
	}

	/** The middle of a route's doorway on Floor: where bodies and fights jam it. */
	inline FVector RouteDoorPoint(EClimbRoute Route, int32 Floor)
	{
		const float Z = FloorBaseZ(Floor) + ClimbPointHeightCm;
		if (Route == EClimbRoute::Escalators)
		{
			return FVector((EscalatorMinX + EscalatorMaxX) * 0.5f, EscalatorFootY, Z);
		}
		return FVector(StairSideX(Route), StairDoorY, Z);
	}

	/** True when Location is inside the route itself (a stairwell, or on the escalators), at any floor. */
	inline bool IsOnRoute(EClimbRoute Route, const FVector& Location)
	{
		if (Route == EClimbRoute::Escalators)
		{
			return Location.X >= EscalatorMinX - 20.f && Location.X <= EscalatorMaxX + 20.f
				&& Location.Y >= EscalatorFootY - 20.f && Location.Y <= EscalatorTopY + 20.f
				&& Location.Z > FloorBaseZ(OneBrahamConstants::FloorPlaza) + 40.f
				&& Location.Z < FloorBaseZ(OneBrahamConstants::FloorLobby) + 40.f;
		}
		if (Route == EClimbRoute::FireStairA || Route == EClimbRoute::SpiralB)
		{
			const float SideX = Route == EClimbRoute::FireStairA ? -Location.X : Location.X;
			return SideX >= StairwellInnerX && SideX <= StairwellOuterX
				&& Location.Y >= StairwellMinY && Location.Y <= StairwellMaxY;
		}
		return false;
	}

	/** The route a location is on, or None when it is out on a floor. */
	inline EClimbRoute RouteAt(const FVector& Location)
	{
		for (const EClimbRoute Route : { EClimbRoute::FireStairA, EClimbRoute::SpiralB, EClimbRoute::Escalators })
		{
			if (IsOnRoute(Route, Location))
			{
				return Route;
			}
		}
		return EClimbRoute::None;
	}

	// --- Which route --------------------------------------------------------

	/** The ways up from the ground floor, in the order AssignOpeningRoutes reads distances. */
	constexpr EClimbRoute OpeningRoutes[] = { EClimbRoute::FireStairA, EClimbRoute::SpiralB, EClimbRoute::Escalators };
	constexpr int32 OpeningRouteCount = UE_ARRAY_COUNT(OpeningRoutes);

	/** Sharing a way up costs this much more than walking to an empty one. */
	constexpr float SharedOpeningCostCm = 100000.f;

	/**
	 * Which way off the ground floor each squad takes: every squad its own
	 * (while there are routes to go round), the set with the shortest total
	 * walk. DistancesCm holds, per squad, the distance from where the squad
	 * spawned to each of OpeningRoutes. Exhaustive: 3^4 sets at most.
	 */
	inline TArray<EClimbRoute> AssignOpeningRoutes(const TArray<TArray<float>>& DistancesCm)
	{
		const int32 Squads = FMath::Min(DistancesCm.Num(), OneBrahamConstants::MaxSquads);
		TArray<EClimbRoute> Best;
		Best.Init(EClimbRoute::None, DistancesCm.Num());
		if (Squads == 0)
		{
			return Best;
		}

		int32 Combos = 1;
		for (int32 Squad = 0; Squad < Squads; ++Squad)
		{
			Combos *= OpeningRouteCount;
		}

		float BestCost = TNumericLimits<float>::Max();
		for (int32 Combo = 0; Combo < Combos; ++Combo)
		{
			float Cost = 0.f;
			int32 Uses[OpeningRouteCount] = {};
			int32 Code = Combo;
			for (int32 Squad = 0; Squad < Squads; ++Squad)
			{
				const int32 Route = Code % OpeningRouteCount;
				Code /= OpeningRouteCount;
				const TArray<float>& Row = DistancesCm[Squad];
				Cost += Row.IsValidIndex(Route) ? FMath::Max(0.f, Row[Route]) : SharedOpeningCostCm;
				Cost += Uses[Route]++ * SharedOpeningCostCm;
			}
			if (Cost < BestCost)
			{
				BestCost = Cost;
				Code = Combo;
				for (int32 Squad = 0; Squad < Squads; ++Squad)
				{
					Best[Squad] = OpeningRoutes[Code % OpeningRouteCount];
					Code /= OpeningRouteCount;
				}
			}
		}
		return Best;
	}

	/**
	 * The route a squad favours on a floor: its opening route on the ground
	 * floor; above it, its opening stair (a squad that came up the escalators
	 * has no favourite and goes by what it finds).
	 */
	inline EClimbRoute PreferredRoute(EClimbRoute Opening, int32 Floor)
	{
		if (Floor == OneBrahamConstants::FloorPlaza)
		{
			return Opening;
		}
		return Opening == EClimbRoute::FireStairA || Opening == EClimbRoute::SpiralB ? Opening : EClimbRoute::None;
	}

	/** What a route looks like from where a squad stands, this second. */
	struct FClimbRouteOption
	{
		EClimbRoute Route = EClimbRoute::None;
		/** False when the way in is shut: a sealed shutter, a locked door, or a route that does not climb from here. */
		bool bUsable = true;
		/** From the squad to the route's way in. */
		float DistanceCm = 0.f;
		/** Live enemies in the doorway or the stairwell above it. */
		int32 Enemies = 0;
		/** Downed or dead pawns there: a body in a 150 cm door is a wall. */
		int32 Bodies = 0;
		/** Team-mates already queued in it. */
		int32 Friends = 0;
		/** Seconds of "a bot of this squad stalled on this route here" still remembered. */
		float RecentBlockSeconds = 0.f;
	};

	/** Score weights, in centimetres of walking: one enemy in a doorway costs nine metres. */
	constexpr float RouteEnemyCostCm = 900.f;
	constexpr float RouteBodyCostCm = 1800.f;
	constexpr float RouteFriendCostCm = 120.f;
	/** Per second of a remembered stall, so the memory fades as it runs out. */
	constexpr float RouteBlockCostPerSecondCm = 120.f;
	/** The squad's own route is worth this much walking... */
	constexpr float RoutePreferenceBonusCm = 1200.f;
	/** ...and staying on the route the squad is on, this much (no flip-flopping). */
	constexpr float RouteStickinessBonusCm = 600.f;

	/** A route's cost: lower is better. */
	inline float ScoreClimbRoute(const FClimbRouteOption& Option, bool bPreferred, bool bCurrent)
	{
		float Score = FMath::Max(0.f, Option.DistanceCm)
			+ Option.Enemies * RouteEnemyCostCm
			+ Option.Bodies * RouteBodyCostCm
			+ Option.Friends * RouteFriendCostCm
			+ FMath::Max(0.f, Option.RecentBlockSeconds) * RouteBlockCostPerSecondCm;
		if (bPreferred)
		{
			Score -= RoutePreferenceBonusCm;
		}
		if (bCurrent)
		{
			Score -= RouteStickinessBonusCm;
		}
		return Score;
	}

	/**
	 * The cheapest usable route (index into Options), INDEX_NONE when every way
	 * up is shut. Ties go to the earlier option.
	 */
	inline int32 PickClimbRoute(TConstArrayView<FClimbRouteOption> Options, EClimbRoute Preferred, EClimbRoute Current)
	{
		int32 Best = INDEX_NONE;
		float BestScore = TNumericLimits<float>::Max();
		for (int32 Index = 0; Index < Options.Num(); ++Index)
		{
			const FClimbRouteOption& Option = Options[Index];
			if (!Option.bUsable || Option.Route == EClimbRoute::None)
			{
				continue;
			}
			const float Score = ScoreClimbRoute(Option, Option.Route == Preferred, Option.Route == Current);
			if (Score < BestScore)
			{
				BestScore = Score;
				Best = Index;
			}
		}
		return Best;
	}

	// --- Departure, legs, the squad -----------------------------------------

	/** Squads leave the plaza this far apart, Alpha first... */
	constexpr float SquadDepartureStaggerSeconds = 3.f;
	/** ...and the members of a squad this far apart: a file, not a scrum in a door. */
	constexpr float MemberDepartureStaggerSeconds = 0.8f;

	inline float DepartureDelaySeconds(int32 SquadId, int32 OrdinalInSquad)
	{
		return (FMath::Abs(SquadId) % OneBrahamConstants::MaxSquads) * SquadDepartureStaggerSeconds
			+ (FMath::Abs(OrdinalInSquad) % OneBrahamConstants::SquadSize) * MemberDepartureStaggerSeconds;
	}

	/**
	 * A squad that has to share its way off the ground floor (three squads, and
	 * the escalators a dead end, leave two squads one stair) reaches its door
	 * this long after the earlier squad's last member: in step, the two met in
	 * the spiral's 120 cm lobby within seconds of each other, fought there, and
	 * plugged it with bodies - the jam the routes exist to prevent.
	 */
	constexpr float SharedOpeningGapSeconds = 5.f;
	/** The plaza walk is a jog. */
	constexpr float OpeningWalkSpeedCmPerSecond = 400.f;
	/** Nobody holds on the plaza longer than this for a shared door: the lockdown is coming. */
	constexpr float MaxSharedOpeningHoldSeconds = 12.f;

	/** When a squad member reaches its opening route's door: hold, stagger, then the walk. */
	inline float OpeningArrivalSeconds(int32 SquadId, int32 OrdinalInSquad, float DistanceCm, float HoldSeconds)
	{
		return HoldSeconds + DepartureDelaySeconds(SquadId, OrdinalInSquad) + FMath::Max(0.f, DistanceCm) / OpeningWalkSpeedCmPerSecond;
	}

	/**
	 * Extra seconds each squad holds on the plaza (on top of its stagger) so
	 * that a squad sharing its opening route with one that leaves before it
	 * arrives SharedOpeningGapSeconds behind that squad's last member. Parallel
	 * arrays, one entry per squad: its id, its opening route, and its walk to
	 * that route's door. A squad with a route of its own never holds.
	 */
	inline TArray<float> SharedOpeningHolds(const TArray<int32>& SquadIds, const TArray<EClimbRoute>& Opening, const TArray<float>& DistancesCm)
	{
		const int32 Count = SquadIds.Num();
		TArray<float> Holds;
		Holds.Init(0.f, Count);

		// In departure order, so a hold carries on to the squad after it.
		TArray<int32> Order;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Order.Add(Index);
		}
		Order.StableSort([&SquadIds](int32 A, int32 B) { return DepartureDelaySeconds(SquadIds[A], 0) < DepartureDelaySeconds(SquadIds[B], 0); });

		for (int32 Later = 1; Later < Order.Num(); ++Later)
		{
			const int32 Me = Order[Later];
			if (!Opening.IsValidIndex(Me) || Opening[Me] == EClimbRoute::None)
			{
				continue;
			}
			const float MyDistance = DistancesCm.IsValidIndex(Me) ? DistancesCm[Me] : 0.f;
			for (int32 Earlier = 0; Earlier < Later; ++Earlier)
			{
				const int32 Them = Order[Earlier];
				if (!Opening.IsValidIndex(Them) || Opening[Them] != Opening[Me])
				{
					continue;
				}
				const float TheirLast = OpeningArrivalSeconds(SquadIds[Them], OneBrahamConstants::SquadSize - 1,
					DistancesCm.IsValidIndex(Them) ? DistancesCm[Them] : 0.f, Holds[Them]);
				const float MyFirst = OpeningArrivalSeconds(SquadIds[Me], 0, MyDistance, Holds[Me]);
				Holds[Me] = FMath::Min(MaxSharedOpeningHoldSeconds,
					FMath::Max(Holds[Me], Holds[Me] + TheirLast + SharedOpeningGapSeconds - MyFirst));
			}
		}
		return Holds;
	}

	/** A leg is two floors: a short path the navmesh query always completes, re-aimed as the bot climbs. */
	constexpr int32 ClimbLegFloors = 2;

	/** The floor a bot on CurrentFloor heads for next on Route, never past the objective. */
	inline int32 ClimbTargetFloor(EClimbRoute Route, int32 CurrentFloor, int32 ObjectiveFloor)
	{
		if (Route == EClimbRoute::Escalators)
		{
			return OneBrahamConstants::FloorLobby;
		}
		return FMath::Clamp(CurrentFloor + ClimbLegFloors, CurrentFloor, FMath::Max(CurrentFloor, ObjectiveFloor));
	}

	/** A bot this many floors above its squad's last climber waits for them... */
	constexpr int32 MaxSquadSpreadFloors = 3;
	/** ...but not for longer than this on any one floor. */
	constexpr float MaxSquadWaitSeconds = 12.f;

	/**
	 * True when a bot should hold on its floor for the squad to close up.
	 * Never when the climb is urgent: the lockdown waits for nobody.
	 */
	inline bool ShouldWaitForSquad(int32 MyFloor, int32 SquadTrailFloor, float WaitedOnThisFloorSeconds, bool bUrgent)
	{
		return !bUrgent && SquadTrailFloor != INDEX_NONE
			&& MyFloor - SquadTrailFloor >= MaxSquadSpreadFloors
			&& WaitedOnThisFloorSeconds < MaxSquadWaitSeconds;
	}

	/** A climber that moved less than this... */
	constexpr float ClimbBlockedDistanceCm = 120.f;
	/** ...in this long is blocked: a body in the door, a queue, a shutter. Shorter than the controller's stall. */
	constexpr float ClimbBlockedSeconds = 4.f;
	/** How long a squad remembers that a route jammed on a floor. */
	constexpr float RouteBlockMemorySeconds = 30.f;

	inline bool IsClimbBlocked(float MovedCm, float SecondsSinceCheck)
	{
		return SecondsSinceCheck >= ClimbBlockedSeconds && MovedCm < ClimbBlockedDistanceCm;
	}

	// --- The lockdown -------------------------------------------------------

	/** Inside this many seconds of the next stage, the climb is urgent. */
	constexpr float LockdownUrgencySeconds = 30.f;

	/**
	 * True when a bot must climb now: its floor is being purged, or the next
	 * stage is close (or its warning is playing) and the bot is below the
	 * highest occupied floor - which is what the next stage seals beneath.
	 */
	inline bool IsClimbUrgent(bool bDirectorRunning, bool bWarningActive, float SecondsToNextStage,
		int32 MyFloor, int32 HighestOccupiedFloor, bool bMyFloorPurged)
	{
		if (bMyFloorPurged)
		{
			return true;
		}
		if (!bDirectorRunning)
		{
			return false;
		}
		const bool bStageClose = bWarningActive || (SecondsToNextStage > 0.f && SecondsToNextStage <= LockdownUrgencySeconds);
		return bStageClose && MyFloor < HighestOccupiedFloor;
	}

	// --- Fights -------------------------------------------------------------

	/** A fight in which nobody has hit anybody for this long is a stalemate... */
	constexpr float StalemateQuietSeconds = 8.f;
	/** ...once it has lasted at least this long (a fight gets a chance first). */
	constexpr float MinFightSecondsBeforeBreakOff = 10.f;
	/** Any fight is broken off after this long unless hits are still landing. */
	constexpr float MaxFightSeconds = 35.f;
	/** "Still landing" means an exchange inside this window. */
	constexpr float ActiveExchangeSeconds = 2.f;
	/** Under lockdown pressure: fight this long, then go. */
	constexpr float UrgentFightSeconds = 4.f;
	/** A break-off climbs for this long before the bot will stand and fight again. */
	constexpr float BreakOffSeconds = 10.f;

	/** Beyond this, a climbing bot fights on the move: it shoots, and keeps climbing. */
	constexpr float StandAndFightRangeCm = 1200.f;

	/**
	 * True when a bot in a fight should stand and fight it (the controller's
	 * advance / engage / back off) rather than keep climbing with its gun up.
	 * Close enemies are fought; distant ones are shot at on the way past -
	 * bots do not lead a moving target, so walking on is also the safer play.
	 */
	inline bool ShouldStandAndFight(float DistanceToTargetCm, bool bTargetVisible)
	{
		return bTargetVisible && DistanceToTargetCm <= StandAndFightRangeCm;
	}

	/**
	 * True when a bot should stop fighting from where it stands and climb on.
	 * FightSeconds is how long it has had a target; SecondsSinceExchange is how
	 * long since it last dealt or took damage.
	 */
	inline bool ShouldBreakOffFight(float FightSeconds, float SecondsSinceExchange, bool bUrgent)
	{
		if (bUrgent)
		{
			return FightSeconds >= UrgentFightSeconds && SecondsSinceExchange >= ActiveExchangeSeconds;
		}
		if (FightSeconds >= MaxFightSeconds)
		{
			return SecondsSinceExchange >= ActiveExchangeSeconds;
		}
		return FightSeconds >= MinFightSecondsBeforeBreakOff && SecondsSinceExchange >= StalemateQuietSeconds;
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
