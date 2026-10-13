// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamBotController.h
// ---------------------------------------------------------------------------
// Practice-mode bots.
//
// ASCENT SHIPS WITH NO AI OPPONENTS - that is a design decision, not a missing
// feature: a twelve-player extraction shooter where every contact is a person is
// the entire point, and AI that plays "well enough" would dilute it. This
// controller exists for the three places where a thinking target is genuinely
// useful:
//
//   1. Testing. Solo-testing a fifteen-floor map without a second machine needs
//      somebody to shoot at, and somebody who shoots back.
//   2. Onboarding. A new player learning the map, the keycard chain and the
//      extraction flow benefits from bots that contest the Skydeck.
//   3. FIRE DRILL practice, where the mode's rhythm (respawn, push, capture)
//      is better learned against mobile opponents than against nothing.
//
// THE BOTS ARE DELIBERATELY MEDIOCRE
//   Reaction time is 0.25-0.85 s, aim error is 1.5-6 degrees, they do not lead
//   moving targets, they lose track of players who break line of sight for more
//   than six seconds, and they never use a utility item. A bot that out-shoots a
//   human teaches nothing and feels awful.
//
// SCOPE LIMIT (documented, not accidental)
//   Bots are a listen-server / local feature. They hold a server-side
//   AOneBrahamPlayerState so teams, damage and (on the host) the scoreboard work
//   correctly, but their state is not replicated to remote clients, because there
//   is no connection owning them. Run practice on a listen server.

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
	/** Walking to a patrol point. */
	Patrol = 1,
	/** Moving to a position it can shoot the enemy from. */
	Advance = 2,
	/** Standing still and shooting. */
	Engage = 3,
	/** Walking backwards while shooting (badly). */
	Retreat = 4,
	/** Reloading behind cover. */
	Reload = 5,
	/** Downed: crawling toward the nearest squadmate, or bleeding out. */
	Downed = 6
};

UCLASS(Blueprintable)
class ONEBRAHAM_API AOneBrahamBotController : public AAIController
{
	GENERATED_BODY()

public:
	AOneBrahamBotController(const FObjectInitializer& ObjectInitializer);

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;

	// ======================================================================
	//  Configuration (set by AOneBrahamGameMode::PopulateWithBots)
	// ======================================================================
	/** 0 = hopeless, 1 = a decent human. Affects reaction, aim and movement. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Bots", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BotSkill = 0.5f;

	/** Which team the bot fights for. Set before the pawn is spawned. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Bots")
	EOneBrahamTeam PreferredTeam = EOneBrahamTeam::None;

	/** Squad index the bot is treated as belonging to (0..3). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Bots")
	int32 PreferredSquadId = 0;

	/** Floor the bot is assigned to defend/patrol. INDEX_NONE = its spawn floor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Bots")
	int32 AssignedFloor = INDEX_NONE;

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

	/** Debug string used by `ob.Bots.Dump`. */
	FString GetBotDebugString() const;

protected:
	UFUNCTION()
	void HandleTargetPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus);

	/** Builds the sight/hearing senses once, in the constructor. */
	void ConfigurePerception();

	/** Server: spawns and possesses the bot's pawn through the game mode. */
	void SpawnBotPawn();

	/** Re-evaluates the target and picks a state for this tick. */
	void UpdateCombatState(float DeltaSeconds);

	/** Patrol / advance movement, one decision every few seconds. */
	void UpdateMovement(float DeltaSeconds);

	/** Turns to face the target within the bot's skill-limited turn rate. */
	void FaceTarget(float DeltaSeconds);

	/** Trigger discipline: bursts, single shots, pauses between them. */
	void UpdateFiring(float DeltaSeconds);

	/** Reloads when it is safe to, which it does not do very well. */
	void UpdateReload();

	/** True when the given actor is on the bot's team. */
	bool IsFriendly(const AActor* Other) const;

	/** True when the bot can see the actor right now (trace + FOV). */
	bool HasLineOfSight(const AActor* Other) const;

	/** Chooses the next patrol destination on the bot's own floor. */
	void PickPatrolDestination();

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

	/** Set when the bot loses sight, so it can remember for a few seconds. */
	float TimeSinceLostSight = 999.f;

	/** Reaction delay before the first shot after acquiring a target. */
	float ReactionRemaining = 0.f;

	/** Burst discipline. */
	float BurstCooldown = 0.f;
	float TimeFiring = 0.f;

	float StateAccumulator = 0.f;
	float MovementAccumulator = 0.f;
	float PatrolPickAccumulator = 0.f;

	/** Last point the bot was told to move to, so it does not re-issue orders. */
	FVector PatrolDestination = FVector::ZeroVector;

	/** Where the bot's feet are, cached for the floor test. */
	int32 CurrentFloor = 1;

	/** Spawn point the bot started at: it patrols around this. */
	FVector HomeLocation = FVector::ZeroVector;

	bool bPawnSpawnAttempted = false;
};
