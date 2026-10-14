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
//
// WHY EVERY METHOD HAS AN INLINE DEFAULT
//   A BlueprintNativeEvent needs a C++ body, and an interface that declares one
//   without providing it hands UHT a symbol with no definition. Giving each of
//   these a real default also makes the documentation above true: the range
//   really is 220 cm when a class does not say otherwise, and a class that
//   forgets to implement CanInteract is inert rather than interactable.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "OneBraham.h"
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
	/**
	 * Text shown in the HUD prompt ("Hold {Interact} - override", "Open door").
	 * Never name a key: write {Interact} and the HUD puts in whatever key does it
	 * on the device the player is holding, after any rebind.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "One Braham|Interaction")
	FText GetInteractionPrompt(const class AOneBrahamPlayerController* Interactor) const;
	virtual FText GetInteractionPrompt_Implementation(const AOneBrahamPlayerController*) const
	{
		// Empty, not a placeholder verb: the HUD treats an empty prompt as "nothing
		// to interact with" and draws nothing at all. An interactable that has not
		// implemented this is therefore invisible rather than showing the wrong
		// verb, which is the failure a player reports as "the prompt was wrong".
		return FText::GetEmpty();
	}

	/** OB.Interaction.* tag used to pick the correct verb/icon in the HUD. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "One Braham|Interaction")
	FGameplayTag GetInteractionTag(const AOneBrahamPlayerController* Interactor) const;
	virtual FGameplayTag GetInteractionTag_Implementation(const AOneBrahamPlayerController*) const
	{
		// Invalid tag: the HUD falls back to the generic interact glyph and the
		// interaction systems treat it as "interact" rather than "open" or "hold".
		return FGameplayTag();
	}

	/** False draws a locked/disabled prompt ("Executive badge required"). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "One Braham|Interaction")
	bool CanInteract(const class AOneBrahamPlayerController* Interactor) const;
	virtual bool CanInteract_Implementation(const AOneBrahamPlayerController*) const
	{
		// False: an interactable is inert until it says otherwise. Server
		// authority means the safe default is the one that refuses.
		return false;
	}

	/** True = hold-to-use (terminal, revive), false = tap-to-use (door, pickup). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "One Braham|Interaction")
	bool RequiresHold(const class AOneBrahamPlayerController* Interactor) const;
	virtual bool RequiresHold_Implementation(const AOneBrahamPlayerController*) const
	{
		// Tap. Most interactables in this building are doors and pickups; the
		// holds (terminals, revives, plants, extraction) all say so explicitly.
		return false;
	}

	/** Seconds of hold required. 0 for tap interactions. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "One Braham|Interaction")
	float GetHoldDuration(const class AOneBrahamPlayerController* Interactor) const;
	virtual float GetHoldDuration_Implementation(const AOneBrahamPlayerController*) const
	{
		return 0.f;
	}

	/** Maximum distance the interactor may be at. Defaults to 220 cm. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "One Braham|Interaction")
	float GetInteractionRange(const class AOneBrahamPlayerController* Interactor) const;
	virtual float GetInteractionRange_Implementation(const AOneBrahamPlayerController*) const
	{
		// 220 cm: the same reach the client traces with and the server validates
		// against. See OneBrahamConstants::InteractRangeCm for why they must match.
		return OneBrahamConstants::InteractRangeCm;
	}

	/**
	 * Called on the SERVER when an interaction starts.
	 * @return false to reject (the client's prompt will flicker to "unavailable")
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "One Braham|Interaction")
	bool OnInteractionStart(class AOneBrahamPlayerController* Interactor);
	virtual bool OnInteractionStart_Implementation(AOneBrahamPlayerController*)
	{
		// True: this hook is an extra veto, not the gate. The gate is CanInteract,
		// which is checked on the server immediately before this runs.
		return true;
	}

	/** Called on the SERVER every ~100 ms while a hold interaction is active. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "One Braham|Interaction")
	void OnInteractionHold(class AOneBrahamPlayerController* Interactor, float HeldSeconds, float Progress01);
	virtual void OnInteractionHold_Implementation(AOneBrahamPlayerController*, float, float) {}

	/** Called on the SERVER when the hold completes or the button is released. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "One Braham|Interaction")
	void OnInteractionEnd(class AOneBrahamPlayerController* Interactor, bool bCompleted);
	virtual void OnInteractionEnd_Implementation(AOneBrahamPlayerController*, bool) {}

	/** Called on the SERVER when a player looks away / walks off mid-interaction. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "One Braham|Interaction")
	void OnInteractionCancelled(class AOneBrahamPlayerController* Interactor);
	virtual void OnInteractionCancelled_Implementation(AOneBrahamPlayerController*) {}

	/** Highlight state for the outline/custom-depth pass on the local client. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "One Braham|Interaction")
	void SetInteractionHighlight(bool bHighlighted);
	virtual void SetInteractionHighlight_Implementation(bool) {}
};
