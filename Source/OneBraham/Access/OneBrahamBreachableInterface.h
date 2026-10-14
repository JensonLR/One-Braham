// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamBreachableInterface.h
// ---------------------------------------------------------------------------
// "Some walls are a decision, not a wall."
//
// ASCENT gives a squad three ways up the tower: a keycard, an override terminal,
// or a breaching charge. The first two are quiet and slow; the third is loud,
// costs a utility slot, and puts a hole in the building that everyone can hear
// from two floors away. This interface is the contract between the charge and
// whatever it is opening: a fire door, a plasterboard partition, a service
// riser, or the Skydeck access gate.
//
// IMPLEMENTATION NOTES FOR THE MAP
//   * Implement this on Blueprints with a `BreachHealth`-style float property.
//   * On damage: spawn the surface impact VFX, play the "charge planted" cue, and
//     when health reaches zero call the map's own breach routine - a Chaos
//     geometry collection release for plasterboard, a door swing + lock break for
//     steel doors, a client-side animation for the Skydeck gate.
//   * Do not delete the actor on breach. Replace the blocking collision with a
//     walk-through opening so navigation rebuilds and the hole is visible.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "OneBrahamBreachableInterface.generated.h"

UINTERFACE(MinimalAPI, BlueprintType)
class UOneBrahamBreachableInterface : public UInterface
{
	GENERATED_BODY()
};

class ONEBRAHAM_API IOneBrahamBreachableInterface
{
	GENERATED_BODY()

public:
	/**
	 * Apply breaching damage. Called on the server from the charge's detonation
	 * (see AOneBrahamProjectileBase). Damage is raw: no armour, no hit zones.
	 *
	 * @param Damage   Raw breaching damage from the charge.
	 * @param Breacher Who planted it. May be null (a stray charge).
	 * @param Origin   Detonation point, for impact direction and VFX.
	 */
	virtual void ApplyBreachDamage(float Damage, AActor* Breacher, const FVector& Origin) = 0;

	/** True once the opening exists and can be walked through. */
	virtual bool IsBreached() const = 0;

	/**
	 * Multiplier applied to incoming breaching damage. A fire door resists, a
	 * plasterboard partition does not. Default is neutral.
	 */
	virtual float GetBreachResistance() const { return 1.f; }

	/** Remaining integrity in the same units as the damage, for the HUD prompt. */
	virtual float GetBreachIntegrity01() const { return 1.f; }

	/**
	 * Which floor this opening sits on, so the lockdown director can distinguish
	 * a breach in the sealed zone (a real escape) from one above it.
	 */
	virtual int32 GetBreachFloor() const { return 1; }
};
