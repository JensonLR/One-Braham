// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamBreachableWall.h
// ---------------------------------------------------------------------------
// "Not every door in this building has a handle."
//
// Four partitions in One Braham are load-bearing in the map's design sense
// rather than the structural one: a plasterboard office partition on floor 5, a
// riser panel on the server floor, a metal stair-lock plate on the executive
// floor, and the canteen's spine wall on floor 14. Each one is a shortcut that
// nobody has a card for, and each one is loud enough that taking it is a public
// decision.
//
// WHY THIS IS AN ACTOR AND NOT A BLUEPRINT
//     The map builder places four of these, all identical apart from their
//     surface and their size, and the validator checks their positions against
//     the build manifest. A Blueprint would be a fifth thing to keep in sync
//     with the manifest and the fourth to be forgotten. This is C++, the builder
//     spawns it directly, and the geometry of the opening is computed from the
//     wall's own dimensions so a wall of any size produces a correct hole.
//
// WHAT A BREACH ACTUALLY DOES
//     1. The blocking wall is switched off and the three frame pieces (two jambs
//        and a lintel) are switched on, so the opening is a real hole with real
//        geometry around it and the player can see how thick the wall was.
//     2. The navigation mesh is dirtied over the wall's bounds, so AI (and, on a
//        listen server, the bots on the other side) can path through it.
//     3. The dust, debris and the charge's own explosion play locally, and the
//        game state announces the breach to everyone: the hole is now everybody's
//        route, which is the cost of taking the shortcut.
//
//     The actor is never destroyed. A wall that has been breached is a permanent
//     feature of that match's building, and a designer can hang shelves of
//     narrative on it (a coat rail of scorched hi-vis) without a second actor.
//
// TODDLER-PROOFING NOTES FOR LEVEL DESIGNERS
//     * Do not rotate the actor on anything but yaw. The opening maths assumes
//       "width on X, thickness on Y, height on Z".
//     * Do not scale the actor. Call SetWallDimensions() instead; scaling breaks
//       the opening size and therefore the navmesh hole.
//     * Never place one of these where the opening would be less than 120 cm
//       wide: a hole a player cannot fit through is a hole a player will report
//       as a bug, and they will be right.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Core/OneBrahamTypes.h"
#include "Access/OneBrahamBreachableInterface.h"
#include "Interaction/OneBrahamInteractionInterface.h"
#include "OneBrahamBreachableWall.generated.h"

class UStaticMeshComponent;
class UMaterialInterface;
class UOneBrahamInventoryComponent;

/** Broadcast on the server when this wall opens, for audio and objective hooks. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOneBrahamWallBreached, AOneBrahamBreachableWall*, Wall, AActor*, Breacher);

/**
 * A partition that a breaching charge can open.
 *
 * Implements both the breach contract (so a charge's detonation finds it) and the
 * interaction contract (so a player looking at an intact wall is told, in the
 * HUD, that it needs a charge rather than a card).
 */
UCLASS(Blueprintable, ClassGroup = (OneBraham), meta = (DisplayName = "One Braham Breachable Wall"))
class ONEBRAHAM_API AOneBrahamBreachableWall : public AActor,
	public IOneBrahamBreachableInterface,
	public IOneBrahamInteractionInterface
{
	GENERATED_BODY()

public:
	AOneBrahamBreachableWall();

	// ======================================================================
	//  Identity and geometry
	// ======================================================================

	/** Stable id from the map builder's BREACHABLE_WALLS table. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Wall")
	FName WallId = NAME_None;

	/** Floor this wall stands on, so the lockdown director can place the hole. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Wall")
	int32 FloorIndex = 0;

	/**
	 * Surface family for impacts and for the material lookup the builder uses:
	 * one of Plasterboard, Metal, Concrete, Glass, Wood, Grating, ServerRack,
	 * Lino, Carpet, Decking, Marble.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Wall")
	FName SurfaceId = TEXT("Plasterboard");

	/**
	 * Local size of the wall in centimetres: X is width, Y is thickness, Z is
	 * height. Keep the actor's yaw at a multiple of 90 degrees.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Wall")
	FVector WallDimensionsCm = FVector(600.f, 25.f, 400.f);

	/** Call from the map builder instead of scaling the actor. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Wall")
	void SetWallDimensions(const FVector& InDimensionsCm, bool bApplyImmediately = true);

	/** The hole's dimensions once opened. Clamped to fit the wall. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Wall")
	FVector2D OpeningSizeCm = FVector2D(140.f, 210.f);

	// ======================================================================
	//  Breaching
	// ======================================================================

	/** Integrity against breaching damage. 260 stonewalls a single charge. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Breach")
	float BreachIntegrity = 260.f;

	/**
	 * Fraction of incoming breaching damage the wall resists. 0.6 means a charge
	 * has to be placed twice on anything structurally braced; plasterboard's
	 * lower value is the reason the floor-5 partition is the cheap route.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Breach")
	float BreachResistance = 0.6f;

	/** A wall that starts breached is a wall somebody already opened. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Breach")
	bool bStartsBreached = false;

	/** Announce the opening to the whole match. The hole is public information. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Breach")
	bool bAnnounceOnBreach = true;

	/** Seconds the plant animation takes before the charge is live. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Breach")
	float PlantSeconds = 1.6f;

	UPROPERTY(BlueprintAssignable, Category = "One Braham|Breach")
	FOneBrahamWallBreached OnWallBreached;

	/** Read-only state for the HUD and for Blueprint glue. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Breach")
	float GetRemainingIntegrity() const { return RemainingIntegrity; }

	UFUNCTION(BlueprintPure, Category = "One Braham|Breach")
	float GetIntegrityFraction() const
	{
		return BreachIntegrity > KINDA_SMALL_NUMBER ? RemainingIntegrity / BreachIntegrity : 0.f;
	}

	/** Server side: open the wall. Also used by the map builder for pre-opened walls. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Breach")
	void ServerBreach(AActor* Breacher);

	/** Which way the wall faces in world space, for debris and decals. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Wall")
	FVector GetWallNormal() const;

	/** Centre of the wall in world space. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Wall")
	FVector GetWallCentre() const;

	// ======================================================================
	//  IOneBrahamBreachableInterface
	// ======================================================================

	virtual void ApplyBreachDamage(float Damage, AActor* Breacher, const FVector& Origin) override;
	virtual bool IsBreached() const override { return bBreached; }
	virtual float GetBreachResistance() const override { return BreachResistance; }
	virtual float GetBreachIntegrity01() const override { return GetIntegrityFraction(); }
	virtual int32 GetBreachFloor() const override { return FloorIndex; }

	// ======================================================================
	//  IOneBrahamInteractionInterface
	// ======================================================================

	virtual FText GetInteractionPrompt_Implementation(const AOneBrahamPlayerController* Interactor) const override;
	virtual FGameplayTag GetInteractionTag_Implementation(const AOneBrahamPlayerController* Interactor) const override;
	virtual bool CanInteract_Implementation(const AOneBrahamPlayerController* Interactor) const override;
	virtual bool RequiresHold_Implementation(const AOneBrahamPlayerController* Interactor) const override;
	virtual float GetHoldDuration_Implementation(const AOneBrahamPlayerController* Interactor) const override;
	virtual float GetInteractionRange_Implementation(const AOneBrahamPlayerController* Interactor) const override;
	virtual bool OnInteractionStart_Implementation(AOneBrahamPlayerController* Interactor) override;
	virtual void OnInteractionEnd_Implementation(AOneBrahamPlayerController* Interactor, bool bCompleted) override;
	virtual void SetInteractionHighlight_Implementation(bool bHighlighted) override;

	/** Event fired on clients when the wall opens, so Blueprint can react locally. */
	UFUNCTION(BlueprintImplementableEvent, Category = "One Braham|Breach")
	void OnWallOpenedLocally(AActor* Breacher);

protected:
	virtual void BeginPlay() override;
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** The intact wall face. Blocking while whole. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Wall")
	TObjectPtr<UStaticMeshComponent> WallMesh;

	/** Two jambs and a lintel, hidden until the wall opens. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Wall")
	TObjectPtr<UStaticMeshComponent> FrameWest;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Wall")
	TObjectPtr<UStaticMeshComponent> FrameEast;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Wall")
	TObjectPtr<UStaticMeshComponent> FrameLintel;

	/** Material applied to the wall and the frame by the map builder. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Wall")
	TObjectPtr<UMaterialInterface> WallMaterial;

	UPROPERTY(ReplicatedUsing = OnRep_Breached, BlueprintReadOnly, Category = "One Braham|Breach")
	bool bBreached = false;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "One Braham|Breach")
	float RemainingIntegrity = 260.f;

	UFUNCTION()
	void OnRep_Breached();

	/**
	 * Server -> every client: the dust, the debris, the bang. Sent unreliably
	 * because a lost breach effect is a cosmetic loss, not a gameplay one; the
	 * state itself replicates reliably through bBreached.
	 */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastPlayBreachEffects(const FVector& Origin);

private:
	/** Recompute the mesh scales and offsets from WallDimensionsCm. */
	void RebuildGeometry();

	/** Paints every wall piece with WallMaterial, in the editor and in play. */
	void ApplyWallMaterial();

	/** Swap the collision and visibility to the opened state. Safe to call twice. */
	void ApplyBreachedState();

	/** The clamped, final opening size for the current wall. */
	FVector2D GetClampedOpeningSize() const;

	/** Custom-depth outline state, driven by the local HUD's focus trace. */
	bool bHighlighted = false;

	/** Mesh scale helper: the engine cube is 100 cm, so scale is centimetres/100. */
	static FVector ScaleForCm(const FVector& SizeCm)
	{
		return SizeCm / 100.f;
	}
};
