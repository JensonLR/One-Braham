// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamVFXStatics.h
// ---------------------------------------------------------------------------
// Every visual effect in ONE BRAHAM goes through here, for one reason: the game
// must look finished before the art pass lands, and it must degrade gracefully
// on a machine that cannot afford Niagara.
//
// TIERS
//   1. Authored Niagara system at the conventional path, if it exists
//      (/Game/OneBraham/VFX/NS_OB_<Effect>). This is what ships.
//   2. Runtime fallback: a pooled light + decal + geometry-less spark actor built
//      in C++ (VFX/OneBrahamImpactFallbackActor.h). This is what makes a clean
//      checkout look intentional rather than broken, and it is also the low
//      scalability path on the minimum spec.
//
// Nothing here allocates per frame, and every spawn is distance-culled against
// the local camera before it is created.

#pragma once

#include "CoreMinimal.h"
#include "Core/OneBrahamTypes.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "OneBrahamVFXStatics.generated.h"

class UMaterialInterface;
class UNiagaraComponent;
class UNiagaraSystem;
class UObject;
class USoundBase;

/** Effect families. Each one maps to a Niagara asset path and a fallback shape. */
UENUM(BlueprintType)
enum class EOneBrahamVFXEffect : uint8
{
	BulletImpact,
	FleshImpact,
	GlassImpact,
	MetalImpact,
	MuzzleFlash,
	Tracer,
	ShellEject,
	FragExplosion,
	BreachExplosion,
	FlashbangBurst,
	SmokeCloud,
	BloodMist,
	WaterSpray,
	SparkShower,
	DustPuff,
	BreachDebris,
	CorpseImpact
};

UCLASS()
class ONEBRAHAM_API UOneBrahamVFXStatics : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// ======================================================================
	//  Bullet impacts
	// ======================================================================
	/**
	 * Spawns the impact effect for a batch of hits. Called on every machine from
	 * AOneBrahamWeaponBase::MulticastPlayImpactFX, so it must be cheap: the local
	 * camera distance gate rejects anything the player cannot plausibly see.
	 */
	static void SpawnImpactsForHits(UObject* WorldContextObject, const TArray<FOneBrahamImpactInfo>& Impacts);

	static void SpawnImpact(UObject* WorldContextObject, const FOneBrahamImpactInfo& Impact);

	/** Rounds snapping past the local player's head. Purely information: it tells
	 *  you that you are being shot at and roughly from where. */
	static void PlayNearMiss(UObject* WorldContextObject, const FVector& Location);

	// ======================================================================
	//  Explosions and utility
	// ======================================================================
	/** Frag / breach detonation: flash, debris, dust, camera impulse, crack. */
	static void SpawnExplosion(UObject* WorldContextObject, const FVector& Location, float RadiusCm, bool bBreach);

	/** Flashbang: a very short, very bright light plus the ringing handled by audio. */
	static void SpawnFlashbang(UObject* WorldContextObject, const FVector& Location, float RadiusCm);

	/** Smoke: the only effect in the game that is gameplay-critical. It must be
	 *  dense enough to break line of sight and cheap enough for three of them. */
	static void SpawnSmokeCloud(UObject* WorldContextObject, const FVector& Location, float RadiusCm, float LifetimeSeconds);

	/** Breaking glass: a pane of ballistic glass on floor 9 is both cover and a
	 *  one-way telegraph. Shatter direction matters. */
	static void SpawnGlassShatter(UObject* WorldContextObject, const FVector& Location, const FVector& Normal, float AreaCm2);

	/** Breaching charge hole: plasterboard dust, screws, insulation. */
	static void SpawnBreachDebris(UObject* WorldContextObject, const FVector& Location, const FVector& Normal);

	// ======================================================================
	//  Generic entry point + helpers
	// ======================================================================
	UFUNCTION(BlueprintCallable, Category = "One Braham|VFX", meta = (WorldContext = "WorldContextObject"))
	static UNiagaraComponent* SpawnEffect(UObject* WorldContextObject, EOneBrahamVFXEffect Effect, const FVector& Location, const FRotator& Rotation = FRotator::ZeroRotator, const FVector& Scale = FVector(1.f));

	/** Loads and caches a Niagara system by conventional effect name. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|VFX")
	static UNiagaraSystem* LoadEffectSystem(EOneBrahamVFXEffect Effect);

	/** Conventional asset path for an effect: /Game/OneBraham/VFX/NS_OB_<Name>. */
	static FString GetEffectAssetPath(EOneBrahamVFXEffect Effect);

	/** Impact family for a surface, so a wall, a desk and a server rack differ. */
	UFUNCTION(BlueprintPure, Category = "One Braham|VFX")
	static EOneBrahamVFXEffect ImpactEffectForSurface(EOneBrahamFootstepSurface Surface);

	/** Decal material path for a surface. Null-safe: returns an empty string when
	 *  the project has no decal for that surface yet. */
	static FString GetDecalPathForSurface(EOneBrahamFootstepSurface Surface);

	/**
	 * Distance gate used by every spawn in this file. Effects the local camera
	 * cannot see, or that are further away than the effect's own budget, are
	 * rejected before anything is allocated.
	 */
	static bool ShouldSpawnForLocalViewer(const UObject* WorldContextObject, const FVector& Location, float MaxDistanceCm);

	/** True when the effect should be culled because the spawn point is off-screen
	 *  and further than the cull distance. */
	static bool IsCulledByViewer(const UObject* WorldContextObject, const FVector& Location, float CullDistanceCm);

	/** Global VFX intensity from the user's settings (0 = off, 1 = full). */
	static float GetVFXIntensity();

	/** Drops every cached Niagara asset. Called from module shutdown. */
	static void ReleaseCachedAssets();
};
