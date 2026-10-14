// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamSupplyPickup.h
// ---------------------------------------------------------------------------
// Ammunition, a grenade or a medkit lying in the building.
//
// Weapons are AOneBrahamPickupBase, because a weapon on the floor is the same
// actor as a weapon in a hand. Everything else a player can pick up is one of
// these: a tap, a grant to the inventory, and the actor is gone. The grant is
// refused (and the box stays) when the player has no room for it, so a full
// pouch does not eat the medkit the squad mate behind you needed.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/OneBrahamInteractionInterface.h"
#include "OneBrahamSupplyPickup.generated.h"

class AOneBrahamCharacter;
class AOneBrahamPlayerController;
class UPointLightComponent;
class UStaticMeshComponent;

UENUM(BlueprintType)
enum class EOneBrahamSupplyKind : uint8
{
	/** Reserve rounds for every weapon the player carries. */
	Ammo = 0,
	/** One utility item: frag, flashbang, smoke or breach charge. */
	Utility = 1,
	/** One medkit. */
	Medkit = 2
};

UCLASS(Blueprintable)
class ONEBRAHAM_API AOneBrahamSupplyPickup : public AActor, public IOneBrahamInteractionInterface
{
	GENERATED_BODY()

public:
	AOneBrahamSupplyPickup();

	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Server: what this pickup is. Call before the first replication. */
	void ServerConfigure(EOneBrahamSupplyKind InKind, FName InUtilityId, int32 InAmount);

	UFUNCTION(BlueprintPure, Category = "One Braham|Loot")
	EOneBrahamSupplyKind GetKind() const { return Kind; }

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
	void OnRep_Contents();

	/** Local: the box's size and the glow's colour for what it holds. */
	void ApplyContentsVisuals();

	/** Server: gives the contents to Character. False, and the box stays, when there is no room. */
	bool ServerGrantTo(AOneBrahamCharacter* Character);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Components")
	TObjectPtr<UStaticMeshComponent> BoxMesh = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Components")
	TObjectPtr<UPointLightComponent> Glow = nullptr;

	UPROPERTY(ReplicatedUsing = OnRep_Contents, BlueprintReadOnly, Category = "One Braham|Loot")
	EOneBrahamSupplyKind Kind = EOneBrahamSupplyKind::Ammo;

	/** Utility_Frag, Utility_Flashbang, Utility_Smoke or Utility_BreachCharge. */
	UPROPERTY(ReplicatedUsing = OnRep_Contents, BlueprintReadOnly, Category = "One Braham|Loot")
	FName UtilityId = NAME_None;

	/** Ammo: a share of each carried weapon's maximum reserve, in percent. Others: a count. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "One Braham|Loot")
	int32 Amount = 1;

	UPROPERTY(EditDefaultsOnly, Category = "One Braham|Loot")
	float PickupRangeCm = 210.f;
};
