// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamDisplayTests.cpp
// ---------------------------------------------------------------------------
// The OLED screens' timing: which slide shows when, and how the copy fades.
// A screen whose copy pops instead of fading, or that shows slide two for a
// frame at the wrong moment, is the kind of detail that makes a lobby look
// like a placeholder.
//
// And when the building speaks: the override's timing, how a tannoy sentence
// is fitted to a panel, which screens carry it, and what it says.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "World/OneBrahamDisplay.h"
#include "World/OneBrahamDisplayDirector.h"
#include "UI/OneBrahamUIStyle.h"

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamDisplayOverrideTimingTest, "OneBraham.World.DisplayOverride",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamDisplayOverrideTimingTest::RunTest(const FString& Parameters)
{
	// The field turns to the building's colours, holds, and turns back.
	const double Start = 10.0;
	const double End = 18.0;
	TestEqual(TEXT("no override, no weight"), AOneBrahamDisplay::OverrideFieldWeightAt(12.0, -1.0, -1.0, 0.8f), 0.f);
	TestEqual(TEXT("nothing before it speaks"), AOneBrahamDisplay::OverrideFieldWeightAt(9.9, Start, End, 0.8f), 0.f);
	TestNearlyEqual(TEXT("all of the field once it has turned"),
		AOneBrahamDisplay::OverrideFieldWeightAt(Start + AOneBrahamDisplay::OverrideFieldSeconds, Start, End, 0.8f), 1.f, 0.001f);
	TestNearlyEqual(TEXT("still all of it at the end of the hold"), AOneBrahamDisplay::OverrideFieldWeightAt(End - 0.01, Start, End, 0.8f), 1.f, 0.001f);
	const float Releasing = AOneBrahamDisplay::OverrideFieldWeightAt(End + 0.4, Start, End, 0.8f);
	TestTrue(TEXT("handing back after it lapses"), Releasing > 0.f && Releasing < 1.f);
	TestEqual(TEXT("gone once the fade is done"), AOneBrahamDisplay::OverrideFieldWeightAt(End + 0.9, Start, End, 0.8f), 0.f);

	// The copy comes up fast (a cut, softened) and is gone by the end of the hold.
	TestEqual(TEXT("copy hidden at the push"), AOneBrahamDisplay::OverrideCopyAlphaAt(Start, Start, End, 0.5f), 0.f);
	TestNearlyEqual(TEXT("copy up within the fade-in"),
		AOneBrahamDisplay::OverrideCopyAlphaAt(Start + AOneBrahamDisplay::OverrideFadeInSeconds, Start, End, 0.5f), 1.f, 0.001f);
	TestTrue(TEXT("the fade-in is quick"), AOneBrahamDisplay::OverrideFadeInSeconds <= 0.3f);
	TestNearlyEqual(TEXT("copy half out a quarter second before the end"),
		AOneBrahamDisplay::OverrideCopyAlphaAt(End - 0.25, Start, End, 0.5f), 0.5f, 0.01f);
	TestEqual(TEXT("copy gone at the end"), AOneBrahamDisplay::OverrideCopyAlphaAt(End, Start, End, 0.5f), 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamDisplayFitTest, "OneBraham.World.DisplayFit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamDisplayFitTest::RunTest(const FString& Parameters)
{
	// Wrapping: as many lines as asked, every word kept, the rag level.
	const FString Line = TEXT("Fire shutters are closing on all floors below 5.");
	TestEqual(TEXT("one line is the line"), AOneBrahamDisplay::WrapToLines(Line, 1), Line);
	const FString Two = AOneBrahamDisplay::WrapToLines(Line, 2);
	TArray<FString> TwoLines;
	Two.ParseIntoArray(TwoLines, TEXT("\n"));
	TestEqual(TEXT("two lines"), TwoLines.Num(), 2);
	if (TwoLines.Num() == 2)
	{
		TestTrue(TEXT("the two lines are within a word of each other"), FMath::Abs(TwoLines[0].Len() - TwoLines[1].Len()) <= 9);
	}
	TestEqual(TEXT("no word lost"), Two.Replace(TEXT("\n"), TEXT(" ")), Line);
	TArray<FString> ThreeLines;
	AOneBrahamDisplay::WrapToLines(Line, 3).ParseIntoArray(ThreeLines, TEXT("\n"));
	TestEqual(TEXT("three lines"), ThreeLines.Num(), 3);
	TestEqual(TEXT("never more lines than words"), AOneBrahamDisplay::WrapToLines(TEXT("Lunch is"), 3), FString(TEXT("Lunch\nis")));
	TestEqual(TEXT("one word is one line"), AOneBrahamDisplay::WrapToLines(TEXT("EXTRACTED"), 3), FString(TEXT("EXTRACTED")));

	// Word spaces: an offline font's space is too narrow, so lone spaces double;
	// the eyebrow's spaced middots and the line breaks are left as they are.
	TestEqual(TEXT("lone spaces double"), AOneBrahamDisplay::OpenWordSpaces(TEXT("The lift lobby is open.")),
		FString(TEXT("The  lift  lobby  is  open.")));
	TestEqual(TEXT("runs of spaces stay"), AOneBrahamDisplay::OpenWordSpaces(TEXT("ONE  \u00B7  ALL")), FString(TEXT("ONE  \u00B7  ALL")));
	TestEqual(TEXT("breaks stay breaks"), AOneBrahamDisplay::OpenWordSpaces(TEXT("Fire shutters\nare closing")),
		FString(TEXT("Fire  shutters\nare  closing")));
	TestEqual(TEXT("one word is untouched"), AOneBrahamDisplay::OpenWordSpaces(TEXT("EXTRACTED")), FString(TEXT("EXTRACTED")));

	// Fitting: shrink to the measure, never grow; wrap only when it pays.
	float Scale = 0.f;
	TestEqual(TEXT("a line that fits stays one line"),
		AOneBrahamDisplay::ChooseFit({ FVector2D(400.0, 50.0), FVector2D(220.0, 100.0) }, 500.f, 300.f, Scale), 0);
	TestEqual(TEXT("at its own size"), Scale, 1.f);

	TestEqual(TEXT("a line twice the measure wraps"),
		AOneBrahamDisplay::ChooseFit({ FVector2D(1000.0, 50.0), FVector2D(520.0, 100.0) }, 500.f, 300.f, Scale), 1);
	TestNearlyEqual(TEXT("and shrinks only what the wrap does not cover"), Scale, 500.f / 520.f, 0.001f);

	// Two lines squeezed into a box barely taller than one come out barely
	// bigger (0.52 against 0.5): not worth the break.
	TestEqual(TEXT("no room for a second line: shrink the first"),
		AOneBrahamDisplay::ChooseFit({ FVector2D(1000.0, 50.0), FVector2D(520.0, 100.0) }, 500.f, 52.f, Scale), 0);
	TestNearlyEqual(TEXT("to the measure"), Scale, 0.5f, 0.001f);
	// With a little more room the wrap pays (0.6 against 0.5).
	TestEqual(TEXT("room enough and the wrap wins"),
		AOneBrahamDisplay::ChooseFit({ FVector2D(1000.0, 50.0), FVector2D(520.0, 100.0) }, 500.f, 60.f, Scale), 1);
	TestNearlyEqual(TEXT("at the height's scale"), Scale, 0.6f, 0.001f);

	TestEqual(TEXT("an empty measure is the line as it is"), AOneBrahamDisplay::ChooseFit({ FVector2D(0.0, 0.0) }, 500.f, 60.f, Scale), 0);
	TestEqual(TEXT("unscaled"), Scale, 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamDisplayDirectorRulesTest, "OneBraham.World.DisplayDirector",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamDisplayDirectorRulesTest::RunTest(const FString& Parameters)
{
	// What the screens say: the point of the paragraph, then the next sentence.
	const FString Lockdown = TEXT("Fire shutters are closing on all floors below 5. Lifts are out of service. "
		"Please use the stairwell nearest to you.");
	TestEqual(TEXT("headline: the first sentence"), UOneBrahamDisplayDirector::HeadlineFor(Lockdown),
		FString(TEXT("Fire shutters are closing on all floors below 5.")));
	TestEqual(TEXT("subline: the second"), UOneBrahamDisplayDirector::SublineFor(Lockdown), FString(TEXT("Lifts are out of service.")));
	TestEqual(TEXT("the building's throat-clearing is not the point"),
		UOneBrahamDisplayDirector::HeadlineFor(TEXT("Attention all staff. The lift lobby is open. Ascend.")),
		FString(TEXT("The lift lobby is open.")));
	TestEqual(TEXT("one sentence is all headline"), UOneBrahamDisplayDirector::HeadlineFor(TEXT("Welcome to One Braham")),
		FString(TEXT("Welcome to One Braham")));
	TestEqual(TEXT("and has no subline"), UOneBrahamDisplayDirector::SublineFor(TEXT("Welcome to One Braham.")), FString());
	TestEqual(TEXT("a short stop is not a sentence"), UOneBrahamDisplayDirector::HeadlineFor(TEXT("St. Mary Axe is closed.")),
		FString(TEXT("St. Mary Axe is closed.")));

	// How long: the tannoy's reading time, within bounds.
	TestEqual(TEXT("a short line holds as long as the subtitle"), UOneBrahamDisplayDirector::HoldSecondsFor(TEXT("Ascend.")), 6.f);
	const float Long = UOneBrahamDisplayDirector::HoldSecondsFor(Lockdown);
	TestTrue(TEXT("a paragraph holds longer"), Long > 6.f && Long <= 14.f);
	TestEqual(TEXT("never a fixture"), UOneBrahamDisplayDirector::HoldSecondsFor(FString::ChrN(400, TEXT('a')).Replace(TEXT("aaaa"), TEXT("aaa "))), 14.f);

	// The eyebrow: the building, every floor, the dusk clock.
	TestEqual(TEXT("eyebrow"), UOneBrahamDisplayDirector::EyebrowFor(FText::FromString(TEXT("17:33"))).ToString(),
		FString(TEXT("ONE BRAHAM  \u00B7  ALL FLOORS  \u00B7  17:33")));

	// The colours: the HUD's rule, so the card and the screens agree.
	FLinearColor PreA, PreB, LockA, LockB, CalmA, CalmB, PurgeA, PurgeB;
	UOneBrahamDisplayDirector::StageColours(EOneBrahamLockdownStage::PreLockdown, PreA, PreB);
	UOneBrahamDisplayDirector::StageColours(EOneBrahamLockdownStage::Lockdown, LockA, LockB);
	UOneBrahamDisplayDirector::StageColours(EOneBrahamLockdownStage::Normal, CalmA, CalmB);
	UOneBrahamDisplayDirector::StageColours(EOneBrahamLockdownStage::Purge, PurgeA, PurgeB);
	for (const EOneBrahamLockdownStage Stage : { EOneBrahamLockdownStage::Normal, EOneBrahamLockdownStage::PreLockdown,
		EOneBrahamLockdownStage::Lockdown, EOneBrahamLockdownStage::Purge })
	{
		for (const bool bEvac : { false, true })
		{
			FLinearColor ScreenA, ScreenB, CardA, CardB;
			UOneBrahamDisplayDirector::StageColours(Stage, ScreenA, ScreenB, bEvac);
			UOneBrahamUIStyle::ToneField(UOneBrahamUIStyle::StageTone(Stage, bEvac), CardA, CardB);
			TestTrue(FString::Printf(TEXT("the screens wear the PA card's field (%s%s)"), *UEnum::GetValueAsString(Stage),
				bEvac ? TEXT(", evacuating") : TEXT("")), ScreenA.Equals(CardA) && ScreenB.Equals(CardB));
		}
	}
	TestFalse(TEXT("pre-lockdown is its own warning, not yet the lockdown"), PreA.Equals(LockA));
	TestFalse(TEXT("calm is not lockdown"), CalmA.Equals(LockA));
	TestFalse(TEXT("lockdown is not purge"), LockA.Equals(PurgeA));
	TestTrue(TEXT("lockdown is yellow"), LockA.R > 0.5f && LockA.G > 0.3f && LockA.B < 0.2f);
	TestTrue(TEXT("purge is red"), PurgeA.R > PurgeA.G * 2.f && PurgeA.R > PurgeA.B * 2.f);
	TestTrue(TEXT("calm is blue"), CalmB.B > CalmB.R && CalmB.B > CalmB.G);

	// Who carries it: the viewer's storey, and anything near.
	const FVector Eye(0.0, 0.0, 170.0);
	const FBox Reception(FVector(2000.0, -500.0, 430.0), FVector(2010.0, 500.0, 690.0));
	TestTrue(TEXT("a screen over the desk is on my floor"),
		UOneBrahamDisplayDirector::IsInEarshot(Eye, Reception, UOneBrahamDisplayDirector::FloorBandCm, UOneBrahamDisplayDirector::NearCm));
	const FBox FarSameFloor(FVector(6000.0, 0.0, 100.0), FVector(6010.0, 300.0, 300.0));
	TestTrue(TEXT("so is one across the floor plate"),
		UOneBrahamDisplayDirector::IsInEarshot(Eye, FarSameFloor, UOneBrahamDisplayDirector::FloorBandCm, UOneBrahamDisplayDirector::NearCm));
	const FBox Canteen(FVector(0.0, 0.0, 15.0 * 400.0 + 160.0), FVector(10.0, 340.0, 15.0 * 400.0 + 310.0));
	TestFalse(TEXT("the canteen fifteen floors up is not"),
		UOneBrahamDisplayDirector::IsInEarshot(Eye, Canteen, UOneBrahamDisplayDirector::FloorBandCm, UOneBrahamDisplayDirector::NearCm));
	const FBox FarFloorBelow(FVector(6000.0, 0.0, -400.0 + 150.0), FVector(6010.0, 300.0, -400.0 + 330.0));
	TestFalse(TEXT("one across the floor plate below, behind the slab, is not"),
		UOneBrahamDisplayDirector::IsInEarshot(Eye, FarFloorBelow, UOneBrahamDisplayDirector::FloorBandCm, UOneBrahamDisplayDirector::NearCm));
	const FBox FarFloorAbove(FVector(6000.0, 0.0, 400.0 + 150.0), FVector(6010.0, 300.0, 400.0 + 330.0));
	TestFalse(TEXT("nor one across the floor plate above"),
		UOneBrahamDisplayDirector::IsInEarshot(Eye, FarFloorAbove, UOneBrahamDisplayDirector::FloorBandCm, UOneBrahamDisplayDirector::NearCm));
	const FBox LowOnMyFloor(FVector(6000.0, 0.0, 40.0), FVector(6010.0, 300.0, 120.0));
	TestTrue(TEXT("a low screen on my floor is"),
		UOneBrahamDisplayDirector::IsInEarshot(Eye, LowOnMyFloor, UOneBrahamDisplayDirector::FloorBandCm, UOneBrahamDisplayDirector::NearCm));
	const FBox UpTheEscalator(FVector(300.0, 0.0, 600.0), FVector(310.0, 400.0, 900.0));
	TestTrue(TEXT("the screen at the top of the escalator, in sight, is"),
		UOneBrahamDisplayDirector::IsInEarshot(Eye, UpTheEscalator, UOneBrahamDisplayDirector::FloorBandCm, UOneBrahamDisplayDirector::NearCm));
	TestFalse(TEXT("an empty box is nowhere"),
		UOneBrahamDisplayDirector::IsInEarshot(Eye, FBox(ForceInit), UOneBrahamDisplayDirector::FloorBandCm, UOneBrahamDisplayDirector::NearCm));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
