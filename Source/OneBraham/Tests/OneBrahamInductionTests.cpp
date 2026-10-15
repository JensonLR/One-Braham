// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamInductionTests.cpp
// ---------------------------------------------------------------------------
// The first-time induction: every step finishes the moment the player does the
// thing, in order, and no step can trap anyone.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Core/OneBrahamInduction.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamInductionTest, "OneBraham.UI.Induction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamInductionTest::RunTest(const FString& Parameters)
{
	FOneBrahamInduction Induction;
	FOneBrahamInductionSnapshot Now;
	TestEqual(TEXT("it starts by asking you to move"), Induction.Step, EOneBrahamInductionStep::Move);
	TestFalse(TEXT("every step has a prompt"), Induction.GetPrompt().IsEmpty());

	// Standing still does nothing.
	TestFalse(TEXT("no progress, no step"), Induction.Tick(Now, 0.1f));

	// Walk, look, sprint, crouch, keycard, gates, climb, fire, reload.
	Now.DistanceWalkedCm = 450.f;
	TestTrue(TEXT("walking four metres finishes Move"), Induction.Tick(Now, 0.1f));
	TestEqual(TEXT("then Look"), Induction.Step, EOneBrahamInductionStep::Look);
	Now.YawTurnedDeg = 60.f;
	TestFalse(TEXT("a small glance is not enough"), Induction.Tick(Now, 0.1f));
	Now.YawTurnedDeg = 200.f;
	TestTrue(TEXT("a proper look round is"), Induction.Tick(Now, 0.1f));
	Now.bSprinting = true;
	TestTrue(TEXT("sprinting finishes Sprint"), Induction.Tick(Now, 0.1f));
	Now.bCrouched = true;
	TestTrue(TEXT("crouching finishes Crouch"), Induction.Tick(Now, 0.1f));
	Now.bHasKeycard = true;
	TestTrue(TEXT("a keycard finishes Keycard"), Induction.Tick(Now, 0.1f));
	Now.bPastGates = true;
	TestTrue(TEXT("through the gates finishes Gates"), Induction.Tick(Now, 0.1f));
	Now.Floor = 1;
	TestTrue(TEXT("level 1 finishes Climb"), Induction.Tick(Now, 0.1f));
	Now.ShotsFired = 2;
	TestFalse(TEXT("two shots are not three"), Induction.Tick(Now, 0.1f));
	Now.ShotsFired = 3;
	TestTrue(TEXT("three shots finish Fire"), Induction.Tick(Now, 0.1f));
	Now.bReloading = true;
	TestTrue(TEXT("reloading finishes Reload"), Induction.Tick(Now, 0.1f));
	TestTrue(TEXT("and the induction is done"), Induction.IsDone());
	TestFalse(TEXT("done stays done"), Induction.Tick(Now, 10.f));

	// Steps measure their own progress: walking before Move began does not count.
	{
		FOneBrahamInduction Fresh;
		FOneBrahamInductionSnapshot Walked;
		Walked.DistanceWalkedCm = 500.f;
		Fresh.Baseline = Walked;
		TestFalse(TEXT("distance walked before the step does not count"), Fresh.Tick(Walked, 0.1f));
	}

	// Nothing traps a player.
	{
		FOneBrahamInduction Stuck;
		FOneBrahamInductionSnapshot Idle;
		TestTrue(TEXT("a step with no progress moves on by itself"),
			Stuck.Tick(Idle, FOneBrahamInduction::StepTimeoutSeconds + 0.1f));
		TestEqual(TEXT("to the next one"), Stuck.Step, EOneBrahamInductionStep::Look);
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
