// Copyright (c) One Braham. All Rights Reserved.
//
// OneBraham.Feel.*
//
// The first-person feel layer is cosmetic, and cosmetic code is where a sign
// error lives for a year: a hit from the left that rolls the view left, a
// heartbeat that stops at zero health, a spring that explodes on a frame hitch,
// a shake that fires at full strength for a player who asked for none. None of
// those crash and none of them are reported as bugs; they are reported as "the
// game feels off". These tests hold the arithmetic still.
//
// Everything asserted here is a static function on
// UOneBrahamFirstPersonFeelComponent or the spring it uses, so no world, pawn or
// camera is needed.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"

#include "Characters/OneBrahamFirstPersonFeelComponent.h"
#include "Core/OneBrahamGameUserSettings.h"
#include "OneBraham.h"

#if WITH_AUTOMATION_TESTS

// ===========================================================================
//  Directional punch
// ===========================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamFeelPunchTest, "OneBraham.Feel.Punch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamFeelPunchTest::RunTest(const FString& Parameters)
{
	using UFeel = UOneBrahamFirstPersonFeelComponent;

	const FVector Eye(0.f, 0.f, 0.f);
	const FRotator Facing(0.f, 0.f, 0.f);	// looking down +X; +Y is the right hand

	const FRotator FromLeft = UFeel::ComputeDirectionalPunch(Eye, Facing, FVector(0.f, -1000.f, 0.f), 1.f, 2.f, 1.5f, 2.f);
	const FRotator FromRight = UFeel::ComputeDirectionalPunch(Eye, Facing, FVector(0.f, 1000.f, 0.f), 1.f, 2.f, 1.5f, 2.f);
	const FRotator FromFront = UFeel::ComputeDirectionalPunch(Eye, Facing, FVector(1000.f, 0.f, 0.f), 1.f, 2.f, 1.5f, 2.f);
	const FRotator FromBehind = UFeel::ComputeDirectionalPunch(Eye, Facing, FVector(-1000.f, 0.f, 0.f), 1.f, 2.f, 1.5f, 2.f);
	const FRotator NoInstigator = UFeel::ComputeDirectionalPunch(Eye, Facing, FVector::ZeroVector, 1.f, 2.f, 1.5f, 2.f);

	// Away from the shooter: a round from the left turns and rolls the view right.
	TestTrue(TEXT("a hit from the left turns the view right"), FromLeft.Yaw > 0.f);
	TestTrue(TEXT("and rolls it right"), FromLeft.Roll > 0.f);
	TestTrue(TEXT("a hit from the right turns the view left"), FromRight.Yaw < 0.f);
	TestTrue(TEXT("and rolls it left"), FromRight.Roll < 0.f);
	TestEqual(TEXT("left and right are mirror images"), static_cast<float>(FromLeft.Yaw), static_cast<float>(-FromRight.Yaw), 0.001f);

	// From the front the head is knocked back; from behind it is shoved forward.
	TestTrue(TEXT("a hit from the front lifts the view"), FromFront.Pitch > 0.f);
	TestTrue(TEXT("a hit from behind drops it"), FromBehind.Pitch < 0.f);
	TestEqual(TEXT("a frontal hit does not turn or roll"), static_cast<float>(FromFront.Yaw), 0.f, 0.001f);

	// The relative frame is the view's, not the world's: turn round and the same
	// world position is now behind you.
	const FRotator TurnedRound = UFeel::ComputeDirectionalPunch(Eye, FRotator(0.f, 180.f, 0.f), FVector(1000.f, 0.f, 0.f), 1.f, 2.f, 1.5f, 2.f);
	TestTrue(TEXT("the punch is measured against the current view"), TurnedRound.Pitch < 0.f);

	// No instigator (a fall, the building): a plain downward flinch.
	TestTrue(TEXT("a hit with no instigator dips the view"), NoInstigator.Pitch < 0.f);
	TestEqual(TEXT("and has no side"), static_cast<float>(NoInstigator.Roll), 0.f, 0.001f);

	// Strength scales linearly and zero strength is no punch.
	const FRotator Weak = UFeel::ComputeDirectionalPunch(Eye, Facing, FVector(0.f, -1000.f, 0.f), 0.5f, 2.f, 1.5f, 2.f);
	TestEqual(TEXT("half the damage is half the punch"), static_cast<float>(Weak.Roll), static_cast<float>(FromLeft.Roll) * 0.5f, 0.001f);
	TestTrue(TEXT("zero strength is no punch"),
		UFeel::ComputeDirectionalPunch(Eye, Facing, FVector(0.f, -1000.f, 0.f), 0.f, 2.f, 1.5f, 2.f).IsNearlyZero());

	return true;
}

// ===========================================================================
//  Heartbeat
// ===========================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamFeelHeartbeatTest, "OneBraham.Feel.Heartbeat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamFeelHeartbeatTest::RunTest(const FString& Parameters)
{
	using UFeel = UOneBrahamFirstPersonFeelComponent;

	TestTrue(TEXT("the heart races as health falls"), UFeel::ComputeHeartbeatBpm(0.f) > UFeel::ComputeHeartbeatBpm(0.25f));
	TestEqual(TEXT("and rests at the low-health line"), UFeel::ComputeHeartbeatBpm(0.3f), UFeel::ComputeHeartbeatBpm(1.f), 0.001f);
	TestTrue(TEXT("a human rate, not a drum roll"), UFeel::ComputeHeartbeatBpm(0.f) <= 140.f && UFeel::ComputeHeartbeatBpm(1.f) >= 60.f);

	// Over two seconds of a racing heart: always in range, and it actually beats
	// (a pulse that never rises or never falls is a constant, not a heart).
	float Lowest = 1.f;
	float Highest = 0.f;
	for (int32 Step = 0; Step < 400; ++Step)
	{
		const float Pulse = UFeel::ComputeHeartbeatPulse(Step * 0.005f, 0.05f);
		Lowest = FMath::Min(Lowest, Pulse);
		Highest = FMath::Max(Highest, Pulse);
		if (Pulse < 0.f || Pulse > 1.f || !FMath::IsFinite(Pulse))
		{
			AddError(FString::Printf(TEXT("heartbeat pulse %f out of range at t=%f"), Pulse, Step * 0.005f));
			return false;
		}
	}

	TestTrue(TEXT("the pulse reaches a beat"), Highest > 0.8f);
	TestTrue(TEXT("and falls quiet between beats"), Lowest < 0.05f);
	return true;
}

// ===========================================================================
//  Springs
// ===========================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamFeelSpringTest, "OneBraham.Feel.Springs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamFeelSpringTest::RunTest(const FString& Parameters)
{
	// An impulse sized for a 2.4 degree peak peaks at about 2.4 degrees.
	for (const float Damping : { 0.3f, 0.45f, 0.7f, 1.f })
	{
		FOneBrahamFeelSpring Spring;
		Spring.Velocity.X = FOneBrahamFeelSpring::ImpulseForPeak(2.4f, 2.6f, Damping);

		float Peak = 0.f;
		for (int32 Frame = 0; Frame < 240; ++Frame)
		{
			Spring.Step(FVector::ZeroVector, 2.6f, Damping, 1.f / 240.f);
			Peak = FMath::Max(Peak, Spring.Value.X);
		}

		TestEqual(*FString::Printf(TEXT("the impulse for a 2.4 degree peak peaks at 2.4 at damping %.2f"), Damping), Peak, 2.4f, 0.15f);
	}

	// It settles, and a hitch cannot launch it: the same punch through 30 Hz
	// frames, then a one-second stall, must come back to rest and stay finite.
	FOneBrahamFeelSpring Spring;
	Spring.Velocity = FVector(FOneBrahamFeelSpring::ImpulseForPeak(6.f, 3.6f, 0.42f));

	for (int32 Frame = 0; Frame < 30; ++Frame)
	{
		Spring.Step(FVector::ZeroVector, 3.6f, 0.42f, 1.f / 30.f);
	}
	Spring.Step(FVector::ZeroVector, 3.6f, 0.42f, 1.f);
	for (int32 Frame = 0; Frame < 120; ++Frame)
	{
		Spring.Step(FVector::ZeroVector, 3.6f, 0.42f, 1.f / 30.f);
	}

	TestTrue(TEXT("a spring survives a frame hitch"), !Spring.Value.ContainsNaN() && Spring.Value.GetAbsMax() < 6.f);
	TestTrue(TEXT("and comes to rest"), Spring.Value.GetAbsMax() < 0.01f && Spring.Velocity.GetAbsMax() < 0.1f);
	return true;
}

// ===========================================================================
//  Shake scale and falloff
// ===========================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamFeelShakeTest, "OneBraham.Feel.Shake",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamFeelShakeTest::RunTest(const FString& Parameters)
{
	using UFeel = UOneBrahamFirstPersonFeelComponent;

	// The falloff: full at the blast, nothing at the radius, and squared so the
	// middle distance is much less than half.
	TestEqual(TEXT("full shake at the blast"), UFeel::ComputeExplosionShakeFalloff(0.f, 4000.f), 1.f, 0.001f);
	TestEqual(TEXT("no shake at the radius"), UFeel::ComputeExplosionShakeFalloff(4000.f, 4000.f), 0.f, 0.001f);
	TestEqual(TEXT("no shake beyond it"), UFeel::ComputeExplosionShakeFalloff(9000.f, 4000.f), 0.f, 0.001f);
	TestEqual(TEXT("a quarter at half the radius"), UFeel::ComputeExplosionShakeFalloff(2000.f, 4000.f), 0.25f, 0.001f);
	TestEqual(TEXT("a zero radius never divides by zero"), UFeel::ComputeExplosionShakeFalloff(10.f, 0.f), 0.f, 0.001f);

	// The accessibility contract from OneBrahamCameraShake.h: off is off, the
	// reduction is a flat 30%, and otherwise the slider is the multiplier.
	UOneBrahamGameUserSettings* Settings = NewObject<UOneBrahamGameUserSettings>(GetTransientPackage(), NAME_None, RF_Transient);
	if (!TestNotNull(TEXT("a settings object to configure"), Settings))
	{
		return false;
	}

	Settings->bCameraShake = true;
	Settings->bScreenShakeReduction = false;
	Settings->CameraShakeIntensity = 0.8f;
	TestEqual(TEXT("the slider scales the shake"), UFeel::ComputeCameraShakeScale(Settings), 0.8f, 0.001f);

	Settings->bCameraShake = false;
	TestEqual(TEXT("shake off is no shake"), UFeel::ComputeCameraShakeScale(Settings), 0.f, 0.001f);

	// SetReducedMotion switches bCameraShake off as it turns the reduction on; the
	// reduction still has to mean 30%, not nothing.
	Settings->bScreenShakeReduction = true;
	TestEqual(TEXT("the reduction is a flat 30%"), UFeel::ComputeCameraShakeScale(Settings), 0.3f, 0.001f);

	TestEqual(TEXT("no settings object means the default"), UFeel::ComputeCameraShakeScale(nullptr), 1.f, 0.001f);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
