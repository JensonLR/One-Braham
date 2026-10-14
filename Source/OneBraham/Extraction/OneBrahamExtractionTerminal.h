// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamExtractionTerminal.h
// ---------------------------------------------------------------------------
// The console in the winch control room: the only thing in the game that calls
// the winch.
//
// THE ONE BUTTON THAT ENDS THE MATCH
//   Everything else in ASCENT is a way of getting up the building. This is the
//   thing you press when you are up. Holding it for three seconds tells the whole
//   tower which squad is leaving, and starts the ten second head start before the
//   helicopter is audible from the stairwell - which is the design's tension
//   valve: the squad that calls the winch has just made itself the most
//   interesting thing in the building, and it has to hold an open deck for thirty
//   seconds to convert.
//
// WHERE THE RULES LIVE
//   UOneBrahamExtractionComponent owns the state machine, the refusal reasons,
//   the timings and the hold. This actor owns the panel, the prompt and the
//   screen. It calls ServerCallExtraction() and puts the refusal it gets back on
//   the caller's banner; it decides nothing about the winch itself.
//
// WHY IT EXISTS AT ALL
//   ServerCallExtraction() - and with it the whole five state sequence of
//   broadcast, arrival, hold, complete and failed - shipped with no caller
//   anywhere in the project, so the win condition of ASCENT was unreachable and
//   the only way a match could end was elimination. The audio manager scored the
//   helicopter, the HUD drew the countdown, the game mode bound the completion
//   handler, and nothing could start it. This is the caller.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/OneBrahamInteractionInterface.h"
#include "Core/OneBrahamTypes.h"
#include "OneBrahamExtractionTerminal.generated.h"

class UStaticMeshComponent;
class UPointLightComponent;
class UTextRenderComponent;
class UOneBrahamAudioManagerComponent;
class UOneBrahamExtractionComponent;
class AOneBrahamGameState;
class AOneBrahamPlayerController;
class AOneBrahamPlayerState;

UCLASS(Blueprintable)
class ONEBRAHAM_API AOneBrahamExtractionTerminal : public AActor, public IOneBrahamInteractionInterface
{
	GENERATED_BODY()

public:
	AOneBrahamExtractionTerminal();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	// ======================================================================
	//  Placement
	// ======================================================================
	/** Stable id for the builder and the logs: "Winch_Control_Console". */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Winch")
	FName TerminalId = NAME_None;

	/** Shown on the fascia. Empty, the label falls back to the id. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Winch")
	FText TerminalDisplayName;

	/**
	 * Seconds of holding before the winch is called. 1.4 s is the length of
	 * `AM_OB_Interact_Radio`, which is the montage this tag plays: the hand comes
	 * off the weapon, keys the set, and comes back. The tension in the call is
	 * not in the button - it is the ten seconds of broadcast that follow it, in
	 * which the whole building is told where you are.
	 *
	 * The component re-validates everything the prompt promised when the hold
	 * completes, so a client that skips the hold still cannot call a winch the
	 * server would refuse.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Winch")
	float CallHoldSeconds = 1.4f;

	/** Reach. The console is waist height under a steel canopy, so: a step away. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Winch")
	float InteractionRangeCm = 220.f;

	// ======================================================================
	//  Queries
	// ======================================================================
	UFUNCTION(BlueprintPure, Category = "One Braham|Winch")
	bool IsWinchLive() const;

	UFUNCTION(BlueprintPure, Category = "One Braham|Winch")
	FText GetTerminalDisplayName() const;

	UFUNCTION(BlueprintPure, Category = "One Braham|Winch")
	UOneBrahamExtractionComponent* GetExtraction() const;

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

protected:
	// --- components -------------------------------------------------------
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Components")
	TObjectPtr<USceneComponent> Root = nullptr;

	/** The console body: a steel desk with a fascia. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Components")
	TObjectPtr<UStaticMeshComponent> BodyMesh = nullptr;

	/** The screen. Emissive if the material has `ScreenTint`, a lit panel if not. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Components")
	TObjectPtr<UStaticMeshComponent> ScreenMesh = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Components")
	TObjectPtr<UPointLightComponent> ScreenLight = nullptr;

	/** WINCH CONTROL / WINCH LIVE / STANDBY, readable while being shot at. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Components")
	TObjectPtr<UTextRenderComponent> LabelText = nullptr;

	// --- state ------------------------------------------------------------
	/** Server: the player currently holding the console, for progress feedback. */
	TWeakObjectPtr<AOneBrahamPlayerController> HoldInteractor;

	/** Server: the last whole second a progress tick was played on. */
	int32 LastProgressSecond = -1;

	/** Local: highlighted by the local player's interaction trace. */
	bool bHighlighted = false;

	/** Local: the colour currently on the screen, so Tick writes it once. */
	FLinearColor AppliedColour = FLinearColor::Black;

	// --- internals --------------------------------------------------------
	/** Reads the winch state and puts it on the screen, the light and the label. */
	void ApplyScreenState();

	/** Local: the outline pass, shared with the keycards and the panels. */
	void ApplyHighlight(bool bEnabled);

	/**
	 * Server: plays one sound here on every machine, through each listener's own
	 * mix. The server-side callers used to walk the player controllers and play
	 * through each one's audio manager: on a listen host that is once per player
	 * on the host's speakers, and on a client - which never runs the server code -
	 * nothing at all.
	 */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastPlayTerminalEffects(FName SoundId, float Volume, float Pitch = 1.f);

	/** The squad the interactor is on, from the game state's own table. */
	int32 ResolveCallingSquadId(const AOneBrahamPlayerController* Interactor) const;

};
