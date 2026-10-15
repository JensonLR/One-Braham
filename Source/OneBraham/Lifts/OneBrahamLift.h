// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamLift.h
// ---------------------------------------------------------------------------
// One lift: a car, its doors, a set of landing doors up the shaft, and the
// call panels that make it go.
//
// The building has nine of these: two banks of four facing each other across
// the core hall, and the glass car at the back of the hall. They are the fast
// way up, and the loud one - a car arriving is a chime and a pair of doors
// opening on whoever is waiting - so holding a lift lobby is a real position
// and riding a lift is a real risk.
//
// WHAT THIS ACTOR OWNS
//   - the car geometry (floor, walls, ceiling, sliding doors), built from
//     properties in OnConstruction so the map builder can place a lift with
//     nothing but a transform and a handful of set_editor_property calls
//   - the landing doors on every served floor, which keep people out of an
//     empty shaft and open only where the car is
//   - the call panels, spawned on the server as small replicated actors so the
//     interaction system's distance and facing checks measure the button the
//     player is looking at, not a point sixty metres down the shaft
//   - the movement, which is not replicated as a position at all: the server
//     replicates the trip (start height, start speed, target floor, start time)
//     and every machine computes the same smooth curve from it
//
// RIDERS
//   The car floor is a movable primitive the CharacterMovementComponent bases on,
//   so a rider moves with the car on every machine, and the movement component
//   sends its position relative to the car - which is what keeps a client rider
//   from being corrected every frame of a fifteen-floor ride.
//
// LOCKDOWN
//   At the Lockdown stage the lift finishes its trip at the nearest floor it can
//   stop at (never the Skydeck), opens its doors and refuses every call until
//   the stage drops back. The lockdown component is queried, not modified.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Lifts/OneBrahamLiftLogic.h"
#include "OneBrahamLift.generated.h"

class UBoxComponent;
class UInstancedStaticMeshComponent;
class UMaterialInterface;
class UPointLightComponent;
class UStaticMesh;
class UStaticMeshComponent;
class UTextRenderComponent;
class AOneBrahamLiftPanel;
class AOneBrahamPlayerController;

/** Which way the doors face, in the actor's own space. */
UENUM(BlueprintType)
enum class EOneBrahamLiftFacing : uint8
{
	/** Doors on the +X side (the west bank, opening east onto the hall). */
	PositiveX = 0,
	/** Doors on the -X side (the east bank). */
	NegativeX = 1,
	/** Doors on the -Y side (the glass car, opening south onto the hall). */
	NegativeY = 2,
	/** Doors on the +Y side. Not used by the shipping core, here for completeness. */
	PositiveY = 3
};

/** Raised on every machine when the car arrives at a floor and its doors begin to open. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOneBrahamLiftArrived, FName, LiftId, int32, Floor);

/** Raised on every machine when the lift goes in or out of lockdown service. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOneBrahamLiftServiceChanged, FName, LiftId, bool, bLockedDown);

UCLASS(Blueprintable)
class ONEBRAHAM_API AOneBrahamLift : public AActor
{
	GENERATED_BODY()

public:
	AOneBrahamLift();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// ======================================================================
	//  Placement. Place the actor at the shaft centre, at floor 0's level,
	//  unrotated; everything below is relative to that.
	// ======================================================================
	/** Stable id, e.g. "Lift_W1" or "Lift_Glass". Logs, the QA overlay, the manifest. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Lift")
	FName LiftId = NAME_None;

	/** Lowest floor this car serves. 0 is the plaza. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Lift", meta = (ClampMin = "0", ClampMax = "30"))
	int32 MinFloor = 0;

	/** Highest floor this car serves. 15 is the Skydeck. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Lift", meta = (ClampMin = "0", ClampMax = "30"))
	int32 MaxFloor = 15;

	/** Floor the car is parked at when the match loads. Clamped into the served range. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Lift", meta = (ClampMin = "0", ClampMax = "30"))
	int32 StartFloor = 1;

	/** Glass walls on the three closed sides: the scenic car at the back of the hall. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Lift")
	bool bGlass = false;

	/** The side the doors are on, in actor space. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Lift")
	EOneBrahamLiftFacing DoorFacing = EOneBrahamLiftFacing::PositiveX;

	/** Floor to floor. The map builder's FLOOR_HEIGHT_CM. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Lift", meta = (ClampMin = "100"))
	float FloorHeightCm = 400.f;

	/** World Z of floor 0's finished floor. The map builder's _floor_z(0) = -400. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Lift")
	float FloorZ0 = -400.f;

	// ======================================================================
	//  Car and shaft dimensions (cm)
	// ======================================================================
	/** Car width across the door face. The banks' shafts are 260 wide inside. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Lift|Dimensions", meta = (ClampMin = "120"))
	float CarWidthCm = 240.f;

	/** Car depth from back wall to door face. The banks' shafts are 240 deep inside. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Lift|Dimensions", meta = (ClampMin = "120"))
	float CarDepthCm = 220.f;

	/** Inside height of the car. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Lift|Dimensions", meta = (ClampMin = "200"))
	float CarHeightCm = 250.f;

	/** Clear door opening, car and landing alike. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Lift|Dimensions", meta = (ClampMin = "60"))
	float DoorWidthCm = 110.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Lift|Dimensions", meta = (ClampMin = "180"))
	float DoorHeightCm = 215.f;

	/** Shaft centre to the hall face of the shaft wall, along DoorFacing. 125 for the banks, 150 for the glass car. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Lift|Dimensions", meta = (ClampMin = "60"))
	float LandingDoorOffsetCm = 125.f;

	/** False skips the landing doors, for a shaft whose doors are part of the level's own geometry. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Lift|Dimensions")
	bool bBuildLandingDoors = true;

	// ======================================================================
	//  Behaviour
	// ======================================================================
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Lift|Behaviour", meta = (ClampMin = "50"))
	float SpeedCmPerSec = 250.f;

	/** The ease in and out. 160 cm/s2 reaches cruise in about 1.6 s. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Lift|Behaviour", meta = (ClampMin = "20"))
	float AccelerationCmPerSec2 = 160.f;

	/** Seconds the doors stay fully open at a stop. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Lift|Behaviour", meta = (ClampMin = "0.5"))
	float DoorOpenSeconds = 2.5f;

	/** Seconds for the doors to slide open or shut. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Lift|Behaviour", meta = (ClampMin = "0.2"))
	float DoorMoveSeconds = 0.9f;

	/** True when the building's Lockdown stage takes this car out of service. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Lift|Behaviour")
	bool bStopsOnLockdown = true;

	/** The floor a lockdown never delivers a rider to. The Skydeck: the last climb is the stairs. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Lift|Behaviour")
	int32 LockdownExcludedFloor = 15;

	// ======================================================================
	//  Look. All optional: the engine cube and default material stand in.
	// ======================================================================
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Lift|Look")
	TObjectPtr<UMaterialInterface> CarMaterial = nullptr;

	/** Used on the closed sides when bGlass is set. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Lift|Look")
	TObjectPtr<UMaterialInterface> GlassMaterial = nullptr;

	/** Car doors and landing doors. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Lift|Look")
	TObjectPtr<UMaterialInterface> DoorMaterial = nullptr;

	// ======================================================================
	//  Builder API
	// ======================================================================
	/**
	 * One call for the content builder: sets the identity, range, glass and facing,
	 * then rebuilds. Python: unreal.OneBrahamLift.configure_lift(actor, ...).
	 */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Lift")
	static void ConfigureLift(AOneBrahamLift* Lift, FName InLiftId, int32 InMinFloor, int32 InMaxFloor,
		bool bInGlass, EOneBrahamLiftFacing InFacing);

	/** Rebuilds the car and landing doors from the current properties. */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "One Braham|Lift")
	void RebuildLift();

	/** World Z of a floor's finished level: FloorZ0 + Floor * FloorHeightCm. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Lift")
	float GetFloorWorldZ(int32 Floor) const { return FloorZ0 + static_cast<float>(Floor) * FloorHeightCm; }

	// ======================================================================
	//  Server API (the panels call these)
	// ======================================================================
	/** A landing call from a floor. False when refused (locked down, not served). */
	bool ServerCallToFloor(int32 Floor, AOneBrahamPlayerController* Caller);

	/** The in-car Up (+1) / Down (-1) button. False when refused. */
	bool ServerCycleCarDestination(int32 Direction, AOneBrahamPlayerController* Caller);

	// ======================================================================
	//  Queries (every machine)
	// ======================================================================
	UFUNCTION(BlueprintPure, Category = "One Braham|Lift")
	FOneBrahamLiftState GetLiftState() const { return LiftState; }

	UFUNCTION(BlueprintPure, Category = "One Braham|Lift")
	bool IsLockedDown() const { return LiftState.bLockedDown; }

	UFUNCTION(BlueprintPure, Category = "One Braham|Lift")
	bool IsMoving() const { return LiftState.Phase == EOneBrahamLiftPhase::Moving; }

	/** The floor nearest the car right now. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Lift")
	int32 GetCarFloor() const;

	/** 0 = shut, 1 = fully open, at the car's current floor. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Lift")
	float GetDoorOpenAlpha() const;

	/** True when a landing call is waiting at this floor. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Lift")
	bool IsFloorCalled(int32 Floor) const { return LiftState.HasCall(Floor); }

	/** True when the car is stopped at this floor with its doors not shut. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Lift")
	bool IsCarOpenAtFloor(int32 Floor) const;

	/** What the in-car Up (+1) or Down (-1) button would choose, INDEX_NONE when it would do nothing. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Lift")
	int32 GetNextCarSelection(int32 Direction) const { return Logic.GetNextCarSelection(Direction); }

	/** The lift's clock: server time, smoothed on clients so the car never steps. */
	double GetLiftTime() const;

	/** The logic's config, rebuilt from the properties. Exposed for the tests and the QA overlay. */
	FOneBrahamLiftConfig MakeConfig() const;

	// ======================================================================
	//  Events
	// ======================================================================
	UPROPERTY(BlueprintAssignable, Category = "One Braham|Lift")
	FOneBrahamLiftArrived OnLiftArrived;

	UPROPERTY(BlueprintAssignable, Category = "One Braham|Lift")
	FOneBrahamLiftServiceChanged OnServiceChanged;

protected:
	// --- components (default subobjects, so they are net-addressable: the car
	//     floor is a movement base, and a base the network cannot name is a base
	//     every client rider is corrected off) ------------------------------
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Components")
	TObjectPtr<USceneComponent> Root = nullptr;

	/** Rotated to DoorFacing. Everything below is built with +X towards the doors. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Components")
	TObjectPtr<USceneComponent> FacingRoot = nullptr;

	/** The car. Its origin is the top of the car floor, at the shaft centre. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Components")
	TObjectPtr<USceneComponent> CarRoot = nullptr;

	/** The floor riders stand on, and the movement base they ride. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Components")
	TObjectPtr<UStaticMeshComponent> CarFloor = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Components")
	TObjectPtr<UStaticMeshComponent> CarCeiling = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Components")
	TObjectPtr<UStaticMeshComponent> CarWallBack = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Components")
	TObjectPtr<UStaticMeshComponent> CarWallLeft = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Components")
	TObjectPtr<UStaticMeshComponent> CarWallRight = nullptr;

	/** The door-face wall either side of the opening, and the header above it. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Components")
	TObjectPtr<UStaticMeshComponent> CarFrontLeft = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Components")
	TObjectPtr<UStaticMeshComponent> CarFrontRight = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Components")
	TObjectPtr<UStaticMeshComponent> CarFrontHeader = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Components")
	TObjectPtr<UStaticMeshComponent> CarDoorLeft = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Components")
	TObjectPtr<UStaticMeshComponent> CarDoorRight = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Components")
	TObjectPtr<UPointLightComponent> CarLight = nullptr;

	/** Floor number above the door, inside the car. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Components")
	TObjectPtr<UTextRenderComponent> CarIndicator = nullptr;

	/** Two instances per served floor (left leaf, right leaf). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "One Braham|Components")
	TObjectPtr<UInstancedStaticMeshComponent> LandingDoors = nullptr;

	// --- replicated state -------------------------------------------------
	UPROPERTY(ReplicatedUsing = OnRep_LiftState, BlueprintReadOnly, Category = "One Braham|Lift")
	FOneBrahamLiftState LiftState;

	UFUNCTION()
	void OnRep_LiftState();

	// --- runtime ----------------------------------------------------------
	/** Server: the decisions. Clients: a copy of the replicated state for prompts. */
	FOneBrahamLiftLogic Logic;

	/** Server: the panels this lift spawned. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<AOneBrahamLiftPanel>> Panels;

	/** Clients: smoothed (server - local) clock offset. */
	double SmoothedClockOffset = 0.0;
	bool bClockInitialised = false;

	/** Local: the floor whose landing doors were last animated, so they can be shut again. */
	int32 AnimatedLandingFloor = INDEX_NONE;

	/** Local: the last phase this machine saw, for the arrival event. */
	EOneBrahamLiftPhase LastSeenPhase = EOneBrahamLiftPhase::Idle;
	bool bLastSeenLockedDown = false;

	/** Server: throttles the lockdown query. */
	float LockdownPollAccumulator = 0.f;

	/** Local: what was last pushed to the components, so a parked car costs nothing. */
	float LastAppliedCarRelZ = TNumericLimits<float>::Max();
	float LastAppliedDoorAlpha = -1.f;
	float LastLandingDoorAlpha = -1.f;
	int32 LastIndicatorFloor = INDEX_NONE;
	bool bLastIndicatorLocked = false;

	// --- internals --------------------------------------------------------
	/** Builds every piece of geometry from the properties. Safe to call repeatedly. */
	void BuildLift();

	/** Server: spawns the landing and car panels. */
	void SpawnPanels();

	/** Server: asks the lockdown director whether lifts should be running. */
	void PollLockdown();

	/** Server: holds the doors while somebody is standing in them. */
	void CheckDoorwaySafety(double Now);

	/** Local: puts the car, the doors and the indicator where the state says. */
	void ApplyPresentation(double Now);

	/** Local: raises arrival and service events when the state says so. */
	void NotifyStateTransitions();

	/** Server: replicates after the logic has changed its mind. */
	void PushStateFromLogic();

	/** Local: updates the smoothed clock. */
	void UpdateClock(float DeltaSeconds);

	/** Positions a box mesh by size (cm) and centre in its parent's space. */
	void PlaceBox(UStaticMeshComponent* Mesh, const FVector& Centre, const FVector& Size) const;

	/** Yaw that turns +X into the door direction. */
	float GetFacingYaw() const;

	/** Shared layout numbers, so construction and runtime always agree. */
	float GetCarDoorX() const;
	float GetLandingDoorX() const;
	float GetSillFrontX() const;

	/** Relative transform of one landing door leaf at an open fraction. */
	FTransform GetLandingDoorTransform(int32 Floor, bool bLeft, float OpenAlpha) const;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> CubeMesh = nullptr;
};
