// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamImpactFallbackActor.h
// ---------------------------------------------------------------------------
// The effect that exists before the effect exists.
//
// A serious shooter lives or dies on impact feedback: the player must see where
// their rounds are going and what they did when they got there. ONE BRAHAM's
// shipping effects are Niagara systems, but the content build creates those
// systems empty (see Tools/Python/onebraham/build_vfx.py), and a game that only
// looks right after the FX pass is a game nobody can review. So every effect
// family also has a procedural stand-in built here from engine primitives
// (/Engine/BasicShapes) and dynamic material instances:
//
//   * surface impacts  - concrete dust and chips, metal sparks, glass shards,
//                        plaster clouds, fibre puffs, water splashes
//   * flesh impacts    - a dark spray along the shot, droplets that arc and
//                        fall, a short mist; restrained, never cartoonish
//   * muzzle flash     - a two-frame emissive flare, petals and a light
//   * tracer           - a thin stretched emissive streak travelling the path
//   * shell ejection   - a brass casing with a real physics arc and bounce
//   * explosions       - flash, expanding fireball, rolling smoke, debris
//   * flashbang, smoke, ricochet, spark shower, dust puff
//
// Particles are simulated on the CPU in Tick (velocity, gravity, drag, a single
// floor plane per effect found by one trace at spawn) except brass and blast
// debris, which use real physics against world geometry only. Every actor is
// pooled by UOneBrahamVFXSubsystem: capped at 48 active, parked and reused when
// finished, never destroyed per shot.
//
// It is also the mandated minimum-spec path on the "Low" quality preset.

#pragma once

#include "CoreMinimal.h"
#include "Core/OneBrahamTypes.h"
#include "GameFramework/Actor.h"
#include "OneBrahamImpactFallbackActor.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UPointLightComponent;
class UStaticMesh;
class UStaticMeshComponent;

/** Which material family a particle draws with. */
UENUM()
enum class EOneBrahamFXLook : uint8
{
	/** Default-lit solid: chips, brass, shards, droplets, debris. */
	Solid,
	/** Unlit emissive: sparks, flare, tracer, fireball. */
	Glow,
	/** Soft translucent: dust, smoke, mist, spray. */
	Haze
};

/** Engine primitive a particle is drawn with. */
UENUM()
enum class EOneBrahamFXShape : uint8
{
	Sphere,
	Cube,
	Cylinder
};

/** One CPU-simulated (or physics) particle. */
USTRUCT()
struct FOneBrahamFXParticle
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Mesh = nullptr;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> Material = nullptr;

	EOneBrahamFXLook Look = EOneBrahamFXLook::Solid;

	FVector Location = FVector::ZeroVector;
	FVector Velocity = FVector::ZeroVector;

	/** Tumble for kinematic particles, degrees per second. */
	FRotator Spin = FRotator::ZeroRotator;
	FRotator Rotation = FRotator::ZeroRotator;

	/** cm/s^2 downward. 980 is a dropped coin; sparks use less, smoke none. */
	float Gravity = 0.f;

	/** Fraction of velocity lost per second. */
	float Drag = 0.f;

	/** Rises against gravity (smoke). cm/s^2 upward. */
	float Buoyancy = 0.f;

	float Age = 0.f;
	float Life = 0.5f;

	/** Seconds before the particle appears (staggered smoke, delayed drips). */
	float Delay = 0.f;

	FVector StartScale = FVector(0.01f);
	FVector EndScale = FVector(0.01f);

	FLinearColor Colour = FLinearColor::White;

	float EmissiveStart = 0.f;
	float EmissiveEnd = 0.f;
	float OpacityStart = 1.f;
	float OpacityEnd = 1.f;

	/** Long axis (local Z) follows the velocity: sparks, droplets, tracer. */
	bool bAlignToVelocity = false;

	/** Real physics (brass, debris). Kinematic otherwise. */
	bool bPhysics = false;

	/** Kinematic: bounce off the effect's floor plane instead of stopping on it. */
	float FloorBounce = 0.f;

	/** Kinematic: disappears on touching the floor plane (droplets become decals). */
	bool bDieOnFloor = false;

	bool bActive = false;
};

UCLASS(NotPlaceable, Transient)
class ONEBRAHAM_API AOneBrahamImpactFallbackActor : public AActor
{
	GENERATED_BODY()

public:
	AOneBrahamImpactFallbackActor();

	virtual void Tick(float DeltaSeconds) override;

	// ======================================================================
	//  Spawners. All return null on a dedicated server or with no world.
	// ======================================================================
	/**
	 * Surface (or flesh, when Impact.bHitCharacter) impact.
	 *
	 * @param Intensity      0..1 from the user's quality preset: particle counts
	 *                       and light radius scale with it.
	 * @param bExplosive     Kept for the old call sites: routes to SpawnExplosion.
	 * @param ShotDirection  Direction the round was travelling; zero when unknown
	 *                       (then the surface normal stands in for it).
	 */
	static AOneBrahamImpactFallbackActor* SpawnForImpact(UWorld* World, const FOneBrahamImpactInfo& Impact, float Intensity,
		bool bExplosive = false, const FVector& ShotDirection = FVector::ZeroVector);

	/** Flesh hit: spray along the shot, droplets with gravity, a short mist. */
	static AOneBrahamImpactFallbackActor* SpawnFlesh(UWorld* World, const FVector& Location, const FVector& Normal,
		const FVector& ShotDirection, float Severity, bool bHeadshot, float Intensity);

	static AOneBrahamImpactFallbackActor* SpawnMuzzleFlash(UWorld* World, const FTransform& Muzzle, float Scale);

	static AOneBrahamImpactFallbackActor* SpawnTracer(UWorld* World, const FVector& Start, const FVector& End, float SpeedCmPerSecond);

	static AOneBrahamImpactFallbackActor* SpawnShellEject(UWorld* World, const FTransform& EjectPort, const FVector& InheritedVelocity, float CalibreScale);

	static AOneBrahamImpactFallbackActor* SpawnExplosion(UWorld* World, const FVector& Location, const FVector& Normal, float RadiusCm, bool bBreach, float Intensity);

	static AOneBrahamImpactFallbackActor* SpawnFlashbang(UWorld* World, const FVector& Location, float RadiusCm);

	static AOneBrahamImpactFallbackActor* SpawnSmoke(UWorld* World, const FVector& Location, float RadiusCm, float LifetimeSeconds);

	/** Grazing hit: sparks and a short streak along the reflected path. */
	static AOneBrahamImpactFallbackActor* SpawnRicochet(UWorld* World, const FVector& Location, const FVector& Normal, const FVector& OutDirection);

	static AOneBrahamImpactFallbackActor* SpawnSparkShower(UWorld* World, const FVector& Location, const FVector& Normal, float Scale);

	static AOneBrahamImpactFallbackActor* SpawnDustPuff(UWorld* World, const FVector& Location, const FVector& Normal, float Scale, const FLinearColor& Colour);

	/** Pool hook: hides everything and stops physics so the actor can be reused. */
	void ResetEffect();

	/** False for effects the pool must never cut short (smoke: it is gameplay). */
	bool CanBeRecycled() const { return !bProtectedFromRecycling; }

protected:
	/** Acquires a pooled actor at a location, or null on a dedicated server. */
	static AOneBrahamImpactFallbackActor* Acquire(UWorld* World, const FVector& Location, const FRotator& Rotation);

	/** Adds a particle; returns null when the per-actor cap is reached. */
	FOneBrahamFXParticle* AddParticle(EOneBrahamFXLook Look, EOneBrahamFXShape Shape, const FVector& Location,
		const FVector& Velocity, float Life, const FVector& StartScale, const FVector& EndScale, const FLinearColor& Colour);

	/** Makes the particle visible, applies its first frame and starts physics if asked. */
	void CommitParticle(FOneBrahamFXParticle& Particle, EOneBrahamFXShape Shape);

	/** Arms the flash light. */
	void SetFlash(const FLinearColor& Colour, float PeakIntensity, float Radius, float Seconds);

	/** One trace down (or along -Normal for a floor) to find where droplets land. */
	void ResolveFloorPlane(const FVector& From);

	/** Pushes age-dependent scale/colour/opacity into the component and material. */
	void ApplyParticleVisuals(FOneBrahamFXParticle& Particle, float Alpha);

	/** Per-family builders. */
	void BuildSurfaceImpact(const FOneBrahamImpactInfo& Impact, const FVector& ShotDirection, float Intensity);
	void BuildFlesh(const FVector& Location, const FVector& Normal, const FVector& ShotDirection, float Severity, bool bHeadshot, float Intensity);
	void BuildSparks(const FVector& Location, const FVector& Direction, int32 Count, float ConeDegrees, float MinSpeed, float MaxSpeed, float Life, float Gravity);
	void BuildDust(const FVector& Location, const FVector& Direction, int32 Count, float MaxScale, float Life, const FLinearColor& Colour, float Opacity);
	void BuildChips(const FVector& Location, const FVector& Direction, int32 Count, float ConeDegrees, float MinSpeed, float MaxSpeed, float SizeCm, const FLinearColor& Colour);

	UPROPERTY(VisibleAnywhere, Category = "One Braham|VFX")
	TObjectPtr<UPointLightComponent> Flash = nullptr;

	/** Particle meshes hang off this. Kept under its original name because
	 *  /Game/OneBraham/VFX/BP_ImpactFallback derives from this class. */
	UPROPERTY(VisibleAnywhere, Category = "One Braham|VFX")
	TObjectPtr<USceneComponent> DebrisRoot = nullptr;

	UPROPERTY()
	TArray<FOneBrahamFXParticle> Particles;

	/** Components kept between uses; Particles borrows from here. */
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> MeshPool;

	int32 MeshPoolCursor = 0;

	/** Seconds the effect runs in total (longest particle, or the light). */
	float LifeSeconds = 0.4f;

	float FlashSeconds = 0.f;
	float PeakLightIntensity = 0.f;
	float Elapsed = 0.f;

	/** Z of the floor under the effect, or -inf when there is none. */
	float FloorZ = -UE_BIG_NUMBER;

	bool bInUse = false;

	/** Set by SpawnSmoke: a cloud that blocks a corridor must not vanish because
	 *  somebody else emptied a magazine into a wall. */
	bool bProtectedFromRecycling = false;

	/** Per-actor cap. A frag is the biggest user at about 36. */
	static constexpr int32 MaxParticles = 40;
};
