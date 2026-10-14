// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamLift.h
// ---------------------------------------------------------------------------
// The glass lifts in the core: the fast way up, and the loud one.
//
// A car runs a shaft and stops at a handful of floors (the lobby, 3 and 9). It
// costs a Staff badge, it announces itself with a chime on every floor it
// passes, and a squad that rides it arrives in a lit glass box in front of
// whoever heard it coming. That is the trade the map is built round.
//
// THE TRAPS
//   * The doors close on whoever stands in them, for real damage, and the
//     kill feed credits "LiftDoors". They then reopen and try again, which is
//     what makes a doorway the worst place on the floor to stand and argue.
//   * The lockdown stops the lifts. From the Lockdown stage on, a car finishes
//     its run, opens and stays open; the call panels say why.
//
// HOW IT MOVES
//   The actor is the car. Every machine works out where the car is from three
//   replicated values (the stop it left, the stop it is going to, and the
//   server time it left) and the shared server clock, so the ride is smooth
//   everywhere without replicating movement, and a player standing in the car
//   is carried by it as a moving base. The landing doors on each stop are
//   components of the same actor with absolute transforms, so they stay on
//   their floors while the car moves.
//
// CALLING IT
//   The interaction surface validates distance against the target actor, and
//   the car may be eight floors away, so each stop has an
//   AOneBrahamLiftCallPanel beside the doors. The car itself is the interaction
//   for anyone inside it: "Lift to floor N".

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Core/OneBrahamTypes.h"
#include "Interaction/OneBrahamInteractionInterface.h"
#include "OneBrahamLift.generated.h"

class AOneBrahamCharacter;
class AOneBrahamLiftCallPanel;
class AOneBrahamPlayerController;
class UBoxComponent;
class USceneComponent;
class UStaticMeshComponent;

UCLASS(Blueprintable)
class ONEBRAHAM_API AOneBrahamLift : public AActor, public IOneBrahamInteractionInterface
{
	GENERATED_BODY()

public:
	AOneBrahamLift();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Stable id, written into the build manifest. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Lift")
	FName LiftId = NAME_None;

	/** Floors the car opens at, lowest first. The shaft spans the first to the last. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Lift")
	TArray<int32> StopFloors = { 1, 3, 9 };

	/** Which way the doors face along Y: -1 opens onto the corridor south of the core. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Lift")
	float DoorFacingY = -1.f;

	/** Seconds per storey of travel. 2.2 s is a real office lift, and slow on purpose. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Lift")
	float SecondsPerFloor = 2.2f;

	/** How long the doors stand open at a stop before they try to close. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Lift")
	float DoorOpenSeconds = 6.f;

	/** Damage dealt to anyone standing in the doorway when the doors close. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Lift")
	float DoorCrushDamage = 35.f;

	/** The badge the car asks for. The lobby car is for staff, not visitors. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Lift")
	EOneBrahamAccessTier RequiredTier = EOneBrahamAccessTier::Staff;

	// ======================================================================
	//  State
	// ======================================================================
	UFUNCTION(BlueprintPure, Category = "One Braham|Lift")
	bool IsMoving() const { return bMoving; }

	UFUNCTION(BlueprintPure, Category = "One Braham|Lift")
	bool AreDoorsOpen() const { return bDoorsOpen; }

	/** The floor the car is at, or last left. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Lift")
	int32 GetCurrentFloor() const;

	/** True while the lockdown has taken the lifts out of service. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Lift")
	bool IsOutOfService() const;

	/** True when Character is standing in the car. */
	bool IsInsideCar(const AActor* Character) const;

	/** Whether a player may operate the lift now, and if not, the reason. */
	bool CanOperate(const AOneBrahamPlayerController* Interactor, FText* OutReason = nullptr) const;

	/**
	 * Server: sends the car to StopFloors[StopIndex]. Refused while it is moving
	 * or out of service. Returns true if the car is on its way, or already there
	 * (the doors then open again).
	 */
	bool ServerRequestStop(int32 StopIndex);

	// ======================================================================
	//  Interaction interface (the car, for whoever is inside)
	// ======================================================================
	virtual FText GetInteractionPrompt_Implementation(const AOneBrahamPlayerController* Interactor) const override;
	virtual FGameplayTag GetInteractionTag_Implementation(const AOneBrahamPlayerController* Interactor) const override;
	virtual bool CanInteract_Implementation(const AOneBrahamPlayerController* Interactor) const override;
	virtual bool RequiresHold_Implementation(const AOneBrahamPlayerController* Interactor) const override;
	virtual float GetHoldDuration_Implementation(const AOneBrahamPlayerController* Interactor) const override;
	virtual float GetInteractionRange_Implementation(const AOneBrahamPlayerController* Interactor) const override;
	virtual bool OnInteractionStart_Implementation(AOneBrahamPlayerController* Interactor) override;
	virtual void OnInteractionEnd_Implementation(AOneBrahamPlayerController* Interactor, bool bCompleted) override;
	virtual void OnInteractionCancelled_Implementation(AOneBrahamPlayerController* Interactor) override;
	virtual void SetInteractionHighlight_Implementation(bool bHighlighted) override;

	/** Reach of the car's own panel and of every call panel, cm. */
	static constexpr float PanelRangeCm = 200.f;

protected:
	UFUNCTION()
	void OnRep_Doors();

	/** Every machine: places the car for the current server time. */
	void UpdateCarPosition();

	/** Every machine: slides the car and landing doors toward their state. */
	void UpdateDoors(float DeltaSeconds);

	/** Server: the car has reached its target. */
	void ServerArrive();

	/** Server: try to close; anyone in the doorway is hit and the doors reopen. */
	void ServerTryCloseDoors();

	/** Server: the next stop a rider is sent to: up the list, then back to the bottom. */
	int32 NextStopFor(int32 FromStopIndex) const;

	float StopZ(int32 StopIndex) const;

	/** The shared clock: the game state's server time, or the world's when there is none. */
	double ServerNow() const;

	/** The everyone-hears-it chime when the car arrives. */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastArrived();

	/** Unscaled root at the centre of the car's floor; the parts hang off it. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Components")
	TObjectPtr<USceneComponent> CarRoot = nullptr;

	/** The car: floor, roof, back and side walls in glass, and the door leaf. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Components")
	TObjectPtr<UStaticMeshComponent> CarFloor = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Components")
	TObjectPtr<UStaticMeshComponent> CarRoof = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Components")
	TObjectPtr<UStaticMeshComponent> CarBack = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Components")
	TObjectPtr<UStaticMeshComponent> CarLeft = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Components")
	TObjectPtr<UStaticMeshComponent> CarRight = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Components")
	TObjectPtr<UStaticMeshComponent> CarDoor = nullptr;

	/** The doorway: who is standing in it when the doors close. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Components")
	TObjectPtr<UBoxComponent> DoorwayVolume = nullptr;

	/** The landing door on each stop, in StopFloors order. Absolute transforms. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> LandingDoors;

	/** Server-spawned call panels, one per stop. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<AOneBrahamLiftCallPanel>> CallPanels;

	UPROPERTY(Replicated)
	int32 FromStopIndex = 0;

	UPROPERTY(Replicated)
	int32 ToStopIndex = 0;

	/** Server time the car left FromStopIndex. */
	UPROPERTY(Replicated)
	double MoveStartTime = 0.0;

	UPROPERTY(Replicated)
	bool bMoving = false;

	UPROPERTY(ReplicatedUsing = OnRep_Doors)
	bool bDoorsOpen = true;

	/** 0 closed, 1 open, eased locally toward bDoorsOpen. */
	float DoorAlpha = 1.f;

	FTimerHandle DoorTimerHandle;

	/** The shaft's centre: where the actor sits at the lowest stop. */
	FVector ShaftBase = FVector::ZeroVector;
};

/**
 * The call button beside a lift's doors on one floor. Spawned by the lift on
 * the server; it exists so the call can be validated against something on the
 * caller's floor.
 */
UCLASS(NotBlueprintable)
class ONEBRAHAM_API AOneBrahamLiftCallPanel : public AActor, public IOneBrahamInteractionInterface
{
	GENERATED_BODY()

public:
	AOneBrahamLiftCallPanel();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Server: which lift and which of its stops this panel calls to. */
	void Configure(AOneBrahamLift* InLift, int32 InStopIndex);

	virtual FText GetInteractionPrompt_Implementation(const AOneBrahamPlayerController* Interactor) const override;
	virtual FGameplayTag GetInteractionTag_Implementation(const AOneBrahamPlayerController* Interactor) const override;
	virtual bool CanInteract_Implementation(const AOneBrahamPlayerController* Interactor) const override;
	virtual bool RequiresHold_Implementation(const AOneBrahamPlayerController* Interactor) const override;
	virtual float GetHoldDuration_Implementation(const AOneBrahamPlayerController* Interactor) const override;
	virtual float GetInteractionRange_Implementation(const AOneBrahamPlayerController* Interactor) const override;
	virtual bool OnInteractionStart_Implementation(AOneBrahamPlayerController* Interactor) override;
	virtual void OnInteractionEnd_Implementation(AOneBrahamPlayerController* Interactor, bool bCompleted) override;
	virtual void OnInteractionCancelled_Implementation(AOneBrahamPlayerController* Interactor) override;
	virtual void SetInteractionHighlight_Implementation(bool bHighlighted) override;

protected:
	UPROPERTY(VisibleAnywhere, Category = "One Braham|Components")
	TObjectPtr<UStaticMeshComponent> Panel = nullptr;

	UPROPERTY(Replicated)
	TObjectPtr<AOneBrahamLift> Lift = nullptr;

	UPROPERTY(Replicated)
	int32 StopIndex = 0;
};
