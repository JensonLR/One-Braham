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
// Every mode with a time limit (ASCENT's hard end at 35 minutes included) is
// ended by the server when the limit runs out - see DecideTimeOutOutcome.
//
// THE END (Docs/06 section 7). Every ending goes through FinishMatch, once:
// the ending is decided by the pure rules in Extraction/OneBrahamMatchEnding.h
// (eliminations are re-checked twice a second against a squad list rebuilt from
// the player states, not only when somebody dies), recorded on the extraction
// component so every machine's end screen names it, announced, and the
// building is frozen - pawns off, purge stopped, winch still. After
// PostMatchSeconds a listen server or a standalone game goes back to the main
// menu (the map is reloaded while the menu map is not in the build) and a
// dedicated server restarts the match.
//
// FIRE DRILL scores kills and its three control points (floors 1, 9 and 15):
// a team alone on a point for its capture time takes it, and a point scores for
// its holder every second they are on it and nobody else is.
//
// DEV (compiled out of Shipping): ob.Match.WarmupSeconds, TimeLimit,
// PostMatchSeconds, ScoreToWin compress a match; ob.Match.FollowBots puts a
// player without a pawn behind the living bots; DevWithdrawToSpectator takes the
// local player out of the fight so a match of bots can be watched to its end
// (-OBSmokeFullMatch).
//
// The GameMode owns: squad assignment, spawn selection, respawn logic, scoring,
// match phase transitions, seamless travel and the bots that fill empty slots.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Core/OneBrahamTypes.h"
#include "Extraction/OneBrahamMatchEnding.h"
#include "OneBrahamGameMode.generated.h"

class UOneBrahamMatchSettingsAsset;
class AOneBrahamGameState;
class AOneBrahamPlayerController;
class AOneBrahamPlayerState;
class AOneBrahamCharacter;
class AOneBrahamSpawnPoint;
class AOneBrahamBotController;

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

/**
 * How a match that ran out of time ends. Docs/06 section 7: in FIRE DRILL the
 * team with more points wins (level scores are a draw); in every other mode the
 * hard limit is the "Time out" ending - nobody extracts, everybody lost.
 */
struct FOneBrahamTimeOutOutcome
{
	EOneBrahamTeam WinningTeam = EOneBrahamTeam::None;
	FString WinningSquadName;
	/** Nobody got out: the result reads as everybody lost. */
	bool bNoSurvivors = false;
	/** FIRE DRILL only: level on points when the clock ran out. */
	bool bDraw = false;
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
	virtual void HandleSeamlessTravelPlayer(AController*& C) override;
	virtual FString InitNewPlayer(APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId,
		const FString& Options, const FString& Portal = TEXT("")) override;
	/** Every rename of a human (login, ServerChangeName) is sanitised and made unique here. */
	virtual void ChangeName(AController* Controller, const FString& NewName, bool bNameChange) override;
	virtual void GetSeamlessTravelActorList(bool bToTransition, TArray<AActor*>& ActorList) override;
	/** Sends remote clients the travel URL without the host's ?OBName=/?Name= (StripPlayerNameOptions). */
	virtual APlayerController* ProcessClientTravel(FString& URL, bool bSeamless, bool bAbsolute) override;
	virtual bool ShouldSpawnAtStartSpot(AController* Player) override { return false; }
	virtual bool PlayerCanRestart_Implementation(APlayerController* Player) override;
	virtual void RestartPlayer(AController* NewPlayer) override;
	virtual void RestartPlayerAtPlayerStart(AController* NewPlayer, AActor* StartSpot) override;
	/** Spawns with AdjustIfPossibleButDontSpawnIfColliding: nudged out of a
	 *  wall's skin if it can be, never stacked inside somebody. */
	virtual APawn* SpawnDefaultPawnAtTransform_Implementation(AController* NewPlayer, const FTransform& SpawnTransform) override;

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

	/**
	 * True when a running match has used up its time limit. A limit of 0 is
	 * unlimited; the clock only runs in Active and Extraction, the same phases
	 * AOneBrahamGameState accumulates PhaseElapsedSeconds in, so the server
	 * ends the match on the second the HUD countdown reaches zero.
	 */
	static bool HasMatchTimeLimitExpired(float TimeLimitSeconds, float ElapsedSeconds, EOneBrahamMatchPhase Phase);

	/** The Docs/06 section 7 outcome of a time-out, for this mode and these team scores. */
	static FOneBrahamTimeOutOutcome DecideTimeOutOutcome(EOneBrahamMatchMode Mode, const TArray<int32>& TeamScores);

	// ======================================================================
	//  FIRE DRILL scoring (Docs/06 "FIRE DRILL scoring")
	// ======================================================================

	/**
	 * One tick of one control point's capture. AloneTeam is the only team with
	 * somebody on the point (INDEX_NONE for nobody, or for both: bContested).
	 * A team alone on a point it does not own builds progress and takes it
	 * after CaptureSeconds; a different team starting resets the progress;
	 * contested, the progress holds; walked away from, it drains at the same
	 * rate. Returns true on the tick the point changes hands.
	 */
	static bool StepControlPointCapture(int32& OwnerTeam, int32& CapturingTeam, float& ProgressSeconds,
		int32 AloneTeam, bool bContested, float StepSeconds, float CaptureSeconds);

	/**
	 * A team's hold income per second: the sum of ScorePerSecond over the points
	 * it is scoring on (owned, somebody of theirs on it, nobody else), times
	 * Multipliers[PointsOwned] (the last entry past the end, 1 with none).
	 */
	static float ComputeHoldScoreRate(const TArray<float>& ScoringPointRates, int32 PointsOwned, const TArray<float>& Multipliers);

	// ======================================================================
	//  Player names
	//
	//  Every human's name is decided here, on the server, from what their
	//  machine sent. The engine writes ?Name= into every login URL itself, from
	//  the online subsystem's nickname, and overwrites any ?Name= the game put
	//  there: under Steam that is the persona name, under NULL it is the
	//  machine's host name and a login id ("Jensons-MacBook-Air.local-1A2B..."),
	//  which every HUD and scoreboard showed cut to twenty characters. So the
	//  name the player chose travels as ?OBName= as well (the game instance adds
	//  it to every URL it builds), the NULL nickname is recognised for what it
	//  is (it is the player's unique id), and the result is sanitised, capped
	//  and made unique among the match's player states. Bots keep the names the
	//  bot controller gives them.
	// ======================================================================

	/** The travel/login option carrying the player-chosen display name. */
	static constexpr const TCHAR* DisplayNameOption = TEXT("OBName");

	/** Longest name a player state carries (the engine's own login cap). */
	static constexpr int32 MaxPlayerNameLength = 20;

	/** Printable, single-spaced, trimmed, without URL delimiters, capped. Empty if nothing usable is left. */
	static FString SanitisePlayerName(const FString& Raw);

	/**
	 * True for a name that is not a person's: empty, the NULL subsystem's
	 * nickname (the unique id string, whole or cut to the cap), or the
	 * engine's defaults ("Player", "NullUser").
	 */
	static bool IsPlaceholderPlayerName(const FString& Name, const FString& UniqueIdString);

	/**
	 * The name a login asks for, by precedence: ?OBName=, then ?Name= unless it
	 * is a placeholder, then the fallback (the local player's profile on a
	 * listen host). Sanitised; empty when none of them is usable.
	 */
	static FString ChooseLoginPlayerName(const FString& Options, const FString& UniqueIdString, const FString& Fallback);

	/**
	 * Desired, or Desired with " 2", " 3"... when another player already has it
	 * (case-insensitive), shortened so the suffix still fits the cap.
	 */
	static FString MakeUniquePlayerName(const FString& Desired, const TArray<FString>& TakenNames);

	/**
	 * True when Current is Desired, or what MakeUniquePlayerName makes of
	 * Desired on a clash ("Jenson Raper 2", a long name cut to fit its suffix).
	 * A client uses it to tell "the server gave me my name" from "the server
	 * never heard it" ("Tomas" is not "Tom"; "Tom 2" is).
	 */
	static bool IsUniqueVariantOfName(const FString& Current, const FString& Desired);

	/**
	 * URL without its ?OBName= and ?Name= options. A server's travel URL carries
	 * its own player's name (the listen host's); a client travels relative to
	 * it and FURL::AddOption replaces the client's own option with the host's,
	 * so every client would rejoin as "<host> 2". Clients are sent this instead.
	 */
	static FString StripPlayerNameOptions(const FString& URL);

	/** Called whenever a player state changes in a way that might end the match. */
	void EvaluateMatchEndConditions();

	/** QA hook for ob.endmatch. WinningTeamIndex -1 = no survivors. */
	void DebugForceMatchResult(int32 WinningTeamIndex);

	/** The ending the match reached; None while it runs. */
	EOneBrahamMatchEnding GetMatchEnding() const { return MatchEnding; }

	bool IsMatchFinished() const { return bMatchFinished; }

	/** Each squad's members, how many are in the fight and how many extracted, from the player states. */
	void GatherSquadStandings(TArray<FOneBrahamSquadStanding>& OutSquads) const;

	/**
	 * Dev (does nothing in Shipping): takes a player out of the match - no squad,
	 * no pawn, spectating the bots - and fills the slot with a bot, so a match
	 * can be watched to its end by the -OBSmokeFullMatch run without the
	 * player's own death deciding it.
	 */
	void DevWithdrawToSpectator(APlayerController* Player);

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

	/** True for a null pawn or a corpse: one the respawn queue should replace. */
	static bool IsPawnDeadForRespawn(const APawn* Pawn);

	/** A queued respawn that found no free spawn point tries again after this long. */
	static constexpr float RespawnRetrySeconds = 1.f;

	/** Picks the best spawn point for a controller, honouring squad spread and enemy distance. */
	AOneBrahamSpawnPoint* SelectSpawnPointFor(AController* Controller, int32 FloorIndex = INDEX_NONE) const;

	/**
	 * Every spawn point the threat rules allow a controller, best first: the
	 * ones that pass in a weighted random order (the first is what
	 * SelectSpawnPointFor returns), then the vetoed ones safest first, then the
	 * ones still resting after use. FloorIndex INDEX_NONE = any floor.
	 */
	void RankSpawnPointsFor(AController* Controller, int32 FloorIndex, TArray<AOneBrahamSpawnPoint*>& OutRanked) const;

	/**
	 * The spawn points to try for a controller, in the order RestartPlayer
	 * tries them: the mode's rules (ASCENT's plaza or late-join floor, FIRE
	 * DRILL's control point floors) ranked by RankSpawnPointsFor, then put in
	 * OneBrahamSpawnRules::OrderSpawnAttempts order - clear points first, the
	 * points that just failed this controller last. OutClear says, per point,
	 * whether a capsule fits on it now (if not, only beside it is tried).
	 */
	void GatherSpawnAttemptsFor(AController* Controller, TArray<AOneBrahamSpawnPoint*>& OutPoints, TArray<bool>& OutClear) const;

	/** ASCENT: the floor a player entering now walks in on (OneBrahamSpawnRules::LateJoinFloor). */
	int32 GetAscentEntryFloor() const;

	/** Spawn points RestartPlayer tries per call before it gives up and queues a retry. */
	static constexpr int32 MaxSpawnAttemptsPerRestart = 6;

	/** Server: gets (or creates) the spawn point registry for the loaded level. */
	void RebuildSpawnPointRegistry();

	// ======================================================================
	//  Bots - fill the squad slots no human holds (OneBrahamBotRules)
	// ======================================================================
	/**
	 * Server: puts a bot in every empty squad slot, up to MaxBots in total
	 * (OneBrahamBotRules::FillAllSlots = no cap). Each bot is given its squad
	 * now and its pawn as soon as the mode lets anyone spawn: in ASCENT that is
	 * the end of the warmup, on the plaza, like every player.
	 */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Bots")
	void PopulateWithBots(int32 MaxBots = -1);

	/** True once the match has any bots in it. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Bots")
	bool IsBotPracticeMode() const { return bBotPracticeMode; }

	/** The bot limit this match resolved in InitGame: -1 fill every slot, 0 none, N a cap. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Bots")
	int32 GetBotLimit() const { return BotLimit; }

	/** The bots currently in the match. */
	int32 GetBotCount() const;

	/**
	 * Where a bot of this squad heads when nobody needs shooting: the Skydeck
	 * pad in ASCENT, one of the control points in FIRE DRILL. False when the
	 * mode or the map has none.
	 */
	bool FindBotObjective(int32 SquadId, int32 OrdinalInSquad, FVector& OutLocation, int32& OutFloor) const;

	/**
	 * Where a bot standing at From should walk to now: its objective when the
	 * navmesh joins the two, else the floor of its own walkable group nearest
	 * the objective (BuildBotFloorGraph). False when there is no objective.
	 */
	bool FindBotTravelTarget(int32 SquadId, int32 OrdinalInSquad, const FVector& From, FVector& OutLocation, int32& OutFloor) const;

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
	void StartActiveMatch();

	/**
	 * The one way a match ends. Ignored once the match has finished, so a kill
	 * landing under the end screen cannot end it again (and restart the clock to
	 * the lobby). WinningSquadId is the squad (FIRE DRILL: the team index).
	 */
	void FinishMatch(EOneBrahamMatchEnding Ending, int32 WinningSquadId, int32 ExtractedCount);

	/** Squad id -> the name the results use ("Braham Compliance"). */
	FString GetWinnerName(int32 WinningSquadId) const;

	/** Marks the winning squad's players still on their feet as on the wire. */
	int32 MarkSquadExtracted(int32 SquadId);

	/** Under the end screen: every pawn stops, takes no damage and fires nothing. */
	void FreezeMatchActors();

	/** The post-match window closed: the menu, a reload or a restart. */
	void HandlePostMatchTimeUp();

	/** Server, FIRE DRILL: control point captures and holding score, once a second. */
	void TickControlPoints(float DeltaSeconds);

	/** Dev overrides (warm-up, time limit), read every tick so -ExecCmds applies. */
	void ApplyDevOverrides();

	/** Applies the settings-derived defaults to the GameMode's player classes. */
	void ConfigureClassesFromSettings();

	/** Reads ?Mode= / ?Name= / ?Archetype= from the travel options. */
	void ParseTravelOptions(const FString& Options);

	/** Builds the replicated match result payload for the end screen. */
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

	/**
	 * Spawns (does not possess) a pawn for Controller at Spot, or on a clear
	 * patch of navmesh beside it in the open. bTrySpot false skips the spot
	 * itself (somebody is standing on it). Null when neither fits.
	 */
	APawn* SpawnPawnAtOrBeside(AController* Controller, AActor* Spot, bool bTrySpot);

	/** The capsule a pawn of this controller's class occupies. */
	void GetPawnCapsuleFor(AController* Controller, float& OutRadius, float& OutHalfHeight) const;

	/**
	 * A human whose squad has just been settled gets a pawn if the mode lets
	 * them have one now: ASCENT's warmup parks them over the plaza, a full
	 * ASCENT lobby makes them a spectator, everything else spawns at once.
	 */
	void StartNewPlayerIfAllowed(APlayerController* NewPlayer);

	/** Server: spawns a bot's pawn through RestartPlayer if the mode allows it now. */
	void SpawnBotPawn(AOneBrahamBotController* Bot);

	/**
	 * True when bots may take the field: a human is in the match to play
	 * against them, and the mode lets anyone spawn (ASCENT: once the warmup
	 * ends). A dedicated server with nobody on it does not run a bot match.
	 */
	bool CanBotsSpawnNow() const;

	/**
	 * A human is taking a squad slot: when every slot is held, a bot gives one
	 * up - from PreferredSquad when that squad has a bot (a player arriving with
	 * a squad from the last map), else by OneBrahamBotRules::PickSquadToYieldBot.
	 */
	void MakeRoomForHuman(int32 PreferredSquad = INDEX_NONE);

	/** Humans holding a squad slot, how many of them are still in the fight (alive or downed), and the bots still in it. */
	void CountInFight(int32& HumansInSquads, int32& HumansInFight, int32& BotsInFight) const;

	/** Humans and bots per squad, from the PlayerStates. */
	void CountSquadMembers(TArray<int32>& OutHumans, TArray<int32>& OutBots) const;

	/** A human needs a slot and every squad is full: one bot leaves. True if one did. */
	bool RemoveBotToMakeRoom(int32 PreferredSquad = INDEX_NONE);

	/** Works out which floors the navmesh joins (BotFloorGroup), and logs it. */
	void BuildBotFloorGraph();

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

	/** Where players died recently: a spawn next to a fresh death is next to
	 *  whoever caused it. Pruned to OneBrahamSpawnRules::RecentDeathSeconds. */
	struct FRecentDeath
	{
		FVector Location = FVector::ZeroVector;
		float Time = 0.f;
	};
	TArray<FRecentDeath> RecentDeaths;

	/** Each player's last killer, so a respawn is never handed back to them. */
	TMap<TWeakObjectPtr<AController>, TWeakObjectPtr<AController>> LastKillerOf;

	/** The spawn points that just failed each controller, oldest first, so the
	 *  next try goes somewhere else (OrderSpawnAttempts). Cleared on a spawn. */
	TMap<TWeakObjectPtr<AController>, TArray<TWeakObjectPtr<AOneBrahamSpawnPoint>>> FailedSpawnsOf;

	FTimerHandle MatchPhaseTimerHandle;
	FTimerHandle RespawnTimerHandle;

	float MatchStartTime = 0.f;
	float WarmupEndTime = 0.f;

	bool bBotPracticeMode = false;
	bool bMatchFinished = false;

	/** -1 fill every empty slot, 0 no bots, N at most N (`?Bots=N`, match settings). */
	int32 BotLimit = 0;

	/** Every bot this match created, for spawning at match start and giving way to humans. */
	UPROPERTY()
	TArray<TObjectPtr<AOneBrahamBotController>> BotControllers;

	/** Hands out unique bot names across the match. */
	int32 NextBotOrdinal = 0;

	/** Floor -> a point on it the bots can path to (its first spawn point). */
	TMap<int32, FVector> BotFloorAnchors;

	/** Floor -> walkable group: floors in one group are joined by navmesh. */
	TMap<int32, int32> BotFloorGroup;

	/** The floor graph waits for a navmesh build in progress (a packaged build's first frames). */
	bool bBotFloorGraphPending = false;

	/** Clock for re-trying bots that could not spawn (a crowded start). */
	float BotSpawnRetryAccumulator = 0.f;

	/** How the match ended (FinishMatch). */
	EOneBrahamMatchEnding MatchEnding = EOneBrahamMatchEnding::None;

	/** Decided once, when the match ends, so the end screen's countdown and the
	 *  travel agree about where everybody is going. */
	EOneBrahamPostMatchAction PostMatchAction = EOneBrahamPostMatchAction::ReloadMap;

	/** Squad list refresh + end check cadence while the match runs. */
	float SquadRefreshAccumulator = 0.f;

	/** One FIRE DRILL point's state: who holds it, who is taking it. */
	struct FControlPointState
	{
		int32 OwnerTeam = INDEX_NONE;
		int32 CapturingTeam = INDEX_NONE;
		float CaptureProgressSeconds = 0.f;
		bool bLocated = false;
		FBox Bounds = FBox(ForceInit);
	};
	TArray<FControlPointState> ControlPointStates;
	float ControlPointAccumulator = 0.f;

	/** Fractional per-second score carried between control point ticks, per team. */
	TArray<float> ControlPointScoreCarry;

	/** Every other player state's name, for MakeUniquePlayerName. */
	TArray<FString> GatherTakenPlayerNames(const APlayerState* Except) const;

	/** The name a local player (listen host, standalone) falls back to: this
	 *  map's own ?OBName=/?Name=, then the profile. Empty for a remote one. */
	FString GetLocalPlayerFallbackName(const AController* Controller) const;

	/** Keeps a human's current name if it is a person's, else the fallback or a
	 *  generated office name; always sanitised and unique. PostLogin and
	 *  seamless travel, after InitNewPlayer has had its say. */
	void EnsureHumanPlayerName(AController* Controller);

	/** Parsed from the travel options. PendingPlayerName is the map URL's own
	 *  ?OBName= (else ?Name=): the listen host's name, never anybody else's. */
	FString PendingModeString;
	FString PendingPlayerName;
	FString PendingArchetype;
};
