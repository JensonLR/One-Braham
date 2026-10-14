// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamBreakableGlass.h
// ---------------------------------------------------------------------------
// A pane of ballistic glass, and the hole it leaves.
//
// One Braham's glass is the only cover in the building that is also a window:
// you can see through it, you cannot shoot through it, and the moment it breaks
// the person on the other side has lost the wall they were hiding behind. That
// trade - information now, cover later - is the reason the partition glazing on
// the server floor and the boardroom is worth placing carefully.
//
// WHY THIS EXISTS
//     `AOneBrahamWeaponBase` has always had a glass branch: it detects a pane by
//     the collision object type, applies point damage to "AOneBrahamBreakableGlass
//     implementing the damage interface", and returns no damage. The class did not
//     exist. Every bullet that hit every pane of glass in this building reported
//     an impact, suppressed its decal because the hole stays clean, played the
//     glass impact and then did nothing at all - and `SpawnGlassShatter`, which
//     was written for exactly this moment, had no caller.
//
// WHAT BREAKING IT DOES
//     1. The shatter plays from the pane's own system, positioned at the hit and
//        oriented along its normal, so a shot from inside breaks towards the
//        shooter - `UOneBrahamVFXStatics::SpawnGlassShatter`.
//     2. The glass stops being cover: the mesh goes invisible and non-colliding
//        in one step, on every machine. The frame is the wall the builder placed
//        the pane in, so the opening is real.
//     3. It is loud, and it is permanent for that match. Breaking a partition is
//        a decision, not a tactic: floors 9 through 12 now know which room you
//        are in.
//
//     A pane never comes back. A breach in this building is a fact about the
//     match, and a pane that repaired itself would be the only thing in One
//     Braham that does.
//
// TODDLER-PROOFING NOTES FOR LEVEL DESIGNERS
//     * Do not scale the actor. Set `PaneSizeCm` and `ThicknessCm` instead; the
//       builder does this for every pane it places, from the table it keeps.
//     * Yaw only. The pane's normal is its forward vector, and the shatter is
//       oriented from it.
//     * `PaneHealth` is in points of weapon damage, not bullets: a 5.56 round is
//       about 34 at the muzzle, so 60 is two rounds and 120 is a magazine's worth.
//       A pane nobody can break is scenery; a pane a rifle opens in one shot is
//       not cover, it is a window.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Core/OneBrahamTypes.h"
#include "OneBrahamBreakableGlass.generated.h"

class UStaticMeshComponent;
class USceneComponent;

UCLASS(Blueprintable)
class ONEBRAHAM_API AOneBrahamBreakableGlass : public AActor
{
	GENERATED_BODY()

public:
	AOneBrahamBreakableGlass();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/**
	 * Point damage from a weapon, or radial damage from a frag or a breaching
	 * charge. Returns the damage actually taken, like every other sink in this
	 * project, so a shooter can be credited for the break.
	 */
	virtual float TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent,
		AController* EventInstigator, AActor* DamageCauser) override;

	// ======================================================================
	//  Placement
	// ======================================================================
	/** Stable id: "Pane_Server_ColdAisle_A". Written into the build manifest. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Glass")
	FName PaneId = NAME_None;

	/** Width (X) and height (Z) in cm. The builder sets both. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Glass")
	FVector2D PaneSizeCm = FVector2D(240.f, 220.f);

	/** Glass thickness in cm, for the frame the player sees through the hole. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Glass")
	float ThicknessCm = 6.f;

	/** Damage the pane absorbs before it goes. Two 5.56 rounds, or one burst. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Glass")
	float PaneHealth = 60.f;

	/** Area of the hole, for the debris burst's scale. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Glass")
	float ShatterAreaCm2 = 9000.f;

	/** Which floor this pane is on, so a designer can filter the outliner. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Glass")
	int32 FloorIndex = 0;

	// ======================================================================
	//  Queries
	// ======================================================================
	UFUNCTION(BlueprintPure, Category = "One Braham|Glass")
	bool IsBroken() const { return bBroken; }

	/** 1.0 on an intact pane, 0.0 the moment it goes. HUD and audio read this. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Glass")
	float GetPaneHealth01() const;

	/** Centre of the pane in world space - where the shatter is built from. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Glass")
	FVector GetPaneCentre() const;

	// ======================================================================
	//  Breaking
	// ======================================================================
	/**
	 * Server: take the pane out now, from a hit at `ImpactPoint`. Public because
	 * a breach charge, a scripted moment or a designer's trigger may want to do
	 * what a bullet does.
	 */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Glass")
	void Shatter(const FVector& ImpactPoint, const FVector& ImpactNormal);

	/** Every machine: the shatter, the hole, and the loss of cover. */
	UFUNCTION(NetMulticast, Reliable)
	void MulticastShatter(FVector_NetQuantize ImpactPoint, FVector_NetQuantizeNormal ImpactNormal);

protected:
	/** Root, so the pane can be rotated and placed like anything else. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Glass")
	TObjectPtr<USceneComponent> Root;

	/** The glazing: the only component that ever collides. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Glass")
	TObjectPtr<UStaticMeshComponent> PaneMesh;

	/** True once the glass is out. Replicated, so a late joiner sees the hole. */
	UPROPERTY(ReplicatedUsing = OnRep_Broken, VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Glass")
	bool bBroken = false;

	/** Server-side accumulator. */
	float AccumulatedDamage = 0.f;

	/**
	 * How cracked the pane looks, 0..255: the damage it has taken as a share of
	 * what breaks it. Replicated as a byte so every machine draws the same cracks
	 * (M_OB_Glass CrackAmount) and a shot window reads as shot.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_Crack)
	uint8 CrackLevel = 0;

	UFUNCTION()
	void OnRep_Crack();

	/** Writes CrackLevel into the pane's material. */
	void ApplyCrack();

	/** The pane's own material instance, made on first crack. */
	UPROPERTY(Transient)
	TObjectPtr<class UMaterialInstanceDynamic> PaneMaterial = nullptr;

	/** Applies `PaneSizeCm` and `ThicknessCm` to the mesh. Never scale the actor. */
	void ApplyDimensions();

	/** Hides the glass and takes away its cover. Idempotent. */
	void ApplyBrokenState();

	UFUNCTION()
	void OnRep_Broken();
};
