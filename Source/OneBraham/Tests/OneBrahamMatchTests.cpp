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
//
// The rest of the end of a match is pinned the same way, from the rules in
// Extraction/OneBrahamMatchEnding.h: which ending an elimination is (the last
// squad standing did not exist, and a wipe during an extraction waited for the
// helicopter), what happens after the end screen, whether a finished match's
// numbers agree with its ending (the -OBSmokeFullMatch verdict), and the end
// screen's own words.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"

#include "Core/OneBrahamGameMode.h"
#include "Core/OneBrahamGameState.h"
#include "Core/OneBrahamGameInstance.h"
#include "Extraction/OneBrahamMatchEnding.h"
#include "UI/OneBrahamMatchEndWidget.h"
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

// ===========================================================================
//  Who is left, and what that ends as
// ===========================================================================
namespace
{
	/** A squad of four with this many in the fight and this many on the wire. */
	FOneBrahamSquadStanding OBSquad(int32 SquadId, int32 InFight, int32 Extracted = 0, int32 Members = 4)
	{
		FOneBrahamSquadStanding Squad;
		Squad.SquadId = SquadId;
		Squad.Members = Members;
		Squad.InFight = InFight;
		Squad.Extracted = Extracted;
		return Squad;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamMatchEliminationEndingTest, "OneBraham.Match.EliminationEnding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamMatchEliminationEndingTest::RunTest(const FString& Parameters)
{
	using namespace OneBrahamMatchEnd;
	const EOneBrahamMatchMode Ascent = EOneBrahamMatchMode::Ascent;

	// Three squads still in it: nothing ends.
	{
		const FOneBrahamEndVerdict Verdict = DecideEliminationEnding(Ascent,
			{ OBSquad(0, 2), OBSquad(1, 4), OBSquad(2, 1) }, INDEX_NONE, 0, 0, 7);
		TestFalse(TEXT("three squads in the fight: the match goes on"), Verdict.bEnds);
	}

	// Two out, one left: the last squad standing, and it is that squad.
	{
		const FOneBrahamEndVerdict Verdict = DecideEliminationEnding(Ascent,
			{ OBSquad(0, 0), OBSquad(1, 3), OBSquad(2, 0) }, INDEX_NONE, 0, 0, 3);
		TestTrue(TEXT("one squad left: the match ends"), Verdict.bEnds);
		TestTrue(TEXT("one squad left: last squad standing"), Verdict.Ending == EOneBrahamMatchEnding::LastSquadStanding);
		TestEqual(TEXT("one squad left: it wins"), Verdict.WinningSquadId, 1);
	}

	// The last squad is choking in the purge: the building decides, not this check.
	{
		FOneBrahamSquadStanding Gassed = OBSquad(1, 2);
		Gassed.OnPurgedFloor = 2;
		const FOneBrahamEndVerdict Verdict = DecideEliminationEnding(Ascent,
			{ OBSquad(0, 0), Gassed, OBSquad(2, 0) }, INDEX_NONE, 0, 0, 2);
		TestFalse(TEXT("the last squad all in the purge is not the winner yet"), Verdict.bEnds);

		Gassed.OnPurgedFloor = 1;
		const FOneBrahamEndVerdict Climbing = DecideEliminationEnding(Ascent,
			{ OBSquad(0, 0), Gassed, OBSquad(2, 0) }, INDEX_NONE, 0, 0, 2);
		TestTrue(TEXT("one of them above the purge: last squad standing"),
			Climbing.Ending == EOneBrahamMatchEnding::LastSquadStanding);
	}

	// The last squad is on the wire: the winch decides, not this check.
	{
		const FOneBrahamEndVerdict Verdict = DecideEliminationEnding(Ascent,
			{ OBSquad(0, 0), OBSquad(1, 3), OBSquad(2, 0) }, 1, 0, 0, 3);
		TestFalse(TEXT("the last squad holding the winch extracts or fails first"), Verdict.bEnds);
	}

	// ... but a winch called by a squad that has since gone does not hold it open.
	{
		const FOneBrahamEndVerdict Verdict = DecideEliminationEnding(Ascent,
			{ OBSquad(0, 0), OBSquad(1, 3), OBSquad(2, 0) }, 2, 0, 0, 3);
		TestTrue(TEXT("another squad's dead winch: last squad standing"), Verdict.Ending == EOneBrahamMatchEnding::LastSquadStanding);
	}

	// Nobody left anywhere: no survivors, extraction or not - there is nobody to finish it.
	{
		const FOneBrahamEndVerdict Verdict = DecideEliminationEnding(Ascent,
			{ OBSquad(0, 0), OBSquad(1, 0), OBSquad(2, 0) }, 1, 0, 0, 0);
		TestTrue(TEXT("everybody out: the match ends"), Verdict.bEnds);
		TestTrue(TEXT("everybody out: no survivors"), Verdict.Ending == EOneBrahamMatchEnding::NoSurvivors);
		TestEqual(TEXT("no survivors has no winner"), Verdict.WinningSquadId, static_cast<int32>(INDEX_NONE));
	}

	// One squad in a one-squad match (a solo run with no bots) is not a win at the start.
	{
		const FOneBrahamEndVerdict Verdict = DecideEliminationEnding(Ascent,
			{ OBSquad(0, 1, 0, 1), OBSquad(1, 0, 0, 0), OBSquad(2, 0, 0, 0) }, INDEX_NONE, 1, 1, 0);
		TestFalse(TEXT("one populated squad is not 'the last standing'"), Verdict.bEnds);
	}

	// Empty squads are not squads.
	{
		const FOneBrahamEndVerdict Verdict = DecideEliminationEnding(Ascent,
			{ OBSquad(0, 0, 0, 0), OBSquad(1, 0, 0, 0) }, INDEX_NONE, 0, 0, 0);
		TestFalse(TEXT("an empty roster is a lobby"), Verdict.bEnds);
	}

	// Every human out, two bot squads still fighting: the staff are withdrawn.
	{
		const FOneBrahamEndVerdict Verdict = DecideEliminationEnding(Ascent,
			{ OBSquad(0, 0), OBSquad(1, 2), OBSquad(2, 3) }, INDEX_NONE, 1, 0, 5);
		TestTrue(TEXT("humans out, bots on: staff withdrawn"), Verdict.Ending == EOneBrahamMatchEnding::StaffWithdrawn);
	}
	{
		const FOneBrahamEndVerdict Verdict = DecideEliminationEnding(Ascent,
			{ OBSquad(0, 0), OBSquad(1, 2), OBSquad(2, 3) }, 2, 1, 0, 5);
		TestFalse(TEXT("humans out, a bot extraction live: let it run"), Verdict.bEnds);
	}
	{
		const FOneBrahamEndVerdict Verdict = DecideEliminationEnding(Ascent,
			{ OBSquad(0, 1), OBSquad(1, 2), OBSquad(2, 3) }, INDEX_NONE, 0, 0, 6);
		TestFalse(TEXT("a match of bots alone (the full-match smoke run) plays on"), Verdict.bEnds);
	}

	// FIRE DRILL respawns: never an elimination ending.
	{
		const FOneBrahamEndVerdict Verdict = DecideEliminationEnding(EOneBrahamMatchMode::FireDrill,
			{ OBSquad(0, 0, 0, 6), OBSquad(1, 0, 0, 6) }, INDEX_NONE, 1, 0, 0);
		TestFalse(TEXT("FIRE DRILL does not end by elimination"), Verdict.bEnds);
	}

	// The clock's endings.
	TestTrue(TEXT("ASCENT time-out is the time out"),
		EndingForTimeOut(Ascent, false) == EOneBrahamMatchEnding::TimeOut);
	TestTrue(TEXT("FIRE DRILL level is a draw"),
		EndingForTimeOut(EOneBrahamMatchMode::FireDrill, true) == EOneBrahamMatchEnding::Draw);
	TestTrue(TEXT("FIRE DRILL with a leader is a time out with a winner"),
		EndingForTimeOut(EOneBrahamMatchMode::FireDrill, false) == EOneBrahamMatchEnding::TimeOut);

	return true;
}

// ===========================================================================
//  After the end screen
// ===========================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamMatchPostMatchTest, "OneBraham.Match.PostMatch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamMatchPostMatchTest::RunTest(const FString& Parameters)
{
	using namespace OneBrahamMatchEnd;

	TestTrue(TEXT("a dedicated server restarts the match"),
		DecidePostMatchAction(true, true) == EOneBrahamPostMatchAction::RestartMatch);
	TestTrue(TEXT("a dedicated server restarts even without a menu map"),
		DecidePostMatchAction(true, false) == EOneBrahamPostMatchAction::RestartMatch);
	TestTrue(TEXT("a listen host or standalone goes to the menu"),
		DecidePostMatchAction(false, true) == EOneBrahamPostMatchAction::ReturnToMenu);
	TestTrue(TEXT("no menu map in the build: the map reloads"),
		DecidePostMatchAction(false, false) == EOneBrahamPostMatchAction::ReloadMap);

	TestEqual(TEXT("the menu map's path"), FString(MainMenuMapPath()), FString(TEXT("/Game/OneBraham/Maps/L_MainMenu")));

	return true;
}

// ===========================================================================
//  Does a finished match agree with itself
// ===========================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamMatchResultCoherenceTest, "OneBraham.Match.ResultCoherence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamMatchResultCoherenceTest::RunTest(const FString& Parameters)
{
	using namespace OneBrahamMatchEnd;
	const EOneBrahamMatchMode Ascent = EOneBrahamMatchMode::Ascent;
	const EOneBrahamMatchMode Drill = EOneBrahamMatchMode::FireDrill;
	const TArray<int32> NoScores;
	FString Problem;

	// Extracted: the winner has people on the wire, and nobody else does.
	TestTrue(TEXT("extracted, coherent"), IsResultCoherent(EOneBrahamMatchEnding::Extracted, Ascent, 1, 3,
		{ OBSquad(0, 0), OBSquad(1, 0, 3), OBSquad(2, 1) }, NoScores, 0, Problem));
	TestFalse(TEXT("extracted with nobody aboard"), IsResultCoherent(EOneBrahamMatchEnding::Extracted, Ascent, 1, 0,
		{ OBSquad(0, 0), OBSquad(1, 2), OBSquad(2, 1) }, NoScores, 0, Problem));
	TestFalse(TEXT("extracted with no winner"), IsResultCoherent(EOneBrahamMatchEnding::Extracted, Ascent, INDEX_NONE, 2,
		{ OBSquad(0, 0), OBSquad(1, 0, 2) }, NoScores, 0, Problem));
	TestFalse(TEXT("a loser on the wire"), IsResultCoherent(EOneBrahamMatchEnding::Extracted, Ascent, 1, 2,
		{ OBSquad(0, 0, 1), OBSquad(1, 0, 2) }, NoScores, 0, Problem));

	// Last squad standing: exactly the winner is left.
	TestTrue(TEXT("last squad standing, coherent"), IsResultCoherent(EOneBrahamMatchEnding::LastSquadStanding, Ascent, 2, 0,
		{ OBSquad(0, 0), OBSquad(1, 0), OBSquad(2, 2) }, NoScores, 0, Problem));
	TestFalse(TEXT("last squad standing with two left"), IsResultCoherent(EOneBrahamMatchEnding::LastSquadStanding, Ascent, 2, 0,
		{ OBSquad(0, 1), OBSquad(1, 0), OBSquad(2, 2) }, NoScores, 0, Problem));
	TestFalse(TEXT("last squad standing, the wrong squad"), IsResultCoherent(EOneBrahamMatchEnding::LastSquadStanding, Ascent, 0, 0,
		{ OBSquad(0, 0), OBSquad(1, 0), OBSquad(2, 2) }, NoScores, 0, Problem));

	// No survivors and the ASCENT time out.
	TestTrue(TEXT("no survivors, coherent"), IsResultCoherent(EOneBrahamMatchEnding::NoSurvivors, Ascent, INDEX_NONE, 0,
		{ OBSquad(0, 0), OBSquad(1, 0), OBSquad(2, 0) }, NoScores, 0, Problem));
	TestFalse(TEXT("no survivors with somebody standing"), IsResultCoherent(EOneBrahamMatchEnding::NoSurvivors, Ascent, INDEX_NONE, 0,
		{ OBSquad(0, 0), OBSquad(1, 1), OBSquad(2, 0) }, NoScores, 0, Problem));
	TestTrue(TEXT("time out with squads still inside, coherent"), IsResultCoherent(EOneBrahamMatchEnding::TimeOut, Ascent, INDEX_NONE, 0,
		{ OBSquad(0, 2), OBSquad(1, 1), OBSquad(2, 0) }, NoScores, 0, Problem));
	TestFalse(TEXT("an ASCENT time out has no winner"), IsResultCoherent(EOneBrahamMatchEnding::TimeOut, Ascent, 1, 0,
		{ OBSquad(0, 2), OBSquad(1, 1) }, NoScores, 0, Problem));

	// FIRE DRILL.
	TestTrue(TEXT("score limit, coherent"), IsResultCoherent(EOneBrahamMatchEnding::ScoreLimit, Drill, 1, 0,
		{ OBSquad(0, 6, 0, 6), OBSquad(1, 6, 0, 6) }, { 150, 200 }, 200, Problem));
	TestFalse(TEXT("score limit short of the limit"), IsResultCoherent(EOneBrahamMatchEnding::ScoreLimit, Drill, 1, 0,
		{ OBSquad(0, 6, 0, 6), OBSquad(1, 6, 0, 6) }, { 150, 190 }, 200, Problem));
	TestTrue(TEXT("drill time out to the leader, coherent"), IsResultCoherent(EOneBrahamMatchEnding::TimeOut, Drill, 0, 0,
		{ OBSquad(0, 6, 0, 6), OBSquad(1, 6, 0, 6) }, { 120, 80 }, 200, Problem));
	TestFalse(TEXT("drill time out to the team behind"), IsResultCoherent(EOneBrahamMatchEnding::TimeOut, Drill, 1, 0,
		{ OBSquad(0, 6, 0, 6), OBSquad(1, 6, 0, 6) }, { 120, 80 }, 200, Problem));
	TestTrue(TEXT("draw when level, coherent"), IsResultCoherent(EOneBrahamMatchEnding::Draw, Drill, INDEX_NONE, 0,
		{ OBSquad(0, 6, 0, 6), OBSquad(1, 6, 0, 6) }, { 90, 90 }, 200, Problem));
	TestFalse(TEXT("a draw with one team ahead"), IsResultCoherent(EOneBrahamMatchEnding::Draw, Drill, INDEX_NONE, 0,
		{ OBSquad(0, 6, 0, 6), OBSquad(1, 6, 0, 6) }, { 91, 90 }, 200, Problem));

	// No ending is never coherent, and says why.
	TestFalse(TEXT("no ending"), IsResultCoherent(EOneBrahamMatchEnding::None, Ascent, INDEX_NONE, 0,
		{ OBSquad(0, 1) }, NoScores, 0, Problem));
	TestFalse(TEXT("an incoherent result names its problem"), Problem.IsEmpty());

	return true;
}

// ===========================================================================
//  The end screen's words
// ===========================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamMatchEndScreenCopyTest, "OneBraham.Match.EndScreenCopy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamMatchEndScreenCopyTest::RunTest(const FString& Parameters)
{
	using namespace OneBrahamMatchEnd;
	const EOneBrahamMatchMode Ascent = EOneBrahamMatchMode::Ascent;

	// Every ending has a headline and a tannoy line; only "none" is quiet.
	for (const EOneBrahamMatchEnding Ending : { EOneBrahamMatchEnding::Extracted, EOneBrahamMatchEnding::LastSquadStanding,
		EOneBrahamMatchEnding::NoSurvivors, EOneBrahamMatchEnding::TimeOut, EOneBrahamMatchEnding::StaffWithdrawn,
		EOneBrahamMatchEnding::ScoreLimit, EOneBrahamMatchEnding::Draw })
	{
		const FString Name = LogNameFor(Ending);
		TestFalse(FString::Printf(TEXT("'%s' has a headline"), *Name), HeadlineFor(Ending).IsEmpty());
		TestFalse(FString::Printf(TEXT("'%s' has a tannoy line"), *Name), AnnouncementFor(Ending, TEXT("Floor 9 Ops")).IsEmpty());
		TestNotEqual(FString::Printf(TEXT("'%s' has a log name"), *Name), Name, FString(TEXT("none")));
	}
	TestTrue(TEXT("no ending, no tannoy"), AnnouncementFor(EOneBrahamMatchEnding::None, FString()).IsEmpty());

	// The winner is named where there is one.
	TestTrue(TEXT("the extraction names the squad"),
		AnnouncementFor(EOneBrahamMatchEnding::Extracted, TEXT("Floor 9 Ops")).ToString().Contains(TEXT("Floor 9 Ops")));
	TestTrue(TEXT("a drill won on time names the team"),
		AnnouncementFor(EOneBrahamMatchEnding::TimeOut, TEXT("Internal Staff")).ToString().Contains(TEXT("Internal Staff")));
	TestTrue(TEXT("winners: extracted, last standing, score limit"),
		HasWinner(EOneBrahamMatchEnding::Extracted) && HasWinner(EOneBrahamMatchEnding::LastSquadStanding)
		&& HasWinner(EOneBrahamMatchEnding::ScoreLimit) && !HasWinner(EOneBrahamMatchEnding::NoSurvivors)
		&& !HasWinner(EOneBrahamMatchEnding::Draw));

	// A squad's fate.
	TestEqual(TEXT("on the wire"), UOneBrahamMatchEndWidget::SquadFateLabel(EOneBrahamMatchEnding::Extracted, Ascent, true, 0, 3).ToString(),
		FString(TEXT("EXTRACTED")));
	TestEqual(TEXT("everybody down"), UOneBrahamMatchEndWidget::SquadFateLabel(EOneBrahamMatchEnding::Extracted, Ascent, false, 0, 0).ToString(),
		FString(TEXT("SIGNED OUT")));
	TestEqual(TEXT("still inside when the building closed"), UOneBrahamMatchEndWidget::SquadFateLabel(EOneBrahamMatchEnding::TimeOut, Ascent, false, 2, 0).ToString(),
		FString(TEXT("STILL INSIDE")));
	TestEqual(TEXT("the last squad"), UOneBrahamMatchEndWidget::SquadFateLabel(EOneBrahamMatchEnding::LastSquadStanding, Ascent, true, 2, 0).ToString(),
		FString(TEXT("LEFT STANDING")));
	TestEqual(TEXT("a level drill"), UOneBrahamMatchEndWidget::SquadFateLabel(EOneBrahamMatchEnding::Draw, EOneBrahamMatchMode::FireDrill, false, 6, 0).ToString(),
		FString(TEXT("LEVEL")));

	// The local verdict tells the player about their own squad.
	TestTrue(TEXT("your squad got out"), UOneBrahamMatchEndWidget::VerdictLine(EOneBrahamMatchEnding::Extracted, Ascent, true, true, 0, 2)
		.ToString().Contains(TEXT("got out")));
	TestTrue(TEXT("your squad did not"), UOneBrahamMatchEndWidget::VerdictLine(EOneBrahamMatchEnding::Extracted, Ascent, true, false, 0, 0)
		.ToString().Contains(TEXT("did not make it out")));
	TestTrue(TEXT("the building is yours"), UOneBrahamMatchEndWidget::VerdictLine(EOneBrahamMatchEnding::LastSquadStanding, Ascent, true, true, 3, 0)
		.ToString().Contains(TEXT("The building is yours")));

	// Where everybody goes.
	TestTrue(TEXT("menu"), UOneBrahamMatchEndWidget::LeaveLine(12.f, EOneBrahamPostMatchAction::ReturnToMenu).ToString().Contains(TEXT("RECEPTION")));
	TestTrue(TEXT("reload"), UOneBrahamMatchEndWidget::LeaveLine(12.f, EOneBrahamPostMatchAction::ReloadMap).ToString().Contains(TEXT("REOPENS")));
	TestTrue(TEXT("dedicated"), UOneBrahamMatchEndWidget::LeaveLine(12.f, EOneBrahamPostMatchAction::RestartMatch).ToString().Contains(TEXT("NEXT SHIFT")));

	return true;
}

// ===========================================================================
//  FIRE DRILL scoring: the capture, the hold, the pace
// ===========================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamMatchFireDrillCaptureTest, "OneBraham.Match.FireDrillCapture",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamMatchFireDrillCaptureTest::RunTest(const FString& Parameters)
{
	int32 Owner = INDEX_NONE;
	int32 Capturing = INDEX_NONE;
	float Progress = 0.f;

	auto CaptureTick = [&](int32 Alone, bool bContested, float Seconds, float CaptureSeconds)
	{
		return AOneBrahamGameMode::StepControlPointCapture(Owner, Capturing, Progress, Alone, bContested, Seconds, CaptureSeconds);
	};

	// A team alone on a neutral point takes it after its capture time, not before.
	for (int32 Second = 0; Second < 7; ++Second)
	{
		TestFalse(TEXT("seven seconds is not eight"), CaptureTick(0, false, 1.f, 8.f));
	}
	TestEqual(TEXT("staff are taking it"), Capturing, 0);
	TestTrue(TEXT("the eighth second takes it"), CaptureTick(0, false, 1.f, 8.f));
	TestEqual(TEXT("staff hold it"), Owner, 0);
	TestEqual(TEXT("nobody is taking it now"), Capturing, static_cast<int32>(INDEX_NONE));

	// Standing on your own point takes nothing again.
	TestFalse(TEXT("the holder does not re-take it"), CaptureTick(0, false, 30.f, 8.f));

	// Contested: the attacker's progress holds, it does not grow or drain.
	CaptureTick(1, false, 5.f, 8.f);
	TestEqual(TEXT("contractors 5 s in"), Progress, 5.f);
	TestFalse(TEXT("contested takes nothing"), CaptureTick(INDEX_NONE, true, 20.f, 8.f));
	TestEqual(TEXT("contested holds the progress"), Progress, 5.f);
	TestEqual(TEXT("still staff's"), Owner, 0);

	// Walked away from (or the holder back on it alone): it drains at the same rate.
	CaptureTick(INDEX_NONE, false, 2.f, 8.f);
	TestEqual(TEXT("drains a second a second"), Progress, 3.f);
	CaptureTick(0, false, 10.f, 8.f);
	TestEqual(TEXT("drained to nothing"), Progress, 0.f);
	TestEqual(TEXT("and nobody is taking it"), Capturing, static_cast<int32>(INDEX_NONE));

	// A flip takes the full capture time from scratch, from the other team too.
	CaptureTick(1, false, 4.f, 8.f);
	TestTrue(TEXT("taken from the holder in the full time"), CaptureTick(1, false, 4.f, 8.f));
	TestEqual(TEXT("contractors hold it"), Owner, 1);

	// A different team starting resets the progress.
	CaptureTick(0, false, 6.f, 8.f);
	CaptureTick(INDEX_NONE, true, 1.f, 8.f);
	TestEqual(TEXT("staff 6 s in"), Progress, 6.f);

	// A zero capture time takes it on the first tick, never divides by it.
	Owner = INDEX_NONE; Capturing = INDEX_NONE; Progress = 0.f;
	TestTrue(TEXT("instant capture"), CaptureTick(1, false, 1.f, 0.f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamMatchFireDrillHoldTest, "OneBraham.Match.FireDrillHold",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamMatchFireDrillHoldTest::RunTest(const FString& Parameters)
{
	const TArray<float> Multipliers = { 1.f, 1.f, 1.25f, 2.f };

	TestEqual(TEXT("nothing held, nothing earned"), AOneBrahamGameMode::ComputeHoldScoreRate({}, 0, Multipliers), 0.f);
	TestEqual(TEXT("owned but nobody on it earns nothing"), AOneBrahamGameMode::ComputeHoldScoreRate({}, 2, Multipliers), 0.f);
	TestEqual(TEXT("one point, its own rate"), AOneBrahamGameMode::ComputeHoldScoreRate({ 0.2f }, 1, Multipliers), 0.2f);
	TestEqual(TEXT("two owned, one manned"), AOneBrahamGameMode::ComputeHoldScoreRate({ 0.2f }, 2, Multipliers), 0.25f);
	TestEqual(TEXT("two owned, both manned"), AOneBrahamGameMode::ComputeHoldScoreRate({ 0.15f, 0.2f }, 2, Multipliers), 0.4375f);
	TestEqual(TEXT("all three"), AOneBrahamGameMode::ComputeHoldScoreRate({ 0.1f, 0.15f, 0.2f }, 3, Multipliers), 0.9f);
	TestEqual(TEXT("a count past the table uses its last entry"), AOneBrahamGameMode::ComputeHoldScoreRate({ 0.1f }, 7, Multipliers), 0.2f);
	TestEqual(TEXT("no table is x1"), AOneBrahamGameMode::ComputeHoldScoreRate({ 0.1f, 0.2f }, 2, {}), 0.3f);
	TestEqual(TEXT("a negative rate earns nothing"), AOneBrahamGameMode::ComputeHoldScoreRate({ -1.f }, 1, Multipliers), 0.f);

	// The shipped tuning, against the mode's rules (Docs/06 "FIRE DRILL scoring").
	UOneBrahamMatchSettingsAsset* Settings = NewObject<UOneBrahamMatchSettingsAsset>(GetTransientPackage());
	Settings->Mode = EOneBrahamMatchMode::FireDrill;
	Settings->EnsureDefaults();
	if (!TestEqual(TEXT("three points"), Settings->ControlPoints.Num(), 3))
	{
		return false;
	}

	TArray<float> Rates;
	for (const FOneBrahamControlPointDef& Point : Settings->ControlPoints)
	{
		Rates.Add(Point.ScorePerSecond);
		TestTrue(*FString::Printf(TEXT("%s is a fight to take (>= 6 s)"), *Point.PointId.ToString()), Point.CaptureSeconds >= 6.f);
	}
	Rates.Sort();
	const float ToWin = static_cast<float>(Settings->ScoreToWin);
	const float Limit = static_cast<float>(Settings->TimeLimitSeconds);

	// Nothing reaches 200 in the old ninety seconds: not even all three points
	// held and manned for the whole drill with nothing else scored.
	const float Lockout = AOneBrahamGameMode::ComputeHoldScoreRate(Rates, 3, Settings->MultiPointHoldMultipliers);
	TestTrue(TEXT("a lockout alone takes more than three minutes"), ToWin / Lockout > 180.f);
	TestTrue(TEXT("a lockout alone ends it inside five minutes"), ToWin / Lockout < 300.f);

	// One room is not the drill: the best single point held all ten minutes
	// does not win it alone.
	TestTrue(TEXT("one point alone cannot win in the time limit"),
		AOneBrahamGameMode::ComputeHoldScoreRate({ Rates.Last() }, 1, Settings->MultiPointHoldMultipliers) * Limit < ToWin);

	// Owning more of the building is worth more than the sum of its rooms.
	const float TwoBest = AOneBrahamGameMode::ComputeHoldScoreRate({ Rates[1], Rates[2] }, 2, Settings->MultiPointHoldMultipliers);
	TestTrue(TEXT("two points beat their sum"), TwoBest > Rates[1] + Rates[2]);
	TestTrue(TEXT("all three beat their sum"), Lockout > Rates[0] + Rates[1] + Rates[2]);

	// Kills and captures are a part of a drill, not the drill.
	TestTrue(TEXT("a kill is a point"), Settings->ScorePerKill >= 1 && Settings->ScorePerKill <= 2);
	TestTrue(TEXT("a capture is a few kills, not a tenth of the drill"),
		Settings->ScorePerCapture > Settings->ScorePerKill && Settings->ScorePerCapture * 10 < Settings->ScoreToWin);
	// A close drill flips its points ten-odd times a team: at more than two
	// kills a capture the flips, not the holding, decided it (7.5 minutes).
	TestTrue(TEXT("a capture is worth at most two kills"), Settings->ScorePerCapture <= 2 * Settings->ScorePerKill);

	return true;
}

// ===========================================================================
//  Player names
// ===========================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamMatchPlayerNamesTest, "OneBraham.Match.PlayerNames",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamMatchPlayerNamesTest::RunTest(const FString& Parameters)
{
	// Sanitised: no URL delimiters, no control characters, single spaces, capped.
	TestEqual(TEXT("plain"), AOneBrahamGameMode::SanitisePlayerName(TEXT("Jenson Raper")), FString(TEXT("Jenson Raper")));
	TestEqual(TEXT("trimmed and single-spaced"), AOneBrahamGameMode::SanitisePlayerName(TEXT("  Jenson \t  Raper  ")), FString(TEXT("Jenson Raper")));
	TestEqual(TEXT("URL delimiters gone"), AOneBrahamGameMode::SanitisePlayerName(TEXT("A?Mode=B#c")), FString(TEXT("AModeBc")));
	TestEqual(TEXT("control characters gone"), AOneBrahamGameMode::SanitisePlayerName(FString(TEXT("Al\x01")) + TEXT("ex\x7F")), FString(TEXT("Alex")));
	TestEqual(TEXT("a line break is a space"), AOneBrahamGameMode::SanitisePlayerName(TEXT("Al\nKim")), FString(TEXT("Al Kim")));
	TestEqual(TEXT("capped"), AOneBrahamGameMode::SanitisePlayerName(TEXT("Bartholomew Fitzwilliam-Smythe")).Len(),
		AOneBrahamGameMode::MaxPlayerNameLength);
	TestEqual(TEXT("no trailing space after the cap"), AOneBrahamGameMode::SanitisePlayerName(TEXT("Nineteen characters x")),
		FString(TEXT("Nineteen characters")));
	TestTrue(TEXT("nothing usable is empty"), AOneBrahamGameMode::SanitisePlayerName(TEXT(" ?#= ")).IsEmpty());
	TestEqual(TEXT("not ASCII is fine"), AOneBrahamGameMode::SanitisePlayerName(TEXT("Zo\u00EB \u00D3 Briain")), FString(TEXT("Zo\u00EB \u00D3 Briain")));

	// The NULL nickname is the unique id, whole or cut to the cap: not a name.
	const FString NullId = TEXT("Jensons-MacBook-Air.local-1A2B3C4D5E6F");
	TestTrue(TEXT("NULL nickname"), AOneBrahamGameMode::IsPlaceholderPlayerName(NullId, NullId));
	TestTrue(TEXT("NULL nickname, cut"), AOneBrahamGameMode::IsPlaceholderPlayerName(TEXT("Jensons-MacBook-Air."), NullId));
	TestTrue(TEXT("engine default"), AOneBrahamGameMode::IsPlaceholderPlayerName(TEXT("Player256"), FString()));
	TestTrue(TEXT("empty"), AOneBrahamGameMode::IsPlaceholderPlayerName(FString(), NullId));
	TestFalse(TEXT("a Steam persona is a name"), AOneBrahamGameMode::IsPlaceholderPlayerName(TEXT("okonkwo"), TEXT("76561198000000000")));
	TestFalse(TEXT("a person"), AOneBrahamGameMode::IsPlaceholderPlayerName(TEXT("Jenson Raper"), NullId));
	TestFalse(TEXT("Players is a word"), AOneBrahamGameMode::IsPlaceholderPlayerName(TEXT("Playerone"), FString()));

	// What a login asks for: ?OBName= over the engine's ?Name= (which is the
	// nickname), ?Name= when it is a person's (Steam), then the fallback.
	TestEqual(TEXT("OBName wins"), AOneBrahamGameMode::ChooseLoginPlayerName(
		FString::Printf(TEXT("?Name=%s?OBName=Jenson Raper?Mode=FireDrill"), *NullId), NullId, TEXT("Host")), FString(TEXT("Jenson Raper")));
	TestEqual(TEXT("a persona in ?Name="), AOneBrahamGameMode::ChooseLoginPlayerName(
		TEXT("?Name=okonkwo"), TEXT("76561198000000000"), FString()), FString(TEXT("okonkwo")));
	TestEqual(TEXT("the nickname gives way to the fallback"), AOneBrahamGameMode::ChooseLoginPlayerName(
		FString::Printf(TEXT("?Name=%s"), *NullId.Left(20)), NullId, TEXT("OBHost")), FString(TEXT("OBHost")));
	TestTrue(TEXT("nothing usable"), AOneBrahamGameMode::ChooseLoginPlayerName(
		FString::Printf(TEXT("?Name=%s"), *NullId), NullId, FString()).IsEmpty());
	TestEqual(TEXT("sanitised on the way"), AOneBrahamGameMode::ChooseLoginPlayerName(
		TEXT("?OBName=  Alex   Kim "), FString(), FString()), FString(TEXT("Alex Kim")));

	// Unique within the match, case-insensitively, the suffix inside the cap.
	const TArray<FString> Taken = { TEXT("Jenson Raper"), TEXT("BOT Hughes"), TEXT("jenson raper 2") };
	TestEqual(TEXT("free name kept"), AOneBrahamGameMode::MakeUniquePlayerName(TEXT("Alex"), Taken), FString(TEXT("Alex")));
	TestEqual(TEXT("a clash takes the next digit"), AOneBrahamGameMode::MakeUniquePlayerName(TEXT("Jenson Raper"), Taken),
		FString(TEXT("Jenson Raper 3")));
	TestEqual(TEXT("a bot's name too"), AOneBrahamGameMode::MakeUniquePlayerName(TEXT("BOT Hughes"), Taken), FString(TEXT("BOT Hughes 2")));
	const FString Long = TEXT("Abcdefghijklmnopqrst");
	const FString LongUnique = AOneBrahamGameMode::MakeUniquePlayerName(Long, { Long });
	TestEqual(TEXT("long name suffixed inside the cap"), LongUnique, FString(TEXT("Abcdefghijklmnopqr 2")));
	TestTrue(TEXT("never past the cap"), LongUnique.Len() <= AOneBrahamGameMode::MaxPlayerNameLength);

	// A client reads its name back: its own, or the server's unique variant of it.
	TestTrue(TEXT("the same name"), AOneBrahamGameMode::IsUniqueVariantOfName(TEXT("jenson raper"), TEXT("Jenson Raper")));
	TestTrue(TEXT("the suffixed name"), AOneBrahamGameMode::IsUniqueVariantOfName(TEXT("Jenson Raper 3"), TEXT("Jenson Raper")));
	TestTrue(TEXT("the long name cut for its suffix"), AOneBrahamGameMode::IsUniqueVariantOfName(LongUnique, Long));
	TestFalse(TEXT("Tomas is not Tom"), AOneBrahamGameMode::IsUniqueVariantOfName(TEXT("Tomas"), TEXT("Tom")));
	TestFalse(TEXT("Tom 2 is not Tom Jones"), AOneBrahamGameMode::IsUniqueVariantOfName(TEXT("Tom 2"), TEXT("Tom Jones")));
	TestFalse(TEXT("a generated name is not the asked one"), AOneBrahamGameMode::IsUniqueVariantOfName(TEXT("Tom from HR"), TEXT("OBClient")));
	TestFalse(TEXT("nothing asked"), AOneBrahamGameMode::IsUniqueVariantOfName(TEXT("Alex"), FString()));

	// A server's travel URL reaches its clients without the host's name in it.
	TestEqual(TEXT("names stripped for clients"), AOneBrahamGameMode::StripPlayerNameOptions(
		TEXT("/Game/OneBraham/Maps/L_Persistent_OneBraham?Mode=FireDrill?Name=Jenson Raper?OBName=Jenson Raper?listen")),
		FString(TEXT("/Game/OneBraham/Maps/L_Persistent_OneBraham?Mode=FireDrill?listen")));
	TestEqual(TEXT("a relative restart is left alone"), AOneBrahamGameMode::StripPlayerNameOptions(TEXT("?Restart?NoSeamlessTravel")),
		FString(TEXT("?Restart?NoSeamlessTravel")));
	TestEqual(TEXT("ServerName is not Name"), AOneBrahamGameMode::StripPlayerNameOptions(TEXT("Map?ServerName=Floor 9?obname=x")),
		FString(TEXT("Map?ServerName=Floor 9")));

	// The generated office name: stable, and always inside the cap.
	TestEqual(TEXT("same seed, same name"), UOneBrahamGameInstance::MakeOfficeDisplayName(42), UOneBrahamGameInstance::MakeOfficeDisplayName(42));
	for (uint32 Seed = 0; Seed < 500; ++Seed)
	{
		const FString Office = UOneBrahamGameInstance::MakeOfficeDisplayName(Seed);
		if (!TestTrue(*FString::Printf(TEXT("'%s' fits"), *Office), Office.Len() <= AOneBrahamGameMode::MaxPlayerNameLength
			&& AOneBrahamGameMode::SanitisePlayerName(Office) == Office && !AOneBrahamGameMode::IsPlaceholderPlayerName(Office, FString())))
		{
			break;
		}
	}

	// What earlier builds wrote to the profile by themselves is not a choice.
	TestTrue(TEXT("the login name is automatic"), UOneBrahamGameInstance::IsAutomaticDisplayName(FPlatformProcess::UserName(false)));
	TestTrue(TEXT("the host name is automatic"), UOneBrahamGameInstance::IsAutomaticDisplayName(FPlatformProcess::ComputerName()));
	TestTrue(TEXT("empty is automatic"), UOneBrahamGameInstance::IsAutomaticDisplayName(FString()));
	TestFalse(TEXT("a chosen name is not"), UOneBrahamGameInstance::IsAutomaticDisplayName(TEXT("Night Porter")));
	const FString HostStem = AOneBrahamGameMode::SanitisePlayerName(FPlatformProcess::ComputerName()).Left(8).TrimEnd();
	TestFalse(TEXT("a chosen name with the host name's opening letters is not"),
		UOneBrahamGameInstance::IsAutomaticDisplayName(HostStem + TEXT(" the Great")));
	TestFalse(TEXT("the default is not"), UOneBrahamGameInstance::IsAutomaticDisplayName(UOneBrahamGameInstance::MakeDefaultDisplayName()));

	return true;
}

#endif // WITH_AUTOMATION_TESTS
