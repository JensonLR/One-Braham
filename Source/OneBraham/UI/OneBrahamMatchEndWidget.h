// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamMatchEndWidget.h
// ---------------------------------------------------------------------------
// The end of a match: END OF DAY REPORT.
//
// THE LOOK
//   The picture letterboxes to 2.39:1, as the front end's films do. The left
//   40% is the tower in section turned on its side - floors down, time across -
//   replaying the match in four seconds: the lockdown rising floor by floor
//   under hatching, each squad's route climbing as a line (yours in signal
//   blue), the kills your squad made as small crossed boxes. The right side is
//   the report: an eyebrow (EXTRACTION CONFIRMED, NO SURVIVORS, ASSEMBLY POINT
//   REPORT), the headline typed on a word at a time in the building's words
//   ("Signed out at 18:41."; or, for a loss, the serif line on a red field),
//   the player's day in four numbers counting up, every squad's fate, and two
//   rows in the front end's directory style: CONTINUE and SIGN OUT. The
//   tannoy's last word sits in the top bar; where everybody goes, and when, in
//   the bottom one.
//
// WHERE THE FACTS COME FROM
//   The ending and the winning squad from UOneBrahamExtractionComponent (which
//   the server records it on, replicated); the numbers from the game state's
//   MatchResult; who is still standing from the replicated squad list; the
//   chart from what this machine recorded (FOneBrahamMatchReplay, handed over
//   by the HUD component). The widget refreshes twice a second from the core
//   ticker while it is up, so a result that replicates a moment after the
//   phase change fills itself in.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Core/OneBrahamTypes.h"
#include "Extraction/OneBrahamMatchEnding.h"
#include "UI/OneBrahamMenuWidget.h"
#include "UI/HUD/OneBrahamHUDFrame.h"
#include "UI/HUD/OneBrahamMatchReplay.h"
#include "OneBrahamMatchEndWidget.generated.h"

class AOneBrahamGameState;
class UOneBrahamPaintedControl;

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

	/** What this machine saw of the match (the chart) and the HUD's last frame
	 *  (the clock, the player, the building), handed over as the match ends. */
	void SetReplay(const FOneBrahamMatchReplay& InReplay, const FOneBrahamHUDFrame& InFrame)
	{
		Replay = InReplay;
		LastFrame = InFrame;
	}

	/** Dev: shows a fabricated ending when the live match has none. */
	void DevSetDemoEnding(bool bDemo) { bDemoEnding = bDemo; }

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

	/** The report's eyebrow for an ending. */
	static FText EyebrowFor(EOneBrahamMatchEnding Ending, EOneBrahamMatchMode Mode, bool bOwnSquadWon);

	/** Seconds between refreshes while it is up. */
	static constexpr float RefreshIntervalSeconds = 0.5f;

protected:
	virtual void BuildLayout() override;

	bool HandleRefreshTick(float DeltaTime);

	/** Everything the painted report draws, refreshed twice a second. */
	struct FMemberLine
	{
		FString Name;
		int32 Kills = 0;
		int32 Downs = 0;
		int32 Deaths = 0;
		int32 Assists = 0;
		EOneBrahamLifeState State = EOneBrahamLifeState::Alive;
		bool bYou = false;
	};

	struct FSquadLine
	{
		FString Name;
		FString Fate;
		bool bOwn = false;
		bool bWinner = false;
		bool bOut = false;
		TArray<FMemberLine> Members;
	};

	struct FReport
	{
		FString Eyebrow;
		FString Headline;
		/** A loss: the serif line on the red field. */
		bool bLoss = false;
		FString WinnerLine;
		FString Verdict;
		FString Tannoy;
		FString Leave;
		FString Context;
		TArray<TPair<FString, float>> Stats;
		TArray<FSquadLine> Squads;
	};

	FReport Report;

	/** Paints the whole report (the chart, the words, the bars). */
	void PaintReport(struct FOneBrahamPainter& P, double Now) const;

	/** The chart's routes and seal line from the replay. */
	void BuildChart(struct FOneBrahamSectionParams& Params) const;

	UPROPERTY(Transient) TObjectPtr<UOneBrahamPaintedControl> Row_Continue = nullptr;
	UPROPERTY(Transient) TObjectPtr<UOneBrahamPaintedControl> Row_SignOut = nullptr;

	EOneBrahamMatchEnding ShownEnding = EOneBrahamMatchEnding::None;

	FOneBrahamMatchReplay Replay;
	FOneBrahamHUDFrame LastFrame;
	bool bDemoEnding = false;

	/** Real time the screen went up: the replay, the typing and the counting
	 *  are functions of it. */
	double ShownAt = 0.0;

	FTSTicker::FDelegateHandle RefreshHandle;
	bool bShowing = false;
};
