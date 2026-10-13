// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamPickupBase.h
// ---------------------------------------------------------------------------
// A weapon on the floor.
//
// In ASCENT loot is the map: you arrive with a sidearm and you leave with
// whatever you can find or take. That makes the dropped weapon a first-class
// actor, not a cosmetic: it carries its own magazine, its reserve pool and its
// attachments, and taking it costs the same interaction slot as reviving a
// colleague or overriding a terminal.
//
// THE LOOP
//   * Death drops every owned weapon at the body, scattered, plus the reserve
//     ammo, so a squad mate can arm themselves off your corpse.
//   * A swapped-out weapon is dropped where you were standing, not deleted.
//   * Anyone can pick up any weapon: faction does not restrict loot, only access.
//   * The server validates range on pickup; a client that teleports to a pickup
//     is rejected and logged (see the anti-cheat section of Docs/18).
//
// Deliberately NOT implemented with physics simulation: a dropped rifle settles
// onto the carpet at a slight yaw. Guns bouncing down a stairwell is a cartoon.

#pragma once

#include "CoreMinimal.h"
#include "Interaction/OneBrahamInteractionInterface.h"
#include "Weapons/OneBrahamWeaponBase.h"
#include "OneBrahamPickupBase.generated.h"

class AOneBrahamCharacter;
class AOneBrahamPlayerController;
class UPointLightComponent;

UCLASS(Blueprintable)
class ONEBRAHAM_API AOneBrahamPickupBase : public AOneBrahamWeaponBase, public IOneBrahamInteractionInterface
{
	GENERATED_BODY()

public:
	AOneBrahamPickupBase();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// ======================================================================
	//  Server: ground state
	// ======================================================================
	/**
	 * Puts this weapon on the floor, preserving its magazine, reserve pool and
	 * attachments. Called on death, on weapon swap, and on loot spawn.
	 *
	 * @param Location Where to place it (the actor is settled onto the surface).
	 * @param bScatter True for death drops, which get a small random offset and
	 *                 yaw so a body does not look like a rack.
	 */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Pickup")
	void ServerDropAt(const FVector& Location, bool bScatter = true);

	/** Server: takes the weapon into a character's hands. Returns false if they cannot carry it. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Pickup")
	bool ServerPickupBy(AOneBrahamCharacter* Character);

	/** Server: cleans up ground state after the weapon component has claimed it. */
	void ServerTakeOffGround();

	UFUNCTION(BlueprintPure, Category = "One Braham|Pickup")
	bool IsOnGround() const { return bIsOnGround; }

	/** Reserve rounds lying with the weapon, transferred on pickup. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Pickup")
	int32 GetGroundReserveAmmo() const { return GroundReserveAmmo; }

	UFUNCTION(BlueprintCallable, Category = "One Braham|Pickup")
	void SetGroundReserveAmmo(int32 Reserve);

	/** True when nobody may take it yet (death drops are briefly unlootable). */
	UFUNCTION(BlueprintPure, Category = "One Braham|Pickup")
	bool IsPickupLocked() const;

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
	void OnRep_GroundState();

	/** Server: settles the actor onto whatever surface is below the drop point. */
	void SettleOnSurface(const FVector& Location, bool bScatter);

	/** Local: shows/hides the pickup mesh, glow and collision for the ground state. */
	void ApplyGroundVisuals();

	/** The mesh the world sees: pickup mesh on the ground, weapon mesh in hand. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Components")
	TObjectPtr<UPointLightComponent> GroundGlow = nullptr;

	UPROPERTY(ReplicatedUsing = OnRep_GroundState, BlueprintReadOnly, Category = "One Braham|Pickup")
	bool bIsOnGround = false;

	/** Reserve rounds that travel with the weapon on the floor. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "One Braham|Pickup")
	int32 GroundReserveAmmo = 0;

	/** Seconds since the drop, replicated implicitly by the server clock at pickup. */
	float TimeOnGround = 0.f;

	/** How long a dropped weapon survives before it fades out. */
	UPROPERTY(EditDefaultsOnly, Category = "One Braham|Pickup")
	float GroundLifetimeSeconds = 240.f;

	/** Grace period after a drop before anyone can take it (stops gank-looting). */
	UPROPERTY(EditDefaultsOnly, Category = "One Braham|Pickup")
	float PickupLockSeconds = 1.25f;

	UPROPERTY(EditDefaultsOnly, Category = "One Braham|Pickup")
	float PickupRangeCm = 220.f;

	/** Highlight state from the local interaction trace (custom depth pass). */
	UPROPERTY(Transient)
	bool bHighlighted = false;

	/** True when the drop came from a corpse, for the ground glow colour. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "One Braham|Pickup")
	bool bDroppedFromCorpse = false;
};
