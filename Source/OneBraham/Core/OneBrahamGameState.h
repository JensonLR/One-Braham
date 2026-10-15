// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamGameState.h
// ---------------------------------------------------------------------------
// Authoritative, replicated match state. Everything the HUD needs to draw a
// coherent picture of the match (phase, lockdown stage, extraction progress,
// squads, killfeed, score) lives here and nowhere else, so a late joiner or a
// spectator rebuilds the entire match picture from one class.
//
// GameStateComponents owned here:
//   * UOneBrahamLockdownComponent - building escalation director
//   * UOneBrahamExtractionComponent - helicopter / extraction flow

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "Core/OneBrahamTypes.h"
#include "OneBrahamGameState.generated.h"

class UOneBrahamLockdownComponent;
class UOneBrahamExtractionComponent;
class UOneBrahamMatchSettingsAsset;
class USoundBase;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOneBrahamMatchPhaseChanged, EOneBrahamMatchPhase, NewPhase);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOneBrahamMatchResultReady, const FOneBrahamMatchResult&, Result);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOneBrahamServerAnnouncement, const FText&, Announcement);

/** Short banner copy: text, seconds, colour. Raised on every client by MulticastWarningBanner. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOneBrahamWarningBanner, const FText&, Text, float, DurationSeconds, FLinearColor, Colour);

UCLASS(BlueprintType)
class ONEBRAHAM_API AOneBrahamGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	AOneBrahamGameState();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void Tick(float DeltaSeconds) override;

	// ======================================================================
	//  Match identity
	// ======================================================================
	UPROPERTY(ReplicatedUsing = OnRep_MatchMode, BlueprintReadOnly, Category = "One Braham|Match")
	EOneBrahamMatchMode MatchMode = EOneBrahamMatchMode::Ascent;

	UPROPERTY(ReplicatedUsing = OnRep_MatchPhase, BlueprintReadOnly, Category = "One Braham|Match")
	EOneBrahamMatchPhase MatchPhase = EOneBrahamMatchPhase::Warmup;

	/** Server display name for the browser and the scoreboard footer. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "One Braham|Match")
	FString ServerName = TEXT("On Break");

	/** True when this instance has no human authority (dedicated host). */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "One Braham|Match")
	bool bIsDedicatedServerMatch = false;

	/** Seconds since the active phase began, replicated at 1 Hz to save bandwidth. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "One Braham|Match")
	float PhaseElapsedSeconds = 0.f;

	/** Total match length in seconds; 0 = unlimited (ASCENT). */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "One Braham|Match")
	float MatchTimeLimitSeconds = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "One Braham|Match")
	float MatchStartServerTime = 0.f;

	// ======================================================================
	//  Squads and teams
	// ======================================================================
	UPROPERTY(ReplicatedUsing = OnRep_Squads, BlueprintReadOnly, Category = "One Braham|Squad")
	TArray<FOneBrahamSquadInfo> Squads;

	/** FIRE DRILL score per team index (index 0 = Internal Staff, 1 = External Contractors). */
	UPROPERTY(ReplicatedUsing = OnRep_TeamScores, BlueprintReadOnly, Category = "One Braham|Fire Drill")
	TArray<int32> TeamScores;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "One Braham|Match")
	int32 SurvivorsRemaining = 0;

	// ======================================================================
	//  Killfeed
	// ======================================================================
	UPROPERTY(ReplicatedUsing = OnRep_KillFeed, BlueprintReadOnly, Category = "One Braham|Killfeed")
	TArray<FOneBrahamKillFeedEntry> KillFeed;

	/** Maximum entries kept in the replicated killfeed array. */
	static constexpr int32 MaxKillFeedEntries = 8;

	// ======================================================================
	//  End of match
	// ======================================================================
	UPROPERTY(ReplicatedUsing = OnRep_MatchResult, BlueprintReadOnly, Category = "One Braham|Match")
	FOneBrahamMatchResult MatchResult;

	// ======================================================================
	//  Components
	// ======================================================================
	UFUNCTION(BlueprintPure, Category = "One Braham|Lockdown")
	UOneBrahamLockdownComponent* GetLockdownComponent() const { return LockdownComponent; }

	UFUNCTION(BlueprintPure, Category = "One Braham|Extraction")
	UOneBrahamExtractionComponent* GetExtractionComponent() const { return ExtractionComponent; }

	UFUNCTION(BlueprintPure, Category = "One Braham|Match")
	UOneBrahamMatchSettingsAsset* GetMatchSettings() const { return MatchSettings; }

	/** Server-only: caches the settings asset so components can read tuned values. */
	void SetMatchSettings(UOneBrahamMatchSettingsAsset* InSettings);

	// ======================================================================
	//  Helpers used by HUD, scoreboard and voice
	// ======================================================================
	UFUNCTION(BlueprintPure, Category = "One Braham|Squad")
	FOneBrahamSquadInfo GetSquadInfo(int32 SquadId) const;

	UFUNCTION(BlueprintPure, Category = "One Braham|Squad")
	int32 GetSquadIdForPlayer(int32 PlayerId) const;

	UFUNCTION(BlueprintPure, Category = "One Braham|Squad")
	int32 GetSquadCount() const { return Squads.Num(); }

	UFUNCTION(BlueprintPure, Category = "One Braham|Squad")
	bool IsSquadEliminated(int32 SquadId) const;

	UFUNCTION(BlueprintPure, Category = "One Braham|Match")
	int32 GetAlivePlayerCount() const;

	/** Server-side: appends a killfeed entry, trimming the oldest. */
	void AddKillFeedEntry(const FOneBrahamKillFeedEntry& Entry);

	/** Server-side: replicates a building announcement (lobby Tannoy) to all clients. */
	UFUNCTION(NetMulticast, Reliable)
	void MulticastAnnounce(const FText& Announcement, bool bPlayTannoySound);

	/** Server-side: brief on-screen mode text (used for lockdown warnings). */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastWarningBanner(const FText& Text, float DurationSeconds, FLinearColor Colour);

	/** Draws/records the current network budget. Called by ob.netbudget and the soak test. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Net")
	void LogNetBudget();

	// ======================================================================
	//  Delegates
	// ======================================================================
	UPROPERTY(BlueprintAssignable, Category = "One Braham|Match") FOneBrahamMatchPhaseChanged OnMatchPhaseChanged;
	UPROPERTY(BlueprintAssignable, Category = "One Braham|Match") FOneBrahamMatchResultReady OnMatchResultReady;
	UPROPERTY(BlueprintAssignable, Category = "One Braham|Match") FOneBrahamKillFeedUpdated OnKillFeedEntryAdded;
	UPROPERTY(BlueprintAssignable, Category = "One Braham|Match") FOneBrahamServerAnnouncement OnAnnouncement;

	/**
	 * A short, imperative banner - "Shutters are down", "Security purge on lower
	 * floors" - as opposed to OnAnnouncement's tannoy paragraph. Both exist
	 * because the building says two different kinds of thing: the sentence a
	 * building management system would read out in full, and the two words that
	 * fit on a HUD.
	 */
	UPROPERTY(BlueprintAssignable, Category = "One Braham|Match") FOneBrahamWarningBanner OnWarningBanner;
	UPROPERTY(BlueprintAssignable, Category = "One Braham|Squad") FOneBrahamServerAnnouncement OnSquadsChanged;

protected:
	UFUNCTION() void OnRep_MatchMode();
	UFUNCTION() void OnRep_MatchPhase();
	UFUNCTION() void OnRep_Squads();
	UFUNCTION() void OnRep_TeamScores();
	UFUNCTION() void OnRep_KillFeed();
	UFUNCTION() void OnRep_MatchResult();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Components")
	TObjectPtr<UOneBrahamLockdownComponent> LockdownComponent = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Components")
	TObjectPtr<UOneBrahamExtractionComponent> ExtractionComponent = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "One Braham|Match")
	TObjectPtr<UOneBrahamMatchSettingsAsset> MatchSettings = nullptr;

	/** Cached previous killfeed length so OnRep only fires the delegate for new rows. */
	int32 LastAcknowledgedKillFeedCount = 0;

	/** Timer used to compress PhaseElapsedSeconds replication to 1 Hz. */
	float PhaseBroadcastAccumulator = 0.f;
};
