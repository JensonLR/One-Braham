// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamKeycard.h
// ---------------------------------------------------------------------------
// The card.
//
// ASCENT's progression is a card chain: the lobby is open, everything above it
// is not, and the only way up is to find what somebody dropped. Cards spawn in
// believable places (a wallet on a desk on 5, a Facilities master in the plant
// room on 13) and, critically, a killed player drops their highest card only.
//
// WHY ONLY THE HIGHEST
//   Dropping the whole set would make a body a treasure chest and turn every
//   fight into a looting animation. Dropping only the highest tier makes the
//   decision "do I go for the card?" a real one, and it makes a Facilities master
//   a genuinely valuable person to kill.
//
// The card is picked up through the standard interaction interface, so it uses
// the same prompt, the same range check and the same anti-cheat validation as a
// dropped weapon.

#pragma once

#include "CoreMinimal.h"
#include "Core/OneBrahamTypes.h"
#include "GameFramework/Actor.h"
#include "Interaction/OneBrahamInteractionInterface.h"
#include "OneBrahamKeycard.generated.h"

class AOneBrahamCharacter;
class AOneBrahamPlayerController;
class UPointLightComponent;
class USceneComponent;
class UStaticMeshComponent;

UCLASS(Blueprintable)
class ONEBRAHAM_API AOneBrahamKeycard : public AActor, public IOneBrahamInteractionInterface
{
	GENERATED_BODY()

public:
	AOneBrahamKeycard();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/**
	 * Access tier this card grants.
	 *
	 * IMPORTANT: the name is part of the contract. UOneBrahamInventoryComponent
	 * copies the tier onto a dropped card by property name (see
	 * ServerDropKeycardOnDeath), so renaming this breaks the loot drop silently.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "One Braham|Keycard")
	EOneBrahamAccessTier AccessTier = EOneBrahamAccessTier::Staff;

	/** Floor this card is associated with, for the HUD prompt and the spawn tables. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "One Braham|Keycard")
	int32 IssuedFloor = 5;

	/** Set by the inventory component when a player dies, for the dropped-card look. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "One Braham|Keycard")
	bool bDroppedFromPlayer = false;

	/** Name of the card's owner printed on the front ("A. NWOSU - FACILITIES"). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "One Braham|Keycard")
	FText PrintedName;

	/** Applies a tier's visual language: colour, light, and who was issued it. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Keycard")
	void ApplyTier(EOneBrahamAccessTier NewTier);

	/** Colour used for the card's own light and the HUD row, per tier. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Keycard")
	static FLinearColor GetTierColour(EOneBrahamAccessTier Tier);

	/** Localised tier name ("STAFF BADGE", "FACILITIES MASTER"). */
	UFUNCTION(BlueprintPure, Category = "One Braham|Keycard")
	static FText GetTierDisplayName(EOneBrahamAccessTier Tier);

	// ======================================================================
	//  Interaction interface
	// ======================================================================
	virtual FText GetInteractionPrompt_Implementation(const AOneBrahamPlayerController* Interactor) const override;
	virtual FGameplayTag GetInteractionTag_Implementation(const AOneBrahamPlayerController* Interactor) const override;
	virtual bool CanInteract_Implementation(const AOneBrahamPlayerController* Interactor) const override;
	virtual bool RequiresHold_Implementation(const AOneBrahamPlayerController* Interactor) const override;
	virtual float GetHoldDuration_Implementation(const AOneBrahamPlayerController* Interactor) const override;
	virtual float GetInteractionRange_Implementation(const AOneBrahamPlayerController* Interactor) const override;
	virtual bool OnInteractionStart_Implementation(AOneBrahamPlayerController* Interactor) override;
	virtual void OnInteractionEnd_Implementation(AOneBrahamPlayerController* Interactor, bool bCompleted) override;
	virtual void OnInteractionCancelled_Implementation(AOneBrahamPlayerController* Interactor) override;
	virtual void SetInteractionHighlight_Implementation(bool bHighlighted) override;

protected:
	UFUNCTION()
	void OnRep_AccessTier();

	/** Local: tint the card and its light for the current tier. */
	void RefreshTierVisuals();

	/** Server: hands the card to a character and removes the actor. */
	bool ServerGrantTo(AOneBrahamCharacter* Character);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Components")
	TObjectPtr<USceneComponent> Root = nullptr;

	/** The card itself: a thin box, always visible, never animated. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Components")
	TObjectPtr<UStaticMeshComponent> CardMesh = nullptr;

	/** A small, tight glow so a card on a dark desk is findable in a hurry. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Components")
	TObjectPtr<UPointLightComponent> CardGlow = nullptr;

	/** Rotation rate in degrees per second. Slow: it is a card, not a power-up. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Keycard")
	float SpinDegreesPerSecond = 22.f;

	/** Vertical bob amplitude in cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Keycard")
	float BobAmplitudeCm = 1.6f;

	/** Cards left on the floor after a wipe are removed after this long. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Keycard")
	float GroundLifetimeSeconds = 300.f;

	/** Name of the material parameter that takes the tier colour. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Keycard")
	FName TierColourParameter = TEXT("TierColour");

	float Elapsed = 0.f;
	float BaseHeight = 0.f;
	bool bHighlighted = false;
};
