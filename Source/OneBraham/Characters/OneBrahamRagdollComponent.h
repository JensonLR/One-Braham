// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamRagdollComponent.h
// ---------------------------------------------------------------------------
// Death physics. ONE BRAHAM's corpses are a big part of how the building reads:
// a body slumped against a server rack tells a better story than a death box.
//
// What this does:
//   * Switches the character to a Chaos physics ragdoll on death with an impulse
//     derived from the killing shot (hit bone + direction + damage).
//   * Applies the "clip friendly" rule: impulse is clamped so bodies do not
//     launch across the room, which looks cheap.
//   * Replaces the capsule with per-bone collision so corpses stack believably
//     in stairwells and doorway chokepoints.
//   * Spawns the lootable death bag and drops the equipment.
//   * Cleans up after DeathBagLifetimeSeconds with a fade-out (bodies must not
//     accumulate forever on a 16-player server).
//   * Provides hit reactions for limbs on non-lethal hits (physics blend on the
//     hit bone only, so a shot to the arm moves the arm).

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Core/OneBrahamTypes.h"
#include "OneBrahamRagdollComponent.generated.h"

class USkeletalMeshComponent;
class UPhysicsAsset;

/** How a corpse came to be lying there - drives the pose and the grime decal. */
UENUM(BlueprintType)
enum class EOneBrahamDeathPose : uint8
{
	/** Default: physics decides. */
	Physics = 0,
	/** Slumped against a wall/desk (detected from the surface behind). */
	Slumped = 1,
	/** Face down on the floor (impacted forward). */
	FaceDown = 2,
	/** Fallen from height - all bones relaxed, no impulse. */
	Fallen = 3,
	/** Executed: knees first, then forward. */
	Executed = 4
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOneBrahamCorpseReady, AActor*, Corpse);

UCLASS(ClassGroup = (OneBraham), meta = (BlueprintSpawnableComponent))
class ONEBRAHAM_API UOneBrahamRagdollComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UOneBrahamRagdollComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// ======================================================================
	//  Config
	// ======================================================================
	/** Seconds before the corpse fades. 0 = keep forever (QA only). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Corpse")
	float CorpseLifetimeSeconds = 300.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Corpse")
	float FadeOutSeconds = 3.f;

	/** Impulse per point of damage. Clamped so bodies never launch. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Corpse")
	float ImpulsePerDamage = 260.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Corpse")
	float MaxImpulse = 42000.f;

	/** Upward bias: people fall down, not up. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Corpse")
	float UpwardBias = 0.18f;

	/** Cap on simultaneous ragdolls on a client - the rest become "frozen" poses. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Corpse")
	int32 MaxActiveRagdolls = 6;

	/** Blend duration when the ragdoll takes over from the animated pose. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Corpse")
	float RagdollBlendTime = 0.18f;

	// ======================================================================
	//  API
	// ======================================================================
	/**
	 * Server: puts the owner into a ragdoll.
	 * @param HitBone    bone that took the killing hit (drives the impulse origin)
	 * @param ShotDir    world-space direction of the shot
	 * @param Damage     final damage of the killing blow
	 */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Corpse")
	void EnterRagdoll(FName HitBone, const FVector& ShotDir, float Damage);

	/** Restores the capsule + animated pose (respawn, revive). */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Corpse")
	void ExitRagdoll();

	/** Server: destroys the corpse immediately (fell out of world, cleanup pass). */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Corpse")
	void DestroyCorpseNow();

	/** Applies a physics impulse to limbs on a non-lethal hit. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Corpse")
	void ApplyHitReaction(FName HitBone, const FVector& ShotDir, float Damage);

	UFUNCTION(BlueprintPure, Category = "One Braham|Corpse")
	bool IsRagdolling() const { return bRagdolling; }

	UFUNCTION(BlueprintPure, Category = "One Braham|Corpse")
	EOneBrahamDeathPose GetDeathPose() const { return DeathPose; }

	UPROPERTY(BlueprintAssignable, Category = "One Braham|Corpse")
	FOneBrahamCorpseReady OnCorpseReady;

	// --- Replicated -------------------------------------------------------
	UPROPERTY(ReplicatedUsing = OnRep_Ragdolling, BlueprintReadOnly, Category = "One Braham|Corpse")
	bool bRagdolling = false;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "One Braham|Corpse")
	EOneBrahamDeathPose DeathPose = EOneBrahamDeathPose::Physics;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "One Braham|Corpse")
	FVector_NetQuantize RagdollImpulse = FVector::ZeroVector;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "One Braham|Corpse")
	FName KillingBone = NAME_None;

protected:
	UFUNCTION() void OnRep_Ragdolling();

	/** Runs the fade-out and destroys the corpse actor's visual components. */
	void BeginFadeOut();

	/** Replaces capsule collision with bone collision so bodies stack properly. */
	void SwapToCorpseCollision();

	/** Restores capsule collision on revive/respawn. */
	void RestoreLivingCollision();

	/** Decides the death pose from the geometry the body landed on. */
	EOneBrahamDeathPose DetermineDeathPose(const FVector& ImpulseDirection) const;

	/** Finds the skeletal mesh to ragdoll (3P body). */
	USkeletalMeshComponent* GetTargetMesh() const;

	TWeakObjectPtr<class AOneBrahamCharacter> CachedCharacter;

	float CorpseSpawnTime = -1.f;
	bool bFadingOut = false;
	float FadeStartTime = -1.f;
};
