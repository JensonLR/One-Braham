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
//     hit bone only, so a shot to the arm moves the arm), and a whole-upper-body
//     stagger on heavy hits and headshots. These are cosmetic and play on every
//     machine from AOneBrahamWeaponBase::MulticastPlayCharacterHitFX.
//   * Blood: a pool that spreads under a body once it has settled, and under a
//     downed player (with a drip trail when they crawl); wound shading on the
//     body's materials. All gated by ob.Gore.
//   * Near-miss delivery: the owning client's "a round just went past your
//     head" RPC lives here because this component is on every character and
//     is owned by that character's player.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Core/OneBrahamTypes.h"
#include "VFX/OneBrahamVFXStatics.h"
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

	/** Applies a physics impulse to limbs on a non-lethal hit. Kept for
	 *  Blueprint callers; PlayHitReaction is the full version. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Corpse")
	void ApplyHitReaction(FName HitBone, const FVector& ShotDir, float Damage);

	/**
	 * Cosmetic flinch on THIS machine: a short partial physics blend below the
	 * hit bone, or below the upper spine for a stagger (Damage >= StaggerDamage
	 * or a headshot). On the victim's own machine it is a small camera flinch
	 * instead, because their third-person body is not what they are looking at.
	 * Does nothing on a dedicated server.
	 */
	void PlayHitReaction(FName HitBone, const FVector& ShotDir, float Damage, bool bHeadshot);

	/** Accumulates wound shading: sets "BloodAmount" (0..1) on the body's
	 *  materials. Does nothing with ob.Gore 0 or on a dedicated server. */
	void AddWoundShading(float Damage);

	/**
	 * Owning client: a round passed within ~1.5 m of this player's head. Sent by
	 * AOneBrahamWeaponBase::NotifyNearMisses, throttled to four a second per
	 * victim on the server - which is why it can afford to be reliable.
	 */
	UFUNCTION(Client, Reliable)
	void ClientNearMiss(FVector_NetQuantize PassLocation);

	/** Damage at or above which a hit staggers the whole upper body. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Corpse")
	float StaggerDamage = 40.f;

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

	/**
	 * Watches the ragdoll's pelvis until it stops, and plays the corpse's impact
	 * on the way down.
	 *
	 * WHY THIS IS NOT JUST A THUD SOUND
	 *     A body that lands silently reads as a bug, and a body that lands with a
	 *     wet slap reads as a different genre. The impact is dust and a dull
	 *     weight: NS_OB_CorpseImpact is authored for exactly that. It fires once,
	 *     at the moment the body's downward speed crosses the threshold, which is
	 *     the frame a viewer reads as "it hit the floor".
	 */
	void TickCorpseLanding(float DeltaTime);

	/** Set while the corpse is falling; cleared when the impact has been played. */
	bool bCorpseFalling = false;

	/** Downward speed of the previous frame, for the landing detection. */
	float LastCorpseFallSpeed = 0.f;

	/** Seconds since the ragdoll started, so a body that never lands is bounded. */
	float CorpseAirTime = 0.f;

	/** Finds the skeletal mesh to ragdoll (3P body). */
	USkeletalMeshComponent* GetTargetMesh() const;

	// --- Hit reaction state (cosmetic, local) -----------------------------
	/** Blends the active reaction out; ends it when its time is up. */
	void TickHitReaction(float DeltaTime);

	/** Stops the active reaction immediately and restores the living mesh. */
	void EndHitReaction(bool bRestoreCollision);

	/** First bone at or above Bone that has a physics body, or NAME_None. */
	FName FindSimulatedBone(USkeletalMeshComponent* Mesh, FName Bone) const;

	FName ReactionBone = NAME_None;
	float ReactionDuration = 0.f;
	float ReactionRemaining = 0.f;
	float ReactionPeakWeight = 0.f;
	bool bReactionActive = false;
	bool bReactionEnabledPhysics = false;

	/** Wound shading so far, 0..1. */
	float WoundAmount = 0.f;

	// --- Blood pools (cosmetic, local) ------------------------------------
	/** Pool under a settled corpse, and pools/drips under a downed player. */
	void TickBlood(float DeltaTime);

	/** Floor point under a world location, ignoring the body. */
	bool FindFloorBelow(const FVector& From, FVector& OutFloor) const;

	/** The blood pass runs at about 2 Hz even while the flinch ticks every frame. */
	float BloodTickAccumulator = 0.f;

	/** Local time this machine saw the body go limp. */
	float LocalRagdollStartTime = -1.f;
	bool bCorpsePoolSpawned = false;

	bool bWasDowned = false;
	float DownedSince = 0.f;
	float StationarySince = 0.f;
	FVector LastBleedLocation = FVector::ZeroVector;
	FVector LastDripLocation = FVector::ZeroVector;
	TArray<FVector> DownedPools;

	TWeakObjectPtr<class AOneBrahamCharacter> CachedCharacter;

	float CorpseSpawnTime = -1.f;
	bool bFadingOut = false;
	float FadeStartTime = -1.f;
};
