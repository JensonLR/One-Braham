// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamUITests.cpp
// ---------------------------------------------------------------------------
// The HUD's two shared rules:
//   * the centre of the screen holds one card at a time (stage card > PA card
//     > notice > a refused reader > the induction), the rest shrink to one
//     ticker line or wait, the crosshair's band stays clear, and the subtitle
//     never repeats a card;
//   * one stage-colour rule (UOneBrahamUIStyle::StageTone) for the menu, the
//     HUD and the building's screens.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"

#include "UI/HUD/OneBrahamCentreMessages.h"
#include "UI/HUD/OneBrahamHUDPaint.h"
#include "UI/OneBrahamHUDWidget.h"
#include "UI/OneBrahamUIStyle.h"
#include "Core/OneBrahamGameUserSettings.h"

#if WITH_AUTOMATION_TESTS

namespace OneBrahamUITestsLocal
{
	using namespace OneBrahamCentre;

	uint32 Mask(std::initializer_list<EOneBrahamCentreSlot> Slots)
	{
		uint32 Out = 0;
		for (const EOneBrahamCentreSlot Slot : Slots)
		{
			Out |= Bit(Slot);
		}
		return Out;
	}

	float Distance(const FLinearColor& A, const FLinearColor& B)
	{
		const FColor X = A.ToFColor(true);
		const FColor Y = B.ToFColor(true);
		return FVector(X.R - Y.R, X.G - Y.G, X.B - Y.B).Size() / 255.f;
	}
}

// ==========================================================================
//  The priority queue
// ==========================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamUICentrePriorityTest, "OneBraham.UI.CentreMessages.Priority",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamUICentrePriorityTest::RunTest(const FString& Parameters)
{
	using namespace OneBrahamUITestsLocal;
	using S = EOneBrahamCentreSlot;
	using Show = EOneBrahamCentreShow;

	// The screenshot that started it: a stage card, a PA card, a refused reader
	// and the induction, all at once.
	{
		const FOneBrahamCentreLayout L = Resolve(Mask({ S::StageCard, S::Announcement, S::BlockedPrompt, S::Induction }));
		TestEqual(TEXT("the stage card holds the centre"), L.Get(S::StageCard), Show::Card);
		TestEqual(TEXT("the PA card waits for it (its queue)"), L.Get(S::Announcement), Show::Wait);
		TestEqual(TEXT("the refused reader shrinks to the ticker"), L.Get(S::BlockedPrompt), Show::Ticker);
		TestEqual(TEXT("the induction waits"), L.Get(S::Induction), Show::Wait);
	}
	{
		const FOneBrahamCentreLayout L = Resolve(Mask({ S::Announcement, S::BlockedPrompt, S::Induction }));
		TestEqual(TEXT("with the stage card gone, the PA card is the card"), L.Get(S::Announcement), Show::Card);
		TestEqual(TEXT("the refused reader is the ticker"), L.Get(S::BlockedPrompt), Show::Ticker);
		TestEqual(TEXT("the induction still waits"), L.Get(S::Induction), Show::Wait);
	}
	{
		const FOneBrahamCentreLayout L = Resolve(Mask({ S::BlockedPrompt, S::Induction }));
		TestEqual(TEXT("the refused reader is a card on its own"), L.Get(S::BlockedPrompt), Show::Card);
		TestEqual(TEXT("and the induction is the ticker under it"), L.Get(S::Induction), Show::Ticker);
	}
	{
		const FOneBrahamCentreLayout L = Resolve(Mask({ S::StageCard, S::Banner, S::Induction }));
		TestEqual(TEXT("a notice under a stage card is the ticker"), L.Get(S::Banner), Show::Ticker);
		TestEqual(TEXT("and the induction waits behind it"), L.Get(S::Induction), Show::Wait);
	}
	{
		const FOneBrahamCentreLayout L = Resolve(Mask({ S::Downed, S::StageCard, S::Announcement, S::Induction }));
		TestEqual(TEXT("your own life outranks the building"), L.Get(S::Downed), Show::Card);
		TestEqual(TEXT("the stage card's word still reaches you, as a line"), L.Get(S::StageCard), Show::Ticker);
		TestEqual(TEXT("down, the induction is not offered at all"), L.Get(S::Induction), Show::Wait);
	}
	{
		const FOneBrahamCentreLayout L = Resolve(Mask({ S::Downed, S::Induction, S::BlockedPrompt }));
		TestEqual(TEXT("down, instructions the player cannot follow never tick"), L.Count(Show::Ticker), 0);
	}
	{
		const FOneBrahamCentreLayout L = Resolve(Mask({ S::Announcement, S::Induction }));
		TestEqual(TEXT("the induction under a PA card shrinks"), L.Get(S::Induction), Show::Ticker);
	}
	TestEqual(TEXT("an empty centre is empty"), Resolve(0).Count(Show::Off), static_cast<int32>(S::Count));

	// Every combination: never two cards, never two tickers, the card is the
	// loudest request, a PA card never shrinks, and nothing asked is lost.
	const uint32 All = 1u << static_cast<uint32>(S::Count);
	for (uint32 M = 0; M < All; ++M)
	{
		const FOneBrahamCentreLayout L = Resolve(M);
		const FString Tag = FString::Printf(TEXT("mask %u"), M);
		TestTrue(*(Tag + TEXT(": at most one card")), L.Count(Show::Card) <= 1);
		TestTrue(*(Tag + TEXT(": at most one ticker")), L.Count(Show::Ticker) <= 1);
		TestEqual(*(Tag + TEXT(": a card whenever anything asks")), L.Count(Show::Card), M != 0 ? 1 : 0);
		TestTrue(*(Tag + TEXT(": no ticker without a card")), L.Count(Show::Ticker) == 0 || L.Count(Show::Card) == 1);
		TestFalse(*(Tag + TEXT(": the PA card never shrinks")), L.IsTicker(S::Announcement));

		int32 Loudest = INDEX_NONE;
		for (int32 Index = 0; Index < static_cast<int32>(S::Count); ++Index)
		{
			const bool bAsked = (M & (1u << Index)) != 0;
			const Show Shown = L.Show[Index];
			TestTrue(*(Tag + TEXT(": asked is shown or waiting, not asked is off")), (Shown != Show::Off) == bAsked);
			if (bAsked && Loudest == INDEX_NONE)
			{
				Loudest = Index;
			}
		}
		if (Loudest != INDEX_NONE)
		{
			TestEqual(*(Tag + TEXT(": the card is the loudest request")), L.Show[Loudest], Show::Card);
		}
	}
	return true;
}

// ==========================================================================
//  The layout: the crosshair's band stays clear
// ==========================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamUICentreLayoutTest, "OneBraham.UI.CentreMessages.Layout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamUICentreLayoutTest::RunTest(const FString& Parameters)
{
	using namespace OneBrahamCentre;

	// In 1080p units (the HUD's geometry is DPI-scaled to them at any
	// resolution), at the largest HUD Scale the setting allows.
	for (const float Scale : { 0.8f, 1.f, 1.2f })
	{
		const float H = 1080.f;
		const float BandTop = CrosshairBandTop * H;
		const float BandBottom = CrosshairBandBottom * H;
		const FString At = FString::Printf(TEXT(" (HUD scale %.1f)"), Scale);

		TestTrue(TEXT("the band is a band"), BandTop < H * 0.5f && BandBottom > H * 0.5f);
		TestTrue(*(TEXT("the stage card ends above the band") + At), StageCardTop * H + StageCardHeightPx * Scale <= BandTop);
		TestTrue(*(TEXT("the PA card at its top ends above the band") + At), AnnouncementTop * H + AnnouncementHeightPx * Scale <= BandTop);
		TestTrue(*(TEXT("the notice ends above the band") + At), BannerY * H + BannerHeightPx * Scale <= BandTop);
		TestTrue(*(TEXT("the reader starts below the band") + At), PromptY * H - PromptHalfHeightPx * Scale >= BandBottom);
		TestTrue(*(TEXT("the induction sits below the band") + At), InductionY * H - 28.f * Scale >= BandBottom);
		TestTrue(*(TEXT("the ticker sits below the band") + At), TickerY * H - 12.f * Scale >= BandBottom);
		TestTrue(*(TEXT("the downed medallion ends above the band") + At), DownedY * H + DownedRadiusPx * Scale <= BandTop);
		TestTrue(*(TEXT("the reader and the induction do not touch") + At),
			PromptY * H + PromptHalfHeightPx * Scale <= InductionY * H - 22.f * Scale);
	}
	return true;
}

// ==========================================================================
//  The subtitle never repeats a card
// ==========================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamUICentreSubtitleTest, "OneBraham.UI.CentreMessages.Subtitle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamUICentreSubtitleTest::RunTest(const FString& Parameters)
{
	using namespace OneBrahamCentre;

	TestEqual(TEXT("words: lower case, no punctuation"), Words(TEXT("  The Lift-lobby is CLOSED.  ")), FString(TEXT("the lift lobby is closed")));
	TestEqual(TEXT("words: an apostrophe joins"), Words(TEXT("They've done it.")), FString(TEXT("theyve done it")));

	const FString Card = TEXT("The lift lobby is closed. Please use the stairs.");
	TestTrue(TEXT("the same words as a card: nothing left to say"),
		SubtitleRemainder(TEXT("The lift lobby is closed. Please use the stairs."), { Card }).IsEmpty());
	TestTrue(TEXT("punctuation and case do not make it new"),
		SubtitleRemainder(TEXT("the lift lobby is closed - please use the stairs!"), { Card }).IsEmpty());

	// The tannoy's paragraph whose first sentence is the PA card's headline.
	const FString Paragraph = TEXT("Attention all staff. Fire shutters are closing below Level 6. Lifts are out of service.");
	TestEqual(TEXT("the card's sentence (and the throat-clearing) goes, the news stays"),
		SubtitleRemainder(Paragraph, { TEXT("Fire shutters are closing below Level 6."), TEXT("The lifts are thinking about what they've done.") }),
		FString(TEXT("Attention all staff. Lifts are out of service.")));
	TestTrue(TEXT("a paragraph that is only the preamble and the card is not drawn"),
		SubtitleRemainder(TEXT("Attention all staff. Fire shutters are closing below Level 6."), { TEXT("Fire shutters are closing below Level 6.") }).IsEmpty());

	const FString Squad = TEXT("Contact, two. Coming up the east stair.");
	TestEqual(TEXT("a line no card says is untouched"), SubtitleRemainder(Squad, { Card }), Squad);
	TestEqual(TEXT("no cards, no change"), SubtitleRemainder(Squad, {}), Squad);
	TestEqual(TEXT("similar is not the same"),
		SubtitleRemainder(TEXT("Fire shutters are closing on all floors below 6."), { TEXT("Fire shutters are closing below Level 6.") }),
		FString(TEXT("Fire shutters are closing on all floors below 6.")));

	// The widget: a stage card up, a PA line pushed under it, the induction's
	// step, and the tannoy saying the card's own words.
	UOneBrahamHUDWidget* HUD = NewObject<UOneBrahamHUDWidget>(GetTransientPackage());
	if (TestNotNull(TEXT("a HUD widget"), HUD))
	{
		FOneBrahamHUDFrame Frame;
		Frame.DuskClock = TEXT("18:06");
		HUD->ApplyHUDFrame(Frame);
		HUD->ShowStageCard(EOneBrahamStageCard::Lockdown);
		HUD->PushAnnouncement(FText::FromString(TEXT("Fire shutters are closing below Level 6.")), FText::GetEmpty(),
			EOneBrahamLockdownStage::Lockdown, 6.f);
		HUD->ShowSubBanner(FText::FromString(TEXT("INDUCTION · Walk in from the street - W A S D")), 6.f);

		const double Now = FPlatformTime::Seconds() + 0.5;
		const FOneBrahamCentreLayout Centre = HUD->ResolveCentre(Now);
		TestTrue(TEXT("widget: the stage card is the card"), Centre.IsCard(EOneBrahamCentreSlot::StageCard));
		TestFalse(TEXT("widget: the PA card is not up under it"), Centre.IsShown(EOneBrahamCentreSlot::Announcement));
		TestTrue(TEXT("widget: the induction is the ticker"), Centre.IsTicker(EOneBrahamCentreSlot::Induction));
		TestEqual(TEXT("widget: one card"), Centre.Count(EOneBrahamCentreShow::Card), 1);

		const TArray<FString> Lines = HUD->CentreCardLines(Centre);
		TestTrue(TEXT("widget: a subtitle of the card's own line is dropped"),
			SubtitleRemainder(TEXT("The lift lobby is closed. Please use the stairs."), Lines).IsEmpty());

		const FOneBrahamCentreLayout Later = HUD->ResolveCentre(HUD->StageCardEnd() + 0.1);
		TestFalse(TEXT("widget: after the card, the stage card is gone"), Later.IsShown(EOneBrahamCentreSlot::StageCard));
	}
	return true;
}

// ==========================================================================
//  A stage escalation makes the calmer PA cards stale
// ==========================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamUICentreStaleTest, "OneBraham.UI.CentreMessages.StaleAnnouncements",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamUICentreStaleTest::RunTest(const FString& Parameters)
{
	UOneBrahamHUDWidget* HUD = NewObject<UOneBrahamHUDWidget>(GetTransientPackage());
	if (!TestNotNull(TEXT("a HUD widget"), HUD))
	{
		return false;
	}
	FOneBrahamHUDFrame Frame;
	Frame.DuskClock = TEXT("18:06");
	Frame.Building.Stage = EOneBrahamLockdownStage::Normal;
	HUD->ApplyHUDFrame(Frame);

	// The calm's card is up, and a second calm card waits behind it.
	HUD->PushAnnouncement(FText::FromString(TEXT("The lift lobby is open. Ascend.")), FText::GetEmpty(), EOneBrahamLockdownStage::Normal, 6.f);
	HUD->PushAnnouncement(FText::FromString(TEXT("The canteen is open.")), FText::GetEmpty(), EOneBrahamLockdownStage::Normal, 6.f);
	TestTrue(TEXT("the calm card is up"), HUD->ResolveCentre(FPlatformTime::Seconds() + 0.2).IsCard(EOneBrahamCentreSlot::Announcement));

	// The shutters start to move: neither calm card may be seen again.
	Frame.Building.Stage = EOneBrahamLockdownStage::PreLockdown;
	HUD->ApplyHUDFrame(Frame);
	const double Later = FPlatformTime::Seconds() + 0.5;
	HUD->ApplyHUDFrame(Frame);
	TestFalse(TEXT("the calm card leaves on the escalation"), HUD->ResolveCentre(Later).IsShown(EOneBrahamCentreSlot::Announcement));

	// A card of the new stage still goes up, once the calm card's 300 ms
	// exit has played (the queue is how it waits).
	HUD->PushAnnouncement(FText::FromString(TEXT("The building will enter lockdown shortly.")), FText::GetEmpty(), EOneBrahamLockdownStage::PreLockdown, 6.f);
	FPlatformProcess::Sleep(0.4f);
	HUD->ApplyHUDFrame(Frame);
	TestTrue(TEXT("the warning's own card goes up"), HUD->ResolveCentre(FPlatformTime::Seconds() + 0.4).IsCard(EOneBrahamCentreSlot::Announcement));
	return true;
}

// ==========================================================================
//  One stage-colour rule
// ==========================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamUIStageColourTest, "OneBraham.UI.StageColour",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamUIStageColourTest::RunTest(const FString& Parameters)
{
	using namespace OneBrahamUITestsLocal;
	using Stage = EOneBrahamLockdownStage;
	using Tone = EOneBrahamStageTone;

	// The mapping.
	TestEqual(TEXT("Normal is the calm"), UOneBrahamUIStyle::StageTone(Stage::Normal), Tone::Calm);
	TestEqual(TEXT("PreLockdown is the warning"), UOneBrahamUIStyle::StageTone(Stage::PreLockdown), Tone::Warning);
	TestEqual(TEXT("Lockdown is the lockdown"), UOneBrahamUIStyle::StageTone(Stage::Lockdown), Tone::Lockdown);
	TestEqual(TEXT("Purge is the purge"), UOneBrahamUIStyle::StageTone(Stage::Purge), Tone::Purge);
	TestEqual(TEXT("an extraction in the calm is the way out"), UOneBrahamUIStyle::StageTone(Stage::Normal, true), Tone::Evac);
	TestEqual(TEXT("an extraction in the lockdown is the way out"), UOneBrahamUIStyle::StageTone(Stage::Lockdown, true), Tone::Evac);
	TestEqual(TEXT("the Purge outranks the way out"), UOneBrahamUIStyle::StageTone(Stage::Purge, true), Tone::Purge);

	// The colours: the tokens the spec names.
	TestEqual(TEXT("calm is signal blue"), UOneBrahamUIStyle::ToneColour(Tone::Calm), UOneBrahamUIStyle::Colour(EOneBrahamColour::SignalBlue));
	TestEqual(TEXT("warning is amber"), UOneBrahamUIStyle::ToneColour(Tone::Warning), UOneBrahamUIStyle::Colour(EOneBrahamColour::WarningAmber));
	TestEqual(TEXT("lockdown is safety yellow"), UOneBrahamUIStyle::ToneColour(Tone::Lockdown), UOneBrahamUIStyle::Colour(EOneBrahamColour::SafetyYellow));
	TestEqual(TEXT("purge is emergency red"), UOneBrahamUIStyle::ToneColour(Tone::Purge), UOneBrahamUIStyle::Colour(EOneBrahamColour::EmergencyRed));
	TestEqual(TEXT("evac is exit green"), UOneBrahamUIStyle::ToneColour(Tone::Evac), UOneBrahamUIStyle::Colour(EOneBrahamColour::EvacGreen));
	TestTrue(TEXT("alpha is honoured"), FMath::IsNearlyEqual(UOneBrahamUIStyle::ToneColour(Tone::Purge, 0.3f).A, 0.3f));

	// Five tones, five colours anyone can tell apart (in the current
	// colour-vision mode: each mode's remap keeps them apart).
	const Tone Tones[] = { Tone::Calm, Tone::Warning, Tone::Lockdown, Tone::Purge, Tone::Evac };
	for (int32 A = 0; A < UE_ARRAY_COUNT(Tones); ++A)
	{
		for (int32 B = A + 1; B < UE_ARRAY_COUNT(Tones); ++B)
		{
			TestTrue(*FString::Printf(TEXT("tones %d and %d are distinct"), A, B),
				Distance(UOneBrahamUIStyle::ToneColour(Tones[A]), UOneBrahamUIStyle::ToneColour(Tones[B])) > 0.12f);
		}
		TestFalse(*FString::Printf(TEXT("tone %d has a word"), A), FString(UOneBrahamUIStyle::ToneWord(Tones[A])).IsEmpty());
	}

	// Every surface goes through the one function.
	for (const Stage S : { Stage::Normal, Stage::PreLockdown, Stage::Lockdown, Stage::Purge })
	{
		const Tone T = UOneBrahamUIStyle::StageTone(S);
		const FString Name = FString::Printf(TEXT("stage %d"), static_cast<int32>(S));
		TestEqual(*(Name + TEXT(": the menu's StageAccent is the rule")), UOneBrahamUIStyle::StageAccent(S), UOneBrahamUIStyle::ToneColour(T));
		TestEqual(*(Name + TEXT(": the HUD's StageAccent is the rule")), OneBrahamHUDStyle::StageAccent(S), UOneBrahamUIStyle::ToneColour(T));
		TestEqual(*(Name + TEXT(": the HUD's hairline is the rule")), OneBrahamHUDStyle::StageHairline(S), UOneBrahamUIStyle::ToneHairline(T));

		FLinearColor FieldA, FieldB, ToneA, ToneB;
		UOneBrahamUIStyle::StageField(S, FieldA, FieldB);
		UOneBrahamUIStyle::ToneField(T, ToneA, ToneB);
		TestEqual(*(Name + TEXT(": the building's screens take the same field")), FieldA, ToneA);
		TestEqual(*(Name + TEXT(": both ends of it")), FieldB, ToneB);
	}

	// The stages that escalate get stronger hairlines than the calm.
	TestTrue(TEXT("the calm hairline is the quietest"),
		UOneBrahamUIStyle::ToneHairline(Tone::Calm).A <= UOneBrahamUIStyle::ToneHairline(Tone::Warning).A
		&& UOneBrahamUIStyle::ToneHairline(Tone::Warning).A <= UOneBrahamUIStyle::ToneHairline(Tone::Purge).A);

	// Type on a field: navy on the light tones, white on the dark ones.
	const FLinearColor Navy = UOneBrahamUIStyle::Colour(EOneBrahamColour::Navy);
	const FLinearColor White = UOneBrahamUIStyle::Colour(EOneBrahamColour::White);
	TestEqual(TEXT("navy on amber"), UOneBrahamUIStyle::ToneInk(Tone::Warning), Navy);
	TestEqual(TEXT("navy on yellow"), UOneBrahamUIStyle::ToneInk(Tone::Lockdown), Navy);
	TestEqual(TEXT("white on red"), UOneBrahamUIStyle::ToneInk(Tone::Purge), White);
	TestEqual(TEXT("white on green"), UOneBrahamUIStyle::ToneInk(Tone::Evac), White);
	TestEqual(TEXT("white on the ribbon"), UOneBrahamUIStyle::ToneInk(Tone::Calm), White);
	return true;
}

// ==========================================================================
//  The colour rule in each colour-vision mode
// ==========================================================================
namespace OneBrahamUITestsLocal
{
	/** CIE L*a*b* of a linear colour (D65), clamped to the gamut. */
	FVector Lab(const FLinearColor& In)
	{
		const float R = FMath::Clamp(In.R, 0.f, 1.f), G = FMath::Clamp(In.G, 0.f, 1.f), B = FMath::Clamp(In.B, 0.f, 1.f);
		const double X = (0.4124 * R + 0.3576 * G + 0.1805 * B) / 0.95047;
		const double Y = 0.2126 * R + 0.7152 * G + 0.0722 * B;
		const double Z = (0.0193 * R + 0.1192 * G + 0.9505 * B) / 1.08883;
		auto F = [](double T) { return T > 0.008856 ? FMath::Pow(T, 1.0 / 3.0) : 7.787 * T + 16.0 / 116.0; };
		return FVector(116.0 * F(Y) - 16.0, 500.0 * (F(X) - F(Y)), 200.0 * (F(Y) - F(Z)));
	}

	/** What an eye with that deficiency sees (Machado et al. 2009, severity 1). */
	FLinearColor Seen(const FLinearColor& C, EOneBrahamColourBlindMode Mode)
	{
		static const float Prot[3][3] = { { 0.152286f, 1.052583f, -0.204868f }, { 0.114503f, 0.786281f, 0.099216f }, { -0.003882f, -0.048116f, 1.051998f } };
		static const float Deut[3][3] = { { 0.367322f, 0.860646f, -0.227968f }, { 0.280085f, 0.672501f, 0.047413f }, { -0.011820f, 0.042940f, 0.968881f } };
		static const float Trit[3][3] = { { 1.255528f, -0.076749f, -0.178779f }, { -0.078411f, 0.930809f, 0.147602f }, { 0.004733f, 0.691367f, 0.303900f } };
		const float (*M)[3] = Mode == EOneBrahamColourBlindMode::Protanopia ? Prot
			: (Mode == EOneBrahamColourBlindMode::Deuteranopia ? Deut : (Mode == EOneBrahamColourBlindMode::Tritanopia ? Trit : nullptr));
		if (M == nullptr)
		{
			return C;
		}
		return FLinearColor(M[0][0] * C.R + M[0][1] * C.G + M[0][2] * C.B,
			M[1][0] * C.R + M[1][1] * C.G + M[1][2] * C.B,
			M[2][0] * C.R + M[2][1] * C.G + M[2][2] * C.B, C.A);
	}

	float Contrast(const FLinearColor& A, const FLinearColor& B)
	{
		const float LA = 0.2126f * A.R + 0.7152f * A.G + 0.0722f * A.B;
		const float LB = 0.2126f * B.R + 0.7152f * B.G + 0.0722f * B.B;
		return (FMath::Max(LA, LB) + 0.05f) / (FMath::Min(LA, LB) + 0.05f);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamUIStageColourBlindTest, "OneBraham.UI.StageColourBlind",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamUIStageColourBlindTest::RunTest(const FString& Parameters)
{
	using namespace OneBrahamUITestsLocal;
	using Tone = EOneBrahamStageTone;

	UOneBrahamGameUserSettings* User = UOneBrahamGameUserSettings::GetOneBrahamSettings();
	if (User == nullptr)
	{
		AddWarning(TEXT("No user settings in this context: the colour-vision modes were not checked."));
		return true;
	}

	const EOneBrahamColourBlindMode Saved = User->ColourBlindMode;
	const EOneBrahamColourBlindMode Modes[] = { EOneBrahamColourBlindMode::Off, EOneBrahamColourBlindMode::Protanopia,
		EOneBrahamColourBlindMode::Deuteranopia, EOneBrahamColourBlindMode::Tritanopia };
	const Tone Tones[] = { Tone::Calm, Tone::Warning, Tone::Lockdown, Tone::Purge, Tone::Evac };
	const TCHAR* ModeNames[] = { TEXT("off"), TEXT("protanopia"), TEXT("deuteranopia"), TEXT("tritanopia") };

	for (int32 M = 0; M < UE_ARRAY_COUNT(Modes); ++M)
	{
		User->ColourBlindMode = Modes[M];

		// Every pair of tones, as that eye sees them, at least a clear step
		// apart (CIE76 delta E 20: the warning must never read as the Purge,
		// nor the way out as the calm).
		for (int32 A = 0; A < UE_ARRAY_COUNT(Tones); ++A)
		{
			for (int32 B = A + 1; B < UE_ARRAY_COUNT(Tones); ++B)
			{
				const float DeltaE = (Lab(Seen(UOneBrahamUIStyle::ToneColour(Tones[A]), Modes[M]))
					- Lab(Seen(UOneBrahamUIStyle::ToneColour(Tones[B]), Modes[M]))).Size();
				TestTrue(*FString::Printf(TEXT("%s: %s and %s are apart (dE %.1f)"), ModeNames[M],
					UOneBrahamUIStyle::ToneWord(Tones[A]), UOneBrahamUIStyle::ToneWord(Tones[B]), DeltaE), DeltaE >= 20.f);
			}
		}

		// The type on each field stays legible after the remap (3:1, large type).
		for (const Tone T : Tones)
		{
			FLinearColor FieldA, FieldB;
			UOneBrahamUIStyle::ToneField(T, FieldA, FieldB);
			const float Ratio = Contrast(UOneBrahamUIStyle::ToneInk(T), FieldA);
			TestTrue(*FString::Printf(TEXT("%s: %s type on its field (%.2f:1)"), ModeNames[M], UOneBrahamUIStyle::ToneWord(T), Ratio), Ratio >= 3.f);
		}
	}

	User->ColourBlindMode = Saved;
	return true;
}

#endif // WITH_AUTOMATION_TESTS
