// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamProceduralAudio.h
// ---------------------------------------------------------------------------
// A no-assets-required audio layer.
//
// ONE BRAHAM ships with an authored audio bank (see Docs/12_Audio.md for the
// full session list), but the build must never be silent, and every gameplay
// cue that carries *information* (the grenade fuse, the low-ammo click, the
// flashbang ring, the extraction rotor, the crack of a round going past your
// head) is synthesised here in code as well.
//
// That gives us three things:
//   1. A playable build straight out of the repository, before a single wav
//      is dropped in.
//   2. A guaranteed audio cue for every mechanic - a fuse beep that exists is
//      a fuse beep that cannot be missed.
//   3. A headless CI smoke test that can assert a cue exists for every keyword
//      in the audio bank without needing the assets on disk.
//
// HOW A SYNTHESISED CUE PLAYS
//   Every cue is rendered once into an immutable PCM buffer, then wrapped in a
//   UOneBrahamSynthWave. That wave hands every play its own ISoundGenerator
//   with its own read cursor, so one cached wave can be played any number of
//   times, concurrently, from the start. (A plain USoundWaveProcedural with
//   QueueAudio is a single shared queue: the first play drains it and every
//   play after that is silence. That was this module's behaviour until the
//   generator was added.)
//
// VARIATION
//   Impacts, gunshots, shell drops and the rest are rendered as a handful of
//   variants with different random seeds - different grain positions, mode
//   frequencies and decay times - and GetSynthesisedSound hands out a different
//   variant from the last one. Together with the manager's per-play pitch jitter
//   that is what stops a burst of fire sounding like a sample on repeat.
//
// All waves are mono 44.1 kHz, transient, and cached for the process lifetime.

#pragma once

#include "CoreMinimal.h"
#include "Sound/SoundWaveProcedural.h"
#include <atomic>
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
 * Live controls for the heartbeat generator, written on the game thread and read
 * on the audio render thread. Atomics rather than a command queue because the
 * values are continuous: the generator only ever wants the latest one.
 */
struct FOneBrahamHeartbeatControl
{
	/** 0 = resting and inaudible, 1 = bleeding out at 150 bpm. */
	std::atomic<float> Intensity { 0.f };
};

/**
 * A procedural wave that gives every play its own generator.
 *
 * Holds either an immutable PCM buffer (every one-shot and loop the synthesiser
 * renders) or a heartbeat control block (the one cue that is synthesised live,
 * because its tempo follows the player's health).
 */
UCLASS(Transient)
class ONEBRAHAM_API UOneBrahamSynthWave : public USoundWaveProcedural
{
	GENERATED_BODY()

public:
	/** Plays this buffer from the start on every play; loops when bLoop is set. */
	void InitFromPcm(TSharedPtr<const TArray<int16>, ESPMode::ThreadSafe> InPcm, int32 InSampleRate, bool bLoop);

	/** Live heartbeat: an endless generator whose rate and level follow Control. */
	void InitAsHeartbeat(TSharedPtr<FOneBrahamHeartbeatControl, ESPMode::ThreadSafe> InControl, int32 InSampleRate);

	virtual ISoundGeneratorPtr CreateSoundGenerator(const FSoundGeneratorInitParams& InParams) override;

private:
	TSharedPtr<const TArray<int16>, ESPMode::ThreadSafe> Pcm;
	TSharedPtr<FOneBrahamHeartbeatControl, ESPMode::ThreadSafe> HeartbeatControl;
	bool bLoopPcm = false;
};

/**
 * Deterministic, dependency-free PCM synthesis.
 *
 * Everything is a namespace of static functions rather than a UObject so it can
 * be called from tests and from a commandlet.
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
	 * Filtered noise burst with a shaped envelope.
	 * @param LowPassAlpha  0 = no filtering (bright hiss), 0.99 = very dark thud.
	 */
	ONEBRAHAM_API USoundWaveProcedural* MakeNoiseBurst(float DurationSeconds, float LowPassAlpha = 0.6f,
		float Amplitude = 0.6f, float AttackSeconds = 0.001f, float BodySeconds = 0.05f);

	/**
	 * The close report of a gunshot: supersonic crack, muzzle blast and a short
	 * pressure thump, scaled by calibre. The distant layer, the action and the
	 * room tail are separate cues the audio manager layers on top.
	 * @param CalibreScale  0.5 = pistol, 1.0 = 5.56, 1.6 = 7.62 marksman.
	 * @param bSuppressed   A quiet thump with the bolt's clack baked in.
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

	/**
	 * A live heartbeat whose tempo and level follow Control->Intensity. Not
	 * cached: each audio manager owns its own, because each machine's listener
	 * has their own pulse.
	 */
	ONEBRAHAM_API USoundWaveProcedural* MakeHeartbeatWave(TSharedPtr<FOneBrahamHeartbeatControl, ESPMode::ThreadSafe> Control);

	/** True if this id has a synthesised implementation. */
	ONEBRAHAM_API bool HasSynthesisedSound(FName SoundId);

	/**
	 * Returns a synthesised wave for a sound id, or null when the id is
	 * asset-only. Every variant of a cue is rendered on the first call and
	 * cached for the life of the process; each later call returns a variant
	 * other than the one returned last time.
	 */
	ONEBRAHAM_API USoundWaveProcedural* GetSynthesisedSound(FName SoundId);

	/**
	 * Renders every combat cue now rather than on its first play. Rendering an
	 * explosion's variants takes a few milliseconds; doing it on the frame the
	 * first grenade goes off is a hitch exactly when the player is paying most
	 * attention. Idempotent and cheap after the first call.
	 */
	ONEBRAHAM_API void PrewarmCombatCues();

	/**
	 * Renders the next combat cue that is not cached yet, one per call, so the
	 * audio manager can spread the work over its first few frames instead of
	 * taking it all at once. Returns false once every combat cue is cached.
	 */
	ONEBRAHAM_API bool PrewarmNextCombatCue();

	/** Drops every cached synthesised wave. Called from module shutdown. */
	ONEBRAHAM_API void ReleaseSynthesisedSounds();
}
