// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamVoiceRules.cpp
// ---------------------------------------------------------------------------
// The pure half of voice chat: the activity detector, the routing rule, the
// proximity shaping curve and the ob.Voice.* console variables. None of this
// touches a world, which is the point - every rule here is asserted directly by
// Tests/OneBrahamVoiceTests.cpp.

#include "Voice/OneBrahamVoiceTypes.h"

#include "HAL/IConsoleManager.h"

// ===========================================================================
//  Console variables
// ===========================================================================
namespace
{
	// Mirrored from UOneBrahamGameUserSettings::ApplyVoiceSettings at
	// ECVF_SetByGameSetting, so the menu drives them, and a QA console override
	// (higher priority) still wins over the menu without editing the save.
	float GVoiceInputGain = 1.f;
	FAutoConsoleVariableRef CVarVoiceInputGain(
		TEXT("ob.Voice.InputGain"),
		GVoiceInputGain,
		TEXT("Linear microphone gain applied to captured voice before the noise gate (0-4). Mirrors Settings -> Audio -> Mic Volume."),
		ECVF_Default);

	float GVoiceOutputGain = 1.f;
	FAutoConsoleVariableRef CVarVoiceOutputGain(
		TEXT("ob.Voice.OutputGain"),
		GVoiceOutputGain,
		TEXT("Linear gain on every received voice-chat stream (0-4). Mirrors Settings -> Audio -> Voice Output."),
		ECVF_Default);

	float GVoiceNoiseGate = 0.009f;
	FAutoConsoleVariableRef CVarVoiceNoiseGate(
		TEXT("ob.Voice.NoiseGate"),
		GVoiceNoiseGate,
		TEXT("RMS level (0-1 of full scale) that opens voice activation. Open mic gates at a third of it, so silence is never sent."),
		ECVF_Default);
}

float OneBrahamVoiceCVars::GetInputGain()
{
	return FMath::Clamp(GVoiceInputGain, 0.f, 4.f);
}

float OneBrahamVoiceCVars::GetOutputGain()
{
	return FMath::Clamp(GVoiceOutputGain, 0.f, 4.f);
}

float OneBrahamVoiceCVars::GetNoiseGate()
{
	return FMath::Clamp(GVoiceNoiseGate, 0.f, 0.2f);
}

// ===========================================================================
//  Voice activity detector
// ===========================================================================
float FOneBrahamVoiceActivityDetector::ComputeRms(const int16* Samples, int32 NumSamples)
{
	if (Samples == nullptr || NumSamples <= 0)
	{
		return 0.f;
	}

	double SumSquares = 0.0;
	for (int32 Index = 0; Index < NumSamples; ++Index)
	{
		const double Normalised = static_cast<double>(Samples[Index]) / 32768.0;
		SumSquares += Normalised * Normalised;
	}

	return static_cast<float>(FMath::Sqrt(SumSquares / static_cast<double>(NumSamples)));
}

float FOneBrahamVoiceActivityDetector::GetEffectiveOpenThreshold() const
{
	return FMath::Max(OpenThreshold, NoiseFloor * NoiseFloorMargin);
}

bool FOneBrahamVoiceActivityDetector::ProcessFrame(float FrameRms, float FrameSeconds)
{
	const float Level = FMath::Max(0.f, FrameRms);
	const float Threshold = GetEffectiveOpenThreshold();

	if (!bOpen)
	{
		if (Level >= Threshold)
		{
			bOpen = true;
			HangRemaining = HangSeconds;
		}
	}
	else if (Level >= Threshold * CloseRatio)
	{
		HangRemaining = HangSeconds;
	}
	else
	{
		HangRemaining -= FMath::Max(0.f, FrameSeconds);
		if (HangRemaining <= 0.f)
		{
			bOpen = false;
			HangRemaining = 0.f;
		}
	}

	// The floor is a minimum follower: it falls quickly (the gaps between words
	// pull it straight back to the room) and rises slowly, so a steady fan or hum
	// eventually stops holding the gate open, but a sentence never raises it far
	// enough to close the gate on the speaker. Updated after the decision, so a
	// frame never moves the bar it is being measured against.
	if (NoiseFloor <= 0.f)
	{
		NoiseFloor = Level;
	}
	else
	{
		const float Rate = Level < NoiseFloor ? 0.25f : 0.004f;
		NoiseFloor = FMath::Lerp(NoiseFloor, Level, Rate);
	}

	// Capped so that a loud room can raise the bar, but never past ordinary speech.
	NoiseFloor = FMath::Min(NoiseFloor, 0.03f);

	return bOpen;
}

void FOneBrahamVoiceActivityDetector::Reset()
{
	bOpen = false;
	HangRemaining = 0.f;
	NoiseFloor = 0.f;
}

// ===========================================================================
//  Routing
// ===========================================================================
EOneBrahamVoiceChannel OneBrahamVoiceRouting::ResolveSpeakerChannel(const FOneBrahamVoiceRouteParty& Speaker)
{
	if (Speaker.Channel == EOneBrahamVoiceChannel::None)
	{
		return EOneBrahamVoiceChannel::None;
	}

	// The documented design: an eliminated player keeps their squad radio (it
	// keeps them in the match instead of bored), and loses everything else. A
	// dead player on proximity is quietly moved to squad rather than silenced,
	// because "I died and my mic stopped working" is the bug report otherwise.
	if (Speaker.bEliminated)
	{
		return EOneBrahamVoiceChannel::Squad;
	}

	return Speaker.Channel;
}

bool OneBrahamVoiceRouting::ResolveRoute(const FOneBrahamVoiceRouteParty& Listener, const FOneBrahamVoiceRouteParty& Speaker,
	const FOneBrahamVoiceRouteRules& Rules, EOneBrahamVoiceChannel& OutChannel)
{
	OutChannel = EOneBrahamVoiceChannel::None;

	if (Listener.PlayerId == INDEX_NONE || Speaker.PlayerId == INDEX_NONE || Listener.PlayerId == Speaker.PlayerId)
	{
		return false;
	}

	// The listener's own mute list. This is the only per-listener mute: it used to
	// read the listener's "muted by" list, which is the opposite relationship and
	// meant muting somebody silenced *you* for *them*.
	if (Listener.MutedPlayerIds != nullptr && Listener.MutedPlayerIds->Contains(Speaker.PlayerId))
	{
		return false;
	}

	// Self-mute is a microphone switch. A listener who muted their own mic still
	// hears their squad - otherwise muting to cough means missing the callout.
	if (Speaker.bSelfMuted)
	{
		return false;
	}

	const EOneBrahamVoiceChannel Channel = ResolveSpeakerChannel(Speaker);

	const bool bSameSquad = Speaker.Team != EOneBrahamTeam::None
		&& Speaker.Team == Listener.Team
		&& Speaker.SquadId != INDEX_NONE
		&& Speaker.SquadId == Listener.SquadId;

	bool bHears = false;

	switch (Channel)
	{
	case EOneBrahamVoiceChannel::Proximity:
	{
		// Heard from wherever the listener's ears are: their own pawn while alive,
		// the player they spectate once eliminated. Enemies included - that is the
		// whole game of proximity chat.
		if (!Rules.bProximityEnabled || !Speaker.bHasLocation || !Listener.bHasLocation || Rules.ProximityRangeCm <= 0.f)
		{
			break;
		}

		bHears = FVector::DistSquared(Listener.Location, Speaker.Location) <= FMath::Square(Rules.ProximityRangeCm);
		break;
	}

	case EOneBrahamVoiceChannel::Squad:
		// Alive or dead on either end: squad radio is the one channel the dead keep.
		bHears = bSameSquad;
		break;

	case EOneBrahamVoiceChannel::Team:
		// Eliminated listeners are on squad radio only.
		bHears = Rules.bTeamEnabled
			&& !Listener.bEliminated
			&& Speaker.Team != EOneBrahamTeam::None
			&& Speaker.Team == Listener.Team;
		break;

	case EOneBrahamVoiceChannel::Announcement:
		bHears = true;
		break;

	case EOneBrahamVoiceChannel::None:
	default:
		break;
	}

	if (bHears)
	{
		OutChannel = Channel;
	}

	return bHears;
}

FOneBrahamProximityVoiceShaping OneBrahamVoiceRouting::ShapeProximityVoice(float DistanceCm, float RangeCm,
	int32 BlockingSurfaces, int32 FloorsApart, bool bSpeakerCrouched)
{
	FOneBrahamProximityVoiceShaping Shaping;

	const float Range = FMath::Max(1.f, RangeCm);
	const float Distance = FMath::Max(0.f, DistanceCm);

	if (Distance >= Range)
	{
		Shaping.Gain = 0.f;
		Shaping.LowPassHz = 400.f;
		return Shaping;
	}

	// --- Distance ---------------------------------------------------------
	// Close to inverse-distance (6 dB per doubling is what a real voice does),
	// softened to an exponent of 0.85 so a teammate across a room is still
	// clearly a voice, then faded to silence over the last quarter of the range
	// so the relay's hard sphere edge is never audible as a cut.
	const float Rolloff = FMath::Pow(ReferenceDistanceCm / FMath::Max(ReferenceDistanceCm, Distance), 0.85f);
	const float EdgeFade = 1.f - FMath::SmoothStep(0.75f * Range, Range, Distance);
	float Gain = Rolloff * EdgeFade;

	// Air absorption: the top end goes first with distance.
	const float DistanceAlpha = FMath::Clamp(Distance / Range, 0.f, 1.f);
	float Cutoff = FMath::Lerp(ClearLowPassHz, 6000.f, DistanceAlpha * DistanceAlpha);

	// --- Crouched: talking under the breath -------------------------------
	// A crouched speaker is effectively whispering: as loud as ever to the person
	// beside them, gone by under half the range. Cheap, and it makes crouching on
	// a stairwell a real way to talk without being found.
	if (bSpeakerCrouched)
	{
		Gain *= 0.85f * (1.f - FMath::SmoothStep(0.12f * Range, 0.45f * Range, Distance));
		Cutoff = FMath::Min(Cutoff, 9000.f);
	}

	// --- Walls ------------------------------------------------------------
	// Each VoiceOccluder surface costs about 8 dB and most of the intelligibility.
	if (BlockingSurfaces > 0)
	{
		Gain *= FMath::Pow(0.4f, static_cast<float>(BlockingSurfaces));
		Cutoff = FMath::Min(Cutoff, FMath::Max(350.f, 1400.f * FMath::Pow(0.55f, static_cast<float>(BlockingSurfaces - 1))));
	}

	// --- Floors -----------------------------------------------------------
	// A concrete slab is the heaviest occluder in an office tower: another storey
	// is a low murmur, two storeys is barely there.
	if (FloorsApart > 0)
	{
		Gain *= FMath::Pow(0.25f, static_cast<float>(FloorsApart));
		Cutoff = FMath::Min(Cutoff, FMath::Max(300.f, 700.f / static_cast<float>(FloorsApart)));
	}

	Shaping.Gain = FMath::Clamp(Gain, 0.f, 1.f);
	Shaping.LowPassHz = FMath::Clamp(Cutoff, 300.f, ClearLowPassHz);
	return Shaping;
}

float OneBrahamVoiceRouting::ResolveProximityRangeCm(float MatchSettingsRangeCm, float OverrideRangeCm, float ProjectDefaultRangeCm)
{
	if (MatchSettingsRangeCm > 0.f)
	{
		return MatchSettingsRangeCm;
	}

	if (OverrideRangeCm > 0.f)
	{
		return OverrideRangeCm;
	}

	if (ProjectDefaultRangeCm > 0.f)
	{
		return ProjectDefaultRangeCm;
	}

	return 3500.f;
}
