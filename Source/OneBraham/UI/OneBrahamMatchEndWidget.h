// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamMatchEndWidget.h
// ---------------------------------------------------------------------------
// The end of a match: what happened, to whom, and where everybody is going.
//
// WHAT IT SHOWS
//   * A context line: the mode, the duration, the final score in FIRE DRILL.
//   * The ending as a headline - EXTRACTED, LAST SQUAD STANDING, NO SURVIVORS,
//     TIME OUT, DRILL COMPLETE, DRAW - and the winner under it.
//   * The local player's own squad's fate in one line: whether they got out.
//   * The tannoy's last word, quoted: the building has the last word, as it
//     has had every other one.
//   * Every squad (FIRE DRILL: both teams), the local one first, with its fate
//     and each member's kills, downs, deaths and assists and whether they got
//     out. Squad-focused, not a leaderboard (Docs/08 section 4).
//   * A footer with the countdown to the menu, the reload, or the next match.
//
// WHERE THE FACTS COME FROM
//   The ending and the winning squad from UOneBrahamExtractionComponent (which
//   the server records it on, replicated); the numbers from the game state's
//   MatchResult; who is still standing from the replicated squad list. The
//   widget refreshes twice a second from the core ticker while it is up, so a
//   result that replicates a moment after the phase change fills itself in.
//
// THE LOOK
//   The scoreboard's: a directory board at the end of the day. Navy, light-grey
//   type, one accent bar, signal blue for what is yours.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Core/OneBrahamTypes.h"
#include "Extraction/OneBrahamMatchEnding.h"
#include "UI/OneBrahamMenuWidget.h"
#include "OneBrahamMatchEndWidget.generated.h"

class AOneBrahamGameState;
class UTextBlock;
class UVerticalBox;

UCLASS()
class ONEBRAHAM_API UOneBrahamMatchEndWidget : public UOneBrahamMenuWidget
{
	GENERATED_BODY()

public:
	UOneBrahamMatchEndWidget(const FObjectInitializer& ObjectInitializer);

	virtual void NativeDestruct() override;

	/** Shows the screen and starts its refresh. Safe to call again. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Match End")
	void ShowMatchEnd();

	/** Re-reads the game state. Public for tests and the HUD component. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Match End")
	void RefreshNow();

	UFUNCTION(BlueprintPure, Category = "One Braham|Match End")
	bool IsShowing() const { return bShowing; }

	/** The ending the screen is showing, for the smoke run's log. */
	EOneBrahamMatchEnding GetShownEnding() const { return ShownEnding; }

	// --- pure helpers (tested) ----------------------------------------------
	/**
	 * A squad's fate in two or three words. bWinner is the ending's winning
	 * squad; InFight and Extracted count its members on their feet (or downed)
	 * and on the wire.
	 */
	static FText SquadFateLabel(EOneBrahamMatchEnding Ending, EOneBrahamMatchMode Mode, bool bWinner,
		int32 InFight, int32 Extracted);

	/** One line to the local player about their own squad. bInSquad false for a spectator. */
	static FText VerdictLine(EOneBrahamMatchEnding Ending, EOneBrahamMatchMode Mode, bool bInSquad, bool bOwnSquadWon,
		int32 OwnInFight, int32 OwnExtracted);

	/** The line under the headline: who won, and how. */
	static FText WinnerLine(EOneBrahamMatchEnding Ending, EOneBrahamMatchMode Mode, const FText& WinnerName,
		int32 ExtractedCount, int32 StaffScore, int32 ContractorScore);

	/** The footer: where everybody goes, and when. */
	static FText LeaveLine(float SecondsLeft, EOneBrahamPostMatchAction Action);

	/** Seconds between refreshes while it is up. */
	static constexpr float RefreshIntervalSeconds = 0.5f;

protected:
	virtual void BuildLayout() override;

	bool HandleRefreshTick(float DeltaTime);

	/** Rebuilds the squad blocks for the current result. */
	void RebuildSquads(const AOneBrahamGameState* GameState, EOneBrahamMatchEnding Ending, int32 WinningSquadId);

	UPROPERTY(Transient) TObjectPtr<UTextBlock> Text_Context = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Text_Headline = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Text_Winner = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Text_Verdict = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Text_Tannoy = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Text_Footer = nullptr;
	UPROPERTY(Transient) TObjectPtr<UVerticalBox> SquadList = nullptr;

	/** What the squad blocks were built from; a change rebuilds them. */
	FString SquadSignature;

	EOneBrahamMatchEnding ShownEnding = EOneBrahamMatchEnding::None;

	FTSTicker::FDelegateHandle RefreshHandle;
	bool bShowing = false;
};
