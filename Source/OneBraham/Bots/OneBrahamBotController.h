// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamBotController.h
// ---------------------------------------------------------------------------
// The bots that fill empty squad slots.
//
// WHERE THEY COME FROM
//   AOneBrahamGameMode fills every slot no human holds when the match starts
//   (OneBrahamBotRules: ASCENT and FIRE DRILL by default, `?Bots=N` to cap,
//   `?Bots=0` for none). A solo session or a half-full lobby therefore still has
//   three squads in the tower (or two full FIRE DRILL teams). The game mode
//   creates the controller, gives it a squad and spawns its pawn with the same
//   rules as a player's - ASCENT bots walk in off the plaza when the warmup
//   ends - and the death path respawns it where the mode respawns anyone.
//
// WHAT THEY DO
//   Travel: walk the baked navmesh (the stairs run ground to roof) toward the
//   mode's objective - the Skydeck pad in ASCENT, a control point in FIRE
//   DRILL - accepting a partial path and re-planning as they climb, with a
//   stall check that walks them out of a dead end. On the objective they hold
//   the area. Fight: a sight scan of every pawn in range (plus the perception
//   component's sight and hearing), a reaction delay, aim that walks onto the
//   target, and burst fire through the weapon's own server shot path, so
//   damage, ammo, FX and the kill feed are exactly a player's.
//
// THE BOTS ARE DELIBERATELY MEDIOCRE
//   Reaction time is 0.25-0.85 s, aim error is 1.5-6 degrees, they do not lead
//   moving targets, they lose track of players who break line of sight for more
//   than six seconds, and they never use a utility item. A bot that out-shoots a
//   human teaches nothing and feels awful. Tuning against play data is M5.
//
// SCOPE LIMIT (documented, not accidental)
//   Bots are server-side. They hold a server-side AOneBrahamPlayerState so
//   teams, damage and the scoreboard work; their pawns replicate like anyone's.
//   They do not open locked doors, call lifts or carry keycards: they climb as
//   far as the navmesh the doors leave open reaches.

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "Core/OneBrahamTypes.h"
#include "OneBrahamBotController.generated.h"

class AOneBrahamCharacter;
class AOneBrahamGameMode;
class UAIPerceptionComponent;
class UAISenseConfig_Hearing;
class UAISenseConfig_Sight;
class UOneBrahamWeaponComponent;

/** What a bot is currently doing. Deliberately a small set. */
UENUM(BlueprintType)
enum class EOneBrahamBotState : uint8
{
	/** No pawn yet (or waiting to respawn). */
	Idle = 0,
	/** Travelling to (or holding) the mode's objective. */
	Patrol = 1,
	/** Moving to a position it can shoot the enemy from. */
	Advance = 2,
	/** Standing still and shooting. */
	Engage = 3,
	/** Walking backwards while shooting (badly). */
	Retreat = 4,
	/** Reloading behind cover. */
	Reload = 5,
	/** Downed or dead: does nothing until the mode respawns it. */
	Downed = 6
};

UCLASS(Blueprintable)
class ONEBRAHAM_API AOneBrahamBotController : public AAIController
{
	GENERATED_BODY()

public:
	AOneBrahamBotController(const FObjectInitializer& ObjectInitializer);

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;

	/** The bot owns its aim: the engine's version zeroes pitch without a focus actor. */
	virtual void UpdateControlRotation(float DeltaTime, bool bUpdatePawn = true) override;

	// ======================================================================
	//  Configuration (set by AOneBrahamGameMode when it fills a squad)
	// ======================================================================
	/** 0 = hopeless, 1 = a decent human. Affects reaction, aim and movement. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Bots", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BotSkill = 0.5f;

	/** Which team the bot fights for. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Bots")
	EOneBrahamTeam PreferredTeam = EOneBrahamTeam::None;

	/** Squad index the bot belongs to. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Bots")
	int32 PreferredSquadId = 0;

	/** The bot's place in its squad (0-based): spreads skill and objectives. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Bots")
	int32 SquadOrdinal = 0;

	/** Pawns this bot has been given this match: a mode without respawns gives one. */
	int32 LivesStarted = 0;

	/**
	 * Server: writes team, squad, name and archetype onto the bot's PlayerState.
	 * Called by the game mode before the pawn is spawned, so spawn selection
	 * already knows which squad it is placing.
	 */
	void InitBot(EOneBrahamTeam Team, int32 SquadId, int32 OrdinalInSquad, float Skill, const FString& DisplayName, EOneBrahamMatchMode Mode);

	/** Name shown in the kill feed and scoreboard. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Bots")
	void SetBotName(const FText& NewName);

	// ======================================================================
	//  Queries
	// ======================================================================
	UFUNCTION(BlueprintPure, Category = "One Braham|Bots")
	EOneBrahamBotState GetBotState() const { return BotState; }

	UFUNCTION(BlueprintPure, Category = "One Braham|Bots")
	AActor* GetCurrentTarget() const { return CurrentTarget.Get(); }

	/** True when the bot has a loaded weapon and line of sight to its target. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Bots")
	bool HasFiringSolution() const;

	/** Rounds this bot has put downrange, damage those rounds did, and how far it has walked. */
	int32 GetShotsFired() const { return ShotsFired; }
	int32 GetShotsHit() const { return ShotsHit; }
	float GetDamageDealt() const { return DamageDealt; }
	float GetDistanceTravelledCm() const { return DistanceTravelledCm; }

	/** Debug string used by `ob.Bots.Dump` and the status log. */
	FString GetBotDebugString() const;

protected:
	UFUNCTION()
	void HandleTargetPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus);

	/** Bound to the pawn's health component: being shot is a contact. */
	UFUNCTION()
	void HandleDamageTaken(float Damage, AActor* DamageInstigator, FVector InstigatorLocation);

	/** Builds the sight/hearing senses once, in the constructor. */
	void ConfigurePerception();

	/** Looks for the nearest visible enemy pawn; the perception system's backstop. */
	void ScanForEnemies();

	/** Takes a new target, starting the reaction delay if the bot had none. */
	void AcquireTarget(AActor* NewTarget);

	/** Re-evaluates the target and picks a state for this tick. */
	void UpdateCombatState(float DeltaSeconds);

	/** Objective travel / advance / retreat, one decision every quarter second. */
	void UpdateMovement(float DeltaSeconds);

	/** Objective travel when nothing needs shooting. */
	void UpdateObjectiveMovement(float DeltaSeconds);

	/** Turns to face the target within the bot's skill-limited turn rate, or the way it walks. */
	void UpdateAim(float DeltaSeconds);

	/** Trigger discipline: bursts, single shots, pauses between them. */
	void UpdateFiring(float DeltaSeconds);

	/** Reloads when it is safe to, which it does not do very well. */
	void UpdateReload();

	/** Periodic `[OB.BOTS] status` line (ob.Bots.StatusInterval). */
	void UpdateStatusLog(float DeltaSeconds);

	/** True when the given actor is on the bot's team. */
	bool IsFriendly(const AActor* Other) const;

	/** True when the bot can see the actor right now (visibility trace). */
	bool HasLineOfSight(const AActor* Other) const;

	/** True when the pawn is a live, standing enemy worth shooting. */
	bool IsValidEnemy(const AActor* Other) const;

	/** Asks the game mode where this bot's objective is. */
	bool RefreshObjective();

	/** A point StepCm away in the first direction (from PreferredDirection round) the capsule can walk straight to. */
	bool FindFreeStep(const FVector& From, const FVector& PreferredDirection, FVector& OutPoint) const;

	/** A navigable point near Origin, or Origin when there is no navmesh. */
	FVector RandomNavPointNear(const FVector& Origin, float RadiusCm) const;

	/** Issues a pathfinding move and logs the first path the bot is given. */
	void MoveBotTo(const FVector& Destination, float AcceptanceRadius);

	/** The weapon component of the bot's pawn, if any. */
	UOneBrahamWeaponComponent* GetWeapons() const;

	AOneBrahamCharacter* GetBotCharacter() const;

	AOneBrahamGameMode* GetOBGameMode() const;

	// --- perception -------------------------------------------------------
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Bots")
	TObjectPtr<UAIPerceptionComponent> OBPerception = nullptr;

	UPROPERTY()
	TObjectPtr<UAISenseConfig_Sight> SightConfig = nullptr;

	UPROPERTY()
	TObjectPtr<UAISenseConfig_Hearing> HearingConfig = nullptr;

	// --- state ------------------------------------------------------------
	EOneBrahamBotState BotState = EOneBrahamBotState::Idle;

	/** Weak so a dead player does not keep a bot looking at a corpse. */
	TWeakObjectPtr<AActor> CurrentTarget;

	/** Seconds since the bot last saw its target; it forgets after a while. */
	float TimeSinceLostSight = 999.f;

	/** Reaction delay before the first shot after acquiring a target. */
	float ReactionRemaining = 0.f;

	/** Burst discipline. */
	float BurstCooldown = 0.f;
	float TimeFiring = 0.f;
	/** World time the weapon may next fire (the bot's own rate of fire). */
	float NextShotTime = 0.f;

	float MovementAccumulator = 0.f;
	float ScanAccumulator = 0.f;
	float StatusAccumulator = 0.f;

	/** Where the bot is going when there is nobody to shoot. */
	FVector ObjectiveLocation = FVector::ZeroVector;
	int32 ObjectiveFloor = INDEX_NONE;
	bool bHasObjective = false;

	/** Current travel destination (objective, hold point or unstick point). */
	FVector TravelDestination = FVector::ZeroVector;
	float RepathAccumulator = 0.f;

	/** Stall detection: where the bot was at the last check, and how long ago. */
	FVector StallCheckLocation = FVector::ZeroVector;
	float StallCheckSeconds = 0.f;
	/** While positive, the bot is walking to a random point to get unstuck. */
	float UnstickRemaining = 0.f;
	int32 StallCount = 0;

	/** Where the bot's feet are, cached for the floor test. */
	int32 CurrentFloor = 1;
	int32 HighestFloorReached = INDEX_NONE;

	/** Spawn point the bot started at. */
	FVector HomeLocation = FVector::ZeroVector;

	/** Odometer. */
	FVector LastTickLocation = FVector::ZeroVector;
	float DistanceTravelledCm = 0.f;

	/** Combat stats for the log. */
	int32 ShotsFired = 0;
	int32 ShotsHit = 0;
	float DamageDealt = 0.f;

	/** Clock for refreshing the held weapons the capsule ignores when moving. */
	float CarriedIgnoreAccumulator = 1.f;
	int32 LivesStartedAtLastIgnore = INDEX_NONE;

	/** The first path is logged per life: it is the navmesh diagnosis. */
	bool bLoggedFirstPath = false;
};
