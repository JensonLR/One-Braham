// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamLockdownComponent.h
// ---------------------------------------------------------------------------
// The building's escalation director. Lives on AOneBrahamGameState.
//
// WHY THIS EXISTS
//   ASCENT is a game about being pushed upward. If nothing forced movement, the
//   optimal strategy would be to sit on floor 1 with a shotgun, and the map's
//   fifteen floors would be decoration. The lockdown director removes that
//   option in four escalating stages, on a timer that every player can read in
//   the HUD, with an audible warning before each step.
//
//   Normal      - everything works. Lifts run. Announcements are polite.
//   PreLockdown - "Attention: the building is now in lockdown." Lift lobby
//                 doors stop accepting cards below the highest occupied floor.
//   Lockdown    - fire shutters seal every stairwell below the highest occupied
//                 floor. Lifts stop. The canteen and Skydeck stay open.
//   Purge       - security purge on skipped floors: sprinklers, gas, and the
//                 building's own security response. Damage over time on floors
//                 at or below PurgeFloorCeiling, which is deliberately below the
//                 Skydeck so the final fight is never decided by gas.
//
// AUTHORITY
//   Stage progression is server-only. The stage, the server time the stage ends
//   at and the "warning active" flag are replicated so the HUD, the audio
//   director and the map's lighting all read the same truth.
//
//   The countdown is a deadline, not a duration: a replicated "seconds left"
//   is only right at the instant it was sent, and a client that joined late (or
//   missed the stage's start) had nothing to count it down from but its own
//   clock. Every machine subtracts the game state's server world time from the
//   one replicated end time instead, so a player who joins mid-Purge reads the
//   same number as the host.
//
// THE END OF A MATCH
//   The game mode stops the director when the match ends (StopDirector): the
//   purge stops pulsing under the end screen and the shutters open. A settings
//   asset with bLockdownEnabled off never starts it. Outside Shipping,
//   ob.Lockdown.StageSeconds compresses every stage for a full-match test run.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Core/OneBrahamTypes.h"
#include "OneBrahamLockdownComponent.generated.h"

class AOneBrahamCharacter;
class AOneBrahamGameState;
class AOneBrahamMatchSettingsAsset;

/** Raised when a floor's shutters should move. Bound by the map's Blueprints. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOneBrahamShutterEvent, int32, FloorIndex, bool, bClosing, float, DurationSeconds);

/** Raised on every stage change, after the announcement. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOneBrahamLockdownStageChanged, EOneBrahamLockdownStage, NewStage);

/**
 * Implemented by anything in the level that can be sealed: fire shutters, lift
 * doors, the Skydeck access gate. Blueprint-friendly so the map does not need
 * C++ to participate.
 */
UINTERFACE(MinimalAPI, BlueprintType)
class UOneBrahamShutterInterface : public UInterface
{
	GENERATED_BODY()
};

class ONEBRAHAM_API IOneBrahamShutterInterface
{
	GENERATED_BODY()

public:
	/** The floor this shutter seals. Floor 0 is the plaza. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "One Braham|Lockdown")
	int32 GetShutterFloor() const;

	/** Move the shutter. Duration is supplied so lifts and steel doors differ. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "One Braham|Lockdown")
	void SetShutterClosed(bool bClosed, float DurationSeconds);

	/** True once the shutter is fully closed and blocking. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "One Braham|Lockdown")
	bool IsShutterClosed() const;
};

UCLASS(ClassGroup = (OneBraham), meta = (BlueprintSpawnableComponent))
class ONEBRAHAM_API UOneBrahamLockdownComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UOneBrahamLockdownComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// ======================================================================
	//  Server control
	// ======================================================================
	/** Starts escalation. Called by the game mode when the match goes live. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Lockdown")
	void StartDirector();

	/** Stops escalation (warm-up, FIRE DRILL, practice). */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Lockdown")
	void StopDirector();

	/** Debug / QA: jumps straight to a stage. Bound to `ob.Lockdown.ForceStage`. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Lockdown")
	void ServerForceStage(EOneBrahamLockdownStage NewStage);

	/** Registers a shutter actor so the director can seal floors. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Lockdown")
	void RegisterShutter(AActor* ShutterActor);

	// ======================================================================
	//  Queries
	// ======================================================================
	UFUNCTION(BlueprintPure, Category = "One Braham|Lockdown")
	EOneBrahamLockdownStage GetStage() const { return Stage; }

	/** True from PreLockdown onward: the building is locked down. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Lockdown")
	bool IsLockdownActive() const { return Stage != EOneBrahamLockdownStage::Normal; }

	/** True while the 15 second warning for the next stage is playing. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Lockdown")
	bool IsWarningActive() const { return bWarningActive; }

	/**
	 * Seconds until the next stage change, on the server's clock (the game
	 * state's server world time), so every machine - a late joiner included -
	 * reads the same number. 0 when the director is not running and once the
	 * Purge has been reached: the Purge is the last stage and has no "next".
	 */
	UFUNCTION(BlueprintPure, Category = "One Braham|Lockdown")
	float GetSecondsToNextStage() const;

	/** Server world time the current stage ends at; 0 when nothing is counting down. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Lockdown")
	double GetStageEndServerTime() const { return StageEndServerTime; }

	/**
	 * The countdown arithmetic, pure so it can be tested without a world:
	 * seconds from ServerNow to StageEnd, 0 when the director is stopped, the
	 * stage is the Purge or no end is set.
	 */
	static float ComputeSecondsToNextStage(EOneBrahamLockdownStage InStage, bool bRunning,
		double InStageEndServerTime, double ServerNow);

	/** Highest floor currently occupied by a living player. Drives which floors seal. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Lockdown")
	int32 GetHighestOccupiedFloor() const { return HighestOccupiedFloor; }

	/** True when the security purge is actively damaging players on this floor. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Lockdown")
	bool IsFloorBeingPurged(int32 FloorIndex) const;

	/** True when this floor's shutters have been told to close. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Lockdown")
	bool IsFloorSealed(int32 FloorIndex) const { return SealedFloors.Contains(FloorIndex); }

	/** How many floors are sealed right now (the QA overlay and the audit tool). */
	UFUNCTION(BlueprintPure, Category = "One Braham|Lockdown")
	int32 GetSealedFloorCount() const { return SealedFloors.Num(); }

	/** How many players the purge has killed this match (end-of-match stats). */
	UFUNCTION(BlueprintPure, Category = "One Braham|Lockdown")
	int32 GetPurgeKillCount() const { return PurgeKillCount; }

	UFUNCTION(BlueprintPure, Category = "One Braham|Lockdown")
	bool IsDirectorRunning() const { return bDirectorRunning; }

	/** Localised status string for the HUD objective line. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Lockdown")
	FText GetStageDisplayText() const;

	/** Announcement copy for a stage, used by the Tannoy and the HUD banner. */
	static FText GetStageAnnouncement(EOneBrahamLockdownStage InStage, int32 HighestFloor);

	// ======================================================================
	//  Events
	// ======================================================================
	/** FloorIndex == INDEX_NONE means "every shutter in the building". */
	UPROPERTY(BlueprintAssignable, Category = "One Braham|Lockdown")
	FOneBrahamShutterEvent OnShutterRequested;

	UPROPERTY(BlueprintAssignable, Category = "One Braham|Lockdown")
	FOneBrahamLockdownStageChanged OnStageChanged;

protected:
	/** Server: applies a stage to the world, then replicates it. */
	void ServerApplyStage(EOneBrahamLockdownStage NewStage, bool bAnnounce);

	/** Server: seals every floor below the highest occupied floor. */
	void ServerSealFloorsBelow(int32 FloorIndex, bool bClose);

	/** Server: purge damage tick on staged floors. */
	void ServerApplyPurgeDamage(float DeltaTime);

	/** Server: recomputes the highest occupied floor from living players. */
	void UpdateHighestOccupiedFloor();

	/** Server: picks the announcement + voice line for a stage. */
	void BroadcastStageAnnouncement(EOneBrahamLockdownStage NewStage);

	/** Local: pushes the stage into the audio director and the map's lighting. */
	void ApplyLocalStageEffects(EOneBrahamLockdownStage NewStage);

	UFUNCTION(NetMulticast, Reliable)
	void MulticastStageChanged(EOneBrahamLockdownStage NewStage, int32 InHighestFloor);

	UFUNCTION()
	void OnRep_Stage();

	// --- replicated state -------------------------------------------------
	UPROPERTY(ReplicatedUsing = OnRep_Stage, BlueprintReadOnly, Category = "One Braham|Lockdown")
	EOneBrahamLockdownStage Stage = EOneBrahamLockdownStage::Normal;

	/**
	 * Server world time (AGameStateBase::GetServerWorldTimeSeconds) at which the
	 * current stage ends. Sent once per stage, not ticked: it is a deadline, and
	 * a deadline is the same number for a player who joined a minute ago. 0 when
	 * nothing is counting down (director stopped, or the Purge).
	 */
	UPROPERTY(Replicated)
	double StageEndServerTime = 0.0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "One Braham|Lockdown")
	bool bWarningActive = false;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "One Braham|Lockdown")
	int32 HighestOccupiedFloor = 0;

	/** Replicated so clients run the same between-packet countdown. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "One Braham|Lockdown")
	bool bDirectorRunning = false;

	// --- server-only ------------------------------------------------------
	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<AActor>> RegisteredShutters;

	/** Floors currently sealed, so we only send shutter commands that matter. */
	TSet<int32> SealedFloors;

	bool bWarningFiredForStage = false;

	/** Throttles the 4 Hz occupancy rescan. */
	float OccupancyAccumulator = 0.f;

	/** Server: (re)starts the current stage's clock from now. */
	void ServerRestartStageClock();

	/** Server: the stage length from the match settings (or the fallback). */
	float GetStageSeconds() const;

	/** Server: last value of ob.Lockdown.ForceStage acted on (edge-triggered). */
	int32 LastForcedStageCVar = -1;

	/** Throttles the Verbose clock log. */
	float ClockLogAccumulator = 0.f;

	/** Purge bookkeeping. */
	int32 PurgeKillCount = 0;
	float PurgeDamageAccumulator = 0.f;

	/** Cached so the tick does not have to walk the player array twice. */
	UPROPERTY(Transient)
	mutable TWeakObjectPtr<AOneBrahamGameState> CachedGameState;
};
