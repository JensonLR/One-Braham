// Copyright (c) One Braham. All Rights Reserved.
//
// OneBraham.Match
//
// Two match faults that a listen-server playtest hides, because the host is
// the server and sees everything right:
//
//   1. ASCENT's hard end at 35 minutes was a HUD countdown to nothing.
//      `AOneBrahamGameMode::Tick` enforced a time limit for FIRE DRILL only, so
//      Docs/06 section 7's "Time out" ending - nobody extracts, everybody lost -
//      could not happen, and an ASCENT match outlived its own clock.
//   2. `AOneBrahamGameState::MatchSettings` was never replicated, so on every
//      remote client `GetMatchSettings()` returned null and each component that
//      read a tuned value fell back to its own hard-coded guess. Clients now
//      resolve the settings from the replicated mode (and the replicated asset
//      path, when the server's settings are a loadable asset).
//
// The time-out is decided by two pure statics on the GameMode so the decision
// is pinned here without a world.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"

#include "Core/OneBrahamGameMode.h"
#include "Core/OneBrahamGameState.h"
#include "Data/OneBrahamMatchSettingsAsset.h"
#include "OneBraham.h"

#if WITH_AUTOMATION_TESTS

// ===========================================================================
//  When the clock has run out
// ===========================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamMatchTimeLimitTest, "OneBraham.Match.TimeLimit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamMatchTimeLimitTest::RunTest(const FString& Parameters)
{
	// ASCENT's 2100 s: the limit is the limit, not a second after it.
	TestFalse(TEXT("one second short is still a match"),
		AOneBrahamGameMode::HasMatchTimeLimitExpired(2100.f, 2099.f, EOneBrahamMatchPhase::Active));
	TestTrue(TEXT("the limit itself ends it"),
		AOneBrahamGameMode::HasMatchTimeLimitExpired(2100.f, 2100.f, EOneBrahamMatchPhase::Active));

	// The helicopter does not stop the clock: a hard end is hard during an
	// extraction too.
	TestTrue(TEXT("the clock runs through Extraction"),
		AOneBrahamGameMode::HasMatchTimeLimitExpired(2100.f, 2200.f, EOneBrahamMatchPhase::Extraction));

	// Warmup and a finished match are not timed, the same phases the game state
	// does not accumulate PhaseElapsedSeconds in.
	TestFalse(TEXT("warmup is not timed"),
		AOneBrahamGameMode::HasMatchTimeLimitExpired(600.f, 9999.f, EOneBrahamMatchPhase::Warmup));
	TestFalse(TEXT("a complete match does not time out again"),
		AOneBrahamGameMode::HasMatchTimeLimitExpired(600.f, 9999.f, EOneBrahamMatchPhase::Complete));

	// 0 is unlimited, not "already over".
	TestFalse(TEXT("no limit never expires"),
		AOneBrahamGameMode::HasMatchTimeLimitExpired(0.f, 9999.f, EOneBrahamMatchPhase::Active));

	return true;
}

// ===========================================================================
//  Who wins when it has
// ===========================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamMatchTimeOutOutcomeTest, "OneBraham.Match.TimeOutOutcome",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamMatchTimeOutOutcomeTest::RunTest(const FString& Parameters)
{
	// ASCENT: Docs/06 section 7, "nobody extracts, everybody lost". No squad is
	// ranked, whatever it had done, and no points enter into it.
	{
		const FOneBrahamTimeOutOutcome Outcome =
			AOneBrahamGameMode::DecideTimeOutOutcome(EOneBrahamMatchMode::Ascent, TArray<int32>{ 3, 9, 1 });
		TestTrue(TEXT("ASCENT time-out: nobody got out"), Outcome.bNoSurvivors);
		TestTrue(TEXT("ASCENT time-out: no winner"), Outcome.WinningTeam == EOneBrahamTeam::None);
		TestFalse(TEXT("ASCENT time-out is not a draw"), Outcome.bDraw);
	}

	// NIGHT SHIFT has no points either and takes the same ending.
	{
		const FOneBrahamTimeOutOutcome Outcome =
			AOneBrahamGameMode::DecideTimeOutOutcome(EOneBrahamMatchMode::NightShift, TArray<int32>{ 0 });
		TestTrue(TEXT("NIGHT SHIFT time-out: nobody got out"), Outcome.bNoSurvivors);
		TestTrue(TEXT("NIGHT SHIFT time-out: no winner"), Outcome.WinningTeam == EOneBrahamTeam::None);
	}

	// FIRE DRILL: most points when the drill ends, either side.
	{
		const FOneBrahamTimeOutOutcome Outcome =
			AOneBrahamGameMode::DecideTimeOutOutcome(EOneBrahamMatchMode::FireDrill, TArray<int32>{ 140, 90 });
		TestTrue(TEXT("FIRE DRILL: Internal Staff ahead wins"), Outcome.WinningTeam == EOneBrahamTeam::InternalStaff);
		TestFalse(TEXT("FIRE DRILL win: somebody survived"), Outcome.bNoSurvivors);
		TestFalse(TEXT("FIRE DRILL win: not a draw"), Outcome.bDraw);
	}
	{
		const FOneBrahamTimeOutOutcome Outcome =
			AOneBrahamGameMode::DecideTimeOutOutcome(EOneBrahamMatchMode::FireDrill, TArray<int32>{ 90, 140 });
		TestTrue(TEXT("FIRE DRILL: External Contractors ahead wins"), Outcome.WinningTeam == EOneBrahamTeam::ExternalContractors);
		TestEqual(TEXT("FIRE DRILL: the winner is named"), Outcome.WinningSquadName, FString(TEXT("External Contractors")));
	}

	// Level is a draw, never a win for team 0 because it was checked first.
	{
		const FOneBrahamTimeOutOutcome Outcome =
			AOneBrahamGameMode::DecideTimeOutOutcome(EOneBrahamMatchMode::FireDrill, TArray<int32>{ 120, 120 });
		TestTrue(TEXT("FIRE DRILL level: a draw"), Outcome.bDraw);
		TestTrue(TEXT("FIRE DRILL level: no winner"), Outcome.WinningTeam == EOneBrahamTeam::None);
		TestFalse(TEXT("FIRE DRILL level: not 'no survivors'"), Outcome.bNoSurvivors);
	}
	{
		const FOneBrahamTimeOutOutcome Outcome =
			AOneBrahamGameMode::DecideTimeOutOutcome(EOneBrahamMatchMode::FireDrill, TArray<int32>{ 0, 0 });
		TestTrue(TEXT("FIRE DRILL nil-nil: a draw"), Outcome.bDraw);
	}

	// No score array at all must not crash or crown somebody.
	{
		const FOneBrahamTimeOutOutcome Outcome =
			AOneBrahamGameMode::DecideTimeOutOutcome(EOneBrahamMatchMode::FireDrill, TArray<int32>());
		TestTrue(TEXT("FIRE DRILL without scores: a draw"), Outcome.bDraw);
		TestTrue(TEXT("FIRE DRILL without scores: no winner"), Outcome.WinningTeam == EOneBrahamTeam::None);
	}

	return true;
}

// ===========================================================================
//  What the server tells a client about its settings
// ===========================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamMatchSettingsPathTest, "OneBraham.Match.SettingsReplicationPath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamMatchSettingsPathTest::RunTest(const FString& Parameters)
{
	// The GameInstance's runtime fallback is a NewObject with no package of its
	// own. It has no path a client could load, so none is replicated and the
	// client resolves by mode instead - a reference to it would never arrive.
	UOneBrahamMatchSettingsAsset* Transient = NewObject<UOneBrahamMatchSettingsAsset>(GetTransientPackage());
	TestTrue(TEXT("a transient fallback replicates no path"),
		AOneBrahamGameState::GetReplicableSettingsPath(Transient).IsNull());
	TestTrue(TEXT("no settings replicate no path"),
		AOneBrahamGameState::GetReplicableSettingsPath(nullptr).IsNull());

	// The real fallback is outered to the GameInstance, not a package. Even
	// public, an object inside another object is not something a client loads.
	UObject* FakeGameInstance = NewObject<UOneBrahamMatchSettingsAsset>(GetTransientPackage());
	UOneBrahamMatchSettingsAsset* Nested = NewObject<UOneBrahamMatchSettingsAsset>(FakeGameInstance, NAME_None, RF_Public);
	TestTrue(TEXT("a fallback outered to another object replicates no path"),
		AOneBrahamGameState::GetReplicableSettingsPath(Nested).IsNull());

	// The authored asset, when this checkout has it, replicates its own path.
	if (UOneBrahamMatchSettingsAsset* Authored = LoadObject<UOneBrahamMatchSettingsAsset>(nullptr,
		TEXT("/Game/OneBraham/Data/DA_MatchSettings_Ascent.DA_MatchSettings_Ascent"), nullptr, LOAD_NoWarn | LOAD_Quiet))
	{
		const FSoftObjectPath Path = AOneBrahamGameState::GetReplicableSettingsPath(Authored);
		TestFalse(TEXT("the authored ASCENT asset replicates a path"), Path.IsNull());
		TestTrue(TEXT("and it is the asset's own path"), Path.ResolveObject() == Authored);
	}
	else
	{
		AddInfo(TEXT("DA_MatchSettings_Ascent is not in this checkout; authored-asset half skipped."));
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
