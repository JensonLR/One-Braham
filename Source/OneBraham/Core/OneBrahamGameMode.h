// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamGameMode.h
// ---------------------------------------------------------------------------
// Server-authoritative match director for both shipping modes.
//
// ASCENT (12 players / 3 squads of 4):
//   Warmup -> Active -> (a squad calls the helicopter) Extraction -> Complete.
//   Win: a squad holds the Skydeck extraction zone for 30 s.
//   Lose: every squad eliminated before that -> "no survivors".
//
// FIRE DRILL (6v6 or 8v8):
//   Warmup -> Active with respawns and three control points -> Complete on
//   score-to-win or time limit.
//
// The GameMode owns: squad assignment, spawn selection, respawn logic, scoring,
// match phase transitions, seamless travel and the bot-practice roster.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Core/OneBrahamTypes.h"
#include "OneBrahamGameMode.generated.h"

class UOneBrahamMatchSettingsAsset;
class AOneBrahamGameState;
class AOneBrahamPlayerController;
class AOneBrahamPlayerState;
class AOneBrahamCharacter;
class AOneBrahamSpawnPoint;

/** One squad's score in FIRE DRILL, cached on the GameMode for fast scoring. */
USTRUCT()
struct FOneBrahamTeamScoreCache
{
	GENERATED_BODY()

	UPROPERTY() int32 TeamIndex = 0;
	UPROPERTY() int32 Score = 0;
	UPROPERTY() int32 Kills = 0;
	UPROPERTY() float ControlPointHoldSeconds = 0.f;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOneBrahamPlayerEliminated, AActor*, Victim, AActor*, Killer);

UCLASS(BlueprintType)
class ONEBRAHAM_API AOneBrahamGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AOneBrahamGameMode();

	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void InitGameState() override;
	virtual void StartPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;
	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void Logout(AController* Exiting) override;
	virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;
	virtual bool ShouldSpawnAtStartSpot(AController* Player) override { return false; }
	virtual bool PlayerCanRestart_Implementation(APlayerController* Player) override;
	virtual void RestartPlayer(AController* NewPlayer) override;
	virtual void RestartPlayerAtPlayerStart(AController* NewPlayer, AActor* StartSpot) override;

	// ======================================================================
	//  Accessors
	// ======================================================================
	UFUNCTION(BlueprintPure, Category = "One Braham|Match")
	UOneBrahamMatchSettingsAsset* GetMatchSettings() const { return MatchSettings; }

	UFUNCTION(BlueprintPure, Category = "One Braham|Match")
	AOneBrahamGameState* GetOBGameState() const;

	UFUNCTION(BlueprintPure, Category = "One Braham|Match")
	EOneBrahamMatchMode GetMatchMode() const;

	// ======================================================================
	//  Match flow (server only)
	// ======================================================================
	UFUNCTION(BlueprintCallable, Category = "One Braham|Match")
	void SetMatchPhase(EOneBrahamMatchPhase NewPhase);

	UFUNCTION(BlueprintCallable, Category = "One Braham|Match")
	EOneBrahamMatchPhase GetMatchPhase() const;

	/** Called by the extraction component when a squad boards the helicopter. */
	void CompleteMatchWithExtraction(EOneBrahamTeam WinningTeam, const FString& WinningSquadName, int32 ExtractedCount);

	/** Called by the extraction component / lockdown director when the match ends without a win. */
	void CompleteMatchNoSurvivors();

	/** FIRE DRILL: add score to a team and check the win condition. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Fire Drill")
	void AddTeamScoreByIndex(int32 TeamIndex, int32 Amount);

	/**
	 * Server: somebody died. Fills the kill feed, credits the killer, and returns
	 * the entry it wrote so the caller can put the same words on the death screen.
	 *
	 * This is the only place a kill is recorded, for the same reason damage has
	 * one entry point: the feed, the scoreboard and the death screen disagreeing
	 * about who killed whom is the kind of bug that survives a playtest because
	 * two of the three are believed.
	 */
	FOneBrahamKillFeedEntry RegisterKill(AActor* Victim, AController* KillerController,
		const FOneBrahamDamageReport& Report);

	/** The weapon id a killer was holding, or None for the building, or a fall. */
	FName GetKillWeaponId(const AController* KillerController) const;

	/**
	 * Which verb the feed prints between the two names.
	 *
	 * Pure and static because the order of these claims is the whole content of
	 * the function: the building outranks everything, a player who killed
	 * themselves is not credited, finishing a downed player is an execution even
	 * if the round went through the head, and only then does a headshot read as
	 * a headshot. Reordering two of the branches is a plausible edit that
	 * changes what the feed says, so a test pins the order.
	 */
	UFUNCTION(BlueprintPure, Category = "One Braham|Match")
	static EOneBrahamKillFeedVerb ChooseKillVerb(bool bKilledByBuilding, bool bSelfKill,
		bool bVictimWasDowned, EOneBrahamHitZone HitZone);

	/** Called whenever a player state changes in a way that might end the match. */
	void EvaluateMatchEndConditions();

	/** QA hook for ob.endmatch. WinningTeamIndex -1 = no survivors. */
	void DebugForceMatchResult(int32 WinningTeamIndex);

	// ======================================================================
	//  Squads
	// ======================================================================
	UFUNCTION(BlueprintPure, Category = "One Braham|Squad")
	int32 GetSquadCount() const;

	UFUNCTION(BlueprintPure, Category = "One Braham|Squad")
	EOneBrahamTeam GetTeamForSquad(int32 SquadId) const;

	/** Server: assigns a newly joined player to the best available squad. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Squad")
	void AssignPlayerToSquad(AOneBrahamPlayerState* PlayerState);

	/** Rebuilds the replicated squad array from the current PlayerStates. */
	void RebuildSquadInfo();

	/** Server: marks the extract flag on a squad so the HUD shows "EXTRACTING". */
	void SetSquadExtractionClaim(int32 SquadId, bool bClaimed);

	/** Dry corporate squad name for the scoreboard/killfeed ("Braham Compliance"). */
	FString GetSquadDisplayName(int32 SquadId) const;

	// ======================================================================
	//  Spawning
	// ======================================================================
	/** Server: respawns a player after the respawn delay (FIRE DRILL). */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Spawn")
	void QueueRespawn(AController* Controller, float DelaySeconds);

	/** Picks the best spawn point for a controller, honouring squad spread and enemy distance. */
	AOneBrahamSpawnPoint* SelectSpawnPointFor(AController* Controller, int32 FloorIndex = INDEX_NONE) const;

	/** Server: gets (or creates) the spawn point registry for the loaded level. */
	void RebuildSpawnPointRegistry();

	// ======================================================================
	//  Bots (practice mode only - ASCENT ships with no AI opponents)
	// ======================================================================
	UFUNCTION(BlueprintCallable, Category = "One Braham|Bots")
	void PopulateWithBots(int32 TotalBots);

	UFUNCTION(BlueprintPure, Category = "One Braham|Bots")
	bool IsBotPracticeMode() const { return bBotPracticeMode; }

	// ======================================================================
	//  Delegates
	// ======================================================================
	UPROPERTY(BlueprintAssignable, Category = "One Braham|Match")
	FOneBrahamPlayerEliminated OnPlayerEliminated;

protected:
	// ======================================================================
	//  Internal flow
	// ======================================================================
	void StartWarmup();

	/** Stocks the building from the match settings' loot profiles, once. */
	void SpawnMatchLoot();

	/** FIRE DRILL: spawns the control points from the match settings. */
	void SpawnControlPoints();

	/** The warmup's tannoy, one announcement at a time. */
	void PlayNextLobbyAnnouncement();

public:
	/** The five lobby announcements, in the order the warmup plays them. */
	static int32 LobbyAnnouncementCount();
	static FText GetLobbyAnnouncement(int32 Index);

protected:
	int32 LobbyAnnouncementIndex = 0;
	FTimerHandle LobbyAnnouncementHandle;

	/** The squad that won, set just before FinishMatch, for the last words. */
	int32 PendingWinningSquadId = INDEX_NONE;
	void StartActiveMatch();
	void FinishMatch(EOneBrahamMatchEnding Ending, EOneBrahamTeam WinningTeam, const FString& WinningSquadName, int32 ExtractedCount, EOneBrahamExtractionFailReason FailReason);

	/** Applies the settings-derived defaults to the GameMode's player classes. */
	void ConfigureClassesFromSettings();

	/** Reads ?Mode= / ?Name= / ?Archetype= from the travel options. */
	void ParseTravelOptions(const FString& Options);

	/** Builds the replicated match result payload for the end screen. */
	/** After the post-match window: remote players to their menus, then the server's own next step. */
	void ReturnPlayersToMenu();

	FOneBrahamMatchResult BuildMatchResult(EOneBrahamTeam WinningTeam, const FString& WinningSquadName, bool bNoSurvivors, int32 ExtractedCount) const;

	/** Broadcasts a Tannoy announcement to everyone (lobby speaker + HUD banner). */
	void Announce(const FText& Text, bool bTannoy = true);

	UFUNCTION()
	void HandleWarmupComplete();

	UFUNCTION()
	void HandleMatchTimeLimitReached();

	/** Bound to UOneBrahamExtractionComponent::OnExtractionComplete. Ends the match. */
	UFUNCTION()
	void HandleExtractionComplete(int32 WinningSquadId, EOneBrahamTeam WinningTeam);

	/** Bound to UOneBrahamExtractionComponent::OnExtractionFailed. Clears the claim. */
	UFUNCTION()
	void HandleExtractionFailed(int32 SquadId, EOneBrahamExtractionFailReason Reason);

	/** Bound to UOneBrahamLockdownComponent::OnStageChanged: updates the objective. */
	UFUNCTION()
	void HandleLockdownStageChanged(EOneBrahamLockdownStage NewStage);

	/** Server: wires the game state's components up once they exist. */
	void BindGameStateComponents();

	/** Fires the "extraction available" objective once the Skydeck is reachable. */
	void UpdateObjectiveState();

	UPROPERTY()
	TObjectPtr<UOneBrahamMatchSettingsAsset> MatchSettings = nullptr;

	/** Squad id (0..TeamCount-1) -> team enum. Cached from the settings asset. */
	UPROPERTY()
	TArray<EOneBrahamTeam> SquadToTeamMap;

	/** Round-robin cursor for squad spreading so squads never share a spawn cluster. */
	int32 NextSquadId = 0;

	UPROPERTY()
	TArray<TObjectPtr<AOneBrahamSpawnPoint>> SpawnPoints;

	UPROPERTY()
	TArray<FOneBrahamTeamScoreCache> TeamScoreCache;

	/** Controllers waiting on a respawn, with their remaining delay. */
	UPROPERTY()
	TMap<TObjectPtr<AController>, float> PendingRespawns;

	FTimerHandle MatchPhaseTimerHandle;
	FTimerHandle RespawnTimerHandle;

	float MatchStartTime = 0.f;
	float WarmupEndTime = 0.f;

	bool bBotPracticeMode = false;

	/** `?Bots=N` from the travel options: the total, dealt round the squads. */
	int32 RequestedBotCount = 0;
	bool bMatchFinished = false;

	/** Parsed from the travel options. */
	FString PendingModeString;
};
