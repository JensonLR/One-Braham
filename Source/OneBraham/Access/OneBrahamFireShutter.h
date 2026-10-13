// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamFireShutter.h
// ---------------------------------------------------------------------------
// The steel roller shutter that comes down when security seals a floor.
//
// These are the things that make a lockdown *feel* like a lockdown: a warning
// strobe, a two-tone chime, and then 400 kilograms of galvanised steel falling
// into its runners two and three quarter seconds later. They matter tactically
// too. When a shutter drops between you and the lift shaft you have lost the
// fast way up, and the only questions left are the stairs, the terminal, or the
// charge on the wall.
//
// WHAT THIS ACTOR OWNS
//   - the movement (a slide along a local axis, interpolated, replicated)
//   - the collision (a seal volume that blocks when closed, so AI and navmesh
//     agree with what the player can see)
//   - the safety (a shutter will *not* close on someone: it waits, and it keeps
//     waiting until they move, because being crushed by level geometry is not a
//     good death in a game about being shot)
//   - the theatre (warning strobe, chime, dust, and the boom that tells the
//     whole floor that the building has just changed its mind about them)
//
// It implements IOneBrahamShutterInterface, so UOneBrahamLockdownComponent
// drives it directly with no Blueprint glue, and it self-registers on BeginPlay
// so a streamed-in floor is sealed the moment it loads.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Lockdown/OneBrahamLockdownComponent.h"
#include "OneBrahamFireShutter.generated.h"

class UBoxComponent;
class UStaticMeshComponent;
class USoundBase;
class UPointLightComponent;

/** Raised locally when a shutter finishes moving, for audio and VFX hooks. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOneBrahamShutterMoved, FName, ShutterId, bool, bClosed);

UCLASS(Blueprintable)
class ONEBRAHAM_API AOneBrahamFireShutter : public AActor, public IOneBrahamShutterInterface
{
	GENERATED_BODY()

public:
	AOneBrahamFireShutter();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// ======================================================================
	//  Placement
	// ======================================================================
	/** Stable id, e.g. "Shutter_F08_StairA". Used by logs and the QA overlay. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Shutter")
	FName ShutterId = NAME_None;

	/** Floor this shutter seals. 0 is the plaza, 15 is the Skydeck. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Shutter")
	int32 FloorIndex = 1;

	/** Second floor this shutter also seals, when one mesh spans a stair core (INDEX_NONE = none). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Shutter")
	int32 SecondaryFloorIndex = INDEX_NONE;

	/** Local-space offset the mesh travels to be fully open. Default: straight up 300 cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Shutter")
	FVector SlideOffset = FVector(0.f, 0.f, 300.f);

	/** Local-space offset the mesh travels to be fully closed (usually zero). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Shutter")
	FVector ClosedOffset = FVector::ZeroVector;

	// ======================================================================
	//  Behaviour
	// ======================================================================
	/** Seconds to travel the full slide when opening. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Shutter")
	float OpenSeconds = 3.2f;

	/** Seconds to travel the full slide when closing. The lockdown's own number
	 *  (2.75 s) is passed in by the director and overrides this. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Shutter")
	float CloseSeconds = 2.75f;

	/** Warning strobe and chime time before the shutter moves down. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Shutter")
	float WarnSeconds = 1.1f;

	/** True when the shutter is down at BeginPlay. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Shutter")
	bool bStartClosed = false;

	/**
	 * False for anything the lockdown must never seal: the Skydeck gate, the
	 * extraction stair. The building can trap people, but it can never trap them
	 * away from the only way out. Data, not a special case in code.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Shutter")
	bool bAllowLockdownClose = true;

	/** True when the shutter resists a breaching charge (0..1 multiplier applied to damage). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Shutter")
	float BreachResistance = 0.55f;

	/** True when the strobe/chime plays before closing. Off for quiet, distant shutters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Shutter")
	bool bWarnBeforeClosing = true;

	/** Seconds the shutter waits for a body to move out of the doorway before it gives up waiting. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Shutter")
	float MaxSafetyDelay = 12.f;

	// ======================================================================
	//  Server API
	// ======================================================================
	/** Server: request a state change. Honours safety and bAllowLockdownClose. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Shutter")
	void ServerSetClosed(bool bNewClosed, float DurationSeconds = -1.f);

	/** Server: drop immediately with no warning. Debug and QA. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Shutter")
	void ServerSlamClosed();

	// ======================================================================
	//  Queries
	// ======================================================================
	UFUNCTION(BlueprintPure, Category = "One Braham|Shutter")
	bool IsClosed() const { return bClosed; }

	UFUNCTION(BlueprintPure, Category = "One Braham|Shutter")
	bool IsMoving() const { return !FMath::IsNearlyEqual(MoveAlpha, TargetAlpha, 0.001f); }

	/** 0 = fully up, 1 = fully down. Drives the HUD map and the audio distance filter. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Shutter")
	float GetClosedAlpha() const { return MoveAlpha; }

	UFUNCTION(BlueprintPure, Category = "One Braham|Shutter")
	bool BlocksWalking() const { return MoveAlpha > 0.08f; }

	// ======================================================================
	//  IOneBrahamShutterInterface
	// ======================================================================
	virtual int32 GetShutterFloor_Implementation() const override;
	virtual void SetShutterClosed_Implementation(bool bClose, float DurationSeconds) override;
	virtual bool IsShutterClosed_Implementation() const override;

	/** Implemented for completeness: a shutter can be breached with a charge. */
	virtual void ApplyBreachDamage(float Damage, AActor* Breacher, const FVector& Origin);

	// ======================================================================
	//  Events
	// ======================================================================
	UPROPERTY(BlueprintAssignable, Category = "One Braham|Shutter")
	FOneBrahamShutterMoved OnShutterMoved;

protected:
	// --- components -------------------------------------------------------
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Components")
	TObjectPtr<USceneComponent> Root = nullptr;

	/** The moving curtain. Also the thing you see and hear. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Components")
	TObjectPtr<UStaticMeshComponent> ShutterMesh = nullptr;

	/** The static frame and runners. Never moves. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Components")
	TObjectPtr<UStaticMeshComponent> FrameMesh = nullptr;

	/** The blocking volume. Sized to the door opening, disabled while open. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Components")
	TObjectPtr<UBoxComponent> SealVolume = nullptr;

	/** Warning strobe above the opening. Only lit while the shutter is armed. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Components")
	TObjectPtr<UPointLightComponent> WarningLight = nullptr;

	// --- replicated state -------------------------------------------------
	UPROPERTY(ReplicatedUsing = OnRep_Closed, BlueprintReadOnly, Category = "One Braham|Shutter")
	bool bClosed = false;

	/** Server-only: true while waiting for the doorway to clear. */
	bool bWaitingForClear = false;

	/** Server-only: how long the shutter has been waiting. */
	float SafetyWaitElapsed = 0.f;

	/** Server-only: pending close duration, applied when the doorway clears. */
	float PendingDuration = 0.f;

	/** Local: current and target slide position, 0 = open, 1 = closed. */
	float MoveAlpha = 0.f;
	float TargetAlpha = 0.f;

	/** Local: seconds for the current move, so open and close can differ. */
	float CurrentMoveSeconds = 3.f;

	/** Local: the mesh's relative location when the shutter is fully down. */
	FVector ClosedRelativeLocation = FVector::ZeroVector;

	/** Local: the world location the audio and VFX are anchored to. */
	FVector GetMidOpeningLocation() const;

	/** Server: true when a living pawn is standing in the doorway. */
	bool IsDoorwayBlocked() const;

	/** Local: applies MoveAlpha to the mesh and the collision state. */
	void ApplyMoveAlpha(float Alpha, bool bForce = false);

	/** Local: strobe + chime, then the move. */
	void PlayWarning();

	/** Local: the thump, the dust, and the floor announcement. */
	void PlayMovementEffects(bool bNowClosed);

	UFUNCTION()
	void OnRep_Closed();

	/** Server: retry timer while the doorway is blocked. */
	void TickSafetyWait(float DeltaSeconds);

	/** How long the shutter has been in the current state, for the strobe. */
	float StateElapsed = 0.f;
};
