// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamSkyTests.cpp
// ---------------------------------------------------------------------------
// The day going down for the Skydeck finale: when it starts, how fast it
// runs, and what the light passes through on the way. A sunset that pops, or
// one that dims the sun in a straight line of lux so the first second does
// nothing and the last one goes black, is the kind of thing that makes the
// store-page frame look like a level.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "World/OneBrahamSkyDirector.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamSkyFinaleStageTest, "OneBraham.World.Sky.FinaleStage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamSkyFinaleStageTest::RunTest(const FString& Parameters)
{
	const EOneBrahamLockdownStage Purge = EOneBrahamLockdownStage::Purge;
	TestFalse(TEXT("daylight while the building is normal"),
		AOneBrahamSkyDirector::IsFinaleStage(EOneBrahamLockdownStage::Normal, Purge));
	TestFalse(TEXT("daylight through the lockdown"),
		AOneBrahamSkyDirector::IsFinaleStage(EOneBrahamLockdownStage::Lockdown, Purge));
	TestTrue(TEXT("dusk at the purge"), AOneBrahamSkyDirector::IsFinaleStage(Purge, Purge));
	TestTrue(TEXT("an earlier finale stage includes the ones after it"),
		AOneBrahamSkyDirector::IsFinaleStage(Purge, EOneBrahamLockdownStage::Lockdown));

	// The shipped director waits for the Purge and takes about twenty seconds.
	const AOneBrahamSkyDirector* Defaults = GetDefault<AOneBrahamSkyDirector>();
	TestTrue(TEXT("the finale is the purge"), Defaults->FinaleStage == Purge);
	TestNearlyEqual(TEXT("twenty seconds of sunset"), Defaults->BlendSeconds, 20.f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamSkyProgressTest, "OneBraham.World.Sky.Progress",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamSkyProgressTest::RunTest(const FString& Parameters)
{
	// Linear progress at 1 / BlendSeconds per second, clamped at both ends.
	TestNearlyEqual(TEXT("half way after ten seconds"),
		AOneBrahamSkyDirector::AdvanceProgress(0.f, true, 10.f, 20.f), 0.5f, 0.001f);
	TestNearlyEqual(TEXT("dusk after twenty"),
		AOneBrahamSkyDirector::AdvanceProgress(0.5f, true, 10.f, 20.f), 1.f, 0.001f);
	TestNearlyEqual(TEXT("and it stays there"),
		AOneBrahamSkyDirector::AdvanceProgress(1.f, true, 5.f, 20.f), 1.f, 0.001f);
	TestNearlyEqual(TEXT("back toward day at the same rate"),
		AOneBrahamSkyDirector::AdvanceProgress(1.f, false, 5.f, 20.f), 0.75f, 0.001f);
	TestNearlyEqual(TEXT("never below day"),
		AOneBrahamSkyDirector::AdvanceProgress(0.1f, false, 30.f, 20.f), 0.f, 0.001f);
	TestNearlyEqual(TEXT("a zero-length blend jumps"),
		AOneBrahamSkyDirector::AdvanceProgress(0.f, true, 0.016f, 0.f), 1.f, 0.001f);
	TestNearlyEqual(TEXT("a negative frame does not run the sun backwards"),
		AOneBrahamSkyDirector::AdvanceProgress(0.4f, true, -1.f, 20.f), 0.4f, 0.001f);

	// Eased: still at the ends, symmetric about the middle.
	TestNearlyEqual(TEXT("ease starts at day"), AOneBrahamSkyDirector::EaseProgress(0.f), 0.f, 0.0001f);
	TestNearlyEqual(TEXT("ease ends at dusk"), AOneBrahamSkyDirector::EaseProgress(1.f), 1.f, 0.0001f);
	TestNearlyEqual(TEXT("ease is half at half"), AOneBrahamSkyDirector::EaseProgress(0.5f), 0.5f, 0.0001f);
	TestTrue(TEXT("ease starts gently"), AOneBrahamSkyDirector::EaseProgress(0.1f) < 0.1f);
	TestTrue(TEXT("and settles gently"), AOneBrahamSkyDirector::EaseProgress(0.9f) > 0.9f);
	TestNearlyEqual(TEXT("ease is symmetric"),
		AOneBrahamSkyDirector::EaseProgress(0.25f) + AOneBrahamSkyDirector::EaseProgress(0.75f), 1.f, 0.0001f);
	TestNearlyEqual(TEXT("ease clamps"), AOneBrahamSkyDirector::EaseProgress(3.f), 1.f, 0.0001f);

	// What the director does with the stage it sees each tick.
	using EStage = EOneBrahamLockdownStage;
	const EStage Purge = EStage::Purge;
	TestNearlyEqual(TEXT("a late joiner's first look at the purge is already dusk"),
		AOneBrahamSkyDirector::StepProgress(0.f, true, Purge, Purge, 0.016f, 20.f), 1.f, 0.001f);
	TestNearlyEqual(TEXT("a first look before the purge is day"),
		AOneBrahamSkyDirector::StepProgress(0.7f, true, EStage::Lockdown, Purge, 0.016f, 20.f), 0.f, 0.001f);
	TestNearlyEqual(TEXT("the purge arriving mid-match blends rather than snaps"),
		AOneBrahamSkyDirector::StepProgress(0.f, false, Purge, Purge, 1.f, 20.f), 0.05f, 0.001f);
	TestNearlyEqual(TEXT("dropping out of the purge blends back"),
		AOneBrahamSkyDirector::StepProgress(1.f, false, EStage::Lockdown, Purge, 1.f, 20.f), 0.95f, 0.001f);
	TestNearlyEqual(TEXT("a reset to normal snaps to day"),
		AOneBrahamSkyDirector::StepProgress(1.f, false, EStage::Normal, Purge, 0.016f, 20.f), 0.f, 0.001f);
	TestNearlyEqual(TEXT("the pre-lockdown is still day"),
		AOneBrahamSkyDirector::StepProgress(0.f, false, EStage::PreLockdown, Purge, 5.f, 20.f), 0.f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamSkyBlendTest, "OneBraham.World.Sky.Blend",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamSkyBlendTest::RunTest(const FString& Parameters)
{
	// Light falls evenly in stops: the geometric mean at half-way.
	TestNearlyEqual(TEXT("8000 to 2500 lux is 4472 at half"),
		AOneBrahamSkyDirector::BlendLuminance(8000.f, 2500.f, 0.5f), 4472.1f, 0.5f);
	TestNearlyEqual(TEXT("dome 2500 to 900 is 1500 at half"),
		AOneBrahamSkyDirector::BlendLuminance(2500.f, 900.f, 0.5f), 1500.f, 0.5f);
	TestNearlyEqual(TEXT("a zero end blends linearly"),
		AOneBrahamSkyDirector::BlendLuminance(0.f, 10.f, 0.5f), 5.f, 0.001f);

	const FOneBrahamSkyState Day;   // the day preset's defaults
	const FOneBrahamSkyState Dusk = GetDefault<AOneBrahamSkyDirector>()->DuskState;

	// The ends are the presets exactly.
	const FOneBrahamSkyState AtDay = AOneBrahamSkyDirector::BlendSkyStates(Day, Dusk, 0.f);
	const FOneBrahamSkyState AtDusk = AOneBrahamSkyDirector::BlendSkyStates(Day, Dusk, 1.f);
	TestNearlyEqual(TEXT("day sun"), AtDay.SunIntensityLux, Day.SunIntensityLux, 0.01f);
	TestNearlyEqual(TEXT("dusk sun"), AtDusk.SunIntensityLux, Dusk.SunIntensityLux, 0.01f);
	TestNearlyEqual(TEXT("dusk dome"), AtDusk.DomeLuminance, 900.f, 0.01f);
	TestTrue(TEXT("dusk sun angle"), AtDusk.SunRotation.Equals(Dusk.SunRotation, 0.01f));
	TestTrue(TEXT("alpha past one holds dusk"),
		AOneBrahamSkyDirector::BlendSkyStates(Day, Dusk, 2.f).SunRotation.Equals(Dusk.SunRotation, 0.01f));

	// The dusk preset matches build_district.SKY_PRESETS["dusk"].
	TestTrue(TEXT("the dusk sun is low"), Dusk.SunRotation.Pitch > -5.f && Dusk.SunRotation.Pitch < 0.f);
	TestTrue(TEXT("the dusk light is warm"), Dusk.SunColour.R > Dusk.SunColour.G && Dusk.SunColour.G > Dusk.SunColour.B);
	TestTrue(TEXT("dusk is dimmer than day"), Dusk.SunIntensityLux < Day.SunIntensityLux);
	TestTrue(TEXT("the dusk haze is amber"), Dusk.DomeHaze.R > Dusk.DomeHaze.B);
	// The fog's inscattering is the dome's haze at the dome's luminance, at both ends.
	TestNearlyEqual(TEXT("dusk fog matches the dusk horizon"),
		Dusk.FogInscatteringLuminance.R, Dusk.DomeHaze.R * Dusk.DomeLuminance, 10.f);
	TestNearlyEqual(TEXT("dusk fog green matches"),
		Dusk.FogInscatteringLuminance.G, Dusk.DomeHaze.G * Dusk.DomeLuminance, 10.f);
	TestNearlyEqual(TEXT("dusk fog blue matches"),
		Dusk.FogInscatteringLuminance.B, Dusk.DomeHaze.B * Dusk.DomeLuminance, 10.f);

	// Half-way: everything between the ends, and the sun on one arc.
	const FOneBrahamSkyState Half = AOneBrahamSkyDirector::BlendSkyStates(Day, Dusk, 0.5f);
	TestTrue(TEXT("sun pitch between"), Half.SunRotation.Pitch < Dusk.SunRotation.Pitch
		&& Half.SunRotation.Pitch > Day.SunRotation.Pitch);
	const FVector DayDir = Day.SunRotation.Vector();
	const FVector DuskDir = Dusk.SunRotation.Vector();
	const FVector HalfDir = Half.SunRotation.Vector();
	const float ToDay = FMath::RadiansToDegrees(FMath::Acos(FVector::DotProduct(HalfDir, DayDir)));
	const float ToDusk = FMath::RadiansToDegrees(FMath::Acos(FVector::DotProduct(HalfDir, DuskDir)));
	TestNearlyEqual(TEXT("the half-way sun is half-way along the arc"), ToDay, ToDusk, 0.1f);
	TestNearlyEqual(TEXT("half-way colour is the mean"), Half.SunColour.G, (Day.SunColour.G + Dusk.SunColour.G) * 0.5f, 0.001f);
	TestNearlyEqual(TEXT("half-way fog density is the mean"), Half.FogDensity, (Day.FogDensity + Dusk.FogDensity) * 0.5f, 1.e-6f);
	TestTrue(TEXT("half-way fog luminance between"), Half.FogInscatteringLuminance.G < Day.FogInscatteringLuminance.G
		&& Half.FogInscatteringLuminance.G > Dusk.FogInscatteringLuminance.G);

	// Monotonic: the sun only ever sinks and dims on the way to dusk.
	float LastLux = Day.SunIntensityLux + 1.f;
	float LastPitch = Day.SunRotation.Pitch - 1.f;
	bool bMonotonic = true;
	for (int32 Step = 0; Step <= 20; ++Step)
	{
		const FOneBrahamSkyState S = AOneBrahamSkyDirector::BlendSkyStates(Day, Dusk, Step / 20.f);
		bMonotonic &= S.SunIntensityLux < LastLux && S.SunRotation.Pitch > LastPitch;
		LastLux = S.SunIntensityLux;
		LastPitch = S.SunRotation.Pitch;
	}
	TestTrue(TEXT("the sun sinks and dims without a step back"), bMonotonic);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
