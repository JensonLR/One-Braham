// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamBotTests.cpp
// ---------------------------------------------------------------------------
// The rules that decide how many bots a match gets and where they sit
// (AI/OneBrahamBotRules.h), pinned without a running match: which modes fill
// by default, how `?Bots=N` and the match settings combine, how a fill spreads
// over the squads, which squad gives a bot up when a human joins, when the
// match ends because no human is left in it, and the stall test that walks a
// stuck bot out of a dead end. And the climb (UOneBrahamBotClimbDirector's
// rules): which way up each squad takes, the tower points it walks to, when it
// leaves the plaza, how far ahead of its squad a bot may get, when a jam is a
// jam, when the lockdown makes the climb urgent, and when a fight is broken off.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "AI/OneBrahamBotRules.h"
#include "Data/OneBrahamMatchSettingsAsset.h"
#include "UObject/Package.h"

#if WITH_AUTOMATION_TESTS

namespace
{
	int32 Sum(const TArray<int32>& Values)
	{
		int32 Total = 0;
		for (const int32 Value : Values)
		{
			Total += Value;
		}
		return Total;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamBotFillDefaultsTest, "OneBraham.Bots.FillDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamBotFillDefaultsTest::RunTest(const FString& Parameters)
{
	using namespace OneBrahamBotRules;

	TestTrue(TEXT("ASCENT fills empty squad slots by default"), ModeFillsWithBotsByDefault(EOneBrahamMatchMode::Ascent));
	TestTrue(TEXT("FIRE DRILL fills to 6v6 by default"), ModeFillsWithBotsByDefault(EOneBrahamMatchMode::FireDrill));
	TestFalse(TEXT("NIGHT SHIFT (co-op beta) does not"), ModeFillsWithBotsByDefault(EOneBrahamMatchMode::NightShift));

	// No option: the settings decide.
	TestEqual(TEXT("no option, settings fill: every slot"), ResolveBotLimit(false, 0, true), FillAllSlots);
	TestEqual(TEXT("no option, settings off: none"), ResolveBotLimit(false, 0, false), 0);
	TestEqual(TEXT("no option, settings cap of 5: five"), ResolveBotLimit(false, 0, true, 5), 5);

	// The option wins over the settings either way.
	TestEqual(TEXT("?Bots=0 turns a filling mode off"), ResolveBotLimit(true, 0, true), 0);
	TestEqual(TEXT("?Bots=3 caps a filling mode"), ResolveBotLimit(true, 3, true), 3);
	TestEqual(TEXT("?Bots=6 turns a non-filling mode on"), ResolveBotLimit(true, 6, false), 6);
	TestEqual(TEXT("a negative option is none"), ResolveBotLimit(true, -4, true), 0);
	TestEqual(TEXT("an absurd option is clamped to the tower's ceiling"), ResolveBotLimit(true, 400, true), MaxBotsPerMatch);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamBotSquadFillTest, "OneBraham.Bots.SquadFill",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamBotSquadFillTest::RunTest(const FString& Parameters)
{
	using namespace OneBrahamBotRules;

	// Solo ASCENT: one human in squad 0, three squads of four. Eleven bots, and
	// every squad ends up full - three squads in the tower.
	{
		const TArray<int32> Bots = ComputeBotsPerSquad(3, 4, { 1, 0, 0 }, FillAllSlots);
		TestEqual(TEXT("solo ASCENT: three squads planned"), Bots.Num(), 3);
		TestEqual(TEXT("solo ASCENT: eleven bots"), Sum(Bots), 11);
		TestEqual(TEXT("solo ASCENT: three bots join the human"), Bots[0], 3);
		TestEqual(TEXT("solo ASCENT: squad 2 is all bots"), Bots[1], 4);
		TestEqual(TEXT("solo ASCENT: squad 3 is all bots"), Bots[2], 4);
	}

	// A partial lobby: 4 + 2 humans. Bots top up only the missing slots.
	{
		const TArray<int32> Bots = ComputeBotsPerSquad(3, 4, { 4, 2, 0 }, FillAllSlots);
		TestEqual(TEXT("partial ASCENT: a full squad gets no bots"), Bots[0], 0);
		TestEqual(TEXT("partial ASCENT: two bots top up squad 2"), Bots[1], 2);
		TestEqual(TEXT("partial ASCENT: squad 3 is all bots"), Bots[2], 4);
	}

	// A full lobby gets nothing.
	TestEqual(TEXT("a full ASCENT lobby gets no bots"), Sum(ComputeBotsPerSquad(3, 4, { 4, 4, 4 }, FillAllSlots)), 0);

	// FIRE DRILL solo: 6v6 means eleven bots, five with the human.
	{
		const TArray<int32> Bots = ComputeBotsPerSquad(2, 6, { 1 }, FillAllSlots);
		TestEqual(TEXT("solo FIRE DRILL: five team-mates"), Bots[0], 5);
		TestEqual(TEXT("solo FIRE DRILL: six opponents"), Bots[1], 6);
	}

	// A cap spreads out: the emptiest squad gets the next bot, so ?Bots=2 for a
	// solo player puts one opponent in each other squad, not two in one.
	{
		const TArray<int32> Bots = ComputeBotsPerSquad(3, 4, { 1, 0, 0 }, 2);
		TestEqual(TEXT("?Bots=2 solo: two bots"), Sum(Bots), 2);
		TestEqual(TEXT("?Bots=2 solo: none with the human"), Bots[0], 0);
		TestEqual(TEXT("?Bots=2 solo: one in squad 2"), Bots[1], 1);
		TestEqual(TEXT("?Bots=2 solo: one in squad 3"), Bots[2], 1);
	}

	// A cap larger than the empty slots stops at the slots.
	TestEqual(TEXT("a cap above the empty slots fills them and stops"),
		Sum(ComputeBotsPerSquad(3, 4, { 3, 3, 3 }, 10)), 3);

	// Degenerate input never produces bots or crashes.
	TestEqual(TEXT("zero squads: no plan"), ComputeBotsPerSquad(0, 4, {}, FillAllSlots).Num(), 0);
	TestEqual(TEXT("a zero cap: no bots"), Sum(ComputeBotsPerSquad(3, 4, { 0, 0, 0 }, 0)), 0);
	TestEqual(TEXT("an over-full squad takes nothing and gives nothing"),
		ComputeBotsPerSquad(2, 4, { 6, 0 }, FillAllSlots)[0], 0);

	// Four squads of eight would be 32 slots: the ceiling holds.
	TestEqual(TEXT("an empty 4x8 match stops at the ceiling"),
		Sum(ComputeBotsPerSquad(4, 8, {}, FillAllSlots)), MaxBotsPerMatch);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamBotYieldTest, "OneBraham.Bots.YieldToHuman",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamBotYieldTest::RunTest(const FString& Parameters)
{
	using namespace OneBrahamBotRules;

	TestEqual(TEXT("no bots: nobody yields"), PickSquadToYieldBot({ 4, 4, 4 }, { 0, 0, 0 }), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("the squad with most bots yields"), PickSquadToYieldBot({ 1, 0, 2 }, { 3, 4, 2 }), 1);
	TestEqual(TEXT("equal bots: the squad with fewer humans yields"), PickSquadToYieldBot({ 2, 1, 0 }, { 2, 2, 1 }), 1);
	TestEqual(TEXT("a full tie goes to the lower squad"), PickSquadToYieldBot({ 0, 0 }, { 3, 3 }), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamBotBehaviourRulesTest, "OneBraham.Bots.BehaviourRules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamBotBehaviourRulesTest::RunTest(const FString& Parameters)
{
	using namespace OneBrahamBotRules;

	// Stall: only after the full window, and only when the bot has barely moved.
	TestFalse(TEXT("too soon to call a stall"), IsStalled(0.f, StallSeconds - 0.1f));
	TestTrue(TEXT("standing still for the window is a stall"), IsStalled(20.f, StallSeconds));
	TestFalse(TEXT("walking is not a stall"), IsStalled(StallDistanceCm + 1.f, StallSeconds + 2.f));

	// Objectives: ASCENT's one, FIRE DRILL's three spread over a team.
	TestEqual(TEXT("no objectives: none picked"), PickObjectiveIndex(0, 1, 2), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("one objective: everybody's"), PickObjectiveIndex(1, 2, 3), 0);
	{
		TSet<int32> Picked;
		for (int32 Ordinal = 0; Ordinal < 6; ++Ordinal)
		{
			const int32 Index = PickObjectiveIndex(3, 0, Ordinal);
			TestTrue(TEXT("an objective index is in range"), Index >= 0 && Index < 3);
			Picked.Add(Index);
		}
		TestEqual(TEXT("a team of six covers all three control points"), Picked.Num(), 3);
	}

	// Skill stays practice-grade: never hopeless, never a decent human.
	for (int32 Ordinal = 0; Ordinal < 16; ++Ordinal)
	{
		const float Skill = SkillForBot(Ordinal);
		TestTrue(*FString::Printf(TEXT("bot %d skill %.2f is mediocre"), Ordinal, Skill), Skill >= 0.25f && Skill <= 0.7f);
	}
	TestNotEqual(TEXT("a squad is not four copies of one player"), SkillForBot(0), SkillForBot(1));

	// The match is for its humans: it ends when the last one is out, but only
	// with bots left to play on, a human who was ever in a squad, and no
	// extraction running (a squad on the winch can still win it).
	TestTrue(TEXT("solo player dead, bots alive: the match ends"), ShouldEndWithNoHumansLeft(1, 0, 11, false));
	TestFalse(TEXT("a human still in the fight: it goes on"), ShouldEndWithNoHumansLeft(3, 1, 9, false));
	TestFalse(TEXT("no bots: the squad rules decide, not this one"), ShouldEndWithNoHumansLeft(4, 0, 0, false));
	TestFalse(TEXT("nobody human ever in a squad (a server waiting for players): it goes on"), ShouldEndWithNoHumansLeft(0, 0, 12, false));
	TestFalse(TEXT("an extraction under way: it goes on"), ShouldEndWithNoHumansLeft(2, 0, 6, true));

	// Names: prefixed, and unique across a full match.
	{
		TSet<FString> Names;
		for (int32 Ordinal = 0; Ordinal < MaxBotsPerMatch * 2; ++Ordinal)
		{
			const FString Name = MakeBotName(Ordinal);
			TestTrue(*FString::Printf(TEXT("'%s' reads as a bot"), *Name), Name.StartsWith(TEXT("BOT ")));
			Names.Add(Name);
		}
		TestEqual(TEXT("bot names are unique"), Names.Num(), MaxBotsPerMatch * 2);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamBotSettingsFieldsTest, "OneBraham.Bots.SettingsFields",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamBotSettingsFieldsTest::RunTest(const FString& Parameters)
{
	using namespace OneBrahamBotRules;

	// Missing asset: the caller's defaults stand.
	{
		bool bFill = true;
		int32 MaxBots = 7;
		TestFalse(TEXT("no asset: nothing read"), ReadSettingsBotFill(nullptr, bFill, MaxBots));
		TestTrue(TEXT("no asset: fill default kept"), bFill);
		TestEqual(TEXT("no asset: cap default kept"), MaxBots, 7);
	}

	// A real asset: whatever fields the class has are read, and an asset class
	// without them leaves the per-mode default in place rather than turning bots
	// off. Both outcomes are legal; a crash or a garbage value is not.
	{
		UOneBrahamMatchSettingsAsset* Settings = NewObject<UOneBrahamMatchSettingsAsset>(GetTransientPackage());
		bool bFill = true;
		int32 MaxBots = FillAllSlots;
		const bool bFound = ReadSettingsBotFill(Settings, bFill, MaxBots);
		if (!bFound)
		{
			TestTrue(TEXT("an asset without bFillWithBots keeps the mode default"), bFill);
		}
		TestTrue(TEXT("MaxBots reads as fill-all or a sane cap"), MaxBots >= FillAllSlots && MaxBots <= 64);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamBotClimbRouteTest, "OneBraham.Bots.ClimbRoutes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamBotClimbRouteTest::RunTest(const FString& Parameters)
{
	using namespace OneBrahamBotRules;

	// Each squad leaves the ground floor its own way, the set with the shortest
	// total walk. Rows are squads, columns stair A, spiral B, escalators - the
	// solo-ASCENT spawns: Alpha by the escalators, Bravo in reception's east
	// end, Charlie north of the core.
	{
		const TArray<TArray<float>> Distances = {
			{ 1900.f, 2700.f, 700.f },
			{ 3100.f, 2400.f, 3500.f },
			{ 1100.f, 1100.f, 2900.f } };
		const TArray<EClimbRoute> Opening = AssignOpeningRoutes(Distances);
		TestEqual(TEXT("one opening route per squad"), Opening.Num(), 3);
		TestTrue(TEXT("Alpha, by the escalators, takes them"), Opening[0] == EClimbRoute::Escalators);
		TestTrue(TEXT("Bravo, east, takes the spiral"), Opening[1] == EClimbRoute::SpiralB);
		TestTrue(TEXT("Charlie takes the stair left over"), Opening[2] == EClimbRoute::FireStairA);
	}
	{
		// Two squads nearest the same stair: one of them goes the other way.
		const TArray<TArray<float>> Distances = { { 500.f, 2000.f, 3000.f }, { 600.f, 2500.f, 2600.f } };
		const TArray<EClimbRoute> Opening = AssignOpeningRoutes(Distances);
		TestTrue(TEXT("two squads never share a way up while a third is free"), Opening[0] != Opening[1]);
		TestTrue(TEXT("the cheaper split overall, not first come first served"), Opening[0] == EClimbRoute::SpiralB && Opening[1] == EClimbRoute::FireStairA);
	}
	{
		// Four squads, three ways: exactly one is shared.
		const TArray<TArray<float>> Distances = { { 100.f, 900.f, 900.f }, { 900.f, 100.f, 900.f }, { 900.f, 900.f, 100.f }, { 200.f, 900.f, 900.f } };
		const TArray<EClimbRoute> Opening = AssignOpeningRoutes(Distances);
		TSet<EClimbRoute> Used(Opening);
		TestEqual(TEXT("four squads use all three ways up"), Used.Num(), 3);
		TestEqual(TEXT("no squads, no plan"), AssignOpeningRoutes({}).Num(), 0);
	}

	// Above the ground a squad keeps favouring its stair; an escalator squad has no favourite.
	TestTrue(TEXT("the opening route is favoured on the ground"), PreferredRoute(EClimbRoute::Escalators, 0) == EClimbRoute::Escalators);
	TestTrue(TEXT("an escalator squad has no favourite above"), PreferredRoute(EClimbRoute::Escalators, 4) == EClimbRoute::None);
	TestTrue(TEXT("a stair squad keeps its stair"), PreferredRoute(EClimbRoute::SpiralB, 9) == EClimbRoute::SpiralB);

	auto Option = [](EClimbRoute Route, float Distance, int32 Enemies = 0, int32 Bodies = 0, int32 Friends = 0, float Blocked = 0.f, bool bUsable = true)
	{
		FClimbRouteOption Out;
		Out.Route = Route;
		Out.DistanceCm = Distance;
		Out.Enemies = Enemies;
		Out.Bodies = Bodies;
		Out.Friends = Friends;
		Out.RecentBlockSeconds = Blocked;
		Out.bUsable = bUsable;
		return Out;
	};

	// The squad's own stair wins a close call...
	{
		const FClimbRouteOption Options[] = { Option(EClimbRoute::FireStairA, 1400.f), Option(EClimbRoute::SpiralB, 900.f) };
		TestEqual(TEXT("the preferred stair wins a close call"), PickClimbRoute(Options, EClimbRoute::FireStairA, EClimbRoute::None), 0);
		TestEqual(TEXT("with no preference the nearer one wins"), PickClimbRoute(Options, EClimbRoute::None, EClimbRoute::None), 1);
	}

	// ...but not a doorway full of enemies or plugged by a body.
	{
		const FClimbRouteOption Enemies[] = { Option(EClimbRoute::FireStairA, 900.f, 3), Option(EClimbRoute::SpiralB, 1500.f) };
		TestEqual(TEXT("three enemies in the preferred door: the other stair"), PickClimbRoute(Enemies, EClimbRoute::FireStairA, EClimbRoute::None), 1);

		const FClimbRouteOption Body[] = { Option(EClimbRoute::FireStairA, 900.f, 0, 1), Option(EClimbRoute::SpiralB, 1400.f) };
		TestEqual(TEXT("a body in the preferred door: the other stair"), PickClimbRoute(Body, EClimbRoute::FireStairA, EClimbRoute::None), 1);

		const FClimbRouteOption Friends[] = { Option(EClimbRoute::FireStairA, 900.f, 0, 0, 3), Option(EClimbRoute::SpiralB, 1500.f) };
		TestEqual(TEXT("team-mates queueing in it do not turn a squad away"), PickClimbRoute(Friends, EClimbRoute::FireStairA, EClimbRoute::None), 0);
	}

	// A shut way in is never taken; nothing open is "no way up".
	{
		const FClimbRouteOption Shut[] = { Option(EClimbRoute::FireStairA, 100.f, 0, 0, 0, 0.f, false), Option(EClimbRoute::SpiralB, 4000.f, 2, 1) };
		TestEqual(TEXT("a shutter down or a locked door is never chosen"), PickClimbRoute(Shut, EClimbRoute::FireStairA, EClimbRoute::FireStairA), 1);

		const FClimbRouteOption AllShut[] = { Option(EClimbRoute::FireStairA, 100.f, 0, 0, 0, 0.f, false), Option(EClimbRoute::SpiralB, 100.f, 0, 0, 0, 0.f, false) };
		TestEqual(TEXT("every way shut: none"), PickClimbRoute(AllShut, EClimbRoute::None, EClimbRoute::None), static_cast<int32>(INDEX_NONE));
		TestEqual(TEXT("no options: none"), PickClimbRoute(TConstArrayView<FClimbRouteOption>(), EClimbRoute::None, EClimbRoute::None), static_cast<int32>(INDEX_NONE));
	}

	// A remembered stall turns a squad away, and the memory fades.
	{
		const FClimbRouteOption Fresh[] = { Option(EClimbRoute::SpiralB, 900.f, 0, 0, 0, RouteBlockMemorySeconds), Option(EClimbRoute::FireStairA, 1700.f) };
		TestEqual(TEXT("a fresh stall on the current stair: re-route"), PickClimbRoute(Fresh, EClimbRoute::SpiralB, EClimbRoute::SpiralB), 1);

		const FClimbRouteOption Faded[] = { Option(EClimbRoute::SpiralB, 900.f, 0, 0, 0, 1.f), Option(EClimbRoute::FireStairA, 1700.f) };
		TestEqual(TEXT("a stall nearly forgotten: stay"), PickClimbRoute(Faded, EClimbRoute::SpiralB, EClimbRoute::SpiralB), 0);
	}

	// Stickiness: a small difference does not flip a squad between stairs.
	{
		const FClimbRouteOption Options[] = { Option(EClimbRoute::FireStairA, 1000.f), Option(EClimbRoute::SpiralB, 700.f) };
		TestEqual(TEXT("the stair the squad is on survives a small difference"), PickClimbRoute(Options, EClimbRoute::None, EClimbRoute::FireStairA), 0);
	}

	// Which routes climb from where.
	TestTrue(TEXT("the escalators climb from the ground floor"), RouteClimbsFromFloor(EClimbRoute::Escalators, 0));
	TestFalse(TEXT("the escalators stop at floor 1"), RouteClimbsFromFloor(EClimbRoute::Escalators, 1));
	TestTrue(TEXT("the fire stair climbs from floor 9"), RouteClimbsFromFloor(EClimbRoute::FireStairA, 9));
	TestFalse(TEXT("nothing climbs from the roof"), RouteClimbsFromFloor(EClimbRoute::SpiralB, OneBrahamConstants::FloorTop));
	TestFalse(TEXT("None is not a route"), RouteClimbsFromFloor(EClimbRoute::None, 3));

	// Legs: two floors, never past the objective; the escalators go to floor 1.
	TestEqual(TEXT("a leg is two floors"), ClimbTargetFloor(EClimbRoute::SpiralB, 4, 15), 6);
	TestEqual(TEXT("the last leg stops at the objective"), ClimbTargetFloor(EClimbRoute::FireStairA, 14, 15), 15);
	TestEqual(TEXT("on the objective's floor: stay"), ClimbTargetFloor(EClimbRoute::FireStairA, 15, 15), 15);
	TestEqual(TEXT("the escalators go to floor 1"), ClimbTargetFloor(EClimbRoute::Escalators, 0, 15), 1);

	// The tower points: the doors where the map builder put them.
	{
		TestEqual(TEXT("floor 1's slab is on the origin"), FloorBaseZ(1), 0.f);
		TestEqual(TEXT("the ground floor is a storey below"), FloorBaseZ(0), -400.f);

		const FVector A = RouteDoorPoint(EClimbRoute::FireStairA, 5);
		const FVector B = RouteDoorPoint(EClimbRoute::SpiralB, 5);
		TestEqual(TEXT("stair A's door is at -x"), A.X, static_cast<double>(-StairDoorX));
		TestEqual(TEXT("stair B's door is at +x"), B.X, static_cast<double>(StairDoorX));
		TestEqual(TEXT("door points are on their floor"), OneBrahamConstants::FloorIndexForZ(A.Z), 5);

		const FVector Landing = RouteLandingPoint(EClimbRoute::SpiralB, 7, 2);
		TestTrue(TEXT("a stair's landing is inside its stairwell"), IsOnRoute(EClimbRoute::SpiralB, Landing));
		TestEqual(TEXT("a stair's landing is on the floor asked for"), OneBrahamConstants::FloorIndexForZ(Landing.Z), 7);
		TestFalse(TEXT("B's landing is not in A"), IsOnRoute(EClimbRoute::FireStairA, Landing));

		const FVector Entry = RouteEntryPoint(EClimbRoute::FireStairA, 3, 1);
		TestTrue(TEXT("a stair's way in is out on the floor, not in the stairwell"), RouteAt(Entry) == EClimbRoute::None);
		TestTrue(TEXT("a stair's way in is in front of its door"), FVector::Dist2D(Entry, A) < 200.f);

		const FVector Top = RouteLandingPoint(EClimbRoute::Escalators, 0, 3);
		TestEqual(TEXT("the escalators arrive on floor 1"), OneBrahamConstants::FloorIndexForZ(Top.Z), 1);
		TestTrue(TEXT("on the escalators half way up is on the route"),
			IsOnRoute(EClimbRoute::Escalators, FVector(EscalatorLaneX(1), (EscalatorFootY + EscalatorTopY) * 0.5f, -200.f)));
		TestTrue(TEXT("the lift hall is on no route"), RouteAt(FVector(0.f, 0.f, 90.f)) == EClimbRoute::None);

		TSet<float> Lanes;
		for (int32 Slot = 0; Slot < 4; ++Slot)
		{
			const float X = EscalatorLaneX(Slot);
			TestTrue(TEXT("a lane is on the escalators"), X > EscalatorMinX && X < EscalatorMaxX);
			Lanes.Add(X);
		}
		TestEqual(TEXT("a squad of four uses all four lanes"), Lanes.Num(), 4);
	}

	TestEqual(TEXT("route names read in the log"), FString(ClimbRouteName(EClimbRoute::SpiralB)), FString(TEXT("SpiralB")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamBotClimbSquadTest, "OneBraham.Bots.ClimbSquad",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamBotClimbSquadTest::RunTest(const FString& Parameters)
{
	using namespace OneBrahamBotRules;

	// Departures: Alpha first, squads apart, members in a file, nobody waits long.
	TestEqual(TEXT("Alpha's first bot goes at once"), DepartureDelaySeconds(0, 0), 0.f);
	TestTrue(TEXT("Bravo leaves after Alpha"), DepartureDelaySeconds(1, 0) > DepartureDelaySeconds(0, 0));
	TestTrue(TEXT("Charlie leaves after Bravo"), DepartureDelaySeconds(2, 0) > DepartureDelaySeconds(1, 0));
	TestTrue(TEXT("a squad's members leave one after another"), DepartureDelaySeconds(1, 1) > DepartureDelaySeconds(1, 0));
	for (int32 Squad = 0; Squad < 3; ++Squad)
	{
		for (int32 Ordinal = 0; Ordinal < 4; ++Ordinal)
		{
			TestTrue(TEXT("nobody stands on the plaza for more than ten seconds"), DepartureDelaySeconds(Squad, Ordinal) <= 10.f);
		}
	}

	// A shared way off the ground floor: the later squad holds until the earlier one is through the door.
	{
		const TArray<int32> Ids = { 0, 1, 2 };
		const TArray<EClimbRoute> Own = { EClimbRoute::FireStairA, EClimbRoute::SpiralB, EClimbRoute::Escalators };
		const TArray<float> Walks = { 2100.f, 2500.f, 1100.f };
		for (const float Hold : SharedOpeningHolds(Ids, Own, Walks))
		{
			TestEqual(TEXT("a squad with a door of its own does not hold"), Hold, 0.f);
		}

		// The solo-ASCENT spawns with the escalators a dead end: Bravo and Charlie share the spiral.
		const TArray<EClimbRoute> Shared = { EClimbRoute::FireStairA, EClimbRoute::SpiralB, EClimbRoute::SpiralB };
		const TArray<float> Holds = SharedOpeningHolds(Ids, Shared, Walks);
		TestEqual(TEXT("Alpha, alone on its stair, does not hold"), Holds[0], 0.f);
		TestEqual(TEXT("Bravo, first to the shared door, does not hold"), Holds[1], 0.f);
		TestTrue(TEXT("Charlie, nearer the shared door but later, holds"), Holds[2] > 0.f);
		TestEqual(TEXT("Charlie reaches the door the gap behind Bravo's last bot"),
			OpeningArrivalSeconds(2, 0, Walks[2], Holds[2]),
			OpeningArrivalSeconds(1, OneBrahamConstants::SquadSize - 1, Walks[1], Holds[1]) + SharedOpeningGapSeconds, 0.01f);

		// The order squads are listed in does not matter.
		const TArray<float> Reversed = SharedOpeningHolds({ 2, 1, 0 }, { EClimbRoute::SpiralB, EClimbRoute::SpiralB, EClimbRoute::FireStairA }, { 1100.f, 2500.f, 2100.f });
		TestEqual(TEXT("the same hold whatever the order"), Reversed[0], Holds[2], 0.01f);
		TestEqual(TEXT("the earlier squad still does not hold"), Reversed[1], 0.f);

		// A squad already behind does not hold; nobody holds longer than the cap.
		const TArray<float> Behind = SharedOpeningHolds({ 0, 1 }, { EClimbRoute::SpiralB, EClimbRoute::SpiralB }, { 0.f, 6000.f });
		TestEqual(TEXT("a squad that arrives well behind anyway does not hold"), Behind[1], 0.f);
		const TArray<float> Capped = SharedOpeningHolds({ 0, 1 }, { EClimbRoute::SpiralB, EClimbRoute::SpiralB }, { 6000.f, 0.f });
		TestEqual(TEXT("the lockdown is coming: the hold is capped"), Capped[1], MaxSharedOpeningHoldSeconds);
		TestEqual(TEXT("no squads, no holds"), SharedOpeningHolds({}, {}, {}).Num(), 0);
	}

	// The squad: three floors ahead of the last climber, wait - for a while.
	TestFalse(TEXT("two floors ahead: climb on"), ShouldWaitForSquad(6, 4, 0.f, false));
	TestTrue(TEXT("three floors ahead: wait"), ShouldWaitForSquad(7, 4, 0.f, false));
	TestFalse(TEXT("waited long enough on this floor: climb on"), ShouldWaitForSquad(7, 4, MaxSquadWaitSeconds, false));
	TestFalse(TEXT("the lockdown is coming: never wait"), ShouldWaitForSquad(9, 2, 0.f, true));
	TestFalse(TEXT("nobody left to wait for"), ShouldWaitForSquad(9, INDEX_NONE, 0.f, false));
	TestFalse(TEXT("the last climber does not wait for itself"), ShouldWaitForSquad(4, 4, 0.f, false));

	// A jam is a jam only after the window, and only standing still.
	TestFalse(TEXT("too soon to call a jam"), IsClimbBlocked(0.f, ClimbBlockedSeconds - 0.5f));
	TestTrue(TEXT("four seconds pressed into a door is a jam"), IsClimbBlocked(30.f, ClimbBlockedSeconds));
	TestFalse(TEXT("climbing is not a jam"), IsClimbBlocked(400.f, ClimbBlockedSeconds));
	TestTrue(TEXT("a jam is called before the controller's own stall"), ClimbBlockedSeconds < StallSeconds);

	// The lockdown: urgent below the highest occupied floor when the next stage is close.
	TestFalse(TEXT("no director, no hurry"), IsClimbUrgent(false, false, 0.f, 2, 9, false));
	TestFalse(TEXT("the next stage a long way off: no hurry"), IsClimbUrgent(true, false, 80.f, 2, 9, false));
	TestTrue(TEXT("the next stage close, below the leaders: hurry"), IsClimbUrgent(true, false, LockdownUrgencySeconds - 1.f, 2, 9, false));
	TestTrue(TEXT("the warning playing, below the leaders: hurry"), IsClimbUrgent(true, true, 50.f, 2, 9, false));
	TestFalse(TEXT("the leaders themselves are not sealed under anyone"), IsClimbUrgent(true, true, 5.f, 9, 9, false));
	TestTrue(TEXT("a purged floor is always urgent"), IsClimbUrgent(false, false, 0.f, 9, 9, true));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamBotStalemateTest, "OneBraham.Bots.ClimbStalemate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamBotStalemateTest::RunTest(const FString& Parameters)
{
	using namespace OneBrahamBotRules;

	// A fight gets its chance.
	TestFalse(TEXT("a fresh fight is fought"), ShouldBreakOffFight(2.f, 2.f, false));
	TestFalse(TEXT("a short quiet spell early on is not a stalemate"), ShouldBreakOffFight(MinFightSecondsBeforeBreakOff - 1.f, StalemateQuietSeconds + 1.f, false));

	// A stalemate in a doorway is broken off.
	TestTrue(TEXT("ten seconds in, nobody hit for eight: break off"), ShouldBreakOffFight(MinFightSecondsBeforeBreakOff, StalemateQuietSeconds, false));
	TestFalse(TEXT("hits still landing: keep fighting"), ShouldBreakOffFight(20.f, 1.f, false));

	// Nothing goes on forever, but a live exchange is finished first.
	TestTrue(TEXT("a long fight with a lull is broken off"), ShouldBreakOffFight(MaxFightSeconds, ActiveExchangeSeconds, false));
	TestFalse(TEXT("a long fight still trading hits is not"), ShouldBreakOffFight(MaxFightSeconds + 5.f, 0.5f, false));

	// The lockdown waits for nobody.
	TestFalse(TEXT("urgent: a few seconds of fighting first"), ShouldBreakOffFight(UrgentFightSeconds - 1.f, 5.f, true));
	TestTrue(TEXT("urgent: then go"), ShouldBreakOffFight(UrgentFightSeconds, ActiveExchangeSeconds, true));
	TestFalse(TEXT("urgent but mid-exchange: finish it"), ShouldBreakOffFight(UrgentFightSeconds + 3.f, 0.5f, true));

	// Close and in sight: stand and fight. Far, or out of sight: shoot on the way past.
	TestTrue(TEXT("an enemy across the lobby is fought"), ShouldStandAndFight(600.f, true));
	TestFalse(TEXT("an enemy across the floor is shot at on the move"), ShouldStandAndFight(StandAndFightRangeCm + 300.f, true));
	TestFalse(TEXT("an enemy out of sight is not stood for"), ShouldStandAndFight(400.f, false));

	// Ordering of the knobs the director relies on.
	TestTrue(TEXT("a break-off climbs long enough to leave the doorway"), BreakOffSeconds >= 6.f);
	TestTrue(TEXT("urgency cuts a fight shorter than a stalemate"), UrgentFightSeconds < MinFightSecondsBeforeBreakOff);
	TestTrue(TEXT("the hard cap is longer than the stalemate test"), MaxFightSeconds > MinFightSecondsBeforeBreakOff + StalemateQuietSeconds);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
