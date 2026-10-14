// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamOverrideTerminal.h
// ---------------------------------------------------------------------------
// The panel you hold E on for twelve seconds while the whole floor listens.
//
// This is the co-op centrepiece of ASCENT. A keycard is quiet and fast; a
// terminal is loud, slow, and the only way up when nobody has the badge. The
// cost is the alarm: a red strobe, a klaxon on the floor, and a name on the
// tannoy. Everyone in the building now knows which floor you are on, and they
// know for twenty five seconds.
//
// THE TWO-PERSON RULE
//   Paired panels - both ends of the server floor, both ends of the Skydeck
//   stair - need twelve seconds alone and three seconds with a squadmate on the
//   other panel. That single rule converts the strongest room in the game into a
//   room you have to take with a friend, and it is why the map has two panels in
//   those two places and nowhere else.
//
// WHERE THE RULES LIVE
//   The zone component owns unlocking. This actor owns the interaction, the
//   alarm, the screen and the strobe. It talks to the zone, never around it.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/OneBrahamInteractionInterface.h"
#include "Core/OneBrahamTypes.h"
#include "OneBrahamOverrideTerminal.generated.h"

class UBoxComponent;
class UStaticMeshComponent;
class UPointLightComponent;
class UTextRenderComponent;
class UOneBrahamAccessZoneComponent;
class UOneBrahamAccessZone;
class UOneBrahamAudioManagerComponent;
class AOneBrahamPlayerController;

/** Raised on every machine when the alarm starts or stops. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOneBrahamTerminalAlarm, FName, TerminalId, bool, bActive);

UCLASS(Blueprintable)
class ONEBRAHAM_API AOneBrahamOverrideTerminal : public AActor, public IOneBrahamInteractionInterface
{
	GENERATED_BODY()

public:
	AOneBrahamOverrideTerminal();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// ======================================================================
	//  Placement
	// ======================================================================
	/** Stable id: "Terminal_Server09_A". */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Terminal")
	FName TerminalId = NAME_None;

	/** Zone this panel opens. Left empty, the panel binds to the nearest door. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Terminal")
	FName TargetZoneId = NAME_None;

	/** The other panel of the pair. Both must be held for the 3 second override. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Terminal")
	FName PairedTerminalId = NAME_None;

	/** Shown on the screen and in the prompt: "SERVER FLOOR - NORTH". */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Terminal")
	FText TerminalDisplayName;

	/** How far away a door can be and still be the one this panel opens. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Terminal")
	float ZoneLinkRadius = 900.f;

	/** True when the panel is mounted on a wall (scales the body differently). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Terminal")
	bool bWallMounted = false;

	// ======================================================================
	//  Alarm
	// ======================================================================
	/** True when forcing this panel trips the floor alarm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Terminal")
	bool bTripsAlarm = true;

	/** Seconds of holding before the alarm starts. Short enough that you cannot
	 *  finish a solo override without setting it off. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Terminal")
	float AlarmStartSeconds = 4.f;

	/** Seconds the alarm keeps sounding after the override finishes or fails. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Terminal")
	float AlarmDurationSeconds = 25.f;

	/** True when the panel announces the override on the building tannoy. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Terminal")
	bool bAnnouncesOnTannoy = true;

	// ======================================================================
	//  Queries
	// ======================================================================
	UFUNCTION(BlueprintPure, Category = "One Braham|Terminal")
	bool IsAlarmActive() const { return bAlarmActive; }

	/** 0..1 progress of the hold currently running on this panel. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Terminal")
	float GetOverrideProgress01() const;

	/** The door this panel opens, resolved at BeginPlay. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Terminal")
	AOneBrahamAccessZone* GetTargetZone() const;

	UFUNCTION(BlueprintPure, Category = "One Braham|Terminal")
	FText GetTerminalDisplayName() const;

	// ======================================================================
	//  IOneBrahamInteractionInterface
	// ======================================================================
	virtual FText GetInteractionPrompt_Implementation(const AOneBrahamPlayerController* Interactor) const override;
	virtual FGameplayTag GetInteractionTag_Implementation(const AOneBrahamPlayerController* Interactor) const override;
	virtual bool CanInteract_Implementation(const AOneBrahamPlayerController* Interactor) const override;
	virtual bool RequiresHold_Implementation(const AOneBrahamPlayerController* Interactor) const override;
	virtual float GetHoldDuration_Implementation(const AOneBrahamPlayerController* Interactor) const override;
	virtual float GetInteractionRange_Implementation(const AOneBrahamPlayerController* Interactor) const override;
	virtual bool OnInteractionStart_Implementation(AOneBrahamPlayerController* Interactor) override;
	virtual void OnInteractionHold_Implementation(AOneBrahamPlayerController* Interactor, float HeldSeconds, float Progress01) override;
	virtual void OnInteractionEnd_Implementation(AOneBrahamPlayerController* Interactor, bool bCompleted) override;
	virtual void OnInteractionCancelled_Implementation(AOneBrahamPlayerController* Interactor) override;
	virtual void SetInteractionHighlight_Implementation(bool bHighlighted) override;

	// ======================================================================
	//  Events
	// ======================================================================
	UPROPERTY(BlueprintAssignable, Category = "One Braham|Terminal")
	FOneBrahamTerminalAlarm OnAlarmChanged;

protected:
	// --- components -------------------------------------------------------
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Components")
	TObjectPtr<USceneComponent> Root = nullptr;

	/** The console body. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Components")
	TObjectPtr<UStaticMeshComponent> BodyMesh = nullptr;

	/** The screen: emissive, tinted by state, dimmed while the alarm is up. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Components")
	TObjectPtr<UStaticMeshComponent> ScreenMesh = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Components")
	TObjectPtr<UPointLightComponent> ScreenLight = nullptr;

	/** The interaction volume: a panel is used from the front, standing still. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Components")
	TObjectPtr<UBoxComponent> UseVolume = nullptr;

	/** Floor label on the fascia, so a player can read it while being shot at. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Components")
	TObjectPtr<UTextRenderComponent> LabelText = nullptr;

	// --- state ------------------------------------------------------------
	UPROPERTY(ReplicatedUsing = OnRep_Alarm, BlueprintReadOnly, Category = "One Braham|Terminal")
	bool bAlarmActive = false;

	/** Server: world time the alarm should stop. */
	float AlarmEndTime = 0.f;

	/** Server: true once the alarm has been raised for the current attempt. */
	bool bAlarmRaised = false;

	/** Server: the interactor currently holding this panel, for delta timing. */
	TWeakObjectPtr<AOneBrahamPlayerController> HoldInteractor;
	float HoldLastSeconds = 0.f;

	/** Local: highlighted by the local player's interaction trace. */
	bool bHighlighted = false;

	/** Cached: the door this panel opens (bound in BeginPlay, refreshed on demand). */
	UPROPERTY(Transient)
	TWeakObjectPtr<AOneBrahamAccessZone> CachedZone;

	// --- internals --------------------------------------------------------
	UFUNCTION()
	void OnRep_Alarm();

	/** Binds the panel to a door: by id when given, by proximity otherwise. */
	void ResolveTargetZone();

	/** Re-binds after level streaming has invalidated the cached door. */
	void RefreshTargetZone();

	/** Server: raises the alarm, once per attempt. */
	void ServerRaiseAlarm();

	/** Server: clears the alarm and tells every machine. */
	void ServerClearAlarm();

	/** Local: the strobe, the screen tint and the klaxon one-shot. */
	void ApplyAlarmPresentation(bool bActive);

	/** True when the paired panel is being held right now. */
	bool IsPairedPanelHeld() const;

	/** The paired terminal actor, if it is loaded. */
	AOneBrahamOverrideTerminal* FindPairedTerminal() const;

	static UOneBrahamAudioManagerComponent* GetAudioFor(const AOneBrahamPlayerController* PC);

	/** Every machine: the audio for one panel event. */
	void PlayTerminalEffects(FName SoundId, float Volume, float Pitch = 1.f);
};
