// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamMatchEnding.h
// ---------------------------------------------------------------------------
// How a match ends, as rules with no world in them.
//
// THE ENDINGS (Docs/06 section 7)
//   ASCENT      Extracted          a squad completes the winch hold
//               LastSquadStanding  every other squad is out and the survivors
//                                  are not on the wire
//               NoSurvivors        every squad is out, the purge included
//               TimeOut            the hard limit: nobody extracts, everybody lost
//               StaffWithdrawn     every human in a squad is out and only bots
//                                  are left fighting: the match is over for the
//                                  people it was for
//   FIRE DRILL  ScoreLimit         a team reaches the points to win
//               TimeOut            the clock runs out with one team ahead
//               Draw               the clock runs out level
//
// WHY HERE
//   The decisions were scattered through AOneBrahamGameMode, read a squad list
//   that nothing kept current, and could not be tested without a world. The
//   game mode now gathers the facts, these functions decide, and the decision
//   is replicated (UOneBrahamExtractionComponent::GetMatchEnding) so every
//   machine's end screen names the same ending the server logged.

#pragma once

#include "CoreMinimal.h"
#include "Core/OneBrahamTypes.h"
#include "OneBrahamMatchEnding.generated.h"

/** The ending a match reached. None while it is still running. */
UENUM(BlueprintType)
enum class EOneBrahamMatchEnding : uint8
{
	None = 0,
	Extracted = 1			UMETA(DisplayName = "Extracted"),
	LastSquadStanding = 2	UMETA(DisplayName = "Last squad standing"),
	NoSurvivors = 3			UMETA(DisplayName = "No survivors"),
	TimeOut = 4				UMETA(DisplayName = "Time out"),
	StaffWithdrawn = 5		UMETA(DisplayName = "Staff withdrawn"),
	ScoreLimit = 6			UMETA(DisplayName = "Score limit"),
	Draw = 7				UMETA(DisplayName = "Draw")
};

/** What the server does when the post-match window closes. */
enum class EOneBrahamPostMatchAction : uint8
{
	/** Everybody to the main menu map. */
	ReturnToMenu,
	/** The same map again: the menu map is not in this build yet. */
	ReloadMap,
	/** A dedicated server goes round again with whoever is still connected. */
	RestartMatch
};

/** One squad as the end rules see it. */
struct FOneBrahamSquadStanding
{
	int32 SquadId = INDEX_NONE;
	/** Everybody in it, alive or not. A squad nobody was ever in is not a squad. */
	int32 Members = 0;
	/** Alive or downed: still able to be revived, still in the fight. */
	int32 InFight = 0;
	/** On the wire. */
	int32 Extracted = 0;
	/** Of InFight, how many are standing on a floor the purge is gassing. */
	int32 OnPurgedFloor = 0;
};

/** What an elimination check decided. */
struct FOneBrahamEndVerdict
{
	bool bEnds = false;
	EOneBrahamMatchEnding Ending = EOneBrahamMatchEnding::None;
	int32 WinningSquadId = INDEX_NONE;
};

namespace OneBrahamMatchEnd
{
	/** Where everybody goes after a match. Another lane builds it; until it is in
	 *  the build the map is reloaded instead (DecidePostMatchAction). */
	inline const TCHAR* MainMenuMapPath() { return TEXT("/Game/OneBraham/Maps/L_MainMenu"); }

	/**
	 * ASCENT's eliminations, checked whenever somebody's state changes:
	 *
	 *   * Nobody in the fight in any squad: NoSurvivors. An extraction in
	 *     progress does not hold the match open - there is nobody to hold it.
	 *   * One squad left in a match that had at least two: LastSquadStanding,
	 *     unless that squad owns the extraction in progress (ExtractionSquadId),
	 *     in which case the winch decides: it extracts, or it fails and this
	 *     check runs again - or unless every one of them is standing in the
	 *     purge, in which case the building decides: they climb out of it, or
	 *     it is NoSurvivors. The purge kills squads one after another, and the
	 *     last one to choke was otherwise named the winner.
	 *   * Every human in a squad out, bots still fighting, no extraction live:
	 *     StaffWithdrawn. (OneBrahamBotRules::ShouldEndWithNoHumansLeft.)
	 *
	 * Other modes respawn and never end this way.
	 */
	ONEBRAHAM_API FOneBrahamEndVerdict DecideEliminationEnding(EOneBrahamMatchMode Mode,
		const TArray<FOneBrahamSquadStanding>& Squads, int32 ExtractionSquadId,
		int32 HumansInSquads, int32 HumansInFight, int32 BotsInFight);

	/** The ending a time-out is, from AOneBrahamGameMode::DecideTimeOutOutcome's two flags. */
	ONEBRAHAM_API EOneBrahamMatchEnding EndingForTimeOut(EOneBrahamMatchMode Mode, bool bDraw);

	/** What to do when the post-match window closes. */
	ONEBRAHAM_API EOneBrahamPostMatchAction DecidePostMatchAction(bool bDedicatedServer, bool bMainMenuExists);

	/** The tannoy's last word. Read out in full on every machine. */
	ONEBRAHAM_API FText AnnouncementFor(EOneBrahamMatchEnding Ending, const FString& WinnerName);

	/** Two or three words for the end screen's headline. */
	ONEBRAHAM_API FText HeadlineFor(EOneBrahamMatchEnding Ending);

	/** The ending's name for a log line: "extracted", "last squad standing". */
	ONEBRAHAM_API FString LogNameFor(EOneBrahamMatchEnding Ending);

	/** True for the endings with a winner (a squad, or a FIRE DRILL team). */
	ONEBRAHAM_API bool HasWinner(EOneBrahamMatchEnding Ending);

	/**
	 * Whether a finished match's numbers agree with the ending it claims, for the
	 * full-match smoke run (-OBSmokeFullMatch). OutProblem names the first
	 * disagreement. TeamScores and ScoreToWin matter to FIRE DRILL only.
	 */
	ONEBRAHAM_API bool IsResultCoherent(EOneBrahamMatchEnding Ending, EOneBrahamMatchMode Mode,
		int32 WinningSquadId, int32 ExtractedCount, const TArray<FOneBrahamSquadStanding>& Squads,
		const TArray<int32>& TeamScores, int32 ScoreToWin, FString& OutProblem);
}
