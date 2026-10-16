// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamBotTests.cpp
// ---------------------------------------------------------------------------
// The rules that decide how many bots a match gets and where they sit
// (AI/OneBrahamBotRules.h), pinned without a running match: which modes fill
// by default, how `?Bots=N` and the match settings combine, how a fill spreads
// over the squads, which squad gives a bot up when a human joins, when the
// match ends because no human is left in it, and the stall test that walks a
// stuck bot out of a dead end.

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

#endif // WITH_AUTOMATION_TESTS
