// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamScoreboardWidget.h
// ---------------------------------------------------------------------------
// The hold-Tab scoreboard. Styled as the directory board in a lobby: who is in
// the building, which squad they report to, and whether they are still on site.
//
// WHAT IT SHOWS
//   * A header: the mode, the phase, the clock (remaining in a timed mode,
//     elapsed in ASCENT) and, in FIRE DRILL, the score between the two teams.
//   * One block per squad (ASCENT, NIGHT SHIFT) or per team (FIRE DRILL), the
//     local player's own block first, because that is the one they opened it for.
//   * Per player: transmit lamp, name, archetype, status, K / D / A, objective
//     score, ping, and a mute button for everybody but themselves.
//
// REFRESH
//   Four times a second while open, from the core ticker rather than the world
//   timer manager or NativeTick: the ticker runs while the game is paused and is
//   unhooked the moment the board closes, so a closed scoreboard costs nothing.
//   Rows are updated in place; the list is rebuilt only when the roster or its
//   order changes, so a mute button is not replaced under the cursor mid-click.
//
// MUTE
//   UOneBrahamVoiceSubsystem::SetPlayerMuted is the one call. The board never
//   keeps its own mute state: it reads IsPlayerMuted on every refresh, so it
//   agrees with the voice system even when something else changed it.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "GameplayTagContainer.h"
#include "Core/OneBrahamTypes.h"
#include "UI/OneBrahamMenuWidget.h"
#include "OneBrahamScoreboardWidget.generated.h"

class AOneBrahamGameState;
class AOneBrahamPlayerState;
class APlayerState;
class UBorder;
class UButton;
class UImage;
class UTextBlock;
class UVerticalBox;

/** The live widgets of one player's row. */
USTRUCT()
struct FOneBrahamScoreboardRowWidgets
{
	GENERATED_BODY()

	TWeakObjectPtr<APlayerState> Player;

	UPROPERTY() TObjectPtr<UBorder> Root = nullptr;
	UPROPERTY() TObjectPtr<UImage> MicLamp = nullptr;
	UPROPERTY() TObjectPtr<UTextBlock> Name = nullptr;
	UPROPERTY() TObjectPtr<UTextBlock> Archetype = nullptr;
	UPROPERTY() TObjectPtr<UTextBlock> Status = nullptr;
	UPROPERTY() TObjectPtr<UTextBlock> Kills = nullptr;
	UPROPERTY() TObjectPtr<UTextBlock> Deaths = nullptr;
	UPROPERTY() TObjectPtr<UTextBlock> Assists = nullptr;
	UPROPERTY() TObjectPtr<UTextBlock> Objective = nullptr;
	UPROPERTY() TObjectPtr<UTextBlock> Ping = nullptr;
	UPROPERTY() TObjectPtr<UButton> MuteButton = nullptr;
	UPROPERTY() TObjectPtr<UTextBlock> MuteLabel = nullptr;
};

/** A group header's live widgets: the team score changes while the board is open. */
USTRUCT()
struct FOneBrahamScoreboardGroupWidgets
{
	GENERATED_BODY()

	int32 GroupKey = INDEX_NONE;

	UPROPERTY() TObjectPtr<UTextBlock> Title = nullptr;
	UPROPERTY() TObjectPtr<UTextBlock> Detail = nullptr;
};

UCLASS()
class ONEBRAHAM_API UOneBrahamScoreboardWidget : public UOneBrahamMenuWidget
{
	GENERATED_BODY()

public:
	UOneBrahamScoreboardWidget(const FObjectInitializer& ObjectInitializer);

	virtual void NativeDestruct() override;

	/** Shows or hides the board. Opening refreshes immediately: a scoreboard that
	 *  opens on a quarter-second-old roster is visibly wrong. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Scoreboard")
	void SetScoreboardOpen(bool bOpen);

	UFUNCTION(BlueprintPure, Category = "One Braham|Scoreboard")
	bool IsScoreboardOpen() const { return bOpen; }

	/** Re-reads the game state and every player state. Public for tests and for
	 *  the HUD component's forced refresh. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Scoreboard")
	void RefreshNow();

	// --- pure helpers (tested) ----------------------------------------------
	/** "BusinessAnalyst" -> "BUSINESS ANALYST", "ITOperations" -> "IT OPERATIONS".
	 *  Reads the leaf of an OB.Character.Archetype.* tag. */
	static FText ArchetypeLabel(const FGameplayTag& ArchetypeTag);

	/** The word on the directory board for a life state. */
	static FText LifeStateLabel(EOneBrahamLifeState State);

	/** The group a player belongs in: a team in FIRE DRILL, a squad otherwise.
	 *  INDEX_NONE for a player not yet placed. */
	static int32 GroupKeyFor(EOneBrahamMatchMode Mode, EOneBrahamTeam Team, int32 SquadId);

	/** Seconds between refreshes while open. */
	static constexpr float RefreshIntervalSeconds = 0.25f;

protected:
	virtual void BuildLayout() override;

	/** Ticker callback: refreshes while open. */
	bool HandleRefreshTick(float DeltaTime);

	void RefreshHeader(const AOneBrahamGameState* GameState);

	/** Builds the rows from scratch for the given grouped, ordered roster. */
	void RebuildRows(const TArray<TPair<int32, TArray<AOneBrahamPlayerState*>>>& Groups,
		const AOneBrahamGameState* GameState);

	/** Writes one player's current state into their row. */
	void UpdateRow(FOneBrahamScoreboardRowWidgets& Row, const AOneBrahamPlayerState* State) const;

	void UpdateGroupHeader(FOneBrahamScoreboardGroupWidgets& Group, const AOneBrahamGameState* GameState) const;

	/** The column headings, built once, aligned with the rows by sharing widths. */
	UWidget* MakeColumnHeadings();

	/** A row's cells, in column order. */
	UWidget* MakeRowCells(FOneBrahamScoreboardRowWidgets& Row);

	void ToggleMute(TWeakObjectPtr<APlayerState> Target);

	// --- layout -------------------------------------------------------------
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Text_ServerLine = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Text_Mode = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Text_Phase = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Text_Clock = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Text_ClockCaption = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Text_Score = nullptr;
	UPROPERTY(Transient) TObjectPtr<UVerticalBox> RowList = nullptr;

	UPROPERTY(Transient) TArray<FOneBrahamScoreboardRowWidgets> Rows;
	UPROPERTY(Transient) TArray<FOneBrahamScoreboardGroupWidgets> GroupHeaders;

	/** Roster signature of the current rows; a change triggers a rebuild. */
	FString RosterSignature;

	/** Binders created for the current rows, released on the next rebuild so the
	 *  array does not grow by twelve every time somebody's score moves them. */
	int32 RowBinderStart = 0;

	FTSTicker::FDelegateHandle RefreshHandle;
	bool bOpen = false;
};
