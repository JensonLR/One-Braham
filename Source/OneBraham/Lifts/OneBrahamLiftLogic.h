// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamLiftLogic.h
// ---------------------------------------------------------------------------
// Everything a lift *decides*, with no world in sight.
//
// A lift is three questions asked sixty times a second: where is the car, are
// the doors open, and where does it go next. None of them needs an actor, a
// component or a network connection to answer, and every one of them is the
// kind of rule that breaks quietly - a car that skips a call, a door that never
// closes, a lockdown that drops a rider on the Skydeck. So the answers live
// here, in plain C++, where OneBraham.Lifts.* can drive them with a fake clock
// and assert on every transition.
//
// HOW THE NETWORK SEES IT
//   The server owns an FOneBrahamLiftLogic and replicates its FOneBrahamLiftState.
//   That state never says "the car is at Z"; it says "the car left this height,
//   at this speed, for this floor, at this server time". Every machine then
//   computes the same position from the same numbers with GetCarZ(), so a rider
//   on a client sees a car that glides rather than one that snaps between
//   packets, and the server never has to send a position at all.
//
// THE MOTION
//   A trapezoid velocity profile: constant acceleration up to the cruise speed,
//   cruise, constant deceleration to rest. That is the ease in and out a real
//   traction lift has, it has a closed form (so a client can sample it at any
//   time without integrating), and it handles the one awkward case a lockdown
//   creates - a car already moving that has to stop somewhere else - by braking
//   first and planning from rest.

#pragma once

#include "CoreMinimal.h"
#include "OneBrahamLiftLogic.generated.h"

/** What the car is doing. Replicated, so every machine animates the same doors. */
UENUM(BlueprintType)
enum class EOneBrahamLiftPhase : uint8
{
	/** Parked at CurrentFloor, doors shut. */
	Idle = 0,
	/** Travelling to TargetFloor. */
	Moving = 1,
	/** Doors sliding open at CurrentFloor. */
	DoorsOpening = 2,
	/** Doors open at CurrentFloor, counting down the dwell. */
	DoorsOpen = 3,
	/** Doors sliding shut at CurrentFloor. */
	DoorsClosing = 4
};

/**
 * The replicated truth of one lift. Small on purpose: a phase, two floors, a
 * departure height and speed, a timestamp and the call buttons. Everything the
 * clients draw is derived from these.
 */
USTRUCT(BlueprintType)
struct ONEBRAHAM_API FOneBrahamLiftState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "One Braham|Lift")
	EOneBrahamLiftPhase Phase = EOneBrahamLiftPhase::Idle;

	/** The floor the car is at, or last left. */
	UPROPERTY(BlueprintReadOnly, Category = "One Braham|Lift")
	int32 CurrentFloor = 0;

	/** The floor the current trip ends at. Meaningful only while Moving. */
	UPROPERTY(BlueprintReadOnly, Category = "One Braham|Lift")
	int32 TargetFloor = 0;

	/** World Z of the car floor when the current trip began. */
	UPROPERTY(BlueprintReadOnly, Category = "One Braham|Lift")
	float MoveStartZ = 0.f;

	/** Signed vertical speed when the current trip began (non-zero only for a re-planned trip). */
	UPROPERTY(BlueprintReadOnly, Category = "One Braham|Lift")
	float MoveStartVelocity = 0.f;

	/** Server time the current phase began. Every timer is measured from this. */
	UPROPERTY(BlueprintReadOnly, Category = "One Braham|Lift")
	double PhaseStartTime = 0.0;

	/** One bit per floor with a landing call waiting. Bit n is floor n. */
	UPROPERTY(BlueprintReadOnly, Category = "One Braham|Lift")
	int32 CallMask = 0;

	/** The floor chosen on the in-car panel, INDEX_NONE when nobody has chosen. */
	UPROPERTY(BlueprintReadOnly, Category = "One Braham|Lift")
	int32 CarDestination = INDEX_NONE;

	/** True while the building's lockdown has stopped this lift. */
	UPROPERTY(BlueprintReadOnly, Category = "One Braham|Lift")
	bool bLockedDown = false;

	bool HasCall(int32 Floor) const
	{
		return Floor >= 0 && Floor < 31 && (CallMask & (1 << Floor)) != 0;
	}
};

/** The numbers a lift is built from. Plain data, copied from the actor's properties. */
struct ONEBRAHAM_API FOneBrahamLiftConfig
{
	int32 MinFloor = 0;
	int32 MaxFloor = ONE_BRAHAM_FLOORS;

	/** World Z of floor 0's finished floor level (the plaza, -400). */
	float FloorZ0 = -400.f;

	/** Floor to floor. 400 cm, the same number the map builder lays the slabs on. */
	float FloorHeightCm = 400.f;

	float SpeedCmPerSec = 250.f;
	float AccelCmPerSec2 = 160.f;

	/** Seconds for the doors to slide fully open or fully shut. */
	float DoorMoveSeconds = 0.9f;

	/** Seconds the doors stay fully open at a stop. */
	float DoorDwellSeconds = 2.5f;

	/** The lowest floor a lockdown never delivers a rider to, and every floor
	 *  above it (the Skydeck and the crown). INDEX_NONE = none. */
	int32 LockdownExcludedFloor = 15;

	float FloorToZ(int32 Floor) const { return FloorZ0 + static_cast<float>(Floor) * FloorHeightCm; }

	/** Nearest served floor to a world Z. */
	int32 ZToNearestFloor(float Z) const;

	bool IsServedFloor(int32 Floor) const { return Floor >= MinFloor && Floor <= MaxFloor; }
};

/** The closed-form motion profile. Static, so tests and clients sample it directly. */
struct ONEBRAHAM_API FOneBrahamLiftMotion
{
	/** Seconds to travel a distance from rest to rest. */
	static float TravelSeconds(float DistanceCm, float MaxSpeed, float Accel);

	/** Seconds for a trip from StartZ, moving at StartVelocity, to rest at TargetZ. */
	static float Duration(float StartZ, float StartVelocity, float TargetZ, float MaxSpeed, float Accel);

	/** Position and signed velocity Time seconds into that trip. Clamped at the ends. */
	static void Sample(float StartZ, float StartVelocity, float TargetZ, float MaxSpeed, float Accel,
		float Time, float& OutZ, float& OutVelocity);

	/** Distance needed to stop from a speed. */
	static float BrakingDistance(float Speed, float Accel)
	{
		return Accel > KINDA_SMALL_NUMBER ? (Speed * Speed) / (2.f * Accel) : 0.f;
	}
};

/**
 * The lift's brain: call queue, door timers, lockdown. The server owns one; a
 * client keeps one only to answer prompt questions from the replicated state.
 */
class ONEBRAHAM_API FOneBrahamLiftLogic
{
public:
	FOneBrahamLiftConfig Config;

	/** Parks the car at a floor with the doors shut. */
	void Reset(int32 Floor, double Now);

	/** Clients: adopt the server's state wholesale. */
	void ApplyReplicatedState(const FOneBrahamLiftState& InState) { State = InState; }

	/**
	 * A landing call. True when accepted (including "already here, doors held").
	 * False when the floor is not served or the lift is locked down.
	 */
	bool RequestLanding(int32 Floor, double Now);

	/** An in-car choice. Replaces the previous in-car choice rather than adding to it. */
	bool SelectCarDestination(int32 Floor, double Now);

	/** The floor the in-car Up (+1) or Down (-1) button would choose next, or INDEX_NONE. */
	int32 GetNextCarSelection(int32 Direction) const;

	/** Lockdown on: drop every call, stop at the nearest allowed floor, open, stay open. */
	void SetLockedDown(bool bLocked, double Now);

	/** Advances the timers. True when the state changed and needs replicating. */
	bool Update(double Now);

	/** Server: holds or reopens the doors at the current floor (the safety edge). */
	bool HoldDoors(double Now);

	/** The floor the queue would serve next, INDEX_NONE when there is nothing to do. */
	int32 ChooseNextTarget() const;

	const FOneBrahamLiftState& GetState() const { return State; }
	bool IsLockedDown() const { return State.bLockedDown; }
	bool HasPendingWork() const { return State.CallMask != 0 || State.CarDestination != INDEX_NONE; }

	/** True when a call to this floor would be accepted right now. */
	bool IsRequestAllowed(int32 Floor) const;

	/** Seconds the current trip takes, 0 when not moving. */
	static float GetMoveDuration(const FOneBrahamLiftState& InState, const FOneBrahamLiftConfig& InConfig);

	/** World Z of the car floor at a time. Every machine calls this with its own clock. */
	static float GetCarZ(const FOneBrahamLiftState& InState, const FOneBrahamLiftConfig& InConfig, double Now);

	/** Signed vertical velocity of the car at a time. */
	static float GetCarVelocity(const FOneBrahamLiftState& InState, const FOneBrahamLiftConfig& InConfig, double Now);

	/** 0 = doors shut, 1 = fully open. */
	static float GetDoorOpenAlpha(const FOneBrahamLiftState& InState, const FOneBrahamLiftConfig& InConfig, double Now);

private:
	void BeginMove(int32 Floor, float StartZ, float StartVelocity, double StartTime);
	void EnterPhase(EOneBrahamLiftPhase NewPhase, double StartTime);
	void ClearCall(int32 Floor);

	/** Doors that are closing reverse from wherever they are, with no jump. */
	void ReopenFromClosing(double Now);

	/** The floor a lockdown stops a moving car at. */
	int32 ChooseLockdownStopFloor(float Z, float Velocity) const;

	FOneBrahamLiftState State;

	/** +1 up, -1 down, 0 idle. The collective rule keeps a car going the way it was going. */
	int32 Direction = 0;
};
