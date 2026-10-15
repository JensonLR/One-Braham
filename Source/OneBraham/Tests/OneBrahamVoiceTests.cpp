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
#include "UObject/Package.h"

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

	// 97 core lines plus 66 archetype variants. The count is documentation the
	// checker also enforces (check_voice_lines.py), so a change here is a change
	// to the script, not to the test.
	TestTrue(TEXT("the core table filled the set"), Set->Lines.Num() >= 160);
	TestEqual(TEXT("97 core lines and 66 variants"), Set->Lines.Num(), 97 + 66);

	// An archetype variant shares its core line's id and differs by filter, so
	// a line is identified by the pair.
	TSet<FString> Seen;
	int32 Subtitleless = 0;
	int32 WrongPrefix = 0;
	int32 NoPriority = 0;

	for (const FOneBrahamVoiceLine& Line : Set->Lines)
	{
		FString Key = Line.LineId.ToString();
		for (const FGameplayTag& Archetype : Line.ArchetypeFilter)
		{
			Key += TEXT("|") + Archetype.ToString();
		}

		if (Seen.Contains(Key))
		{
			AddError(FString::Printf(TEXT("duplicate line %s"), *Key));
		}

		Seen.Add(Key);

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

// ===========================================================================
//  Voice activation
// ===========================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamVoiceActivityDetectorTest, "OneBraham.Voice.ActivityDetector",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamVoiceActivityDetectorTest::RunTest(const FString& Parameters)
{
	constexpr float Frame = 0.02f;

	// RMS is a fraction of full scale: silence is zero, a full-scale square wave
	// is one, and a null buffer is silence rather than a crash.
	{
		int16 Silence[320] = {};
		TestEqual(TEXT("silence has no level"), FOneBrahamVoiceActivityDetector::ComputeRms(Silence, 320), 0.f);

		int16 Square[320];
		for (int32 Index = 0; Index < 320; ++Index)
		{
			Square[Index] = (Index % 2 == 0) ? 32767 : -32767;
		}
		TestEqual(TEXT("a full-scale square wave is level 1"),
			FOneBrahamVoiceActivityDetector::ComputeRms(Square, 320), 1.f, 0.001f);
		TestEqual(TEXT("no samples is silence"), FOneBrahamVoiceActivityDetector::ComputeRms(nullptr, 0), 0.f);
	}

	// A quiet room never opens the gate; a voice opens it on the first frame.
	{
		FOneBrahamVoiceActivityDetector Gate;
		bool bEverOpened = false;
		for (int32 Index = 0; Index < 50; ++Index)
		{
			bEverOpened |= Gate.ProcessFrame(0.001f, Frame);
		}
		TestFalse(TEXT("a second of room noise transmits nothing"), bEverOpened);
		TestTrue(TEXT("speech opens the gate at once"), Gate.ProcessFrame(0.08f, Frame));
	}

	// The hang time holds the gate through the gaps between words, then lets go.
	{
		FOneBrahamVoiceActivityDetector Gate;
		Gate.ProcessFrame(0.08f, Frame);

		bool bHeldThroughGap = true;
		for (int32 Index = 0; Index < 15; ++Index)
		{
			bHeldThroughGap &= Gate.ProcessFrame(0.f, Frame);
		}
		TestTrue(TEXT("a 300 ms pause does not close the gate"), bHeldThroughGap);

		for (int32 Index = 0; Index < 10; ++Index)
		{
			Gate.ProcessFrame(0.f, Frame);
		}
		TestFalse(TEXT("500 ms of silence does"), Gate.bOpen);
	}

	// Hysteresis: a level between the close and open thresholds keeps an open
	// gate open, but does not open a closed one - no flutter on the HUD icon.
	{
		FOneBrahamVoiceActivityDetector Open;
		FOneBrahamVoiceActivityDetector Closed;
		for (int32 Index = 0; Index < 50; ++Index)
		{
			Open.ProcessFrame(0.002f, Frame);
			Closed.ProcessFrame(0.002f, Frame);
		}

		TestTrue(TEXT("speech opens the first gate"), Open.ProcessFrame(0.02f, Frame));

		bool bStayedOpen = true;
		bool bOpenedClosed = false;
		for (int32 Index = 0; Index < 100; ++Index)
		{
			bStayedOpen &= Open.ProcessFrame(0.007f, Frame);
			bOpenedClosed |= Closed.ProcessFrame(0.007f, Frame);
		}

		TestTrue(TEXT("a level just under the threshold keeps an open gate open"), bStayedOpen);
		TestFalse(TEXT("and does not open a closed one"), bOpenedClosed);
	}

	// The noise floor learns a steady hum: a desktop fan above the threshold
	// eventually stops holding the gate open, and speech over it still opens it.
	{
		FOneBrahamVoiceActivityDetector Gate;
		for (int32 Index = 0; Index < 750; ++Index)
		{
			Gate.ProcessFrame(0.02f, Frame);
		}

		TestFalse(TEXT("fifteen seconds of fan noise no longer transmits"), Gate.bOpen);
		TestTrue(TEXT("the bar rose above the hum"), Gate.GetEffectiveOpenThreshold() > 0.02f);
		TestTrue(TEXT("but a voice over the fan still opens it"), Gate.ProcessFrame(0.15f, Frame));
	}

	{
		FOneBrahamVoiceActivityDetector Gate;
		Gate.ProcessFrame(0.2f, Frame);
		Gate.Reset();
		TestFalse(TEXT("Reset closes the gate"), Gate.bOpen);
		TestEqual(TEXT("and forgets the room"), Gate.NoiseFloor, 0.f);
	}

	return true;
}

// ===========================================================================
//  Routing
// ===========================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamVoiceRoutingTest, "OneBraham.Voice.Routing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamVoiceRoutingTest::RunTest(const FString& Parameters)
{
	using namespace OneBrahamVoiceRouting;

	auto MakeParty = [](int32 PlayerId, EOneBrahamTeam Team, int32 SquadId, const FVector& Location)
	{
		FOneBrahamVoiceRouteParty Party;
		Party.PlayerId = PlayerId;
		Party.Team = Team;
		Party.SquadId = SquadId;
		Party.bHasLocation = true;
		Party.Location = Location;
		return Party;
	};

	FOneBrahamVoiceRouteRules Rules;
	Rules.ProximityRangeCm = 3500.f;

	const FOneBrahamVoiceRouteParty Speaker = MakeParty(1, EOneBrahamTeam::Alpha, 0, FVector::ZeroVector);
	const FOneBrahamVoiceRouteParty Squadmate = MakeParty(2, EOneBrahamTeam::Alpha, 0, FVector(1000.f, 0.f, 0.f));
	const FOneBrahamVoiceRouteParty NearEnemy = MakeParty(3, EOneBrahamTeam::Bravo, 1, FVector(2000.f, 0.f, 0.f));
	const FOneBrahamVoiceRouteParty FarEnemy = MakeParty(4, EOneBrahamTeam::Bravo, 1, FVector(5000.f, 0.f, 0.f));
	const FOneBrahamVoiceRouteParty FarSquadmate = MakeParty(5, EOneBrahamTeam::Alpha, 0, FVector(90000.f, 0.f, 0.f));
	const FOneBrahamVoiceRouteParty OtherSquad = MakeParty(6, EOneBrahamTeam::Alpha, 2, FVector(500.f, 0.f, 0.f));

	EOneBrahamVoiceChannel Channel = EOneBrahamVoiceChannel::None;

	// --- Proximity: a sphere, enemies included ---------------------------
	TestTrue(TEXT("a squadmate in range hears proximity"), ResolveRoute(Squadmate, Speaker, Rules, Channel));
	TestEqual(TEXT("on the proximity channel"), Channel, EOneBrahamVoiceChannel::Proximity);
	TestTrue(TEXT("an enemy in range hears it too"), ResolveRoute(NearEnemy, Speaker, Rules, Channel));
	TestFalse(TEXT("an enemy out of range does not"), ResolveRoute(FarEnemy, Speaker, Rules, Channel));
	TestFalse(TEXT("nor a squadmate out of range"), ResolveRoute(FarSquadmate, Speaker, Rules, Channel));
	TestFalse(TEXT("nobody hears themselves"), ResolveRoute(Speaker, Speaker, Rules, Channel));

	{
		FOneBrahamVoiceRouteRules Off = Rules;
		Off.bProximityEnabled = false;
		TestFalse(TEXT("a match without proximity voice routes none"), ResolveRoute(Squadmate, Speaker, Off, Channel));
	}

	// --- Mutes -----------------------------------------------------------
	{
		const TArray<int32> Muted = { Speaker.PlayerId };
		FOneBrahamVoiceRouteParty Listener = Squadmate;
		Listener.MutedPlayerIds = &Muted;
		TestFalse(TEXT("a listener who muted the speaker hears nothing"), ResolveRoute(Listener, Speaker, Rules, Channel));

		// The old rule read the listener's "muted by" list: muting somebody
		// silenced yourself for them. The speaker's own list must not matter.
		FOneBrahamVoiceRouteParty MutingSpeaker = Speaker;
		const TArray<int32> SpeakerMutes = { Squadmate.PlayerId };
		MutingSpeaker.MutedPlayerIds = &SpeakerMutes;
		TestTrue(TEXT("a speaker's own mute list does not silence them"), ResolveRoute(Squadmate, MutingSpeaker, Rules, Channel));

		FOneBrahamVoiceRouteParty SelfMutedSpeaker = Speaker;
		SelfMutedSpeaker.bSelfMuted = true;
		TestFalse(TEXT("a self-muted speaker is heard by nobody"), ResolveRoute(Squadmate, SelfMutedSpeaker, Rules, Channel));

		FOneBrahamVoiceRouteParty SelfMutedListener = Squadmate;
		SelfMutedListener.bSelfMuted = true;
		TestTrue(TEXT("a listener who muted their own mic still hears"), ResolveRoute(SelfMutedListener, Speaker, Rules, Channel));
	}

	// --- Squad and team radio -------------------------------------------
	{
		FOneBrahamVoiceRouteParty Radio = Speaker;
		Radio.Channel = EOneBrahamVoiceChannel::Squad;
		TestTrue(TEXT("squad radio reaches a squadmate at any distance"), ResolveRoute(FarSquadmate, Radio, Rules, Channel));
		TestEqual(TEXT("on the squad channel"), Channel, EOneBrahamVoiceChannel::Squad);
		TestFalse(TEXT("but not another squad"), ResolveRoute(OtherSquad, Radio, Rules, Channel));
		TestFalse(TEXT("nor an enemy beside the speaker"), ResolveRoute(NearEnemy, Radio, Rules, Channel));

		FOneBrahamVoiceRouteParty Unassigned = Radio;
		Unassigned.Team = EOneBrahamTeam::None;
		FOneBrahamVoiceRouteParty UnassignedListener = FarSquadmate;
		UnassignedListener.Team = EOneBrahamTeam::None;
		TestFalse(TEXT("players with no team share no squad"), ResolveRoute(UnassignedListener, Unassigned, Rules, Channel));

		FOneBrahamVoiceRouteParty TeamRadio = Speaker;
		TeamRadio.Channel = EOneBrahamVoiceChannel::Team;
		TestTrue(TEXT("team radio reaches another squad of the team"), ResolveRoute(OtherSquad, TeamRadio, Rules, Channel));
		TestFalse(TEXT("but not the enemy"), ResolveRoute(NearEnemy, TeamRadio, Rules, Channel));

		FOneBrahamVoiceRouteRules NoTeam = Rules;
		NoTeam.bTeamEnabled = false;
		TestFalse(TEXT("a match without team voice routes none"), ResolveRoute(OtherSquad, TeamRadio, NoTeam, Channel));

		FOneBrahamVoiceRouteParty Off = Speaker;
		Off.Channel = EOneBrahamVoiceChannel::None;
		TestFalse(TEXT("the Off channel reaches nobody"), ResolveRoute(Squadmate, Off, Rules, Channel));
	}

	// --- The eliminated --------------------------------------------------
	// Dead players keep squad radio, and only squad radio.
	{
		FOneBrahamVoiceRouteParty Dead = Speaker;
		Dead.bEliminated = true;
		Dead.bHasLocation = false;

		TestEqual(TEXT("a dead speaker on proximity is moved to squad radio"),
			ResolveSpeakerChannel(Dead), EOneBrahamVoiceChannel::Squad);
		TestTrue(TEXT("their squad hears them at any distance"), ResolveRoute(FarSquadmate, Dead, Rules, Channel));
		TestEqual(TEXT("over the radio"), Channel, EOneBrahamVoiceChannel::Squad);
		TestFalse(TEXT("an enemy beside the body hears nothing"), ResolveRoute(NearEnemy, Dead, Rules, Channel));

		FOneBrahamVoiceRouteParty DeadOnTeam = Dead;
		DeadOnTeam.Channel = EOneBrahamVoiceChannel::Team;
		TestFalse(TEXT("a dead speaker's team radio stays inside the squad"), ResolveRoute(OtherSquad, DeadOnTeam, Rules, Channel));

		FOneBrahamVoiceRouteParty DeadOff = Dead;
		DeadOff.Channel = EOneBrahamVoiceChannel::None;
		TestFalse(TEXT("a dead player who switched voice off stays off"), ResolveRoute(FarSquadmate, DeadOff, Rules, Channel));
	}

	// Spectators hear proximity from where the watched player stands.
	{
		FOneBrahamVoiceRouteParty Spectator = MakeParty(7, EOneBrahamTeam::Bravo, 1, FVector(500.f, 0.f, 0.f));
		Spectator.bEliminated = true;
		TestTrue(TEXT("a spectator watching somebody near the speaker hears them"), ResolveRoute(Spectator, Speaker, Rules, Channel));
		TestEqual(TEXT("spatialised, on proximity"), Channel, EOneBrahamVoiceChannel::Proximity);

		Spectator.Location = FVector(60000.f, 0.f, 0.f);
		TestFalse(TEXT("but not when the watched player is across the building"), ResolveRoute(Spectator, Speaker, Rules, Channel));

		Spectator.bHasLocation = false;
		TestFalse(TEXT("nor with nobody to watch"), ResolveRoute(Spectator, Speaker, Rules, Channel));

		FOneBrahamVoiceRouteParty DeadSquadmate = FarSquadmate;
		DeadSquadmate.bEliminated = true;
		FOneBrahamVoiceRouteParty Radio = Speaker;
		Radio.Channel = EOneBrahamVoiceChannel::Squad;
		TestTrue(TEXT("a dead squadmate still hears squad radio"), ResolveRoute(DeadSquadmate, Radio, Rules, Channel));

		Radio.Channel = EOneBrahamVoiceChannel::Team;
		TestFalse(TEXT("but not team radio"), ResolveRoute(DeadSquadmate, Radio, Rules, Channel));
	}

	// The building's channel is everyone's.
	{
		FOneBrahamVoiceRouteParty Building = Speaker;
		Building.Channel = EOneBrahamVoiceChannel::Announcement;
		TestTrue(TEXT("announcements reach the far side of the map"), ResolveRoute(FarEnemy, Building, Rules, Channel));
	}

	// --- Range precedence ------------------------------------------------
	// The match setting wins whenever it names a range: the old default of 3500
	// in the chat config silently shadowed it.
	TestEqual(TEXT("the match's range wins"), ResolveProximityRangeCm(5000.f, 3500.f, 3500.f), 5000.f);
	TestEqual(TEXT("an override applies without match settings"), ResolveProximityRangeCm(0.f, 2000.f, 3500.f), 2000.f);
	TestEqual(TEXT("then the project default"), ResolveProximityRangeCm(0.f, 0.f, 3000.f), 3000.f);
	TestEqual(TEXT("then the documented 35 m"), ResolveProximityRangeCm(0.f, 0.f, 0.f), 3500.f);
	TestEqual(TEXT("the chat config no longer carries a range of its own"), FOneBrahamVoiceChatConfig().ProximityRangeCm, 0.f);

	return true;
}

// ===========================================================================
//  Proximity shaping
// ===========================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOneBrahamVoiceProximityShapingTest, "OneBraham.Voice.ProximityShaping",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOneBrahamVoiceProximityShapingTest::RunTest(const FString& Parameters)
{
	using namespace OneBrahamVoiceRouting;
	constexpr float Range = 3500.f;

	// Beside you: full level, unfiltered.
	const FOneBrahamProximityVoiceShaping Beside = ShapeProximityVoice(100.f, Range, 0, 0, false);
	TestEqual(TEXT("a voice beside you is at full level"), Beside.Gain, 1.f, 0.001f);
	TestTrue(TEXT("and unfiltered"), Beside.LowPassHz > 19000.f);

	// Natural rolloff: never rises with distance, is well below linear at ten
	// metres (the ear hears level logarithmically), and is silent at the range.
	float Previous = 2.f;
	bool bMonotonic = true;
	for (float Distance = 0.f; Distance <= Range + 500.f; Distance += 50.f)
	{
		const float Gain = ShapeProximityVoice(Distance, Range, 0, 0, false).Gain;
		bMonotonic &= Gain <= Previous + KINDA_SMALL_NUMBER;
		Previous = Gain;
	}
	TestTrue(TEXT("gain never rises with distance"), bMonotonic);

	const float TenMetres = ShapeProximityVoice(1000.f, Range, 0, 0, false).Gain;
	TestTrue(TEXT("ten metres is a clear but quieter voice"), TenMetres > 0.2f && TenMetres < 0.5f);
	TestEqual(TEXT("silent at the range"), ShapeProximityVoice(Range, Range, 0, 0, false).Gain, 0.f);
	TestEqual(TEXT("and beyond it"), ShapeProximityVoice(Range * 2.f, Range, 0, 0, false).Gain, 0.f);
	TestTrue(TEXT("far voices lose their top end"),
		ShapeProximityVoice(3000.f, Range, 0, 0, false).LowPassHz < ShapeProximityVoice(500.f, Range, 0, 0, false).LowPassHz);

	// Walls: each surface is quieter and duller than the last.
	const FOneBrahamProximityVoiceShaping Clear = ShapeProximityVoice(800.f, Range, 0, 0, false);
	const FOneBrahamProximityVoiceShaping OneWall = ShapeProximityVoice(800.f, Range, 1, 0, false);
	const FOneBrahamProximityVoiceShaping TwoWalls = ShapeProximityVoice(800.f, Range, 2, 0, false);
	TestTrue(TEXT("a wall costs at least 6 dB"), OneWall.Gain <= Clear.Gain * 0.5f);
	TestTrue(TEXT("and muffles to under 1.5 kHz"), OneWall.LowPassHz <= 1500.f);
	TestTrue(TEXT("a second wall is quieter still"), TwoWalls.Gain < OneWall.Gain);
	TestTrue(TEXT("and duller still"), TwoWalls.LowPassHz < OneWall.LowPassHz);

	// Floors: another storey is a murmur, two is almost nothing.
	const FOneBrahamProximityVoiceShaping OneFloor = ShapeProximityVoice(800.f, Range, 0, 1, false);
	const FOneBrahamProximityVoiceShaping TwoFloors = ShapeProximityVoice(800.f, Range, 0, 2, false);
	TestTrue(TEXT("a voice on another floor is at least 12 dB down"), OneFloor.Gain <= Clear.Gain * 0.25f + KINDA_SMALL_NUMBER);
	TestTrue(TEXT("and heavily muffled"), OneFloor.LowPassHz <= 700.f);
	TestTrue(TEXT("two floors is quieter than one"), TwoFloors.Gain < OneFloor.Gain);
	TestTrue(TEXT("a floor muffles more than a wall"), OneFloor.Gain < OneWall.Gain);

	// Crouched speakers whisper: as clear as ever up close, gone at mid range.
	const float StandingNear = ShapeProximityVoice(150.f, Range, 0, 0, false).Gain;
	const float CrouchedNear = ShapeProximityVoice(150.f, Range, 0, 0, true).Gain;
	const float StandingMid = ShapeProximityVoice(2000.f, Range, 0, 0, false).Gain;
	const float CrouchedMid = ShapeProximityVoice(2000.f, Range, 0, 0, true).Gain;
	TestTrue(TEXT("a crouched voice is nearly as loud up close"), CrouchedNear >= StandingNear * 0.8f);
	TestTrue(TEXT("and a whisper at twenty metres"), CrouchedMid < StandingMid * 0.1f);

	// Every output is a usable value, whatever the input.
	const FOneBrahamProximityVoiceShaping Degenerate = ShapeProximityVoice(-50.f, 0.f, 99, 99, true);
	TestTrue(TEXT("degenerate input stays in range"), Degenerate.Gain >= 0.f && Degenerate.Gain <= 1.f
		&& Degenerate.LowPassHz >= 300.f && Degenerate.LowPassHz <= ClearLowPassHz);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
