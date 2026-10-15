// Copyright (c) One Braham. All Rights Reserved.
//
// OneBraham.Lifts
//
// The lift's rules, driven with a fake clock and no world. A lift that skips a
// call, a door that never closes or a lockdown that finishes a trip to the
// Skydeck all look fine in a five-minute playtest and ruin a match in the
// twentieth minute, so the decisions live in FOneBrahamLiftLogic and these
// tests walk it through every transition the actor relies on.
//
//   Travel   - the floor-to-Z mapping agrees with the map builder, the motion
//              profile is the trip the docs promise, and two machines sampling
//              the same replicated trip see the same car
//   Queue    - calls are served in collective order, the doors run their
//              timers, the in-car buttons cycle, and doors reverse cleanly
//   Lockdown - a lockdown stops a moving car at the nearest floor it can stop
//              at, never the Skydeck, opens it, keeps it open and refuses calls

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Lifts/OneBrahamLiftLogic.h"
#include "OneBraham.h"

#if WITH_AUTOMATION_TESTS

namespace
{
	/** The shipping numbers: 250 cm/s, 160 cm/s2, 0.9 s doors, 2.5 s dwell, plaza at -400. */
	FOneBrahamLiftConfig OBLiftTestConfig()
	{
		return FOneBrahamLiftConfig();
	}

	/** Runs Update at a fixed step from one time to another, the way a server tick would. */
	void OBLiftTestRun(FOneBrahamLiftLogic& Logic, double From, double To, double Step = 1.0 / 30.0)
	{
		for (double T = From; T <= To; T += Step)
		{
			Logic.Update(T);
		}
		Logic.Update(To);
	}
}

// ===========================================================================
//  Travel
// ===========================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamLiftTravelTest, "OneBraham.Lifts.Travel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamLiftTravelTest::RunTest(const FString& Parameters)
{
	const FOneBrahamLiftConfig Config = OBLiftTestConfig();

	// The map builder's _floor_z(i) is (i - 1) * FLOOR_HEIGHT_CM: the lobby is
	// Z = 0, the plaza -400, the Skydeck 5600. A lift that disagrees by one floor
	// stops its car a storey away from every door in the building.
	TestEqual(TEXT("floor 0 (the plaza) is at -400"), Config.FloorToZ(0), -400.f);
	TestEqual(TEXT("floor 1 (the lobby) is at 0"), Config.FloorToZ(1), 0.f);
	TestEqual(TEXT("floor 15 (the Skydeck) is at 5600"), Config.FloorToZ(15), 5600.f);

	for (int32 Floor = 0; Floor <= 15; ++Floor)
	{
		TestEqual(FString::Printf(TEXT("floor %d matches the builder's pitch"), Floor),
			Config.FloorToZ(Floor), (Floor - 1) * OneBrahamConstants::FloorHeightCm);
		TestEqual(FString::Printf(TEXT("floor %d round-trips through Z"), Floor),
			Config.ZToNearestFloor(Config.FloorToZ(Floor)), Floor);
	}

	TestEqual(TEXT("150 cm above the lobby is still the lobby"), Config.ZToNearestFloor(150.f), 1);
	TestEqual(TEXT("210 cm above the lobby is nearer the mezzanine"), Config.ZToNearestFloor(210.f), 2);
	TestEqual(TEXT("below the plaza clamps to the lowest served floor"), Config.ZToNearestFloor(-5000.f), 0);
	TestEqual(TEXT("above the roof clamps to the highest served floor"), Config.ZToNearestFloor(99999.f), 15);

	// Travel time: a trapezoid. One storey only just reaches cruise; the full
	// shaft is 24 s at 250 cm/s plus one ramp's worth of time.
	const float Accel = Config.AccelCmPerSec2;
	const float Speed = Config.SpeedCmPerSec;
	TestEqual(TEXT("cruise speed is 250 cm/s"), Speed, 250.f);
	TestEqual(TEXT("doors stay open 2.5 s at a stop"), Config.DoorDwellSeconds, 2.5f);

	const float OneFloor = FOneBrahamLiftMotion::TravelSeconds(400.f, Speed, Accel);
	TestTrue(TEXT("one storey takes 400/250 + 250/160 s"),
		FMath::IsNearlyEqual(OneFloor, 400.f / 250.f + 250.f / 160.f, 0.01f));

	const float FullShaft = FOneBrahamLiftMotion::TravelSeconds(6000.f, Speed, Accel);
	TestTrue(TEXT("plaza to Skydeck takes 6000/250 + 250/160 s"),
		FMath::IsNearlyEqual(FullShaft, 6000.f / 250.f + 250.f / 160.f, 0.01f));

	const float Hop = FOneBrahamLiftMotion::TravelSeconds(100.f, Speed, Accel);
	TestTrue(TEXT("a short hop is a triangle that never reaches cruise"),
		FMath::IsNearlyEqual(Hop, 2.f * FMath::Sqrt(100.f / Accel), 0.01f));

	TestEqual(TEXT("travel time is symmetric up and down"),
		FOneBrahamLiftMotion::Duration(0.f, 0.f, -1200.f, Speed, Accel),
		FOneBrahamLiftMotion::Duration(-1200.f, 0.f, 0.f, Speed, Accel));

	// The curve itself: monotonic, never over speed, symmetric, exact at the end.
	{
		const float Start = Config.FloorToZ(1);
		const float Target = Config.FloorToZ(6);
		const float Duration = FOneBrahamLiftMotion::Duration(Start, 0.f, Target, Speed, Accel);

		float PreviousZ = Start;
		bool bMonotonic = true;
		bool bWithinSpeed = true;

		for (int32 Step = 0; Step <= 200; ++Step)
		{
			float Z = 0.f;
			float V = 0.f;
			FOneBrahamLiftMotion::Sample(Start, 0.f, Target, Speed, Accel, Duration * Step / 200.f, Z, V);
			bMonotonic &= Z >= PreviousZ - 0.001f;
			bWithinSpeed &= FMath::Abs(V) <= Speed + 0.01f;
			PreviousZ = Z;
		}

		TestTrue(TEXT("the car never moves backwards on a trip"), bMonotonic);
		TestTrue(TEXT("the car never exceeds cruise speed"), bWithinSpeed);

		float StartZ = 0.f, StartV = 0.f, MidZ = 0.f, MidV = 0.f, EndZ = 0.f, EndV = 0.f;
		FOneBrahamLiftMotion::Sample(Start, 0.f, Target, Speed, Accel, 0.f, StartZ, StartV);
		FOneBrahamLiftMotion::Sample(Start, 0.f, Target, Speed, Accel, Duration * 0.5f, MidZ, MidV);
		FOneBrahamLiftMotion::Sample(Start, 0.f, Target, Speed, Accel, Duration + 5.f, EndZ, EndV);

		TestEqual(TEXT("the trip starts at rest"), StartV, 0.f);
		TestTrue(TEXT("half the time is half the distance (ease in mirrors ease out)"),
			FMath::IsNearlyEqual(MidZ, (Start + Target) * 0.5f, 0.5f));
		TestEqual(TEXT("the trip ends exactly on the landing"), EndZ, Target);
		TestEqual(TEXT("and at rest"), EndV, 0.f);
	}

	// A re-planned trip: moving up at 200 cm/s, told to stop below. It brakes,
	// reverses and lands, and never goes further than its braking distance.
	{
		const float Duration = FOneBrahamLiftMotion::Duration(1000.f, 200.f, 800.f, Speed, Accel);
		float Highest = 1000.f;

		for (int32 Step = 0; Step <= 200; ++Step)
		{
			float Z = 0.f;
			float V = 0.f;
			FOneBrahamLiftMotion::Sample(1000.f, 200.f, 800.f, Speed, Accel, Duration * Step / 200.f, Z, V);
			Highest = FMath::Max(Highest, Z);
		}

		float EndZ = 0.f;
		float EndV = 0.f;
		FOneBrahamLiftMotion::Sample(1000.f, 200.f, 800.f, Speed, Accel, Duration, EndZ, EndV);

		TestTrue(TEXT("a reversing car overshoots by its braking distance and no more"),
			Highest <= 1000.f + FOneBrahamLiftMotion::BrakingDistance(200.f, Accel) + 0.5f);
		TestTrue(TEXT("and still lands on the target"), FMath::IsNearlyEqual(EndZ, 800.f, 0.01f));
	}

	// Two machines, one replicated trip: the client's copy of the state gives the
	// same car at the same time. This is the whole of the lift's netcode.
	{
		FOneBrahamLiftLogic Server;
		Server.Config = Config;
		Server.Reset(1, 100.0);
		Server.RequestLanding(9, 100.0);
		Server.Update(100.0);

		FOneBrahamLiftLogic Client;
		Client.Config = Config;
		Client.ApplyReplicatedState(Server.GetState());

		bool bAgree = true;
		for (double T = 100.0; T < 140.0; T += 0.37)
		{
			bAgree &= FOneBrahamLiftLogic::GetCarZ(Server.GetState(), Server.Config, T)
				== FOneBrahamLiftLogic::GetCarZ(Client.GetState(), Client.Config, T);
		}

		TestTrue(TEXT("server and client compute the same car from the same state"), bAgree);
	}

	return true;
}

// ===========================================================================
//  Queue
// ===========================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamLiftQueueTest, "OneBraham.Lifts.Queue",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamLiftQueueTest::RunTest(const FString& Parameters)
{
	const FOneBrahamLiftConfig Config = OBLiftTestConfig();

	// --- collective order -------------------------------------------------------
	{
		FOneBrahamLiftLogic Logic;
		Logic.Config = Config;
		Logic.Reset(1, 0.0);

		TestTrue(TEXT("a call to floor 5 is accepted"), Logic.RequestLanding(5, 0.0));
		TestTrue(TEXT("a call to floor 3 is accepted"), Logic.RequestLanding(3, 0.0));
		Logic.Update(0.0);

		TestEqual(TEXT("an idle car leaves at once"), Logic.GetState().Phase, EOneBrahamLiftPhase::Moving);
		TestEqual(TEXT("and stops at the nearer call on the way"), Logic.GetState().TargetFloor, 3);

		TestTrue(TEXT("a call from below while going up is accepted"), Logic.RequestLanding(0, 0.5));

		const double Arrive3 = FOneBrahamLiftMotion::TravelSeconds(800.f, Config.SpeedCmPerSec, Config.AccelCmPerSec2);
		Logic.Update(Arrive3 - 0.01);
		TestEqual(TEXT("the car is still moving a moment before arrival"), Logic.GetState().Phase, EOneBrahamLiftPhase::Moving);

		Logic.Update(Arrive3);
		TestEqual(TEXT("it arrives at floor 3"), Logic.GetState().CurrentFloor, 3);
		TestEqual(TEXT("and starts opening"), Logic.GetState().Phase, EOneBrahamLiftPhase::DoorsOpening);
		TestFalse(TEXT("floor 3's call is answered"), Logic.GetState().HasCall(3));
		TestTrue(TEXT("halfway through opening, the doors are half open"),
			FMath::IsNearlyEqual(FOneBrahamLiftLogic::GetDoorOpenAlpha(Logic.GetState(), Config, Arrive3 + Config.DoorMoveSeconds * 0.5), 0.5f, 0.01f));

		const double Opened = Arrive3 + Config.DoorMoveSeconds;
		const double Closing = Opened + Config.DoorDwellSeconds;
		const double Shut = Closing + Config.DoorMoveSeconds;

		Logic.Update(Opened);
		TestEqual(TEXT("the doors open fully"), Logic.GetState().Phase, EOneBrahamLiftPhase::DoorsOpen);

		Logic.Update(Closing - 0.01);
		TestEqual(TEXT("and stay open for the dwell"), Logic.GetState().Phase, EOneBrahamLiftPhase::DoorsOpen);

		Logic.Update(Closing);
		TestEqual(TEXT("then close"), Logic.GetState().Phase, EOneBrahamLiftPhase::DoorsClosing);

		Logic.Update(Shut);
		TestEqual(TEXT("a car going up keeps going up while anybody above wants it"), Logic.GetState().TargetFloor, 5);
		TestEqual(TEXT("leaving the moment the doors shut, not a frame later"), Logic.GetState().PhaseStartTime, Shut);

		const double Arrive5 = Shut + FOneBrahamLiftMotion::TravelSeconds(800.f, Config.SpeedCmPerSec, Config.AccelCmPerSec2);
		OBLiftTestRun(Logic, Shut, Arrive5 + Config.DoorMoveSeconds * 2.0 + Config.DoorDwellSeconds + 0.1);
		TestEqual(TEXT("after floor 5 the car turns round for floor 0"), Logic.GetState().TargetFloor, 0);
		TestEqual(TEXT("and is on its way"), Logic.GetState().Phase, EOneBrahamLiftPhase::Moving);
	}

	// --- the doors -----------------------------------------------------------------
	{
		FOneBrahamLiftLogic Logic;
		Logic.Config = Config;
		Logic.Reset(4, 0.0);

		TestTrue(TEXT("calling the car to the floor it is parked on opens it"), Logic.RequestLanding(4, 0.0));
		Logic.Update(0.0);
		TestEqual(TEXT("doors opening at the same floor"), Logic.GetState().Phase, EOneBrahamLiftPhase::DoorsOpening);
		TestEqual(TEXT("without moving"), Logic.GetState().CurrentFloor, 4);

		const double Closing = Config.DoorMoveSeconds + Config.DoorDwellSeconds;
		OBLiftTestRun(Logic, 0.0, Closing + Config.DoorMoveSeconds * 0.25);
		TestEqual(TEXT("the doors are closing"), Logic.GetState().Phase, EOneBrahamLiftPhase::DoorsClosing);

		const double Now = Closing + Config.DoorMoveSeconds * 0.25;
		const float Before = FOneBrahamLiftLogic::GetDoorOpenAlpha(Logic.GetState(), Config, Now);
		TestTrue(TEXT("pressing the button on closing doors reopens them"), Logic.RequestLanding(4, Now));
		const float After = FOneBrahamLiftLogic::GetDoorOpenAlpha(Logic.GetState(), Config, Now);

		TestEqual(TEXT("reopening"), Logic.GetState().Phase, EOneBrahamLiftPhase::DoorsOpening);
		TestTrue(TEXT("from exactly where they were, with no jump"), FMath::IsNearlyEqual(Before, After, 0.001f));
	}

	// --- the in-car buttons -------------------------------------------------------
	{
		FOneBrahamLiftLogic Logic;
		Logic.Config = Config;
		Logic.Reset(1, 0.0);

		TestEqual(TEXT("Up from the lobby offers floor 2"), Logic.GetNextCarSelection(1), 2);
		TestTrue(TEXT("choosing it is accepted"), Logic.SelectCarDestination(2, 0.0));
		TestEqual(TEXT("Up again cycles on to floor 3"), Logic.GetNextCarSelection(1), 3);
		Logic.SelectCarDestination(3, 0.0);
		TestEqual(TEXT("the in-car choice replaces the last one"), Logic.GetState().CarDestination, 3);
		TestEqual(TEXT("Down cycles back to floor 2"), Logic.GetNextCarSelection(-1), 2);

		Logic.Update(0.0);
		TestEqual(TEXT("the car leaves for the chosen floor"), Logic.GetState().TargetFloor, 3);

		FOneBrahamLiftLogic Top;
		Top.Config = Config;
		Top.Reset(15, 0.0);
		TestEqual(TEXT("there is no Up from the top floor"), Top.GetNextCarSelection(1), INDEX_NONE);

		FOneBrahamLiftLogic Bottom;
		Bottom.Config = Config;
		Bottom.Reset(0, 0.0);
		TestEqual(TEXT("there is no Down from the plaza"), Bottom.GetNextCarSelection(-1), INDEX_NONE);
	}

	// --- served range ---------------------------------------------------------------
	{
		FOneBrahamLiftLogic Logic;
		Logic.Config = Config;
		Logic.Config.MinFloor = 2;
		Logic.Config.MaxFloor = 10;
		Logic.Reset(0, 0.0);

		TestEqual(TEXT("a car parked out of range is clamped into it"), Logic.GetState().CurrentFloor, 2);
		TestFalse(TEXT("a call below the served range is refused"), Logic.RequestLanding(1, 0.0));
		TestFalse(TEXT("a call above it is refused"), Logic.RequestLanding(11, 0.0));
		TestFalse(TEXT("a nonsense floor is refused"), Logic.RequestLanding(-3, 0.0));
		TestTrue(TEXT("a call inside it is accepted"), Logic.RequestLanding(10, 0.0));
	}

	// --- a long hitch ---------------------------------------------------------------
	{
		FOneBrahamLiftLogic Logic;
		Logic.Config = Config;
		Logic.Reset(1, 0.0);
		Logic.RequestLanding(8, 0.0);
		Logic.Update(0.0);
		Logic.Update(500.0);

		TestEqual(TEXT("a server hitch resolves the whole trip at once"), Logic.GetState().CurrentFloor, 8);
		TestEqual(TEXT("and leaves the car parked"), Logic.GetState().Phase, EOneBrahamLiftPhase::Idle);
		TestFalse(TEXT("with nothing left to do"), Logic.HasPendingWork());
	}

	return true;
}

// ===========================================================================
//  Lockdown
// ===========================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamLiftLockdownTest, "OneBraham.Lifts.Lockdown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamLiftLockdownTest::RunTest(const FString& Parameters)
{
	const FOneBrahamLiftConfig Config = OBLiftTestConfig();
	TestEqual(TEXT("the lockdown never delivers riders to the Skydeck"), Config.LockdownExcludedFloor, OneBrahamConstants::FloorSkydeck);

	// --- mid-shaft, heading for the Skydeck ---------------------------------------
	{
		FOneBrahamLiftLogic Logic;
		Logic.Config = Config;
		Logic.Reset(1, 0.0);
		Logic.RequestLanding(15, 0.0);
		Logic.RequestLanding(12, 0.0);
		Logic.Update(0.0);
		TestEqual(TEXT("the car sets off for floor 12 first"), Logic.GetState().TargetFloor, 12);

		const double Now = 10.0;
		const float Z = FOneBrahamLiftLogic::GetCarZ(Logic.GetState(), Config, Now);
		const float V = FOneBrahamLiftLogic::GetCarVelocity(Logic.GetState(), Config, Now);
		TestTrue(TEXT("ten seconds in, the car is climbing at cruise"), FMath::IsNearlyEqual(V, Config.SpeedCmPerSec, 0.5f));

		Logic.SetLockedDown(true, Now);

		const FOneBrahamLiftState& State = Logic.GetState();
		TestTrue(TEXT("the lift is locked down"), State.bLockedDown);
		TestEqual(TEXT("every waiting call is dropped"), State.CallMask, 0);
		TestEqual(TEXT("the car is still moving"), State.Phase, EOneBrahamLiftPhase::Moving);

		const float StopZ = Config.FloorToZ(State.TargetFloor);
		TestTrue(TEXT("it stops at a floor ahead of it"), StopZ >= Z);
		TestTrue(TEXT("one it can reach without an emergency stop"),
			StopZ - Z >= FOneBrahamLiftMotion::BrakingDistance(V, Config.AccelCmPerSec2) - 1.f);
		TestTrue(TEXT("and the nearest such floor"),
			StopZ - Z < FOneBrahamLiftMotion::BrakingDistance(V, Config.AccelCmPerSec2) + Config.FloorHeightCm);
		TestEqual(TEXT("the re-planned trip starts where the car was"),
			FOneBrahamLiftLogic::GetCarZ(State, Config, Now), Z);

		TestFalse(TEXT("a landing call is refused"), Logic.RequestLanding(3, Now + 0.1));
		TestFalse(TEXT("an in-car choice is refused"), Logic.SelectCarDestination(2, Now + 0.1));
		TestEqual(TEXT("the in-car buttons offer nothing"), Logic.GetNextCarSelection(1), INDEX_NONE);

		const double Arrive = Now + FOneBrahamLiftLogic::GetMoveDuration(State, Config);
		OBLiftTestRun(Logic, Now, Arrive + 120.0, 0.25);

		TestEqual(TEXT("the car arrives"), Logic.GetState().CurrentFloor, Config.ZToNearestFloor(StopZ));
		TestEqual(TEXT("with its doors open"), Logic.GetState().Phase, EOneBrahamLiftPhase::DoorsOpen);
		TestEqual(TEXT("and they stay open"), FOneBrahamLiftLogic::GetDoorOpenAlpha(Logic.GetState(), Config, Arrive + 120.0), 1.f);
	}

	// --- one floor below the Skydeck, climbing towards it --------------------------
	{
		FOneBrahamLiftLogic Logic;
		Logic.Config = Config;
		Logic.Reset(14, 0.0);
		Logic.RequestLanding(15, 0.0);
		Logic.Update(0.0);

		// One second out of floor 14, still accelerating for the Skydeck. There is
		// no allowed floor ahead of it, so it must brake, reverse and settle back.
		const double Now = 1.0;
		Logic.SetLockedDown(true, Now);

		const FOneBrahamLiftState& State = Logic.GetState();
		TestEqual(TEXT("a car climbing to the Skydeck goes back to floor 14"), State.TargetFloor, 14);

		const double Arrive = Now + FOneBrahamLiftLogic::GetMoveDuration(State, Config);
		float Highest = -TNumericLimits<float>::Max();
		for (double T = Now; T <= Arrive; T += 0.02)
		{
			Highest = FMath::Max(Highest, FOneBrahamLiftLogic::GetCarZ(State, Config, T));
		}

		TestTrue(TEXT("and never reaches the Skydeck landing on the way"), Highest < Config.FloorToZ(15) - 1.f);
		TestTrue(TEXT("it lands on floor 14"),
			FMath::IsNearlyEqual(FOneBrahamLiftLogic::GetCarZ(State, Config, Arrive), Config.FloorToZ(14), 0.01f));
	}

	// --- parked, and back in service --------------------------------------------------
	{
		FOneBrahamLiftLogic Logic;
		Logic.Config = Config;
		Logic.Reset(3, 0.0);

		Logic.SetLockedDown(true, 1.0);
		TestEqual(TEXT("a parked car opens its doors when the lockdown starts"), Logic.GetState().Phase, EOneBrahamLiftPhase::DoorsOpening);

		OBLiftTestRun(Logic, 1.0, 60.0, 0.5);
		TestEqual(TEXT("and keeps them open"), Logic.GetState().Phase, EOneBrahamLiftPhase::DoorsOpen);

		Logic.SetLockedDown(false, 60.0);
		TestFalse(TEXT("the lockdown can end"), Logic.IsLockedDown());
		TestTrue(TEXT("calls are accepted again"), Logic.RequestLanding(9, 60.0));

		OBLiftTestRun(Logic, 60.0, 60.0 + Config.DoorDwellSeconds + Config.DoorMoveSeconds + 0.1);
		TestEqual(TEXT("the doors close after a fresh dwell and the car answers"), Logic.GetState().TargetFloor, 9);
		TestEqual(TEXT("by moving"), Logic.GetState().Phase, EOneBrahamLiftPhase::Moving);
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
