// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamLiftPanel.h
// ---------------------------------------------------------------------------
// A lift button: the call panel beside a landing door, or the Up / Down pair
// inside the car.
//
// WHY A SEPARATE ACTOR
//   The interaction system validates a press against the target actor's
//   location - the server measures eye to actor and checks the player is facing
//   it. A lift actor stands at the bottom of a sixty-metre shaft, so a button on
//   floor twelve that was "the lift" would be out of range for everyone who
//   pressed it. Each button is its own small replicated actor, placed where the
//   button is, and the checks mean what they say. The lift spawns them on the
//   server; nothing in the level has to place them.
//
// WHAT A PRESS DOES
//   Landing: calls the car to this floor, or holds its doors if it is already
//   here. In the car: Up and Down cycle the destination one floor per press,
//   from whatever was chosen last, and the car leaves when its doors shut.
//   During lockdown every panel refuses, and says why.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/OneBrahamInteractionInterface.h"
#include "OneBrahamLiftPanel.generated.h"

class UBoxComponent;
class UStaticMeshComponent;
class AOneBrahamLift;
class AOneBrahamPlayerController;

/** What this button does. */
UENUM(BlueprintType)
enum class EOneBrahamLiftPanelRole : uint8
{
	/** Beside a landing door: calls the car to PanelFloor. */
	Landing = 0,
	/** Inside the car: next floor up. */
	CarUp = 1,
	/** Inside the car: next floor down. */
	CarDown = 2
};

UCLASS(NotPlaceable)
class ONEBRAHAM_API AOneBrahamLiftPanel : public AActor, public IOneBrahamInteractionInterface
{
	GENERATED_BODY()

public:
	AOneBrahamLiftPanel();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Server: binds the button to its lift. Called once, straight after spawning. */
	void InitPanel(AOneBrahamLift* InLift, EOneBrahamLiftPanelRole InRole, int32 InFloor);

	UFUNCTION(BlueprintPure, Category = "One Braham|Lift")
	AOneBrahamLift* GetLift() const { return Lift; }

	UFUNCTION(BlueprintPure, Category = "One Braham|Lift")
	EOneBrahamLiftPanelRole GetPanelRole() const { return PanelRole; }

	UFUNCTION(BlueprintPure, Category = "One Braham|Lift")
	int32 GetPanelFloor() const { return PanelFloor; }

	// ======================================================================
	//  IOneBrahamInteractionInterface
	// ======================================================================
	virtual FText GetInteractionPrompt_Implementation(const AOneBrahamPlayerController* Interactor) const override;
	virtual FGameplayTag GetInteractionTag_Implementation(const AOneBrahamPlayerController* Interactor) const override;
	virtual bool CanInteract_Implementation(const AOneBrahamPlayerController* Interactor) const override;
	virtual float GetInteractionRange_Implementation(const AOneBrahamPlayerController* Interactor) const override;
	virtual bool OnInteractionStart_Implementation(AOneBrahamPlayerController* Interactor) override;
	virtual void OnInteractionEnd_Implementation(AOneBrahamPlayerController* Interactor, bool bCompleted) override;
	virtual void SetInteractionHighlight_Implementation(bool bHighlighted) override;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Components")
	TObjectPtr<USceneComponent> Root = nullptr;

	/** The faceplate. No collision: the trace target is the box. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Components")
	TObjectPtr<UStaticMeshComponent> PlateMesh = nullptr;

	/** Blocks the Interactable channel and nothing else. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Components")
	TObjectPtr<UBoxComponent> UseVolume = nullptr;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "One Braham|Lift")
	TObjectPtr<AOneBrahamLift> Lift = nullptr;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "One Braham|Lift")
	EOneBrahamLiftPanelRole PanelRole = EOneBrahamLiftPanelRole::Landing;

	/** The landing this panel is on. Unused by the car buttons. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "One Braham|Lift")
	int32 PanelFloor = 0;

	/** True when the interactor's pawn is alive and able to press a button. */
	static bool IsInteractorAble(const AOneBrahamPlayerController* Interactor);
};
