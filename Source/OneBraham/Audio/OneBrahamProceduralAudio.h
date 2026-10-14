// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamProceduralAudio.h
// ---------------------------------------------------------------------------
// A no-assets-required audio fallback layer.
//
// ONE BRAHAM ships with an authored audio bank (see Docs/12_Audio.md for the
// full session list), but the build must never be silent, and every gameplay
// cue that carries *information* (the grenade fuse, the low-ammo click, the
// flashbang ring, the extraction rotor) is synthesised here in code as well.
//
// That gives us three things:
//   1. A playable build straight out of the repository, before a single wav
//      is dropped in.
//   2. A guaranteed audio cue for every mechanic - a fuse beep that exists is
//      a fuse beep that cannot be missed.
//   3. A headless CI smoke test that can assert a cue exists for every keyword
//      in the audio bank without needing the assets on disk.
//
// All waves are mono 44.1 kHz 16-bit PCM, transient, and cheap.

#pragma once

#include "CoreMinimal.h"
#include "Sound/SoundWaveProcedural.h"
#include "OneBrahamProceduralAudio.generated.h"

/** Tone shape for the simple oscillator helpers. */
UENUM(BlueprintType)
enum class EOneBrahamToneShape : uint8
{
	Sine		UMETA(DisplayName = "Sine"),
	Square		UMETA(DisplayName = "Square"),
	Triangle	UMETA(DisplayName = "Triangle"),
	/** Falling exponential sweep - the classic film "system failure" tone. */
	SweepDown	UMETA(DisplayName = "Sweep Down"),
	/** Rising exponential sweep - lift arrival, override terminal accept. */
	SweepUp		UMETA(DisplayName = "Sweep Up")
};

/**
 * Deterministic, dependency-free PCM synthesis.
 *
 * Everything is a namespace of static functions rather than a UObject so it can
 * be called from the audio thread's friends, from tests, and from a commandlet.
 */
namespace OneBrahamProceduralAudio
{
	/** Output sample rate. 44.1 kHz mono - the smallest thing every platform accepts. */
	static constexpr int32 SampleRate = 44100;

	/**
	 * A simple tone with a percussive attack and exponential decay.
	 * @param FrequencyHz  Base frequency.
	 * @param DurationSeconds
	 * @param Shape        Waveform.
	 * @param Amplitude    0..1 peak.
	 * @param DecaySeconds How long the tail takes to fall to -60 dB (0 = no decay).
	 */
	ONEBRAHAM_API USoundWaveProcedural* MakeTone(float FrequencyHz, float DurationSeconds,
		EOneBrahamToneShape Shape = EOneBrahamToneShape::Sine, float Amplitude = 0.35f, float DecaySeconds = 0.f);

	/**
	 * Filtered noise burst with a shaped envelope - the basis of every impact.
	 * @param LowPassAlpha  0 = no filtering (bright hiss), 0.99 = very dark thud.
	 */
	ONEBRAHAM_API USoundWaveProcedural* MakeNoiseBurst(float DurationSeconds, float LowPassAlpha = 0.6f,
		float Amplitude = 0.6f, float AttackSeconds = 0.001f, float BodySeconds = 0.05f);

	/**
	 * A layer of two noise bursts plus a low thump and a short tail, scaled by
	 * calibre. Used for gunshots and explosions until authored audio is present.
	 * @param CalibreScale  0.5 = pistol, 1.0 = 5.56, 1.6 = 7.62 marksman.
	 * @param bSuppressed   Softer, duller, tail-free.
	 */
	ONEBRAHAM_API USoundWaveProcedural* MakeGunshot(float CalibreScale = 1.f, bool bSuppressed = false);

	/**
	 * Flashbang tinnitus: a high sine pair with a slow beating tremolo and a long
	 * decay. Loops cleanly enough to sit on top of the muffled world.
	 */
	ONEBRAHAM_API USoundWaveProcedural* MakeTinnitusRinging(float DurationSeconds = 5.f);

	/**
	 * Sustained helicopter rotor: a low blade-slap pulse train over a broad hum.
	 * Seamless loop at the given period so it can be looped by an audio component.
	 */
	ONEBRAHAM_API USoundWaveProcedural* MakeHelicopterRotorLoop(float LoopSeconds = 2.f);

	/**
	 * The fire-alarm two-tone that plays during a lockdown. Two alternating tones
	 * with the exact cadence of a UK commercial building alarm.
	 */
	ONEBRAHAM_API USoundWaveProcedural* MakeFireAlarmLoop(float LoopSeconds = 3.f);

	/** Short UI blip (menu move, accept, deny) - never more than 90 ms. */
	ONEBRAHAM_API USoundWaveProcedural* MakeUISound(FName SoundId);

	/** True if this id has a synthesised implementation. */
	ONEBRAHAM_API bool HasSynthesisedSound(FName SoundId);

	/**
	 * Returns a synthesised wave for a sound id, or null when the id is
	 * asset-only. Results are cached per id for the life of the process, so
	 * repeated calls are free.
	 */
	ONEBRAHAM_API USoundWaveProcedural* GetSynthesisedSound(FName SoundId);

	/** Drops every cached synthesised wave. Called from module shutdown. */
	ONEBRAHAM_API void ReleaseSynthesisedSounds();
}
