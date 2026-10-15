// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamDisplayTests.cpp
// ---------------------------------------------------------------------------
// The OLED screens' timing: which slide shows when, and how the copy fades.
// A screen whose copy pops instead of fading, or that shows slide two for a
// frame at the wrong moment, is the kind of detail that makes a lobby look
// like a placeholder.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "World/OneBrahamDisplay.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamDisplayTimingTest, "OneBraham.World.Display",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamDisplayTimingTest::RunTest(const FString& Parameters)
{
	// Slides advance every SlideSeconds and wrap.
	TestEqual(TEXT("slide 0 at the start"), AOneBrahamDisplay::SlideIndexAt(0.0, 7.f, 3), 0);
	TestEqual(TEXT("slide 1 after one period"), AOneBrahamDisplay::SlideIndexAt(7.5, 7.f, 3), 1);
	TestEqual(TEXT("and back to slide 0 after three"), AOneBrahamDisplay::SlideIndexAt(21.2, 7.f, 3), 0);
	TestEqual(TEXT("no slides is no slide"), AOneBrahamDisplay::SlideIndexAt(4.0, 7.f, 0), INDEX_NONE);
	TestEqual(TEXT("time before zero holds the first"), AOneBrahamDisplay::SlideIndexAt(-3.0, 7.f, 3), 0);

	// The copy fades in, holds, fades out - never pops.
	TestNearlyEqual(TEXT("invisible at the change"), AOneBrahamDisplay::CopyAlphaAt(0.0, 7.f, 0.8f), 0.f, 0.001f);
	TestNearlyEqual(TEXT("half way in"), AOneBrahamDisplay::CopyAlphaAt(0.4, 7.f, 0.8f), 0.5f, 0.01f);
	TestNearlyEqual(TEXT("fully in between fades"), AOneBrahamDisplay::CopyAlphaAt(3.5, 7.f, 0.8f), 1.f, 0.001f);
	TestNearlyEqual(TEXT("half way out"), AOneBrahamDisplay::CopyAlphaAt(6.6, 7.f, 0.8f), 0.5f, 0.01f);
	TestTrue(TEXT("a fade longer than half the slide is clamped"),
		AOneBrahamDisplay::CopyAlphaAt(3.5, 7.f, 10.f) > 0.9f);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
