// Copyright (c) One Braham. All Rights Reserved.
//
// OneBraham.Animation.*, OneBraham.Extraction.*
//
// The two pieces of the world that every player watches and nobody controls:
// the other players' bodies, and the helicopter. Both are pure functions of
// replicated state, which is what makes them testable without a world: the
// pose family a weapon holds, the angle a body moves at, and the path the
// helicopter flies for a given fraction of its approach.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Characters/OneBrahamAnimInstance.h"
#include "Extraction/OneBrahamHelicopter.h"
#include "OneBraham.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamAnimPoseTest, "OneBraham.Animation.PoseAndDirection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamAnimPoseTest::RunTest(const FString& Parameters)
{
	// Every armed category holds a weapon pose; only utility is empty-handed.
	TestEqual(TEXT("a pistol is held as a pistol"),
		UOneBrahamAnimInstance::PoseForCategory(EOneBrahamWeaponCategory::SuppressedPistol), EOneBrahamWeaponPose::Pistol);
	TestEqual(TEXT("a pump shotgun is held as a shotgun"),
		UOneBrahamAnimInstance::PoseForCategory(EOneBrahamWeaponCategory::PumpShotgun), EOneBrahamWeaponPose::Shotgun);
	TestEqual(TEXT("an SMG is held as a rifle"),
		UOneBrahamAnimInstance::PoseForCategory(EOneBrahamWeaponCategory::SMG), EOneBrahamWeaponPose::Rifle);
	TestEqual(TEXT("utility is empty-handed"),
		UOneBrahamAnimInstance::PoseForCategory(EOneBrahamWeaponCategory::Utility), EOneBrahamWeaponPose::Unarmed);

	// Direction is movement against facing: forwards 0, strafing right 90,
	// backwards 180 either way, and standing still 0 rather than a NaN.
	const FRotator North(0.f, 0.f, 0.f);
	TestNearlyEqual(TEXT("forwards"), UOneBrahamAnimInstance::DirectionOf(FVector(300.f, 0.f, 0.f), North), 0.f, 0.01f);
	TestNearlyEqual(TEXT("strafing right"), UOneBrahamAnimInstance::DirectionOf(FVector(0.f, 300.f, 0.f), North), 90.f, 0.01f);
	TestNearlyEqual(TEXT("backwards"), FMath::Abs(UOneBrahamAnimInstance::DirectionOf(FVector(-300.f, 0.f, 0.f), North)), 180.f, 0.01f);
	TestNearlyEqual(TEXT("standing still"), UOneBrahamAnimInstance::DirectionOf(FVector::ZeroVector, North), 0.f, 0.01f);
	TestNearlyEqual(TEXT("falling does not count as moving"),
		UOneBrahamAnimInstance::DirectionOf(FVector(0.f, 0.f, -900.f), North), 0.f, 0.01f);
	TestNearlyEqual(TEXT("relative to where the body faces"),
		UOneBrahamAnimInstance::DirectionOf(FVector(0.f, 300.f, 0.f), FRotator(0.f, 90.f, 0.f)), 0.f, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamHelicopterPathTest, "OneBraham.Extraction.HelicopterPath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamHelicopterPathTest::RunTest(const FString& Parameters)
{
	const FVector Pad(0.f, -800.f, OneBrahamConstants::FloorZ(OneBrahamConstants::FloorSkydeck));
	const FVector Approach(16000.f, -9000.f, 4000.f);
	const FVector Hover(0.f, 0.f, 1800.f);

	// The approach starts where the map says it comes from and ends exactly on
	// the hover point: the moment the HUD's arrival timer reaches zero, the
	// helicopter is over the pad, not still flying in.
	const FTransform Start = AOneBrahamHelicopter::ComputePose(EOneBrahamExtractionState::Arriving, 0.f, 0.0, Pad, Approach, Hover);
	const FTransform End = AOneBrahamHelicopter::ComputePose(EOneBrahamExtractionState::Arriving, 1.f, 0.0, Pad, Approach, Hover);
	TestTrue(TEXT("the approach starts at the approach point"), Start.GetLocation().Equals(Pad + Approach, 1.f));
	TestTrue(TEXT("the approach ends over the pad"), (End.GetLocation() - (Pad + Hover)).Size2D() < 1.f);

	// It never dips below the hover height on the way in: a helicopter that
	// clips the Skydeck balustrade on approach is a trailer nobody can use.
	for (int32 Step = 0; Step <= 20; ++Step)
	{
		const FTransform Pose = AOneBrahamHelicopter::ComputePose(EOneBrahamExtractionState::Arriving,
			Step / 20.f, 0.0, Pad, Approach, Hover);
		TestTrue(FString::Printf(TEXT("above the hover height at %d/20"), Step),
			Pose.GetLocation().Z >= Pad.Z + Hover.Z - 30.f);
	}

	// Holding is a hover over the pad, bobbing but never drifting off it.
	const FTransform Holding = AOneBrahamHelicopter::ComputePose(EOneBrahamExtractionState::Holding, 1.f, 12.5, Pad, Approach, Hover);
	TestTrue(TEXT("holding is over the pad"), (Holding.GetLocation() - (Pad + Hover)).Size2D() < 1.f);
	TestTrue(TEXT("holding bobs less than half a metre"), FMath::Abs(Holding.GetLocation().Z - (Pad.Z + Hover.Z)) <= 50.f);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
