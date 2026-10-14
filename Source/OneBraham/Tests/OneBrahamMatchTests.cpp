// Copyright (c) One Braham. All Rights Reserved.
//
// OneBraham.Match.*
//
// FIRE DRILL's capture rule is a pure function on purpose: who is on the point
// goes in, where the point goes comes out, and every case Docs/06 names can be
// checked without a world.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Match/OneBrahamControlPoint.h"
#include "Match/OneBrahamSmokeSubsystem.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamControlPointCaptureTest, "OneBraham.Match.ControlPointCapture",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamControlPointCaptureTest::RunTest(const FString& Parameters)
{
	int32 Owner = INDEX_NONE;
	int32 Capturer = INDEX_NONE;
	float Progress = 0.f;
	bool bContested = false;

	// Staff alone on a neutral point: half way after half the capture time.
	AOneBrahamControlPoint::StepCapture(2, 0, 2.f, 4.f, Owner, Capturer, Progress, bContested);
	TestEqual(TEXT("half captured"), Progress, 0.5f);
	TestEqual(TEXT("staff capturing"), Capturer, 0);
	TestEqual(TEXT("not owned yet"), Owner, static_cast<int32>(INDEX_NONE));

	// Both teams: frozen and contested.
	AOneBrahamControlPoint::StepCapture(1, 1, 2.f, 4.f, Owner, Capturer, Progress, bContested);
	TestTrue(TEXT("contested"), bContested);
	TestEqual(TEXT("frozen"), Progress, 0.5f);

	// Empty: the half-built claim stays.
	AOneBrahamControlPoint::StepCapture(0, 0, 10.f, 4.f, Owner, Capturer, Progress, bContested);
	TestFalse(TEXT("not contested"), bContested);
	TestEqual(TEXT("claim kept"), Progress, 0.5f);

	// Staff finish it.
	AOneBrahamControlPoint::StepCapture(1, 0, 3.f, 4.f, Owner, Capturer, Progress, bContested);
	TestEqual(TEXT("staff own it"), Owner, 0);
	TestEqual(TEXT("full"), Progress, 1.f);
	TestEqual(TEXT("nobody capturing"), Capturer, static_cast<int32>(INDEX_NONE));

	// Contractors flip it: drain first (neutral at the bottom), then build.
	AOneBrahamControlPoint::StepCapture(0, 3, 4.f, 4.f, Owner, Capturer, Progress, bContested);
	TestEqual(TEXT("drained to neutral"), Owner, static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("contractors capturing"), Capturer, 1);
	AOneBrahamControlPoint::StepCapture(0, 3, 4.f, 4.f, Owner, Capturer, Progress, bContested);
	TestEqual(TEXT("contractors own it: a flip is twice a capture"), Owner, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamSmokeSightTest, "OneBraham.Match.SmokeBlocksSight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamSmokeSightTest::RunTest(const FString& Parameters)
{
	const FVector Cloud(0.f, 0.f, 100.f);
	const float Radius = 500.f;

	// Straight through the middle: blocked.
	TestTrue(TEXT("through the cloud"), UOneBrahamSmokeSubsystem::SegmentHitsCloud(
		FVector(-2000.f, 0.f, 100.f), FVector(2000.f, 0.f, 100.f), Cloud, Radius));
	// Passing well wide: clear.
	TestFalse(TEXT("wide of the cloud"), UOneBrahamSmokeSubsystem::SegmentHitsCloud(
		FVector(-2000.f, 800.f, 100.f), FVector(2000.f, 800.f, 100.f), Cloud, Radius));
	// Grazing the thin outer edge: still seen.
	TestFalse(TEXT("grazing the edge"), UOneBrahamSmokeSubsystem::SegmentHitsCloud(
		FVector(-2000.f, 470.f, 100.f), FVector(2000.f, 470.f, 100.f), Cloud, Radius));
	// Both ends short of the cloud: the segment stops before it.
	TestFalse(TEXT("stopping short"), UOneBrahamSmokeSubsystem::SegmentHitsCloud(
		FVector(-2000.f, 0.f, 100.f), FVector(-700.f, 0.f, 100.f), Cloud, Radius));
	// Standing inside it: blind.
	TestTrue(TEXT("inside the cloud"), UOneBrahamSmokeSubsystem::SegmentHitsCloud(
		FVector(0.f, 0.f, 100.f), FVector(2000.f, 0.f, 100.f), Cloud, Radius));

	// The cloud grows: nothing at the pop, most of it in the first second, all of it by GrowSeconds.
	TestEqual(TEXT("nothing at the pop"), UOneBrahamSmokeSubsystem::CloudRadiusAt(Radius, 0.f), 0.f);
	TestTrue(TEXT("most of it after a second"), UOneBrahamSmokeSubsystem::CloudRadiusAt(Radius, 1.f) > Radius * 0.6f);
	TestEqual(TEXT("full size"), UOneBrahamSmokeSubsystem::CloudRadiusAt(Radius, UOneBrahamSmokeSubsystem::GrowSeconds + 1.f), Radius);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
