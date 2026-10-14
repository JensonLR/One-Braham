// Copyright (c) One Braham. All Rights Reserved.
//
// OneBraham.Voice.*
//
// Voice in this game is two systems wearing one name: a squad radio carrying
// 16 kHz frames through the relay at 20 ms a frame, and a scripted line set in
// which every line id the gameplay code can ask for has to exist, subtitle
// included, before any audio has been recorded. Both are data, and both fail
// quietly. A block of voice chat that silently stops is a squad having a worse
// match than they think; a line id that resolves to nothing is a character with
// nothing to say at the exact moment the game needed a voice.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Core/OneBrahamGameSettings.h"
#include "OneBraham.h"
#include "Voice/OneBrahamVoiceCodec.h"
#include "Voice/OneBrahamVoiceTypes.h"
#include "Voice/OneBrahamVoiceSubsystem.h"

#if WITH_AUTOMATION_TESTS

// ===========================================================================
//  The transport's shape
// ===========================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamVoiceCodecShapeTest, "OneBraham.Voice.CodecShape",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamVoiceCodecShapeTest::RunTest(const FString& Parameters)
{
	const UOneBrahamGameSettings& Settings = UOneBrahamGameSettings::Get();

	// 16 kHz mono, 20 ms frames: 320 samples, 640 bytes of raw PCM, and a
	// transport budget of 1024 bytes that the compressed frame has to fit inside.
	TestEqual(TEXT("the codec and the settings agree on the sample rate"),
		Settings.VoiceSampleRate, FOneBrahamVoiceCodec::SampleRate);
	TestEqual(TEXT("mono"), FOneBrahamVoiceCodec::NumChannels, Settings.VoiceNumChannels);
	TestEqual(TEXT("a frame is 20 ms"), FOneBrahamVoiceCodec::FrameSamples,
		FOneBrahamVoiceCodec::SampleRate / 50);
	TestEqual(TEXT("the settings' frame length is the codec's"),
		Settings.VoiceFrameSamples, FOneBrahamVoiceCodec::FrameSamples);
	TestEqual(TEXT("50 frames a second"), FOneBrahamVoiceCodec::SampleRate / FOneBrahamVoiceCodec::FrameSamples, 50);
	TestEqual(TEXT("a raw frame is 640 bytes"), FOneBrahamVoiceCodec::RawFrameBytes,
		FOneBrahamVoiceCodec::FrameSamples * static_cast<int32>(sizeof(int16)));

	// The relay budget: a raw frame has to fit inside the compressed frame's
	// transport ceiling, or the codec has nothing to compress into.
	TestTrue(TEXT("a raw frame fits the transport"), FOneBrahamVoiceCodec::RawFrameBytes
		<= FOneBrahamVoiceCodec::MaxTransportFrameBytes);

	// 50 frames a second at the 1024-byte ceiling is 51 kB/s of worst-case traffic
	// per talker, which is the number that made this a relay rather than an engine
	// voice session.
	TestTrue(TEXT("worst-case voice traffic is under 64 kB/s"),
		FOneBrahamVoiceCodec::MaxTransportFrameBytes * 50 <= 65536);

	// Voice distance and occlusion: an open floor carries, a fire shutter does not
	// so much as muffle. Both live in the settings, and both are reasons a squad
	// has to decide where to talk rather than where to stand.
	TestTrue(TEXT("voice carries further than a room"), Settings.VoiceMaxDistanceCm > 1000.f);
	TestEqual(TEXT("voice distance is the documented 35 m"), Settings.VoiceMaxDistanceCm, 3500.f, 1.f);
	TestTrue(TEXT("a closed shutter muffles voice rather than blocking it"),
		Settings.VoiceOccludedFalloffMultiplier > 1.f && Settings.VoiceOccludedFalloffMultiplier < 10.f);

	return true;
}

// ===========================================================================
//  The script
// ===========================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamVoiceLineSetTest, "OneBraham.Voice.LineSet",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamVoiceLineSetTest::RunTest(const FString& Parameters)
{
	UOneBrahamVoiceLineSet* Set = NewObject<UOneBrahamVoiceLineSet>(
		GetTransientPackage(), NAME_None, RF_Transient);

	if (Set == nullptr)
	{
		AddError(TEXT("Could not create a transient voice line set."));
		return false;
	}

	// An empty set fills itself from the compiled-in table. This is the pre-
	// recording contract: the game is playable and readable before a single line
	// is in the bank.
	TestEqual(TEXT("a fresh set has no lines"), Set->Lines.Num(), 0);

	Set->EnsureCoreLines();

	// 105 core lines plus 66 archetype variants. The count is documentation the
	// checker also enforces (check_voice_lines.py), so a change here is a change
	// to the script, not to the test.
	TestTrue(TEXT("the core table filled the set"), Set->Lines.Num() >= 160);
	TestEqual(TEXT("105 core lines and 66 variants"), Set->Lines.Num(), 105 + 66);

	// Every row is a different line: the variants are the six archetypes' own
	// words, not six recordings of the same sentence.
	TSet<FString> Subtitles;
	for (const FOneBrahamVoiceLine& Line : Set->Lines)
	{
		Subtitles.Add(Line.Subtitle.ToString());
	}
	TestTrue(TEXT("at least 120 distinct lines"), Subtitles.Num() >= 120);

	TSet<FName> Seen;
	int32 Subtitleless = 0;
	int32 WrongPrefix = 0;
	int32 NoPriority = 0;

	for (const FOneBrahamVoiceLine& Line : Set->Lines)
	{
		if (Seen.Contains(Line.LineId))
		{
			AddError(FString::Printf(TEXT("duplicate line id %s"), *Line.LineId.ToString()));
		}

		Seen.Add(Line.LineId);

		if (Line.Subtitle.IsEmpty())
		{
			++Subtitleless;
		}

		if (!Line.LineId.ToString().StartsWith(TEXT("V_")))
		{
			++WrongPrefix;
		}

		// A line with no chance of being spoken is a line that does not exist.
		if (Line.Chance <= 0.f)
		{
			++NoPriority;
		}
	}

	TestEqual(TEXT("every line has a unique id"), Seen.Num(), Set->Lines.Num());
	TestEqual(TEXT("every line has a subtitle"), Subtitleless, 0);
	TestEqual(TEXT("every line id is a V_ id"), WrongPrefix, 0);
	TestEqual(TEXT("every line has a chance of being said"), NoPriority, 0);

	// The five line ids the comms wheel and the extraction flow depend on. If one
	// of these disappears the wheel draws an empty segment, so they are asserted
	// by name rather than by count.
	const FName Required[] = {
		TEXT("V_CONTACT_ENEMY"), TEXT("V_PUSH_NOW"), TEXT("V_HOLD_POSITION"),
		TEXT("V_RELOADING"), TEXT("V_MAN_DOWN")
	};

	for (const FName& Id : Required)
	{
		TArray<const FOneBrahamVoiceLine*> Found;
		Set->FindLines(Id, Found);
		TestTrue(*FString::Printf(TEXT("%s exists"), *Id.ToString()), Found.Num() > 0);
	}

	// Idempotent: this runs from a CallInEditor button and from the subsystem's
	// own resolve path, and running it twice must not duplicate the script.
	const int32 Before = Set->Lines.Num();
	Set->EnsureCoreLines();
	TestEqual(TEXT("EnsureCoreLines is idempotent"), Set->Lines.Num(), Before);

	// FindLines with an unknown id is empty rather than a default line. The probe
	// is deliberately not a V_ id: check_voice_lines.py reads every V_ literal in
	// the tree as a reference to the script, and it is right to, so a test that
	// wants an id the script does not have has to say so unambiguously.
	{
		TArray<const FOneBrahamVoiceLine*> None;
		Set->FindLines(TEXT("TEST_UnknownLineId"), None);
		TestEqual(TEXT("an unknown id finds nothing"), None.Num(), 0);
	}

	// PickLine either returns a line in the set or nothing at all - never a
	// dangling pointer into a temporary.
	{
		FRandomStream Stream(20261013);
		const FOneBrahamVoiceLine* Picked = Set->PickLine(TEXT("V_CONTACT_ENEMY"), FGameplayTag(), Stream);
		TestNotNull(TEXT("PickLine returns a line for a core id"), Picked);

		if (Picked != nullptr)
		{
			TestEqual(TEXT("and it is the line that was asked for"), Picked->LineId,
				FName(TEXT("V_CONTACT_ENEMY")));
			TestTrue(TEXT("and it belongs to the set"), Set->Lines.ContainsByPredicate(
				[Picked](const FOneBrahamVoiceLine& Candidate) { return &Candidate == Picked; }));
		}

		TestNull(TEXT("PickLine returns nothing for an unknown id"),
			Set->PickLine(TEXT("TEST_UnknownLineId"), FGameplayTag(), Stream));
	}

	return true;
}

// ===========================================================================
//  The comms wheel
// ===========================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamVoiceWheelTest, "OneBraham.Voice.Wheel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamVoiceWheelTest::RunTest(const FString& Parameters)
{
	UOneBrahamVoiceLineSet* Set = NewObject<UOneBrahamVoiceLineSet>(
		GetTransientPackage(), NAME_None, RF_Transient);

	if (Set == nullptr)
	{
		AddError(TEXT("Could not create a transient voice line set."));
		return false;
	}

	Set->EnsureCoreLines();

	// Eight segments. The wheel is a fixed-size radial menu, so the count is part
	// of the layout: nine segments would overdraw the ring and seven would leave a
	// gap the player cannot explain.
	TSet<FName> Ids;
	TSet<FString> Labels;
	int32 Segments = 0;

	for (int32 Segment = 0; Segment < 64; ++Segment)
	{
		FName LineId = NAME_None;
		FText Label;
		UOneBrahamVoiceSubsystem::GetQuickLineWheel(LineId, Label, Segment);

		if (LineId.IsNone() && Label.IsEmpty())
		{
			break;
		}

		++Segments;

		TestFalse(*FString::Printf(TEXT("segment %d has an id"), Segment), LineId.IsNone());
		TestFalse(*FString::Printf(TEXT("segment %d has a label"), Segment), Label.IsEmpty());
		TestTrue(*FString::Printf(TEXT("segment %d is a V_ id"), Segment),
			LineId.ToString().StartsWith(TEXT("V_")));

		// Two segments pointing at the same line would be one wasted gesture.
		TestFalse(*FString::Printf(TEXT("segment %d is unique"), Segment), Ids.Contains(LineId));
		Ids.Add(LineId);
		Labels.Add(Label.ToString());
	}

	TestEqual(TEXT("the wheel has eight segments"), Segments, 8);
	TestEqual(TEXT("no label is reused"), Labels.Num(), Segments);

	// Every segment has to resolve against the script. This is the assertion that
	// would have caught the invented ids: a wheel drawn from names nobody wrote
	// down is a wheel of empty buttons.
	int32 Dangling = 0;
	for (const FName& Id : Ids)
	{
		TArray<const FOneBrahamVoiceLine*> Found;
		Set->FindLines(Id, Found);

		if (Found.Num() == 0)
		{
			AddError(FString::Printf(TEXT("the wheel's %s is not in the script"), *Id.ToString()));
			++Dangling;
		}
	}

	TestEqual(TEXT("every wheel segment exists in the script"), Dangling, 0);

	// Out of range is a no-op rather than a read off the end of the arrays: the
	// wheel widget asks for a segment index computed from the mouse, and a player
	// who closes the wheel with the cursor outside the ring asks for segment -1.
	{
		FName LineId = TEXT("Unchanged");
		FText Label = FText::FromString(TEXT("Unchanged"));

		UOneBrahamVoiceSubsystem::GetQuickLineWheel(LineId, Label, -1);
		TestTrue(TEXT("segment -1 yields nothing"), LineId.IsNone() && Label.IsEmpty());

		LineId = TEXT("Unchanged");
		Label = FText::FromString(TEXT("Unchanged"));
		UOneBrahamVoiceSubsystem::GetQuickLineWheel(LineId, Label, 8);
		TestTrue(TEXT("segment 8 - one past the end - yields nothing"),
			LineId.IsNone() && Label.IsEmpty());

		UOneBrahamVoiceSubsystem::GetQuickLineWheel(LineId, Label, 1000000);
		TestTrue(TEXT("an absurd index yields nothing"), LineId.IsNone() && Label.IsEmpty());
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
