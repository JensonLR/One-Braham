// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamVFXSubsystem.h
// ---------------------------------------------------------------------------
// The bookkeeping half of the VFX library: everything that has to outlive the
// single function call that spawned it.
//
//   * Blood decals (splatter, drips, pools). Hard cap of 64 per client with
//     oldest-first recycling - splatters are recycled before pools, because a
//     pool under a body is what tells the next squad up the stairs what
//     happened here, and a spray on a wall from two minutes ago is not.
//   * Pools grow. A body does not arrive with a puddle under it; the puddle
//     spreads over ten to fifteen seconds, which is the detail that makes a
//     floor feel lived-in rather than set-dressed.
//   * Bullet holes. 96 live per client, oldest first (Docs/11 section 5).
//   * The procedural fallback actors (AOneBrahamImpactFallbackActor). They are
//     pooled here: finished actors are hidden and parked rather than destroyed,
//     and the number active at once is capped, so a twelve-player firefight on
//     a clean checkout costs a bounded, predictable amount.
//
// It is a world subsystem so a level change empties it for free, and it ticks
// only while it has decals to grow or fade.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "OneBrahamVFXSubsystem.generated.h"

class AOneBrahamImpactFallbackActor;
class UDecalComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class USceneComponent;

/** One blood decal the subsystem is looking after. */
USTRUCT()
struct FOneBrahamManagedDecal
{
	GENERATED_BODY()

	UPROPERTY()
	TWeakObjectPtr<UDecalComponent> Decal;

	UPROPERTY()
	TWeakObjectPtr<UMaterialInstanceDynamic> Material;

	/** World time the decal appeared. */
	float SpawnTime = 0.f;

	/** Seconds the decal stays at full strength before it starts to fade. */
	float HoldSeconds = 45.f;

	/** Seconds the fade takes. */
	float FadeSeconds = 4.f;

	/** Seconds over which a pool spreads from StartExtent to EndExtent. 0 = no growth. */
	float GrowSeconds = 0.f;

	/** Projected half-size (Y, Z) at spawn and at full growth. */
	FVector2D StartExtent = FVector2D(10.f, 10.f);
	FVector2D EndExtent = FVector2D(10.f, 10.f);

	/** Opacity the material is driven to at full strength. */
	float BaseOpacity = 1.f;

	/** Pools are the last thing recycled. */
	bool bPool = false;
};

UCLASS()
class ONEBRAHAM_API UOneBrahamVFXSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Hard caps. In code rather than config: they are performance contracts. */
	static constexpr int32 MaxBloodDecals = 64;
	static constexpr int32 MaxBulletHoles = 96;
	static constexpr int32 MaxActiveFallbackActors = 48;

	/** The subsystem for a world context, or null (no world, editor preview, dedicated server). */
	static UOneBrahamVFXSubsystem* Get(const UObject* WorldContextObject);

	// --- UWorldSubsystem / FTickableGameObject ------------------------------
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual ETickableTickType GetTickableTickType() const override;
	virtual bool IsTickable() const override;

	// ======================================================================
	//  Blood
	// ======================================================================
	/**
	 * Projects a blood decal onto a surface.
	 *
	 * @param Location      Point on the surface.
	 * @param Normal        Surface normal at that point (the decal projects
	 *                      against it, so floors, walls and ceilings all work).
	 * @param AlongSurface  Direction the splatter streaks in, in world space; it
	 *                      is projected onto the surface. Zero for a round mark.
	 * @param RadiusCm      Half-size across the streak.
	 * @param Elongation    Length / width ratio (1 = round).
	 * @param HoldSeconds   Seconds at full strength before the fade.
	 * @param AttachTo      Movable surface (a door) to ride with, or null.
	 * @param bPool         Pool: grows from a fifth of its size over GrowSeconds.
	 */
	UDecalComponent* SpawnBloodDecal(const FVector& Location, const FVector& Normal, const FVector& AlongSurface,
		float RadiusCm, float Elongation, float HoldSeconds, USceneComponent* AttachTo = nullptr,
		bool bPool = false, float GrowSeconds = 0.f);

	/** Number of live blood decals (tests and the stat overlay). */
	int32 GetBloodDecalCount() const { return BloodDecals.Num(); }

	/** Removes every blood decal at once (ob.Gore 0, match reset). */
	void ClearBlood();

	// ======================================================================
	//  Bullet holes
	// ======================================================================
	/** Registers a bullet-hole decal so the 96-hole cap can recycle the oldest. */
	void RegisterBulletHole(UDecalComponent* Decal);

	// ======================================================================
	//  Fallback actor pool
	// ======================================================================
	/** A parked actor if there is one, otherwise a new one; recycles the oldest
	 *  active actor when the active cap is reached. Never null in a game world. */
	AOneBrahamImpactFallbackActor* AcquireFallbackActor(const FVector& Location, const FRotator& Rotation);

	/** Called by the actor when its effect has finished. */
	void ReleaseFallbackActor(AOneBrahamImpactFallbackActor* Actor);

	int32 GetActiveFallbackCount() const { return ActiveFallbacks.Num(); }

	/** Decal orientation for a surface normal: X (the projection axis) points
	 *  into the surface and Y runs along AlongSurface when it is usable. Public
	 *  and static because the bullet-hole path and the tests share it. */
	static FRotator DecalRotationForSurface(const FVector& Normal, const FVector& AlongSurface, float RollDegreesIfRound);

private:
	/** Material for blood: the authored splatter material, or null when the
	 *  content build has not made it yet (and then there is no blood decal - a
	 *  square red decal is worse than none). */
	UMaterialInterface* GetBloodMaterial();

	/** Removes the oldest splatter (or the oldest pool if there are only pools). */
	void RecycleOneBloodDecal();

	UPROPERTY()
	TArray<FOneBrahamManagedDecal> BloodDecals;

	UPROPERTY()
	TArray<TWeakObjectPtr<UDecalComponent>> BulletHoles;

	UPROPERTY()
	TArray<TObjectPtr<AOneBrahamImpactFallbackActor>> ActiveFallbacks;

	UPROPERTY()
	TArray<TObjectPtr<AOneBrahamImpactFallbackActor>> ParkedFallbacks;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> BloodMaterial = nullptr;

	bool bBloodMaterialResolved = false;

	/** Accumulator so pool growth pushes render state ten times a second, not every frame. */
	float GrowthAccumulator = 0.f;
};
