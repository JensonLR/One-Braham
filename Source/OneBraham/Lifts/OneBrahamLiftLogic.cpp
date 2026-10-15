// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamLiftLogic.cpp

#include "Lifts/OneBrahamLiftLogic.h"

namespace
{
	/** Seconds of dwell an in-car choice guarantees, so a rider can tap again. */
	constexpr float OBLiftSelectionGraceSeconds = 1.5f;

	/** Below this a speed is "stopped". cm/s. */
	constexpr float OBLiftStoppedSpeed = 0.5f;

	/** Loop guard for Update: a long hitch can chain several phases, never more than this. */
	constexpr int32 OBLiftMaxTransitionsPerUpdate = 8;

	/**
	 * The rest-to-rest (or "already moving the right way") trapezoid, measured as
	 * distance along the direction of travel. Returns the duration; samples the
	 * distance covered and speed at Time when asked.
	 *
	 * The peak speed is the cruise speed when the trip is long enough to reach it,
	 * and otherwise the speed at which the acceleration and braking ramps meet:
	 * a one-floor hop never reaches 250 cm/s and should not pretend to.
	 */
	float OBLiftBaseProfile(float StartSpeed, float Distance, float MaxSpeed, float Accel, float Time,
		float* OutDistance, float* OutSpeed)
	{
		const float A = FMath::Max(Accel, 1.f);
		const float VMax = FMath::Max(MaxSpeed, 1.f);
		const float U0 = FMath::Clamp(StartSpeed, 0.f, VMax);
		const float D = FMath::Max(Distance, 0.f);

		const float Peak = FMath::Clamp(FMath::Sqrt((2.f * A * D + U0 * U0) * 0.5f), U0, VMax);

		const float T1 = (Peak - U0) / A;
		const float D1 = (Peak * Peak - U0 * U0) / (2.f * A);
		const float T3 = Peak / A;
		const float D3 = (Peak * Peak) / (2.f * A);
		const float D2 = FMath::Max(0.f, D - D1 - D3);
		const float T2 = Peak > KINDA_SMALL_NUMBER ? D2 / Peak : 0.f;
		const float Total = T1 + T2 + T3;

		if (OutDistance != nullptr || OutSpeed != nullptr)
		{
			float Covered = 0.f;
			float Speed = 0.f;

			if (Time <= 0.f)
			{
				Covered = 0.f;
				Speed = U0;
			}
			else if (Time < T1)
			{
				Covered = U0 * Time + 0.5f * A * Time * Time;
				Speed = U0 + A * Time;
			}
			else if (Time < T1 + T2)
			{
				Covered = D1 + Peak * (Time - T1);
				Speed = Peak;
			}
			else if (Time < Total)
			{
				const float Tau = Time - T1 - T2;
				Covered = D1 + D2 + Peak * Tau - 0.5f * A * Tau * Tau;
				Speed = Peak - A * Tau;
			}
			else
			{
				Covered = D;
				Speed = 0.f;
			}

			// The ramps are exact, but float rounding can leave the last sample a
			// hair past the landing. A car that rests 0.01 cm below the slab is a
			// rider who "falls" a frame and plays a landing sound.
			if (OutDistance != nullptr) { *OutDistance = FMath::Clamp(Covered, 0.f, D); }
			if (OutSpeed != nullptr) { *OutSpeed = FMath::Max(Speed, 0.f); }
		}

		return Total;
	}

	/**
	 * A trip, decomposed: an optional braking segment (when the car is moving the
	 * wrong way, or too fast to stop in time), then a trapezoid from wherever that
	 * leaves it. Both halves are closed form, so the whole trip is.
	 */
	struct FOBLiftPlan
	{
		float BrakeSeconds = 0.f;
		float StartZ = 0.f;
		float StartVelocity = 0.f;
		float BaseStartZ = 0.f;
		float BaseSign = 1.f;
		float BaseStartSpeed = 0.f;
		float BaseDistance = 0.f;
	};

	FOBLiftPlan OBLiftMakePlan(float StartZ, float StartVelocity, float TargetZ, float Accel)
	{
		const float A = FMath::Max(Accel, 1.f);

		FOBLiftPlan Plan;
		Plan.StartZ = StartZ;
		Plan.StartVelocity = StartVelocity;

		float Delta = TargetZ - StartZ;
		float Sign = Delta >= 0.f ? 1.f : -1.f;
		float AlongSpeed = StartVelocity * Sign;

		const bool bMoving = FMath::Abs(StartVelocity) > OBLiftStoppedSpeed;
		const bool bWrongWay = AlongSpeed < -OBLiftStoppedSpeed;
		const bool bOvershoots = FOneBrahamLiftMotion::BrakingDistance(AlongSpeed, A) > FMath::Abs(Delta) + 0.5f;

		if (bMoving && (bWrongWay || bOvershoots))
		{
			// Stop first, then plan from rest. Average speed while braking is half
			// the start speed, so the stop point is a one-liner.
			Plan.BrakeSeconds = FMath::Abs(StartVelocity) / A;
			Plan.BaseStartZ = StartZ + StartVelocity * Plan.BrakeSeconds * 0.5f;

			Delta = TargetZ - Plan.BaseStartZ;
			Sign = Delta >= 0.f ? 1.f : -1.f;
			AlongSpeed = 0.f;
		}
		else
		{
			Plan.BaseStartZ = StartZ;
		}

		Plan.BaseSign = Sign;
		Plan.BaseStartSpeed = FMath::Max(AlongSpeed, 0.f);
		Plan.BaseDistance = FMath::Abs(Delta);
		return Plan;
	}
}

// ===========================================================================
//  Config
// ===========================================================================

int32 FOneBrahamLiftConfig::ZToNearestFloor(float Z) const
{
	const float Height = FMath::Max(FloorHeightCm, 1.f);
	const int32 Floor = FMath::RoundToInt((Z - FloorZ0) / Height);
	return FMath::Clamp(Floor, MinFloor, FMath::Max(MinFloor, MaxFloor));
}

// ===========================================================================
//  Motion
// ===========================================================================

float FOneBrahamLiftMotion::TravelSeconds(float DistanceCm, float MaxSpeed, float Accel)
{
	return OBLiftBaseProfile(0.f, FMath::Abs(DistanceCm), MaxSpeed, Accel, 0.f, nullptr, nullptr);
}

float FOneBrahamLiftMotion::Duration(float StartZ, float StartVelocity, float TargetZ, float MaxSpeed, float Accel)
{
	const FOBLiftPlan Plan = OBLiftMakePlan(StartZ, StartVelocity, TargetZ, Accel);
	return Plan.BrakeSeconds
		+ OBLiftBaseProfile(Plan.BaseStartSpeed, Plan.BaseDistance, MaxSpeed, Accel, 0.f, nullptr, nullptr);
}

void FOneBrahamLiftMotion::Sample(float StartZ, float StartVelocity, float TargetZ, float MaxSpeed, float Accel,
	float Time, float& OutZ, float& OutVelocity)
{
	const float A = FMath::Max(Accel, 1.f);
	const FOBLiftPlan Plan = OBLiftMakePlan(StartZ, StartVelocity, TargetZ, A);

	if (Time < Plan.BrakeSeconds)
	{
		const float T = FMath::Max(Time, 0.f);
		const float BrakeSign = StartVelocity >= 0.f ? 1.f : -1.f;
		OutZ = StartZ + StartVelocity * T - BrakeSign * 0.5f * A * T * T;
		OutVelocity = StartVelocity - BrakeSign * A * T;
		return;
	}

	float Covered = 0.f;
	float Speed = 0.f;
	OBLiftBaseProfile(Plan.BaseStartSpeed, Plan.BaseDistance, MaxSpeed, A, Time - Plan.BrakeSeconds, &Covered, &Speed);

	OutZ = Plan.BaseStartZ + Plan.BaseSign * Covered;
	OutVelocity = Plan.BaseSign * Speed;
}

// ===========================================================================
//  Presentation (every machine)
// ===========================================================================

float FOneBrahamLiftLogic::GetMoveDuration(const FOneBrahamLiftState& InState, const FOneBrahamLiftConfig& InConfig)
{
	if (InState.Phase != EOneBrahamLiftPhase::Moving)
	{
		return 0.f;
	}

	return FOneBrahamLiftMotion::Duration(InState.MoveStartZ, InState.MoveStartVelocity,
		InConfig.FloorToZ(InState.TargetFloor), InConfig.SpeedCmPerSec, InConfig.AccelCmPerSec2);
}

float FOneBrahamLiftLogic::GetCarZ(const FOneBrahamLiftState& InState, const FOneBrahamLiftConfig& InConfig, double Now)
{
	if (InState.Phase != EOneBrahamLiftPhase::Moving)
	{
		return InConfig.FloorToZ(InState.CurrentFloor);
	}

	float Z = 0.f;
	float Velocity = 0.f;
	FOneBrahamLiftMotion::Sample(InState.MoveStartZ, InState.MoveStartVelocity, InConfig.FloorToZ(InState.TargetFloor),
		InConfig.SpeedCmPerSec, InConfig.AccelCmPerSec2, static_cast<float>(Now - InState.PhaseStartTime), Z, Velocity);
	return Z;
}

float FOneBrahamLiftLogic::GetCarVelocity(const FOneBrahamLiftState& InState, const FOneBrahamLiftConfig& InConfig, double Now)
{
	if (InState.Phase != EOneBrahamLiftPhase::Moving)
	{
		return 0.f;
	}

	float Z = 0.f;
	float Velocity = 0.f;
	FOneBrahamLiftMotion::Sample(InState.MoveStartZ, InState.MoveStartVelocity, InConfig.FloorToZ(InState.TargetFloor),
		InConfig.SpeedCmPerSec, InConfig.AccelCmPerSec2, static_cast<float>(Now - InState.PhaseStartTime), Z, Velocity);
	return Velocity;
}

float FOneBrahamLiftLogic::GetDoorOpenAlpha(const FOneBrahamLiftState& InState, const FOneBrahamLiftConfig& InConfig, double Now)
{
	const float MoveSeconds = FMath::Max(InConfig.DoorMoveSeconds, 0.01f);
	const float Elapsed = static_cast<float>(Now - InState.PhaseStartTime);

	switch (InState.Phase)
	{
	case EOneBrahamLiftPhase::DoorsOpening:
		return FMath::Clamp(Elapsed / MoveSeconds, 0.f, 1.f);
	case EOneBrahamLiftPhase::DoorsOpen:
		return 1.f;
	case EOneBrahamLiftPhase::DoorsClosing:
		return 1.f - FMath::Clamp(Elapsed / MoveSeconds, 0.f, 1.f);
	case EOneBrahamLiftPhase::Idle:
	case EOneBrahamLiftPhase::Moving:
	default:
		return 0.f;
	}
}

// ===========================================================================
//  Server decisions
// ===========================================================================

void FOneBrahamLiftLogic::Reset(int32 Floor, double Now)
{
	State = FOneBrahamLiftState();
	State.CurrentFloor = FMath::Clamp(Floor, Config.MinFloor, FMath::Max(Config.MinFloor, Config.MaxFloor));
	State.TargetFloor = State.CurrentFloor;
	State.MoveStartZ = Config.FloorToZ(State.CurrentFloor);
	State.PhaseStartTime = Now;
	Direction = 0;
}

bool FOneBrahamLiftLogic::IsRequestAllowed(int32 Floor) const
{
	// Bit 31 is the sign bit of the call mask; a 32-storey building is not this one.
	return !State.bLockedDown && Config.IsServedFloor(Floor) && Floor >= 0 && Floor < 31;
}

void FOneBrahamLiftLogic::EnterPhase(EOneBrahamLiftPhase NewPhase, double StartTime)
{
	State.Phase = NewPhase;
	State.PhaseStartTime = StartTime;
}

void FOneBrahamLiftLogic::ClearCall(int32 Floor)
{
	if (Floor >= 0 && Floor < 31)
	{
		State.CallMask &= ~(1 << Floor);
	}

	if (State.CarDestination == Floor)
	{
		State.CarDestination = INDEX_NONE;
	}
}

void FOneBrahamLiftLogic::BeginMove(int32 Floor, float StartZ, float StartVelocity, double StartTime)
{
	State.TargetFloor = Floor;
	State.MoveStartZ = StartZ;
	State.MoveStartVelocity = StartVelocity;

	const float Delta = Config.FloorToZ(Floor) - StartZ;
	if (!FMath::IsNearlyZero(Delta))
	{
		Direction = Delta > 0.f ? 1 : -1;
	}

	EnterPhase(EOneBrahamLiftPhase::Moving, StartTime);
}

void FOneBrahamLiftLogic::ReopenFromClosing(double Now)
{
	// Start the opening phase in the past by exactly the fraction the doors are
	// still open, so the alpha is continuous: the doors stop and reverse, they do
	// not jump back to fully shut and start again.
	const float MoveSeconds = FMath::Max(Config.DoorMoveSeconds, 0.01f);
	const float Closed = FMath::Clamp(static_cast<float>(Now - State.PhaseStartTime), 0.f, MoveSeconds);
	const float OpenFraction = 1.f - Closed / MoveSeconds;
	EnterPhase(EOneBrahamLiftPhase::DoorsOpening, Now - OpenFraction * MoveSeconds);
}

bool FOneBrahamLiftLogic::HoldDoors(double Now)
{
	switch (State.Phase)
	{
	case EOneBrahamLiftPhase::Idle:
		EnterPhase(EOneBrahamLiftPhase::DoorsOpening, Now);
		return true;
	case EOneBrahamLiftPhase::DoorsOpening:
		return true;
	case EOneBrahamLiftPhase::DoorsOpen:
		// A fresh dwell: somebody is walking in.
		State.PhaseStartTime = FMath::Max(State.PhaseStartTime, Now);
		return true;
	case EOneBrahamLiftPhase::DoorsClosing:
		ReopenFromClosing(Now);
		return true;
	case EOneBrahamLiftPhase::Moving:
	default:
		return false;
	}
}

bool FOneBrahamLiftLogic::RequestLanding(int32 Floor, double Now)
{
	if (!IsRequestAllowed(Floor))
	{
		return false;
	}

	if (State.Phase != EOneBrahamLiftPhase::Moving && Floor == State.CurrentFloor)
	{
		// The car is here. Pressing the button is "hold the doors", exactly as it
		// is in every lift lobby in the world.
		return HoldDoors(Now);
	}

	State.CallMask |= (1 << Floor);
	return true;
}

bool FOneBrahamLiftLogic::SelectCarDestination(int32 Floor, double Now)
{
	if (!IsRequestAllowed(Floor))
	{
		return false;
	}

	if (State.Phase != EOneBrahamLiftPhase::Moving && Floor == State.CurrentFloor)
	{
		State.CarDestination = INDEX_NONE;
		return HoldDoors(Now);
	}

	State.CarDestination = Floor;

	// Choosing a floor with the doors open buys the rider a little more dwell, so
	// cycling up five floors does not get cut off half way by the doors closing
	// and the car leaving for floor three.
	if (State.Phase == EOneBrahamLiftPhase::DoorsOpen)
	{
		const double Latest = Now - FMath::Max(0.f, Config.DoorDwellSeconds - OBLiftSelectionGraceSeconds);
		State.PhaseStartTime = FMath::Max(State.PhaseStartTime, Latest);
	}

	return true;
}

int32 FOneBrahamLiftLogic::GetNextCarSelection(int32 InDirection) const
{
	if (State.bLockedDown || InDirection == 0)
	{
		return INDEX_NONE;
	}

	// Cycle from what is already chosen, so tapping Up three times means "three
	// floors up", not "one floor up, three times".
	const int32 Base = State.CarDestination != INDEX_NONE
		? State.CarDestination
		: (State.Phase == EOneBrahamLiftPhase::Moving ? State.TargetFloor : State.CurrentFloor);

	const int32 Next = Base + (InDirection > 0 ? 1 : -1);
	return IsRequestAllowed(Next) ? Next : INDEX_NONE;
}

int32 FOneBrahamLiftLogic::ChooseNextTarget() const
{
	const int32 Here = State.Phase == EOneBrahamLiftPhase::Moving ? State.TargetFloor : State.CurrentFloor;

	int32 Above = INDEX_NONE;
	int32 Below = INDEX_NONE;

	auto Consider = [&](int32 Floor)
	{
		if (Floor == Here || !Config.IsServedFloor(Floor))
		{
			return;
		}

		if (Floor > Here)
		{
			Above = (Above == INDEX_NONE) ? Floor : FMath::Min(Above, Floor);
		}
		else
		{
			Below = (Below == INDEX_NONE) ? Floor : FMath::Max(Below, Floor);
		}
	};

	for (int32 Floor = 0; Floor < 31; ++Floor)
	{
		if (State.HasCall(Floor))
		{
			Consider(Floor);
		}
	}

	if (State.CarDestination != INDEX_NONE)
	{
		Consider(State.CarDestination);
	}

	// The collective rule: keep going the way you were going while anybody wants
	// that way. It is what stops a busy car ping-ponging between two floors while
	// the person on floor twelve waits for ever.
	if (Direction > 0)
	{
		return Above != INDEX_NONE ? Above : Below;
	}

	if (Direction < 0)
	{
		return Below != INDEX_NONE ? Below : Above;
	}

	if (Above == INDEX_NONE) { return Below; }
	if (Below == INDEX_NONE) { return Above; }
	return (Above - Here) <= (Here - Below) ? Above : Below;
}

int32 FOneBrahamLiftLogic::ChooseLockdownStopFloor(float Z, float Velocity) const
{
	const float Braking = FOneBrahamLiftMotion::BrakingDistance(FMath::Abs(Velocity), Config.AccelCmPerSec2);
	const int32 TravelSign = Velocity > OBLiftStoppedSpeed ? 1 : (Velocity < -OBLiftStoppedSpeed ? -1 : 0);

	int32 Best = INDEX_NONE;
	float BestDistance = TNumericLimits<float>::Max();

	// First choice: the nearest floor ahead that the car can stop at without an
	// emergency stop. Never the excluded floor - the lockdown closes the Skydeck
	// to the lifts, and a car that happened to be heading there must not finish
	// the trip.
	for (int32 Floor = Config.MinFloor; Floor <= Config.MaxFloor; ++Floor)
	{
		if (Floor == Config.LockdownExcludedFloor)
		{
			continue;
		}

		const float Dz = Config.FloorToZ(Floor) - Z;
		float Distance = FMath::Abs(Dz);

		if (TravelSign != 0)
		{
			const float Ahead = Dz * static_cast<float>(TravelSign);
			if (Ahead < Braking - 1.f)
			{
				continue;
			}

			Distance = Ahead;
		}

		if (Distance < BestDistance)
		{
			BestDistance = Distance;
			Best = Floor;
		}
	}

	// Nothing reachable ahead (the car was a floor below the Skydeck and climbing):
	// brake, reverse, and settle on the nearest allowed floor behind.
	if (Best == INDEX_NONE)
	{
		for (int32 Floor = Config.MinFloor; Floor <= Config.MaxFloor; ++Floor)
		{
			if (Floor == Config.LockdownExcludedFloor)
			{
				continue;
			}

			const float Distance = FMath::Abs(Config.FloorToZ(Floor) - Z);
			if (Distance < BestDistance)
			{
				BestDistance = Distance;
				Best = Floor;
			}
		}
	}

	return Best != INDEX_NONE ? Best : Config.ZToNearestFloor(Z);
}

void FOneBrahamLiftLogic::SetLockedDown(bool bLocked, double Now)
{
	if (State.bLockedDown == bLocked)
	{
		return;
	}

	State.bLockedDown = bLocked;

	if (!bLocked)
	{
		// Back in service: a fresh dwell, then the doors close and the car waits
		// for its first call like any other morning.
		if (State.Phase == EOneBrahamLiftPhase::DoorsOpen)
		{
			State.PhaseStartTime = Now;
		}
		return;
	}

	// Every waiting call is void. Nobody is coming for you.
	State.CallMask = 0;
	State.CarDestination = INDEX_NONE;

	switch (State.Phase)
	{
	case EOneBrahamLiftPhase::Moving:
	{
		const float Z = GetCarZ(State, Config, Now);
		const float Velocity = GetCarVelocity(State, Config, Now);
		const int32 Stop = ChooseLockdownStopFloor(Z, Velocity);

		if (Stop != State.TargetFloor)
		{
			BeginMove(Stop, Z, Velocity, Now);
		}
		break;
	}

	case EOneBrahamLiftPhase::Idle:
		EnterPhase(EOneBrahamLiftPhase::DoorsOpening, Now);
		break;

	case EOneBrahamLiftPhase::DoorsClosing:
		ReopenFromClosing(Now);
		break;

	case EOneBrahamLiftPhase::DoorsOpening:
	case EOneBrahamLiftPhase::DoorsOpen:
	default:
		break;
	}
}

bool FOneBrahamLiftLogic::Update(double Now)
{
	bool bChanged = false;

	// True once this Update has itself moved the car into Idle, so a car that
	// shut its doors at t = 10.0 and has a call waiting leaves at 10.0, not at
	// whenever the next frame happened to land.
	bool bChainedIntoIdle = false;

	for (int32 Guard = 0; Guard < OBLiftMaxTransitionsPerUpdate; ++Guard)
	{
		bool bStep = false;

		switch (State.Phase)
		{
		case EOneBrahamLiftPhase::Idle:
		{
			const double When = bChainedIntoIdle ? State.PhaseStartTime : Now;

			if (State.bLockedDown || State.HasCall(State.CurrentFloor) || State.CarDestination == State.CurrentFloor)
			{
				ClearCall(State.CurrentFloor);
				EnterPhase(EOneBrahamLiftPhase::DoorsOpening, When);
				bStep = true;
				break;
			}

			const int32 Next = ChooseNextTarget();
			if (Next != INDEX_NONE)
			{
				BeginMove(Next, Config.FloorToZ(State.CurrentFloor), 0.f, When);
				bStep = true;
			}
			else
			{
				Direction = 0;
			}
			break;
		}

		case EOneBrahamLiftPhase::Moving:
		{
			const double Arrive = State.PhaseStartTime + GetMoveDuration(State, Config);
			if (Now >= Arrive)
			{
				State.CurrentFloor = State.TargetFloor;
				State.MoveStartZ = Config.FloorToZ(State.CurrentFloor);
				State.MoveStartVelocity = 0.f;
				ClearCall(State.CurrentFloor);
				EnterPhase(EOneBrahamLiftPhase::DoorsOpening, Arrive);
				bStep = true;
			}
			break;
		}

		case EOneBrahamLiftPhase::DoorsOpening:
		{
			const double Opened = State.PhaseStartTime + Config.DoorMoveSeconds;
			if (Now >= Opened)
			{
				EnterPhase(EOneBrahamLiftPhase::DoorsOpen, Opened);
				bStep = true;
			}
			break;
		}

		case EOneBrahamLiftPhase::DoorsOpen:
		{
			// Locked down: the doors stay open. The car is out of service, and a
			// car with its doors shut is a box somebody can be trapped in.
			if (State.bLockedDown)
			{
				break;
			}

			const double Closing = State.PhaseStartTime + Config.DoorDwellSeconds;
			if (Now >= Closing)
			{
				EnterPhase(EOneBrahamLiftPhase::DoorsClosing, Closing);
				bStep = true;
			}
			break;
		}

		case EOneBrahamLiftPhase::DoorsClosing:
		{
			if (State.bLockedDown)
			{
				ReopenFromClosing(Now);
				bStep = true;
				break;
			}

			const double Shut = State.PhaseStartTime + Config.DoorMoveSeconds;
			if (Now >= Shut)
			{
				EnterPhase(EOneBrahamLiftPhase::Idle, Shut);
				bChainedIntoIdle = true;
				bStep = true;
			}
			break;
		}

		default:
			break;
		}

		if (!bStep)
		{
			break;
		}

		bChanged = true;
	}

	return bChanged;
}
