// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamVoiceTypes.h
// ---------------------------------------------------------------------------
// Shared voice vocabulary: character barks (behaviour lines), the shipped voice
// script, and the live proximity/radio chat transport.

#pragma once

#include "CoreMinimal.h"
#include "Core/OneBrahamTypes.h"
#include "Engine/DataAsset.h"
#include "UObject/SoftObjectPtr.h"
#include "OneBrahamVoiceTypes.generated.h"

class USoundBase;
class USoundAttenuation;

/**
 * How important a behaviour line is.
 *
 * The subsystem runs a single-line-at-a-time budget per character, and priority
 * decides who wins when two systems want to talk. Tactical information always
 * beats flavour, because a player needs "reloading!" more than "lovely carpet".
 */
UENUM(BlueprintType)
enum class EOneBrahamVoicePriority : uint8
{
	/** Idle muttering. First thing to be dropped. */
	Ambient		= 0,
	/** Character flavour (a low, dry comment). */
	Flavour		= 1,
	/** Useful information. */
	Tactical	= 2,
	/** Combat-critical: contact, reload, downed, grenade. */
	Critical	= 3
};

/** One line of the voice script. */
USTRUCT(BlueprintType)
struct ONEBRAHAM_API FOneBrahamVoiceLine
{
	GENERATED_BODY()

	/** Stable id, e.g. V_DOWNED_CALL_HELP. The script in Docs/14_Voice_Script.md
	 *  and the recorded assets are keyed by this, so it is never renamed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Voice")
	FName LineId = NAME_None;

	/** On-screen subtitle. British English, dry, never a pun. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Voice")
	FText Subtitle;

	/** The recorded line. When null the subsystem generates nothing audible and
	 *  only shows the subtitle, which keeps the game readable pre-recording. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Voice")
	TSoftObjectPtr<USoundBase> Sound;

	/** Higher wins when two lines compete. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Voice")
	EOneBrahamVoicePriority Priority = EOneBrahamVoicePriority::Tactical;

	/** Global cooldown for this exact line, seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Voice")
	float CooldownSeconds = 8.f;

	/** Per-request chance of actually speaking, 0..1. Keeps lines from becoming
	 *  a metronome. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Voice")
	float Chance = 1.f;

	/** Which archetypes may say it. Empty = all six. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Voice")
	TArray<FGameplayTag> ArchetypeFilter;

	/** Subtitle display time; 0 = derive from the sound length. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Voice")
	float SubtitleSeconds = 0.f;
};

/**
 * The complete behaviour-line set for the game.
 *
 * Shipped as /Game/OneBraham/Voice/DA_VoiceLines. The code-only fallback table
 * in OneBrahamVoiceLineSet.cpp carries the same ids so nothing is ever missing;
 * a designer can then assign the recorded assets line by line.
 */
UCLASS(BlueprintType)
class ONEBRAHAM_API UOneBrahamVoiceLineSet : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Voice", meta = (TitleProperty = "LineId"))
	TArray<FOneBrahamVoiceLine> Lines;

	/** Adds any missing core line from the C++ fallback table. Idempotent. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Voice")
	void EnsureCoreLines();

	/** All lines with this id (there is normally exactly one). */
	void FindLines(FName LineId, TArray<const FOneBrahamVoiceLine*>& OutLines) const;

	/** Pick a variant at random, honouring the archetype filter. Null if none. */
	const FOneBrahamVoiceLine* PickLine(FName LineId, FGameplayTag Archetype, FRandomStream& Stream) const;
};

/** Live voice chat transport settings, resolved from the match settings asset. */
USTRUCT(BlueprintType)
struct ONEBRAHAM_API FOneBrahamVoiceChatConfig
{
	GENERATED_BODY()

	/** Frames per second the server will relay per talker before it throttles. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Voice|Transport")
	int32 MaxFramesPerSecond = 60;

	/** Bytes per second the server will relay per talker. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Voice|Transport")
	int32 MaxBytesPerSecond = 64000;

	/** Frames per second sent by the local capture path (20 ms frames). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Voice|Transport")
	int32 SendFramesPerSecond = 50;

	/** Explicit proximity range override in cm. 0 = unset, which is the default:
	 *  the match settings asset is authoritative whenever it names a range, and
	 *  this only wins when no match settings exist (tests, a bare PIE map). It
	 *  used to default to 3500, which silently shadowed the match setting. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Voice|Transport")
	float ProximityRangeCm = 0.f;

	/** Jitter buffer depth in frames. 3 frames = 60 ms, imperceptible locally,
	 *  enough to ride out a mobile-style connection spike. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Voice|Transport")
	int32 JitterBufferFrames = 3;

	/** Radio-filter cutoff for Squad/Team channels. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Voice|Transport")
	float RadioLowPassHz = 3200.f;
};

/** Per-talker relay accounting, used to enforce the transport budget server side. */
USTRUCT()
struct FOneBrahamVoiceBudget
{
	GENERATED_BODY()

	/** Ring of frame timestamps for the last second. */
	TArray<float> RecentFrameTimes;
	TArray<int32> RecentFrameSizes;
	float WindowStart = 0.f;
	int32 DroppedFrames = 0;

	/** True when the talker is inside budget. Records the frame when it passes. */
	bool TryConsume(float Now, int32 FrameBytes, int32 MaxFramesPerSecond, int32 MaxBytesPerSecond);
	void Reset();
};

// ===========================================================================
//  Pure voice rules
// ---------------------------------------------------------------------------
//  Everything below is deliberately free of UObjects and worlds, so the rules
//  that decide who hears whom, how loud, and when a microphone is "speaking"
//  can be asserted by the automation tests without a running match.
// ===========================================================================

/**
 * Voice activity detector: RMS level against a noise gate, with hysteresis and
 * a hang time.
 *
 * WHY THESE THREE PARTS
 *   - The gate alone chops words: speech dips below any useful threshold
 *     between syllables, so the gate needs a hang time that holds it open.
 *   - Hysteresis (closing at a lower level than it opened) stops a voice that
 *     sits right on the threshold from fluttering the HUD icon on and off.
 *   - The adaptive noise floor follows a fan or a noisy room while the gate is
 *     closed, so a threshold that works in a quiet bedroom does not hold the
 *     gate open forever next to a desktop tower.
 */
struct ONEBRAHAM_API FOneBrahamVoiceActivityDetector
{
	/** Linear RMS (0..1 of full scale) that opens the gate. */
	float OpenThreshold = 0.009f;

	/** The gate closes below OpenThreshold * CloseRatio. */
	float CloseRatio = 0.6f;

	/** How long the gate stays open after the level drops. */
	float HangSeconds = 0.4f;

	/** The gate never opens below NoiseFloor * NoiseFloorMargin. */
	float NoiseFloorMargin = 2.5f;

	// --- state ---
	bool bOpen = false;
	float HangRemaining = 0.f;
	float NoiseFloor = 0.f;

	/** RMS of PCM16 samples as a 0..1 fraction of full scale. */
	static float ComputeRms(const int16* Samples, int32 NumSamples);

	/** Feeds one frame's RMS. Returns true when this frame should be transmitted. */
	bool ProcessFrame(float FrameRms, float FrameSeconds);

	/** The level the gate needs right now, noise floor included. */
	float GetEffectiveOpenThreshold() const;

	void Reset();
};

/**
 * One side of a routing decision, flattened out of a PlayerState so the rule
 * can run in a test. The server fills it from replicated state.
 */
struct ONEBRAHAM_API FOneBrahamVoiceRouteParty
{
	int32 PlayerId = INDEX_NONE;
	EOneBrahamTeam Team = EOneBrahamTeam::None;
	int32 SquadId = INDEX_NONE;

	/** Dead or spectating. Downed players are alive for voice: they are the ones
	 *  the squad most needs to hear. */
	bool bEliminated = false;

	/** Microphone muted by its owner. A self-muted listener still hears. */
	bool bSelfMuted = false;

	/** The channel the player selected (only meaningful for a speaker). */
	EOneBrahamVoiceChannel Channel = EOneBrahamVoiceChannel::Proximity;

	/**
	 * Speaker: where the voice comes from (their pawn). Listener: where they hear
	 * from - their own pawn when alive, the spectated player's pawn when
	 * eliminated. bHasLocation false means "nowhere in the world": a dead
	 * speaker, or a spectator with nobody to watch.
	 */
	bool bHasLocation = false;
	FVector Location = FVector::ZeroVector;

	/** Listener only: players this listener has muted. Not owned. */
	const TArray<int32>* MutedPlayerIds = nullptr;
};

/** Match-level switches the routing rule honours. */
struct ONEBRAHAM_API FOneBrahamVoiceRouteRules
{
	float ProximityRangeCm = 3500.f;
	bool bProximityEnabled = true;
	bool bTeamEnabled = true;
};

/** What a proximity voice should sound like at the listener: gain and muffle. */
struct ONEBRAHAM_API FOneBrahamProximityVoiceShaping
{
	/** Linear gain, 0..1, applied on top of the user's voice volume. */
	float Gain = 1.f;

	/** Low-pass cutoff in Hz; 20 kHz is "no filter". */
	float LowPassHz = 20000.f;
};

namespace OneBrahamVoiceRouting
{
	/** Cutoff treated as "unfiltered". */
	constexpr float ClearLowPassHz = 20000.f;

	/** Distance at which the inverse-distance rolloff starts. Inside this a voice
	 *  is at full level, which is roughly "the person next to you". */
	constexpr float ReferenceDistanceCm = 250.f;

	/** The channel a speaker's frames actually travel on. Eliminated players are
	 *  forced onto squad radio whatever they selected - they keep talking to
	 *  their squad, but a corpse never gives away positions over proximity. */
	ONEBRAHAM_API EOneBrahamVoiceChannel ResolveSpeakerChannel(const FOneBrahamVoiceRouteParty& Speaker);

	/**
	 * The server's routing rule. True when Listener should receive Speaker's
	 * frame; OutChannel is the channel the listener should play it on.
	 */
	ONEBRAHAM_API bool ResolveRoute(const FOneBrahamVoiceRouteParty& Listener, const FOneBrahamVoiceRouteParty& Speaker,
		const FOneBrahamVoiceRouteRules& Rules, EOneBrahamVoiceChannel& OutChannel);

	/**
	 * How a proximity voice should sound.
	 *
	 * @param DistanceCm        speaker mouth to listener ear
	 * @param RangeCm           the match's proximity range (gain is 0 at and beyond it)
	 * @param BlockingSurfaces  VoiceOccluder surfaces between them (0 = clear)
	 * @param FloorsApart       storeys between them (0 = same floor)
	 * @param bSpeakerCrouched  crouched speakers talk under their breath
	 */
	ONEBRAHAM_API FOneBrahamProximityVoiceShaping ShapeProximityVoice(float DistanceCm, float RangeCm,
		int32 BlockingSurfaces, int32 FloorsApart, bool bSpeakerCrouched);

	/** Range precedence: match settings, then an explicit override, then the
	 *  project default, then the documented 3500 cm. Non-positive means unset. */
	ONEBRAHAM_API float ResolveProximityRangeCm(float MatchSettingsRangeCm, float OverrideRangeCm, float ProjectDefaultRangeCm);
}

namespace OneBrahamVoiceCVars
{
	/** ob.Voice.InputGain - linear microphone gain applied before the gate. */
	ONEBRAHAM_API float GetInputGain();

	/** ob.Voice.OutputGain - linear gain on every received voice stream. */
	ONEBRAHAM_API float GetOutputGain();

	/** ob.Voice.NoiseGate - RMS threshold that opens voice activation. */
	ONEBRAHAM_API float GetNoiseGate();
}
