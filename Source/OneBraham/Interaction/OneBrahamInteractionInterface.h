// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamInteractionInterface.h
// ---------------------------------------------------------------------------
// Every interactable in ONE BRAHAM implements this: doors, terminals, lifts,
// keycard readers, weapon pickups, death bags, revivable colleagues, extraction
// beacons and the fire shutter override panels.
//
// The interaction flow is deliberately uniform so the HUD prompt, the input
// handling and the anti-cheat validation all have exactly one code path:
//
//   Client tick -> AOneBrahamPlayerController::UpdateInteractionTarget
//                 (trace on the Interactable channel)
//              -> HUD prompt drawn from GetInteractionPrompt()
//   Player presses E / holds F
//              -> UOneBrahamInteractComponent::BeginInteraction(Target, Tag)
//              -> Server RPC: the server re-validates distance, facing and
//                 state, then runs OnInteractionStart/Complete on the target
//
// Hold interactions (terminal override, revive, breach placement, extraction)
// report progress every 100 ms through OnInteractionHold; the server is the
// only side that decides completion.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Core/OneBrahamTypes.h"
#include "OneBrahamInteractionInterface.generated.h"

UINTERFACE(MinimalAPI, BlueprintType)
class UOneBrahamInteractionInterface : public UInterface
{
	GENERATED_BODY()
};

class ONEBRAHAM_API IOneBrahamInteractionInterface
{
	GENERATED_BODY()

public:
	/** Text shown in the HUD prompt ("Hold E - Override terminal", "F - Open door"). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "One Braham|Interaction")
	FText GetInteractionPrompt(const class AOneBrahamPlayerController* Interactor) const;
	virtual FText GetInteractionPrompt_Implementation(const AOneBrahamPlayerController* Interactor) const;

	/** OB.Interaction.* tag used to pick the correct verb/icon in the HUD. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "One Braham|Interaction")
	FGameplayTag GetInteractionTag(const AOneBrahamPlayerController* Interactor) const;
	virtual FGameplayTag GetInteractionTag_Implementation(const AOneBrahamPlayerController* Interactor) const;

	/** False draws a locked/disabled prompt ("Executive badge required"). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "One Braham|Interaction")
	bool CanInteract(const class AOneBrahamPlayerController* Interactor) const;
	virtual bool CanInteract_Implementation(const AOneBrahamPlayerController* Interactor) const;

	/** True = hold-to-use (terminal, revive), false = tap-to-use (door, pickup). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "One Braham|Interaction")
	bool RequiresHold(const class AOneBrahamPlayerController* Interactor) const;
	virtual bool RequiresHold_Implementation(const AOneBrahamPlayerController* Interactor) const;

	/** Seconds of hold required. 0 for tap interactions. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "One Braham|Interaction")
	float GetHoldDuration(const class AOneBrahamPlayerController* Interactor) const;
	virtual float GetHoldDuration_Implementation(const AOneBrahamPlayerController* Interactor) const;

	/** Maximum distance the interactor may be at. Defaults to 220 cm. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "One Braham|Interaction")
	float GetInteractionRange(const class AOneBrahamPlayerController* Interactor) const;
	virtual float GetInteractionRange_Implementation(const AOneBrahamPlayerController* Interactor) const;

	/**
	 * Called on the SERVER when an interaction starts.
	 * @return false to reject (the client's prompt will flicker to "unavailable")
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "One Braham|Interaction")
	bool OnInteractionStart(class AOneBrahamPlayerController* Interactor);
	virtual bool OnInteractionStart_Implementation(AOneBrahamPlayerController* Interactor);

	/** Called on the SERVER every ~100 ms while a hold interaction is active. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "One Braham|Interaction")
	void OnInteractionHold(class AOneBrahamPlayerController* Interactor, float HeldSeconds, float Progress01);
	virtual void OnInteractionHold_Implementation(AOneBrahamPlayerController* Interactor, float HeldSeconds, float Progress01);

	/** Called on the SERVER when the hold completes or the button is released. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "One Braham|Interaction")
	void OnInteractionEnd(class AOneBrahamPlayerController* Interactor, bool bCompleted);
	virtual void OnInteractionEnd_Implementation(AOneBrahamPlayerController* Interactor, bool bCompleted);

	/** Called on the SERVER when a player looks away / walks off mid-interaction. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "One Braham|Interaction")
	void OnInteractionCancelled(class AOneBrahamPlayerController* Interactor);
	virtual void OnInteractionCancelled_Implementation(AOneBrahamPlayerController* Interactor);

	/** Highlight state for the outline/custom-depth pass on the local client. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "One Braham|Interaction")
	void SetInteractionHighlight(bool bHighlighted);
	virtual void SetInteractionHighlight_Implementation(bool bHighlighted);
};
