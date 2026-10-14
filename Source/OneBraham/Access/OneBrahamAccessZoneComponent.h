// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamAccessZoneComponent.h
// ---------------------------------------------------------------------------
// The door, the reader, and the terminal - all three, as one component.
//
// WHY ONE COMPONENT
//   Every locked space in One Braham needs the same three things: a zone that is
//   either open or shut, something that can let you through it, and a way to
//   force it. Splitting them into three actors means three interaction prompts,
//   three anti-cheat paths and three chances for the map to be inconsistent.
//   This component owns all of it, and the map places one on each door, each lift
//   shaft and each floor transition.
//
// THE THREE WAYS UP, AND WHAT EACH ONE COSTS
//   1. Keycard      - instant, silent, requires having taken it off a body.
//   2. Override     - hold E on the panel: 12 seconds alone, 3 seconds if a
//                     squadmate holds the second panel (the "two-person rule" is
//                     the map's best co-op moment outside the extraction).
//   3. Breach       - a charge, 3 seconds on the timer, and the whole floor hears
//                     it. Implemented by whatever is placed over the door; see
//                     IOneBrahamBreachableInterface.
//
// The lockdown director can also force a zone open (Escalation) when the building
// itself starts forcing people upward, which is what stops a squad from holding a
// sealed floor forever.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Core/OneBrahamTypes.h"
#include "OneBrahamAccessZoneComponent.generated.h"

class AOneBrahamPlayerController;
class UOneBrahamAudioManagerComponent;

/** Raised on every machine when a zone's state changes, so doors can animate. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOneBrahamZoneChanged, FName, ZoneId, bool, bUnlocked, EOneBrahamUnlockMethod, Method);

/** Raised while an override is in progress, for the progress bar and the hum. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOneBrahamOverrideProgress, FName, ZoneId, float, Progress01);

UCLASS(ClassGroup = (OneBraham), meta = (BlueprintSpawnableComponent))
class ONEBRAHAM_API UOneBrahamAccessZoneComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UOneBrahamAccessZoneComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// ======================================================================
	//  Placement
	// ======================================================================
	/** Stable zone id from the map spec: "Lift_Lobby_To_3", "Server_9", "Skydeck_Gate". */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Access")
	FName ZoneId = NAME_None;

	/** What the HUD and the door sign call this place. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Access")
	FText ZoneDisplayName;

	/** Badge needed to open it with a card. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Access")
	EOneBrahamAccessTier RequiredTier = EOneBrahamAccessTier::Staff;

	/** Floor the zone sits on (0 = plaza). Used by the lockdown director. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Access")
	int32 FloorIndex = 1;

	/** Destinations reachable through this zone: floor indices, for the map's lifts. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Access")
	TArray<int32> ServesFloors;

	// ======================================================================
	//  Rules
	// ======================================================================
	/** Seconds of terminal hold to override alone (12 s shipping). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Access")
	float OverrideSeconds = 12.f;

	/** Seconds of terminal hold when a second panel is held (3 s shipping). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Access")
	float OverrideTwoPersonSeconds = 3.f;

	/** True when a breaching charge can defeat this zone's own door. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Access")
	bool bBreachable = true;

	/** True when the lockdown director may force this open as the building escalates. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Access")
	bool bEscalationOpens = true;

	/** True when the zone starts unlocked (the lobby, the plaza, the stairs). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Access")
	bool bUnlockedAtStart = false;

	/** Second panel that makes the override fast. Pair both ends of a lift shaft. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Access")
	FName PairedZoneId = NAME_None;

	// ======================================================================
	//  Server API
	// ======================================================================
	/** True when the given badge set can open this zone with a card. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Access")
	bool CanUnlockWithTier(EOneBrahamAccessTier HeldTier) const;

	/** Server: opens the zone. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Access")
	bool ServerUnlock(EOneBrahamUnlockMethod Method, int32 UnlockerPlayerId = -1);

	/** Server: closes the zone again (the lockdown director, or a reset). */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Access")
	void ServerLock();

	/** Server: begins or continues an override hold. Returns progress 0..1. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Access")
	float ServerHoldOverride(AOneBrahamPlayerController* Interactor, bool bPairedPanelHeld, float DeltaSeconds);

	/** Server: cancels an in-progress override (player moved, was shot, stopped). */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Access")
	void ServerCancelOverride(AOneBrahamPlayerController* Interactor);

	// ======================================================================
	//  Queries (safe on clients)
	// ======================================================================
	UFUNCTION(BlueprintPure, Category = "One Braham|Access")
	bool IsUnlocked() const { return bUnlocked; }

	UFUNCTION(BlueprintPure, Category = "One Braham|Access")
	EOneBrahamUnlockMethod GetUnlockMethod() const { return LastUnlockMethod; }

	UFUNCTION(BlueprintPure, Category = "One Braham|Access")
	float GetOverrideProgress01() const { return OverrideProgress; }

	UFUNCTION(BlueprintPure, Category = "One Braham|Access")
	bool IsOverrideInProgress() const { return OverrideProgress > 0.f; }

	/** The prompt the HUD shows when this zone is the interaction target. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Access")
	FText GetInteractionPrompt(const AOneBrahamPlayerController* Interactor) const;

	/** Seconds the override will take for this player (the paired rule changes it). */
	UFUNCTION(BlueprintPure, Category = "One Braham|Access")
	float GetOverrideSecondsFor(const AOneBrahamPlayerController* Interactor) const;

	/** Localised description of why a player cannot open it ("Executive badge required"). */
	UFUNCTION(BlueprintPure, Category = "One Braham|Access")
	FText GetRefusalText(const AOneBrahamPlayerController* Interactor) const;

	// ======================================================================
	//  Events
	// ======================================================================
	UPROPERTY(BlueprintAssignable, Category = "One Braham|Access")
	FOneBrahamZoneChanged OnZoneChanged;

	UPROPERTY(BlueprintAssignable, Category = "One Braham|Access")
	FOneBrahamOverrideProgress OnOverrideProgress;

	/** Authority-side, matching the project's naming: the server is the system of record. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Access")
	bool IsSystemAuthoritative() const { return GetOwnerRole() == ROLE_Authority; }

protected:
	/** Server: the one place the flag, the delegates and the net update change. */
	void ApplyUnlockState(bool bNewUnlocked, EOneBrahamUnlockMethod Method, int32 UnlockerPlayerId);

	/** Local: the audio/lighting response to a state change. */
	void ApplyLocalZoneEffects(bool bNewUnlocked, EOneBrahamUnlockMethod Method);

	UFUNCTION(NetMulticast, Reliable)
	void MulticastZoneUnlocked(EOneBrahamUnlockMethod Method, int32 UnlockerPlayerId);

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastOverrideFeedback(float Progress01);

	UFUNCTION()
	void OnRep_Unlocked();

	// --- replicated state -------------------------------------------------
	UPROPERTY(ReplicatedUsing = OnRep_Unlocked, BlueprintReadOnly, Category = "One Braham|Access")
	bool bUnlocked = false;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "One Braham|Access")
	EOneBrahamUnlockMethod LastUnlockMethod = EOneBrahamUnlockMethod::None;

	/** Who opened it, for the kill feed and the "who has been here" storytelling. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "One Braham|Access")
	int32 UnlockedByPlayerId = INDEX_NONE;

	/** Server-side progress through a terminal hold. Not replicated: feedback is. */
	float OverrideProgress = 0.f;

	/** The interactor currently holding the terminal, so a second player cannot stack. */
	TWeakObjectPtr<AOneBrahamPlayerController> ActiveOverrider;

	/** Throttle for the override progress feedback RPC. */
	float FeedbackAccumulator = 0.f;

	/** Cached: the audio manager of the local player, for the terminal hum. */
	TWeakObjectPtr<UOneBrahamAudioManagerComponent> CachedLocalAudio;
};
