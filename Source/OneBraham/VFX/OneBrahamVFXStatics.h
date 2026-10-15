// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamVFXStatics.h
// ---------------------------------------------------------------------------
// Every visual effect in ONE BRAHAM goes through here, for one reason: the game
// must look finished before the art pass lands, and it must degrade gracefully
// on a machine that cannot afford Niagara.
//
// TIERS
//   1. Authored Niagara system at the conventional path, if it exists *and has
//      at least one emitter* (/Game/OneBraham/VFX/NS_OB_<Effect>). This is
//      what ships. build_vfx.py creates the seventeen systems empty, so an
//      empty system is treated exactly like a missing one.
//   2. Runtime fallback: a pooled procedural effect built from engine shapes
//      (VFX/OneBrahamImpactFallbackActor.h). This is what makes a clean
//      checkout look intentional rather than broken, and it is also the low
//      scalability path on the minimum spec.
//
// GORE
//   Blood (spray, decals, pools, wound shading) is gated by the console
//   variable ob.Gore (1 = on, the default; 0 = off). With gore off a character
//   hit still produces a grey fabric puff, because hit feedback is gameplay.
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
	 *
	 * ShotOrigin is where the shot came from (the weapon's muzzle). Knowing the
	 * direction lets debris and blood spray *along* the shot and lets grazing
	 * hits on metal and concrete ricochet. Zero means "unknown" and disables both.
	 */
	static void SpawnImpactsForHits(UObject* WorldContextObject, const TArray<FOneBrahamImpactInfo>& Impacts, const FVector& ShotOrigin = FVector::ZeroVector);

	static void SpawnImpact(UObject* WorldContextObject, const FOneBrahamImpactInfo& Impact, const FVector& ShotOrigin = FVector::ZeroVector);

	/** Sparks and a short streak along the reflected path. Cosmetic only. */
	static void SpawnRicochet(UObject* WorldContextObject, const FVector& Location, const FVector& Normal, const FVector& OutDirection);

	/**
	 * Cosmetic chance that a round arriving at this angle on this surface
	 * glances off. 0 for anything soft, and 0 above about 22 degrees of grazing.
	 * @param IncomingDirection direction of travel (normalised)
	 */
	static float RicochetChance(EOneBrahamFootstepSurface Surface, const FVector& IncomingDirection, const FVector& Normal);

	/** Rounds snapping past the local player's head. Purely information: it tells
	 *  you that you are being shot at and roughly from where. Plays the whizz and
	 *  a very small camera flinch on the machine that calls it. */
	static void PlayNearMiss(UObject* WorldContextObject, const FVector& Location);

	/** A small camera flinch on this machine (being hit, being shot at),
	 *  scaled by the player's camera-shake settings and off when they are off. */
	static void PlayLocalCameraFlinch(UObject* WorldContextObject, float Scale);

	// ======================================================================
	//  Blood
	// ======================================================================
	/** ob.Gore: false when the player (or the platform) has switched blood off. */
	static bool IsGoreEnabled();

	/**
	 * Blood for a character hit, played on every machine from the weapon's hit
	 * multicast: splatter on whatever is up to ~2.5 m behind the victim along
	 * the shot, drips on the floor beneath, scaled by damage (headshots larger).
	 */
	static void SpawnBloodForHit(UObject* WorldContextObject, const AActor* Victim, const FVector& Location, const FVector& ShotDirection,
		float Damage, bool bHeadshot);

	/** A pool spreading under a body: radius in cm, grows over GrowSeconds. */
	static void SpawnBloodPool(UObject* WorldContextObject, const FVector& FloorLocation, float RadiusCm, float GrowSeconds, float HoldSeconds);

	/** A single round drip on the floor below a point (downed players leave a trail). */
	static void SpawnBloodDrip(UObject* WorldContextObject, const AActor* IgnoreActor, const FVector& Above, float RadiusCm);

	// ======================================================================
	//  Weapon effects with a procedural fallback
	// ======================================================================
	/** Muzzle flash fallback: only call when the weapon has no usable Niagara system. */
	static void SpawnMuzzleFlashFallback(UObject* WorldContextObject, const FTransform& Muzzle, float Scale);

	/** Tracer fallback: a thin emissive streak that travels the path. */
	static void SpawnTracerFallback(UObject* WorldContextObject, const FVector& Start, const FVector& End, float SpeedCmPerSecond);

	/** Brass fallback: a casing with a physics arc. */
	static void SpawnShellEjectFallback(UObject* WorldContextObject, const FTransform& EjectPort, const FVector& InheritedVelocity, float CalibreScale);

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

	/** Loads and caches a Niagara system by conventional effect name. Returns
	 *  null for a missing system *and* for one with no emitters, which is what
	 *  the content build creates (see IsNiagaraSystemUsable). */
	UFUNCTION(BlueprintCallable, Category = "One Braham|VFX")
	static UNiagaraSystem* LoadEffectSystem(EOneBrahamVFXEffect Effect);

	/**
	 * True when a system will draw something: it exists and has at least one
	 * enabled emitter. An empty system spawns, activates, reports success and
	 * draws nothing, which is the worst kind of missing asset - this is the
	 * check that sends those to the procedural fallback instead.
	 */
	static bool IsNiagaraSystemUsable(const UNiagaraSystem* System);

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
