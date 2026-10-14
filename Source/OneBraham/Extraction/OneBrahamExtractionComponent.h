// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamExtractionComponent.h
// ---------------------------------------------------------------------------
// The win condition of ASCENT, as a component on AOneBrahamGameState.
//
// THE SEQUENCE (all timings from UOneBrahamMatchSettingsAsset)
//   1. INACTIVE   - nothing has happened. The Skydeck terminals read "STANDBY".
//   2. BROADCAST  - a squad called the winch. A 10 second head start during which
//                   the whole building is told: lights, Tannoy, a 2D sting, and
//                   a HUD banner on every machine. This window is the design's
//                   tension valve: the calling squad knows they have just made
//                   themselves the most interesting thing in the tower.
//   3. ARRIVING   - 45 seconds of helicopter: rotor audio positions on the pad,
//                   the searchlight sweeps the deck, and the winch zone arms.
//   4. HOLDING    - 30 seconds inside the winch zone with no enemy squad also
//                   inside it. Contested time does not count (it pauses, it does
//                   not reset), which is what makes a last-second push worth it.
//   5. COMPLETE   - the squad is on the wire. The game mode ends the match.
//
// FAILURE MODES: the calling squad is wiped (CallersEliminated), an enemy squad
// pushes them out and they do not get the time back before the helicopter loiters
// out (LoiterExpired), or the security purge kills the whole deck (Purge).
//
// AUTHORITY: state machine server-side; state + hold progress replicated so every
// client can draw the same countdown without a single RPC per frame.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Core/OneBrahamTypes.h"
#include "OneBrahamExtractionComponent.generated.h"

class AOneBrahamCharacter;
class AOneBrahamGameState;
class AOneBrahamPlayerState;

/** Raised when the match is won: the game mode binds to this to finish up. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOneBrahamExtractionComplete, int32, WinningSquadId, EOneBrahamTeam, WinningTeam);

/** Raised when a squad's attempt fails, with the reason for the results screen. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOneBrahamExtractionFailed, int32, SquadId, EOneBrahamExtractionFailReason, Reason);

UCLASS(ClassGroup = (OneBraham), meta = (BlueprintSpawnableComponent))
class ONEBRAHAM_API UOneBrahamExtractionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UOneBrahamExtractionComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// ======================================================================
	//  Server API
	// ======================================================================
	/**
	 * A squad starts the winch from a Skydeck terminal.
	 * Returns false (with a reason) when the call is not legal.
	 */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Extraction")
	bool ServerCallExtraction(int32 SquadId, AOneBrahamPlayerState* CallingPlayer, FText& OutRefusal);

	/** Server: sets the winch pad location. Called by the map's extraction zone. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Extraction")
	void ServerSetHelipadLocation(const FVector& Location, float HoldRadiusCm = 900.f);

	/** Debug / QA: jumps the state machine forward. Bound to `ob.extraction.fast`,
	 *  which calls the winch first when nothing has called it yet - a QA pass
	 *  that has to walk to the Skydeck and hold a console is a QA pass nobody
	 *  runs. `ob.extraction.abort` exercises the failure path. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Extraction")
	void ServerSkipToHolding();

	/** Server: aborts an attempt (match ending by another route). */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Extraction")
	void ServerAbortExtraction(EOneBrahamExtractionFailReason Reason);

	// ======================================================================
	//  Queries (safe on clients)
	// ======================================================================
	UFUNCTION(BlueprintPure, Category = "One Braham|Extraction")
	EOneBrahamExtractionState GetExtractionState() const { return ExtractionState; }

	/** True from BROADCAST through HOLDING: the attempt is live and matters. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Extraction")
	bool IsExtractionActive() const;

	UFUNCTION(BlueprintPure, Category = "One Braham|Extraction")
	float GetHoldProgress01() const { return HoldProgress01; }

	/** Seconds left inside the current state (arrival, hold or loiter). */
	UFUNCTION(BlueprintPure, Category = "One Braham|Extraction")
	float GetSecondsRemainingInHold() const { return SecondsRemainingInState; }

	UFUNCTION(BlueprintPure, Category = "One Braham|Extraction")
	float GetSecondsRemainingInState() const { return SecondsRemainingInState; }

	UFUNCTION(BlueprintPure, Category = "One Braham|Extraction")
	int32 GetHoldingSquadId() const { return HoldingSquadId; }

	UFUNCTION(BlueprintPure, Category = "One Braham|Extraction")
	int32 GetClaimingSquadId() const { return ClaimingSquadId; }

	/** True when the given squad is the one currently making progress. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Extraction")
	bool IsSquadHoldingExtraction(int32 SquadId) const;

	/** True when an enemy squad is inside the zone and progress is paused. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Extraction")
	bool IsContested() const { return bContested; }

	/**
	 * True while the Skydeck should read as hostile: red floodlights, klaxon,
	 * no ambient music. Drives the deck's lighting and audio sub-level.
	 */
	UFUNCTION(BlueprintPure, Category = "One Braham|Extraction")
	bool IsDeckAlarmActive() const;

	/** Where the helicopter is (rotor audio, searchlight, compass marker). */
	UFUNCTION(BlueprintPure, Category = "One Braham|Extraction")
	FVector GetHelipadLocation() const { return HelipadLocation; }

	UFUNCTION(BlueprintPure, Category = "One Braham|Extraction")
	float GetHoldRadiusCm() const { return HoldRadiusCm; }

	UFUNCTION(BlueprintPure, Category = "One Braham|Extraction")
	EOneBrahamExtractionFailReason GetLastFailReason() const { return LastFailReason; }

	/** 0..1 progress through the arrival window, for the Skydeck beacon light. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Extraction")
	float GetArrivalAlpha01() const { return ArrivalAlpha01; }

	/** Number of players of the claiming squad inside the zone right now. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Extraction")
	int32 GetClaimantsInZone() const { return ClaimantsInZone; }

	/** Number of enemy players inside the zone right now. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Extraction")
	int32 GetContestersInZone() const { return ContestersInZone; }

	// ======================================================================
	//  Events
	// ======================================================================
	UPROPERTY(BlueprintAssignable, Category = "One Braham|Extraction")
	FOneBrahamExtractionComplete OnExtractionComplete;

	UPROPERTY(BlueprintAssignable, Category = "One Braham|Extraction")
	FOneBrahamExtractionFailed OnExtractionFailed;

		/** Replicated state change (declared in Core/OneBrahamTypes.h). */
	UPROPERTY(BlueprintAssignable, Category = "One Braham|Extraction")
	FOneBrahamExtractionStateChanged OnExtractionStateChanged;

protected:
	/** Server: transitions state, resets timers and broadcasts. */
	void ServerSetState(EOneBrahamExtractionState NewState);

	/** Server: per-tick evaluation of who is in the zone and what that means. */
	void ServerEvaluateHold(float DeltaTime);

	/** Counts players inside the winch zone, split by squad. */
	void CountPlayersInZone(int32& OutClaimants, int32& OutContesters) const;

	/** True when the calling squad still has someone on their feet. */
	bool IsClaimingSquadAlive() const;

	/**
	 * Pushes the replicated state into this machine's audio director: the rotor bed
	 * on the pad, its volume and pitch tracking the approach, and the music state.
	 * Local only and a no-op on a dedicated server. Same shape as the lockdown's
	 * local stage effects: one replicated state, and every machine that has a local
	 * player turns it into sound, rather than an RPC per client per state change.
	 */
	void ApplyLocalExtractionAudio();

	/** Plays the squad voice lines and the local stings for a state change. */
	UFUNCTION(NetMulticast, Reliable)
	void MulticastExtractionStateChanged(EOneBrahamExtractionState NewState, int32 SquadId, float DurationSeconds, int32 CallerPlayerId);

	UFUNCTION()
	void OnRep_ExtractionState();

	UFUNCTION()
	void OnRep_HoldProgress();

	// --- replicated state -------------------------------------------------
	UPROPERTY(ReplicatedUsing = OnRep_ExtractionState, BlueprintReadOnly, Category = "One Braham|Extraction")
	EOneBrahamExtractionState ExtractionState = EOneBrahamExtractionState::Inactive;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "One Braham|Extraction")
	int32 ClaimingSquadId = INDEX_NONE;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "One Braham|Extraction")
	int32 HoldingSquadId = INDEX_NONE;

	UPROPERTY(ReplicatedUsing = OnRep_HoldProgress, BlueprintReadOnly, Category = "One Braham|Extraction")
	float HoldProgress01 = 0.f;

	/** Seconds left in the current state, replicated at 4 Hz for the HUD countdown. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "One Braham|Extraction")
	float SecondsRemainingInState = 0.f;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "One Braham|Extraction")
	bool bContested = false;

	/** 0..1 through the BROADCAST+ARRIVING windows, for the deck lighting. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "One Braham|Extraction")
	float ArrivalAlpha01 = 0.f;

	/** Pad location. Replicated once on first set: it never moves after that. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "One Braham|Extraction")
	FVector HelipadLocation = FVector(0.f, 0.f, 6000.f);

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "One Braham|Extraction")
	float HoldRadiusCm = 900.f;

	// --- server-only ------------------------------------------------------
	/** Head-start window before the helicopter becomes audible building-wide. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "One Braham|Extraction")
	int32 BroadcastDurationSeconds = 10;

	/** Live count of players in the zone, refreshed every 0.25 s. */
	int32 ClaimantsInZone = 0;
	int32 ContestersInZone = 0;

	EOneBrahamExtractionFailReason LastFailReason = EOneBrahamExtractionFailReason::None;

	/** Time since the hold started being uncontested, for the contest UX. */
	float UncontestedHoldTime = 0.f;

	/** Throttles the 4 Hz replication of the countdown while the state is static. */
	float ReplicationAccumulator = 0.f;

	/** Total time spent in the current state, for the arrival alpha. */
	float TimeInState = 0.f;

	// --- local ------------------------------------------------------------
	/** What the local audio director was last told; sound is a per-machine thing. */
	bool bPushedExtractionAudio = false;
	float PushedApproach01 = -1.f;

	/** Cached for the loudest possible announcement. */
	UPROPERTY(Transient)
	mutable TWeakObjectPtr<AOneBrahamGameState> CachedGameState;
};
