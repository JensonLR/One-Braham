// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamAccessZone.h
// ---------------------------------------------------------------------------
// The door. Not the rules - those live in UOneBrahamAccessZoneComponent - but
// the object you actually walk up to, look at, and either badge through, force
// open, or blow off its runners.
//
// WHY THIS IS AN ACTOR AND NOT A BLUEPRINT
//   Every locked door in the game has the same three counterplay options, and
//   every one of them has to work identically on a dedicated server with no
//   art, no audio and no listeners. That is C++ work. The look of the door -
//   mesh, materials, signage, the little lit reader - is data on this actor, so
//   the art pass is mesh and material swaps with no logic to re-author.
//
// WHAT IT OWNS
//   - the leaves and their movement (slide or swing), replicated from the zone
//     component's unlocked flag
//   - the collider that actually stops you (a barrier box, not the leaves: a
//     door you can shoot through and vault is a door that fights navmesh)
//   - the card reader light: red locked, amber overriding, green open
//   - breaching: this actor implements IOneBrahamBreachableInterface, so a
//     charge placed on it carves the doorway open for good
//   - the two-person rule: if the paired zone is being held right now, this
//     door's own override takes 3 seconds instead of 12
//
// The map builder places these on every floor transition, so the entire vertical
// progression of the building - the thing ASCENT is built around - is these
// actors plus the lockdown shutters and the terminals.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Access/OneBrahamBreachableInterface.h"
#include "Interaction/OneBrahamInteractionInterface.h"
#include "Core/OneBrahamTypes.h"
#include "OneBrahamAccessZone.generated.h"

class UBoxComponent;
class UStaticMeshComponent;
class UPointLightComponent;
class USceneComponent;
class UOneBrahamAccessZoneComponent;
class UOneBrahamAudioManagerComponent;
class AOneBrahamPlayerController;

/** Local presentation event: the leaves have started moving. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOneBrahamDoorStateChanged, FName, ZoneId, bool, bOpen);

UCLASS(Blueprintable)
class ONEBRAHAM_API AOneBrahamAccessZone : public AActor,
                                            public IOneBrahamInteractionInterface,
                                            public IOneBrahamBreachableInterface
{
	GENERATED_BODY()

public:
	AOneBrahamAccessZone();

	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// ======================================================================
	//  Placement: the same knobs as the zone component, exposed here too
	// ======================================================================
	// WHY THEY ARE DUPLICATED
	//   The map builder places these actors procedurally and sets the access data
	//   on the *actor*, because that is the object it spawned. Rather than make the
	//   build script reach into a component by name (fragile, and invisible to a
	//   designer selecting the door in the level), the data lives on the actor and
	//   is copied into the component before the component begins play (see
	//   ApplyPlacementToZone). The component stays the single authority for the
	//   rules at runtime; these are the placement-time values.
	//
	//   They replicate once (initial only) so a zone spawned at runtime reaches
	//   clients with its id and tier; a zone loaded from the level already has them.
	/** Stable zone id, e.g. "Office_Floor09", "Skydeck_Gate". */
	UPROPERTY(EditAnywhere, Replicated, BlueprintReadOnly, Category = "One Braham|Access")
	FName ZoneId = NAME_None;

	/** What the door sign and the prompt call this place. */
	UPROPERTY(EditAnywhere, Replicated, BlueprintReadOnly, Category = "One Braham|Access")
	FText ZoneDisplayName;

	/** Badge needed to open it with a card; Override means terminal only. */
	UPROPERTY(EditAnywhere, Replicated, BlueprintReadOnly, Category = "One Braham|Access")
	EOneBrahamAccessTier RequiredTier = EOneBrahamAccessTier::Staff;

	/** Floor the door sits on (0 = plaza, 15 = Skydeck). */
	UPROPERTY(EditAnywhere, Replicated, BlueprintReadOnly, Category = "One Braham|Access")
	int32 FloorIndex = 1;

	/** True when the lockdown director may force this open as the building escalates. */
	UPROPERTY(EditAnywhere, Replicated, BlueprintReadOnly, Category = "One Braham|Access")
	bool bEscalationOpens = true;

	/** The other panel/door of a pair: both held together override in 3 seconds. */
	UPROPERTY(EditAnywhere, Replicated, BlueprintReadOnly, Category = "One Braham|Access")
	FName PairedZoneId = NAME_None;

	/** True when this door has no card reader at all (plaza, public corridors). */
	UPROPERTY(EditAnywhere, Replicated, BlueprintReadOnly, Category = "One Braham|Access")
	bool bUnlockedAtStart = false;

	// ======================================================================
	//  Placement and look
	// ======================================================================
	/** Leaves slide apart (default) or swing. Swinging doors need DoorSwingAngle. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Door")
	bool bSwingDoors = false;

	/** Degrees a swinging leaf travels. 92 is a door that stays where you put it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Door")
	float DoorSwingAngle = 92.f;

	/** Seconds for the leaves to travel. Lift doors are slower than office doors. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Door")
	float DoorOpenSeconds = 1.15f;

	/** True when the door starts open (public corridors, the plaza). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Door")
	bool bStartsOpen = false;

	/** True when a breaching charge can defeat this door rather than only its walls. */
	UPROPERTY(EditAnywhere, Replicated, BlueprintReadOnly, Category = "One Braham|Door")
	bool bBreachable = true;

	/** Raw breaching damage the door absorbs before it opens. Doors resist more
	 *  than plasterboard: a charge on a fire door is a decision, not a shortcut. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Door")
	float BreachIntegrity = 260.f;

	/** Multiplier applied to incoming breaching damage (1.0 = full). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Door")
	float BreachResistance = 0.6f;

	/** True when the door has its own override panel, so it can be forced without
	 *  a separate terminal actor. Lift shafts and stairwells have one. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Door")
	bool bHasOnboardPanel = true;

	/** True when the door is critical to the extraction route: the lockdown
	 *  director may open it but never ask it to seal again. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Door")
	bool bExtractionCritical = false;

	// ======================================================================
	//  Queries
	// ======================================================================
	UFUNCTION(BlueprintPure, Category = "One Braham|Door")
	UOneBrahamAccessZoneComponent* GetZone() const { return Zone; }

	UFUNCTION(BlueprintPure, Category = "One Braham|Door")
	FName GetZoneId() const;

	UFUNCTION(BlueprintPure, Category = "One Braham|Door")
	bool IsOpen() const { return bOpen; }

	/** Server and client: writes the placement values onto the zone component.
	 *  Runs from PostInitializeComponents (level-loaded and spawned zones) and
	 *  again just before the component's BeginPlay, which is the first point a
	 *  client-side spawned zone has its replicated placement. Idempotent. */
	void ApplyPlacementToZone();

	/** Server or client: the pairing partner, if one is placed and loaded. */
	AOneBrahamAccessZone* FindPairedZone() const;

	/** Finds any placed zone by id. Linear search over a handful of actors, which
	 *  is cheaper than maintaining a registry that has to survive level streaming. */
	static AOneBrahamAccessZone* FindZoneById(const UObject* WorldContextObject, FName InZoneId);

	/** Every placed zone, for the HUD map and the QA overlay. */
	static void GetAllZones(const UObject* WorldContextObject, TArray<AOneBrahamAccessZone*>& OutZones);

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
	virtual void OnInteractionHold_Implementation(AOneBrahamPlayerController* Interactor, float HeldSeconds, float Progress01) override;
	virtual void OnInteractionEnd_Implementation(AOneBrahamPlayerController* Interactor, bool bCompleted) override;
	virtual void OnInteractionCancelled_Implementation(AOneBrahamPlayerController* Interactor) override;
	virtual void SetInteractionHighlight_Implementation(bool bHighlighted) override;

	// ======================================================================
	//  IOneBrahamBreachableInterface
	// ======================================================================
	virtual void ApplyBreachDamage(float Damage, AActor* Breacher, const FVector& Origin) override;
	virtual bool IsBreached() const override { return bBreached; }
	virtual float GetBreachResistance() const override { return BreachResistance; }
	virtual float GetBreachIntegrity01() const override;
	virtual int32 GetBreachFloor() const override;

	// ======================================================================
	//  Events
	// ======================================================================
	UPROPERTY(BlueprintAssignable, Category = "One Braham|Door")
	FOneBrahamDoorStateChanged OnDoorStateChanged;

protected:
	// --- components -------------------------------------------------------
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Components")
	TObjectPtr<USceneComponent> Root = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Components")
	TObjectPtr<UStaticMeshComponent> FrameMesh = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Components")
	TObjectPtr<UStaticMeshComponent> LeafLeftMesh = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Components")
	TObjectPtr<UStaticMeshComponent> LeafRightMesh = nullptr;

	/** The thing that actually stops a pawn. Disabled the moment the door opens. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Components")
	TObjectPtr<UBoxComponent> BarrierVolume = nullptr;

	/** The card reader: a small panel that glows red, amber or green. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Components")
	TObjectPtr<UStaticMeshComponent> ReaderMesh = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Components")
	TObjectPtr<UPointLightComponent> ReaderLight = nullptr;

	/** All of the rules: tier, override timing, pairing, unlock method. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Components")
	TObjectPtr<UOneBrahamAccessZoneComponent> Zone = nullptr;

	// ======================================================================
	//  State
	// ======================================================================
	UPROPERTY(ReplicatedUsing = OnRep_DoorState, BlueprintReadOnly, Category = "One Braham|Door")
	bool bOpen = false;

	/** True once a charge has taken the door off its runners. Permanent. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "One Braham|Door")
	bool bBreached = false;

	/** Remaining integrity, server-side only. */
	float RemainingIntegrity = 0.f;

	/** Local: 0 = shut, 1 = fully open. */
	float OpenAlpha = 0.f;

	/** Local: the leaves' relative transforms when shut, captured once. */
	FVector ClosedLeftLocation = FVector::ZeroVector;
	FVector ClosedRightLocation = FVector::ZeroVector;
	FRotator ClosedLeftRotation = FRotator::ZeroRotator;
	FRotator ClosedRightRotation = FRotator::ZeroRotator;

	/** Local: highlighted by the local player's interaction trace. */
	bool bHighlighted = false;

	/** Server: the interactor currently holding the panel, for delta timing. */
	TWeakObjectPtr<AOneBrahamPlayerController> HoldInteractor;
	float HoldLastSeconds = 0.f;

	/** Local: how long the reader light has been showing its current state. */
	float ReaderStateTime = 0.f;

	// ======================================================================
	//  Internals
	// ======================================================================
	UFUNCTION()
	void OnRep_DoorState();

	/** Local: moves the leaves and the collision to match OpenAlpha. */
	void ApplyOpenAlpha(float Alpha);

	/** Server: called when the zone component reports an unlock. */
	UFUNCTION()
	void HandleZoneChanged(FName InZoneId, bool bUnlocked, EOneBrahamUnlockMethod Method);

	/** Local: the reader light and the one-shot for a state change. */
	void PlayDoorEffects(EOneBrahamUnlockMethod Method, bool bUnlocked);

	/** True when the paired zone's panel is being held right now. */
	bool IsPairedPanelHeld() const;

	/** True when the player is carrying a badge that opens this zone. */
	bool HasBadgeFor(const AOneBrahamPlayerController* Interactor) const;

	/** True when the player behind this controller is dead or spectating. */
	static bool IsInteractorEliminated(const AOneBrahamPlayerController* Interactor);

	/** Every local machine: the audio director of this player, if any. */
	static UOneBrahamAudioManagerComponent* GetAudioFor(const AOneBrahamPlayerController* PC);

	/** Local: the world-space point the sound and the breach VFX come from. */
	FVector GetBarrierCentre() const;
};
