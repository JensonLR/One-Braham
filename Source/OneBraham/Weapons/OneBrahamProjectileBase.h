// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamProjectileBase.h
// ---------------------------------------------------------------------------
// Thrown and placed utility: frag, flashbang, smoke and breach charge.
//
// All four share one actor with a behaviour switch rather than four classes,
// because they share 90% of their logic (ballistic arc, bounce, fuse, server
// authority, FX) and diverge only in the detonation effect.
//
// Server authority: the projectile simulates on the server. Clients receive the
// launch parameters and simulate locally for a smooth arc; the server's result
// (impact point + detonation) is authoritative and clients snap to it.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Core/OneBrahamTypes.h"
#include "VFX/OneBrahamVFXStatics.h"
#include "OneBrahamProjectileBase.generated.h"

class UStaticMeshComponent;
class UProjectileMovementComponent;
class UNiagaraComponent;
class UNiagaraSystem;
class UAudioComponent;
class USphereComponent;
class URadialForceComponent;

UENUM(BlueprintType)
enum class EOneBrahamUtilityType : uint8
{
	Frag = 0,
	Flashbang = 1,
	Smoke = 2,
	BreachCharge = 3,
	Incendiary = 4		// reserved for a post-launch cosmetic variant; not shipped
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOneBrahamUtilityDetonated, AOneBrahamProjectileBase*, Projectile);

UCLASS(Blueprintable)
class ONEBRAHAM_API AOneBrahamProjectileBase : public AActor
{
	GENERATED_BODY()

public:
	AOneBrahamProjectileBase();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// ======================================================================
	//  Setup
	// ======================================================================
	/** Called immediately after spawn by the inventory component. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Utility")
	void InitialiseUtility(FName InUtilityId, const FVector& InThrowDirection, class AOneBrahamCharacter* InThrower);

	/** Applies the throw velocity (called right after InitialiseUtility). */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Utility")
	void LaunchWithVelocity(const FVector& Velocity);

	/** Server: converts this projectile into a planted breach charge on a surface. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Utility")
	void ServerPlantAsBreachCharge(AActor* HostActor, const FVector& SurfacePoint, const FVector& SurfaceNormal);

	UFUNCTION(BlueprintPure, Category = "One Braham|Utility")
	EOneBrahamUtilityType GetUtilityType() const { return UtilityType; }

	UFUNCTION(BlueprintPure, Category = "One Braham|Utility")
	float GetFuseRemaining() const { return FuseRemaining; }

	// ======================================================================
	//  Tuning (all overridable per utility id in the data asset)
	// ======================================================================
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Utility")
	float FuseSeconds = 3.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Utility")
	float BlastRadius = 420.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Utility")
	float MaxDamage = 118.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Utility")
	float MinDamage = 22.f;

	/** How far the flash effect reaches (flashbang). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Utility")
	float FlashRadius = 1400.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Utility")
	float MaxFlashDuration = 5.0f;

	/** Smoke cloud size + lifetime. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Utility")
	float SmokeRadius = 620.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Utility")
	float SmokeDuration = 18.f;

	/** Breach charges: damage applied to the door/barrier's health. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Utility")
	float BreachDamage = 220.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Utility")
	float BreachRadius = 260.f;

	// ======================================================================
	//  Events
	// ======================================================================
	UPROPERTY(BlueprintAssignable, Category = "One Braham|Utility")
	FOneBrahamUtilityDetonated OnDetonated;

	// --- Components -------------------------------------------------------
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Components")
	TObjectPtr<USphereComponent> CollisionSphere = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Components")
	TObjectPtr<UStaticMeshComponent> Mesh = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Components")
	TObjectPtr<UProjectileMovementComponent> ProjectileMovement = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Components")
	TObjectPtr<UNiagaraComponent> TrailFX = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Components")
	TObjectPtr<class UAudioComponent> BeepAudio = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Components")
	TObjectPtr<UNiagaraComponent> DetonationFX = nullptr;

	// ======================================================================
	//  Replicated state
	// ======================================================================
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "One Braham|Utility")
	EOneBrahamUtilityType UtilityType = EOneBrahamUtilityType::Frag;

	UPROPERTY(ReplicatedUsing = OnRep_FuseRemaining, BlueprintReadOnly, Category = "One Braham|Utility")
	float FuseRemaining = 3.5f;

	UPROPERTY(ReplicatedUsing = OnRep_Detonated, BlueprintReadOnly, Category = "One Braham|Utility")
	bool bDetonated = false;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "One Braham|Utility")
	bool bIsPlanted = false;

	/** Who threw it - the trap needs to know who to credit/blame. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "One Braham|Utility")
	FString ThrowerName;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "One Braham|Utility")
	int32 ThrowerPlayerId = -1;

protected:
	UFUNCTION() void OnRep_Detonated();
	UFUNCTION() void OnRep_FuseRemaining();

	/** Fires on every machine that can see this actor: shake, audio, local VFX.
	 *  Unreliable because a missing shake on a dropped packet is not a problem. */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastDetonationFeedback(FVector_NetQuantize DetonationLocation);

	/** Server: the fuse ran out (or the charge was shot). */
	void Detonate();

	void ApplyFragDamage();
	void ApplyFlashEffect();
	void ApplySmokeEffect();
	void ApplyBreachDamage();

	/**
	 * Server: every intact pane of glass inside `RadiusCm` with line of sight to
	 * the blast comes out. Frags and breaching charges take the windows with them,
	 * which is what makes a grenade in a server room change the shape of the room
	 * rather than just killing whoever was standing in it.
	 */
	void ApplyBlastToGlass(const FVector& Origin, float RadiusCm);

	/** Bounce handling: reports the surface for the impact sound + decal. */
	UFUNCTION()
	void OnProjectileBounce(const FHitResult& ImpactResult, const FVector& ImpactVelocity);

	/** Server: increments the grenade bounce count and starts the fuse early after bounces. */
	UFUNCTION()
	void OnProjectileStop(const FHitResult& ImpactResult);
	/**
	 * The Niagara effect this utility detonates with.
	 *
	 * Frag -> FragExplosion, Flashbang -> FlashbangBurst, Smoke -> SmokeCloud,
	 * Breach charge -> BreachExplosion. Kept as a mapping in C++ rather than as
	 * four Blueprint defaults because the fallback below has to be able to name
	 * the system it wants before any Blueprint has been authored.
	 */
	EOneBrahamVFXEffect GetDetonationEffect() const;

	/**
	 * Points DetonationFX at a system.
	 *
	 * RESOLUTION ORDER
	 *     1. The system already assigned (the Blueprint's own, hand-authored FX).
	 *     2. UOneBrahamVFXStatics::LoadEffectSystem(GetDetonationEffect()) - the
	 *        conventional /Game/OneBraham/VFX/NS_OB_* asset that the content build
	 *        generates.
	 *
	 * Without step 2, a frag thrown on a fresh checkout detonates with the audio,
	 * the damage and the camera shake, and no visual whatsoever: every one of the
	 * utility effects would be reachable only from a Blueprint that a human had
	 * to remember to configure. This is the same fallback the HUD uses for its
	 * widget class and the weapon component uses for its registry.
	 */
	void ResolveDetonationFX();



	TWeakObjectPtr<class AOneBrahamCharacter> Thrower;

	/** Clears the fuse. Called the moment the grenade goes off. */
	void ClearTimers();

	FTimerHandle FuseTimerHandle;

	/** How many times it has bounced - a grenade that bounced is more likely to
	 *  have been a bad throw, and the "cook-off" quip fires from the audio manager. */
	int32 BounceCount = 0;

	/** Wall clock time of the last fuse beep (drives the accelerating beep). */
	float LastBeepTime = -1000.f;

public:
	UFUNCTION(BlueprintPure, Category = "One Braham|Utility")
	int32 GetBounceCount() const { return BounceCount; }
};
