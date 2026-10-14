// Copyright (c) One Braham. All Rights Reserved.

#include "Audio/OneBrahamProceduralAudio.h"

#include "OneBraham.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

namespace OneBrahamProceduralAudio
{
	// -----------------------------------------------------------------------
	//  Internal helpers
	// -----------------------------------------------------------------------
	namespace
	{
		/** A mono float buffer that is mixed in the 0..1 domain then packed to PCM16. */
		struct FBuffer
		{
			TArray<float> Samples;

			explicit FBuffer(float DurationSeconds)
			{
				const int32 Count = FMath::Max(1, FMath::CeilToInt(DurationSeconds * SampleRate));
				Samples.SetNumZeroed(Count);
			}

			int32 Num() const { return Samples.Num(); }
			float DurationSeconds() const { return static_cast<float>(Num()) / static_cast<float>(SampleRate); }

			FORCEINLINE void Add(int32 Index, float Value)
			{
				if (Samples.IsValidIndex(Index))
				{
					Samples[Index] += Value;
				}
			}
		};

		/** Deterministic white noise so the same cue sounds identical every build. */
		struct FNoise
		{
			uint32 State = 0x1F2E3D4Cu;

			FORCEINLINE float Next()
			{
				// xorshift32 - cheap, deterministic, no shared RNG state.
				State ^= State << 13;
				State ^= State >> 17;
				State ^= State << 5;
				return (static_cast<float>(State) / static_cast<float>(0xFFFFFFFFu)) * 2.f - 1.f;
			}
		};

		FORCEINLINE float Oscillator(EOneBrahamToneShape Shape, float Phase01)
		{
			switch (Shape)
			{
			case EOneBrahamToneShape::Square:		return Phase01 < 0.5f ? 1.f : -1.f;
			case EOneBrahamToneShape::Triangle:		return 4.f * FMath::Abs(Phase01 - 0.5f) - 1.f;
			case EOneBrahamToneShape::Sine:
			default:								return FMath::Sin(Phase01 * 2.f * PI);
			}
		}

		/** Exponential decay to -60 dB over DecaySeconds. */
		FORCEINLINE float DecayGain(float TimeSeconds, float DecaySeconds)
		{
			if (DecaySeconds <= KINDA_SMALL_NUMBER)
			{
				return 1.f;
			}
			return FMath::Pow(10.f, -3.f * TimeSeconds / DecaySeconds);
		}

		/** Attack/body/release envelope shaped like a percussive transient. */
		FORCEINLINE float PercussiveEnv(float TimeSeconds, float AttackSeconds, float BodySeconds, float ReleaseSeconds)
		{
			if (TimeSeconds < AttackSeconds)
			{
				return AttackSeconds <= KINDA_SMALL_NUMBER ? 1.f : (TimeSeconds / AttackSeconds);
			}
			const float AfterAttack = TimeSeconds - AttackSeconds;
			if (AfterAttack < BodySeconds)
			{
				return 1.f;
			}
			const float IntoRelease = AfterAttack - BodySeconds;
			return ReleaseSeconds <= KINDA_SMALL_NUMBER ? 0.f : FMath::Max(0.f, 1.f - (IntoRelease / ReleaseSeconds));
		}

		/** One-pole low-pass; Alpha near 1 = dark. */
		FORCEINLINE float LowPass(float Input, float& State, float Alpha)
		{
			State = State + (1.f - Alpha) * (Input - State);
			return State;
		}

		/** Soft clip so nothing ever leaves the speaker as a digital square edge. */
		FORCEINLINE float SoftClip(float In)
		{
			return FMath::Tanh(In * 1.15f);
		}

		/** Pack a mixed float buffer into a playable transient procedural wave. */
		USoundWaveProcedural* Bake(const FBuffer& Buffer, float Gain, bool bLooping)
		{
			const int32 Count = Buffer.Num();

			// Find the peak, then normalise to the requested gain. This keeps every
			// synthesised cue at a predictable level relative to the others, which
			// matters far more than absolute loudness.
			float Peak = 0.f;
			for (const float Sample : Buffer.Samples)
			{
				Peak = FMath::Max(Peak, FMath::Abs(Sample));
			}
			const float Normalise = Peak > KINDA_SMALL_NUMBER ? (Gain / Peak) : 0.f;

			TArray<int16> Pcm;
			Pcm.SetNumUninitialized(Count);

			for (int32 Index = 0; Index < Count; ++Index)
			{
				const float Value = SoftClip(Buffer.Samples[Index] * Normalise);
				Pcm[Index] = static_cast<int16>(FMath::Clamp(Value, -1.f, 1.f) * 32767.f);
			}

			USoundWaveProcedural* Wave = NewObject<USoundWaveProcedural>(GetTransientPackage());
			Wave->SetSampleRate(SampleRate);
			Wave->NumChannels = 1;
			Wave->Duration = Buffer.DurationSeconds();
			Wave->SoundGroup = SOUNDGROUP_Default;
			Wave->bLooping = bLooping;
			Wave->Volume = 1.f;
			Wave->bCanProcessAsync = false;

			// QueueAudio copies into the wave's internal ring buffer.
			Wave->QueueAudio(reinterpret_cast<const uint8*>(Pcm.GetData()), Pcm.Num() * sizeof(int16));

			return Wave;
		}

		/** Process-wide cache of synthesised waves. Released on module shutdown. */
	TMap<FName, TStrongObjectPtr<USoundWaveProcedural>> GSynthesisedCache;

	/** A metallic click: a short bright noise transient plus two ringing partials. */
		FBuffer MakeClickBuffer(float DurationSeconds, float LowAlpha, float RingHz, float RingGain)
		{
			FBuffer Buffer(DurationSeconds);
			FNoise Noise;
			float LpState = 0.f;

			for (int32 Index = 0; Index < Buffer.Num(); ++Index)
			{
				const float Time = static_cast<float>(Index) / static_cast<float>(SampleRate);

				const float Env = PercussiveEnv(Time, 0.0008f, 0.004f, DurationSeconds - 0.005f);
				const float Body = LowPass(Noise.Next(), LpState, LowAlpha) * Env;

				const float Ring = (FMath::Sin(2.f * PI * RingHz * Time) * 0.5f
					+ FMath::Sin(2.f * PI * RingHz * 1.63f * Time) * 0.3f)
					* DecayGain(Time, DurationSeconds * 0.6f) * RingGain;

				Buffer.Add(Index, Body + Ring);
			}

			return Buffer;
		}
	}

	// -----------------------------------------------------------------------
	//  Public synthesis
	// -----------------------------------------------------------------------
	USoundWaveProcedural* MakeTone(float FrequencyHz, float DurationSeconds, EOneBrahamToneShape Shape,
		float Amplitude, float DecaySeconds)
	{
		FBuffer Buffer(FMath::Max(0.005f, DurationSeconds));

		const bool bSweep = Shape == EOneBrahamToneShape::SweepDown || Shape == EOneBrahamToneShape::SweepUp;
		const float StartHz = bSweep ? (Shape == EOneBrahamToneShape::SweepDown ? FrequencyHz * 3.f : FrequencyHz * 0.4f) : FrequencyHz;
		const float EndHz = bSweep ? (Shape == EOneBrahamToneShape::SweepDown ? FrequencyHz * 0.4f : FrequencyHz * 3.f) : FrequencyHz;
		const EOneBrahamToneShape Wave = bSweep ? EOneBrahamToneShape::Sine : Shape;

		float Phase = 0.f;
		for (int32 Index = 0; Index < Buffer.Num(); ++Index)
		{
			const float Time = static_cast<float>(Index) / static_cast<float>(SampleRate);
			const float Alpha = Time / Buffer.DurationSeconds();
			const float Hz = FMath::Lerp(StartHz, EndHz, Alpha * Alpha);

			Phase = FMath::Fmod(Phase + (Hz / static_cast<float>(SampleRate)), 1.f);

			// 3 ms attack removes the click at the start of a hard-edged square.
			const float Attack = FMath::Min(1.f, Time / 0.003f);
			const float Gain = DecaySeconds > 0.f ? DecayGain(Time, DecaySeconds) : 1.f;

			Buffer.Add(Index, Oscillator(Wave, Phase) * Attack * Gain);
		}

		return Bake(Buffer, Amplitude, false);
	}

	USoundWaveProcedural* MakeNoiseBurst(float DurationSeconds, float LowPassAlpha, float Amplitude,
		float AttackSeconds, float BodySeconds)
	{
		FBuffer Buffer(FMath::Max(0.01f, DurationSeconds));
		FNoise Noise;
		float LpState = 0.f;

		const float Release = FMath::Max(0.01f, Buffer.DurationSeconds() - AttackSeconds - BodySeconds);

		for (int32 Index = 0; Index < Buffer.Num(); ++Index)
		{
			const float Time = static_cast<float>(Index) / static_cast<float>(SampleRate);
			const float Env = PercussiveEnv(Time, AttackSeconds, BodySeconds, Release);
			Buffer.Add(Index, LowPass(Noise.Next(), LpState, LowPassAlpha) * Env);
		}

		return Bake(Buffer, Amplitude, false);
	}

	USoundWaveProcedural* MakeGunshot(float CalibreScale, bool bSuppressed)
	{
		const float Scale = FMath::Clamp(CalibreScale, 0.4f, 2.2f);
		const float Duration = bSuppressed ? 0.14f : (0.28f + 0.14f * Scale);

		FBuffer Buffer(Duration);
		FNoise Noise;

		// Three layers, which is the minimum for a gunshot to read as a gunshot:
		//   crack    - the supersonic snap, very short and very bright
		//   body     - the propellant blast, a filtered noise burst with a thump
		//   tail     - room reflection, longer and darker (removed when suppressed)
		const float CrackDecay = bSuppressed ? 0.010f : 0.022f / Scale;
		const float BodyDecay = bSuppressed ? 0.055f : 0.10f + 0.05f * Scale;
		const float TailDecay = 0.22f * Scale;

		float CrackLp = 0.f;
		float BodyLp = 0.f;
		float TailLp = 0.f;

		for (int32 Index = 0; Index < Buffer.Num(); ++Index)
		{
			const float Time = static_cast<float>(Index) / static_cast<float>(SampleRate);
			const float NoiseSample = Noise.Next();

			// Bright crack: almost unfiltered.
			const float Crack = LowPass(NoiseSample, CrackLp, bSuppressed ? 0.55f : 0.08f)
				* DecayGain(Time, CrackDecay) * (bSuppressed ? 0.45f : 1.f);

			// Blast body: dark, with a low-frequency 'thump' from a decaying sine.
			const float BodyThump = FMath::Sin(2.f * PI * (95.f / Scale) * Time) * DecayGain(Time, BodyDecay * 0.7f);
			const float Body = LowPass(NoiseSample, BodyLp, 0.72f) * DecayGain(Time, BodyDecay) * 1.4f
				+ BodyThump * 0.9f;

			// Room tail: suppressed weapons have almost none, which is the point.
			const float Tail = bSuppressed
				? 0.f
				: LowPass(NoiseSample, TailLp, 0.93f) * DecayGain(FMath::Max(0.f, Time - 0.02f), TailDecay) * 0.55f;

			Buffer.Add(Index, Crack + Body + Tail);
		}

		return Bake(Buffer, bSuppressed ? 0.30f : 0.72f, false);
	}

	USoundWaveProcedural* MakeTinnitusRinging(float DurationSeconds)
	{
		FBuffer Buffer(FMath::Max(0.5f, DurationSeconds));

		// Two close high partials beat against each other - that slow wobble is what
		// makes it read as "my ears are ringing" rather than "a sine wave is playing".
		const float HzA = 4180.f;
		const float HzB = 4235.f;

		for (int32 Index = 0; Index < Buffer.Num(); ++Index)
		{
			const float Time = static_cast<float>(Index) / static_cast<float>(SampleRate);
			const float Fade = FMath::Min(1.f, Time / 0.05f);
			const float Gain = DecayGain(Time, DurationSeconds * 0.85f);

			const float Value = (FMath::Sin(2.f * PI * HzA * Time) + FMath::Sin(2.f * PI * HzB * Time) * 0.85f) * 0.5f;
			Buffer.Add(Index, Value * Fade * Gain);
		}

		return Bake(Buffer, 0.22f, false);
	}

	USoundWaveProcedural* MakeHelicopterRotorLoop(float LoopSeconds)
	{
		const float Period = FMath::Max(0.35f, LoopSeconds);
		FBuffer Buffer(Period);
		FNoise Noise;
		float LpState = 0.f;

		// Main rotor: two blade slaps per revolution, each a filtered noise puff, over
		// a continuous low harmonic hum from the turbine.
		const float Slaps = 2.f;
		const float SlapDecay = 0.09f;

		for (int32 Index = 0; Index < Buffer.Num(); ++Index)
		{
			const float Time = static_cast<float>(Index) / static_cast<float>(SampleRate);
			const float Phase = FMath::Fmod(Time / Period, 1.f);

			float Slap = 0.f;
			for (int32 SlapIndex = 0; SlapIndex < 2; ++SlapIndex)
			{
				const float SlapTime = Phase * Period - (SlapIndex * Period / Slaps);
				if (SlapTime >= 0.f)
				{
					Slap += LowPass(Noise.Next(), LpState, 0.86f) * DecayGain(SlapTime, SlapDecay);
				}
			}

			const float Turbine = FMath::Sin(2.f * PI * 44.f * Phase) * 0.35f
				+ FMath::Sin(2.f * PI * 61.f * Phase) * 0.18f;

			Buffer.Add(Index, Slap * 0.9f + Turbine);
		}

		// Crossfade the last 40 ms into the first 40 ms so the loop is seamless.
		const int32 FadeCount = FMath::Min(Buffer.Num() / 4, FMath::RoundToInt(0.04f * SampleRate));
		for (int32 Index = 0; Index < FadeCount; ++Index)
		{
			const float Alpha = static_cast<float>(Index) / static_cast<float>(FadeCount);
			const int32 TailIndex = Buffer.Num() - FadeCount + Index;

			const float Head = Buffer.Samples[Index];
			const float Tail = Buffer.Samples[TailIndex];
			Buffer.Samples[TailIndex] = FMath::Lerp(Head, Tail, Alpha);
		}

		return Bake(Buffer, 0.55f, true);
	}

	USoundWaveProcedural* MakeFireAlarmLoop(float LoopSeconds)
	{
		// Cadence of a UK commercial two-stage alarm, which is what actually sits in
		// the stairwells of a City office: 0.6 s at 970 Hz, beat of silence, 0.6 s at
		// 780 Hz, longer silence. Two cycles per loop and it never draws attention.
		struct FStage { float Start; float Length; float Hz; };
		static const FStage Stages[] = {
			{ 0.00f, 0.60f, 970.f },
			{ 0.65f, 0.60f, 780.f },
			{ 1.50f, 0.60f, 970.f },
			{ 2.15f, 0.60f, 780.f }
		};

		const float Period = FMath::Max(1.0f, LoopSeconds);
		FBuffer Buffer(Period);

		for (int32 Index = 0; Index < Buffer.Num(); ++Index)
		{
			const float Time = static_cast<float>(Index) / static_cast<float>(SampleRate);
			const float Wrapped = FMath::Fmod(Time, Period);

			float Value = 0.f;
			for (const FStage& Stage : Stages)
			{
				if (Wrapped >= Stage.Start && Wrapped <= Stage.Start + Stage.Length)
				{
					const float Local = Wrapped - Stage.Start;
					const float Env = FMath::Min(Local / 0.02f, (Stage.Length - Local) / 0.02f);
					Value = FMath::Sin(2.f * PI * Stage.Hz * Time) * FMath::Clamp(Env, 0.f, 1.f);
					break;
				}
			}

			Buffer.Add(Index, Value);
		}

		return Bake(Buffer, 0.40f, true);
	}

	/**
	 * True for the ids MakeUISound can synthesise.
	 *
	 * This list and MakeUISound's branches are the same list, and they have to
	 * stay that way: this function is what HasSynthesisedSound answers with, and
	 * that answer is what the audio bank's validator uses to decide whether a cue
	 * with no asset is silent. When the two drifted, the bank reported twenty
	 * three working UI cues as silent - the recipes were all there, under the
	 * names the game plays (UI_HitMarker, UI_KillConfirm, UI_LowAmmo_Click), while
	 * the whitelist still listed the addresses an older naming scheme used.
	 */
	bool IsSynthesisedUICue(const FString& Id)
	{
		static const TCHAR* const Substrings[] = {
			TEXT("Hover"), TEXT("Accept"), TEXT("Confirm"), TEXT("Deny"), TEXT("Error"),
			TEXT("ScoreTick"), TEXT("LowAmmo"), TEXT("HitMarker"), TEXT("Kill"), TEXT("Click")
		};

		for (const TCHAR* const Substring : Substrings)
		{
			if (Id.Contains(Substring))
			{
				return true;
			}
		}

		return false;
	}

	// -----------------------------------------------------------------------
	//  Ambience beds
	//
	//  A room with no bed is a silent room, and in a game where everything a
	//  player knows about a fight two floors up arrives as sound, a silent room
	//  is a broken room. This is one parameterised recipe rather than eleven
	//  hand-written loops: a zone is a small set of numbers saying what its air
	//  is doing, which is how Docs/12 describes the beds anyway.
	//
	//  An authored wave at the path the bank names always wins. These are what
	//  plays until then, and on a fresh clone they are the difference between an
	//  empty building and One Braham at four in the afternoon.
	// -----------------------------------------------------------------------
	struct FAmbienceRecipe
	{
		/** Bed length. Long enough that the loop point is inaudible. */
		float Duration = 6.f;

		/** Noise colour: 0 = a dark rumble, 1 = bright hiss (rain, fan walls). */
		float Colour = 0.5f;
		float NoiseLevel = 0.5f;

		/** Two continuous tones: plant hum, extractor, wind sub. */
		float ToneLowHz = 0.f;
		float ToneHighHz = 0.f;
		float ToneLevel = 0.f;

		/** A repeating event: a lift arriving, a clock, a fridge cycling, a
		 *  distant room alarm. A period of 0 disables it. */
		float EventPeriodSeconds = 0.f;
		float EventHz = 0.f;
		float EventLevel = 0.f;

		/** Slow gust on the noise layer, so the bed breathes. 0 = steady. */
		float GustHz = 0.f;
		float GustDepth = 0.f;
	};

	/** The bed for an id, or null when the id is not one of ours. */
	const FAmbienceRecipe* GetAmbienceRecipe(FName SoundId)
	{
		// A plain aggregate rather than TPair: TTuple's templated constructor
		// cannot deduce a nested braced recipe initialiser.
		struct FAmbienceEntry
		{
			FName Key;
			FAmbienceRecipe Value;
		};

		// Ordered as the bank is, so the two read side by side.
		static const FAmbienceEntry Recipes[] = {
			// Street: rain on the canopy over traffic, and a bus idling.
			{ TEXT("AMB_Plaza_Street"),   { 7.f, 0.62f, 0.55f, 38.f, 57.f, 0.16f, 3.6f, 118.f, 0.20f, 0.22f, 0.30f } },
			// The same weather on glass, close and brighter, no traffic.
			{ TEXT("AMB_Plaza_Rain"),     { 6.f, 0.88f, 0.60f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.35f, 0.16f } },
			// Marble hall: nothing moving, and the room itself.
			{ TEXT("AMB_Lobby"),          { 8.f, 0.30f, 0.40f, 96.f, 143.f, 0.13f, 0.f, 0.f, 0.f, 0.10f, 0.20f } },
			// Lift lobby: shaft machinery, and a car arriving every four seconds.
			{ TEXT("AMB_Lift_Lobby"),     { 8.f, 0.34f, 0.42f, 52.f, 78.f, 0.24f, 4.f, 660.f, 0.22f, 0.08f, 0.14f } },
			// Open plan: HVAC, and the monitors nobody turned off.
			{ TEXT("AMB_Office_Hum"),     { 6.f, 0.44f, 0.46f, 120.f, 315.f, 0.15f, 0.f, 0.f, 0.f, 0.06f, 0.12f } },
			// Server floor: a wall of fans with a room alarm chirping off-beat.
			{ TEXT("AMB_Server_Room"),    { 6.f, 0.74f, 0.66f, 120.f, 240.f, 0.18f, 5.5f, 1980.f, 0.12f, 0.05f, 0.10f } },
			// Executive: the quietest room in the building, and a clock.
			{ TEXT("AMB_Executive"),      { 8.f, 0.22f, 0.24f, 62.f, 0.f, 0.10f, 1.f, 1320.f, 0.10f, 0.f, 0.f } },
			// Canteen: extractor hoods over a fridge cycling under the counter.
			{ TEXT("AMB_Canteen"),        { 6.f, 0.68f, 0.58f, 88.f, 176.f, 0.18f, 1.5f, 0.f, 0.26f, 0.12f, 0.18f } },
			// Skydeck: wind past the parapet, and London a long way underneath.
			{ TEXT("AMB_Skydeck_Wind"),   { 8.f, 0.52f, 0.78f, 24.f, 41.f, 0.20f, 0.f, 0.f, 0.f, 0.14f, 0.52f } },
			// Stairwell: a concrete tube, and a fire door someone else closed.
			{ TEXT("AMB_Stairwell_Door"), { 8.f, 0.26f, 0.30f, 44.f, 0.f, 0.14f, 11.f, 92.f, 0.28f, 0.04f, 0.08f } },
			// The fallback bed: a city, heard from inside.
			{ TEXT("AMB_London_Bed"),     { 8.f, 0.40f, 0.44f, 45.f, 71.f, 0.14f, 0.f, 0.f, 0.f, 0.09f, 0.20f } }
		};

		for (const FAmbienceEntry& Pair : Recipes)
		{
			if (Pair.Key == SoundId)
			{
				return &Pair.Value;
			}
		}

		return nullptr;
	}

	bool IsSynthesisedAmbience(FName SoundId)
	{
		return GetAmbienceRecipe(SoundId) != nullptr;
	}

	USoundWaveProcedural* MakeAmbienceLoop(FName SoundId)
	{
		const FAmbienceRecipe* Recipe = GetAmbienceRecipe(SoundId);

		if (Recipe == nullptr)
		{
			return nullptr;
		}

		FBuffer Buffer(Recipe->Duration);
		FNoise Noise;
		float LowState = 0.f;
		float EventState = 0.f;

		const float EventPeriod = Recipe->EventPeriodSeconds;
		const float EventDecay = 0.09f;

		for (int32 Index = 0; Index < Buffer.Num(); ++Index)
		{
			const float Time = static_cast<float>(Index) / static_cast<float>(SampleRate);

			const float Gust = Recipe->GustDepth > 0.f
				? 1.f + Recipe->GustDepth * FMath::Sin(2.f * PI * Recipe->GustHz * Time)
				: 1.f;
			const float Bed = LowPass(Noise.Next(), LowState, Recipe->Colour) * Recipe->NoiseLevel * Gust;

			float Tone = 0.f;
			if (Recipe->ToneLowHz > 0.f)
			{
				Tone += FMath::Sin(2.f * PI * Recipe->ToneLowHz * Time) * 0.6f;
			}
			if (Recipe->ToneHighHz > 0.f)
			{
				Tone += FMath::Sin(2.f * PI * Recipe->ToneHighHz * Time) * 0.4f;
			}
			Tone *= Recipe->ToneLevel;

			// The repeating event: a short knock through the noise bed, with an
			// optional ring on it (a lift chime, a clock, an alarm chirp).
			float Event = 0.f;
			if (EventPeriod > 0.f && Recipe->EventLevel > 0.f)
			{
				const float Phase = FMath::Fmod(Time, EventPeriod);
				const float Knock = DecayGain(Phase, EventDecay) * LowPass(Noise.Next(), EventState, 0.55f);
				const float Ring = Recipe->EventHz > 0.f
					? FMath::Sin(2.f * PI * Recipe->EventHz * Phase) * DecayGain(Phase, EventDecay * 4.f)
					: 0.f;
				Event = (Knock + Ring * 0.7f) * Recipe->EventLevel;
			}

			Buffer.Add(Index, Bed + Tone + Event);
		}

		// Crossfade the tail into the head. Without this the bed ticks once a
		// loop, which is the sort of detail that makes a game feel cheap.
		const int32 FadeCount = FMath::Min(Buffer.Num() / 4, FMath::RoundToInt(0.05f * SampleRate));
		for (int32 Index = 0; Index < FadeCount; ++Index)
		{
			const float Alpha = static_cast<float>(Index) / static_cast<float>(FadeCount);
			const int32 TailIndex = Buffer.Num() - FadeCount + Index;
			const float Head = Buffer.Samples[Index];
			const float Tail = Buffer.Samples[TailIndex];

			Buffer.Samples[TailIndex] = FMath::Lerp(Head, Tail, Alpha);
		}

		return Bake(Buffer, 0.42f, true);
	}

	USoundWaveProcedural* MakeUISound(FName SoundId)
	{
		const FString Id = SoundId.ToString();

		if (Id.Contains(TEXT("Hover")))
		{
			return MakeTone(1650.f, 0.045f, EOneBrahamToneShape::Sine, 0.16f, 0.03f);
		}
		if (Id.Contains(TEXT("Accept")) || Id.Contains(TEXT("Confirm")))
		{
			return MakeTone(880.f, 0.10f, EOneBrahamToneShape::Triangle, 0.30f, 0.09f);
		}
		if (Id.Contains(TEXT("Deny")) || Id.Contains(TEXT("Error")))
		{
			return MakeTone(180.f, 0.16f, EOneBrahamToneShape::Square, 0.26f, 0.14f);
		}
		if (Id.Contains(TEXT("ScoreTick")))
		{
			return MakeTone(1320.f, 0.06f, EOneBrahamToneShape::Sine, 0.22f, 0.05f);
		}
		if (Id.Contains(TEXT("LowAmmo")))
		{
			return MakeTone(2100.f, 0.05f, EOneBrahamToneShape::Square, 0.20f, 0.04f);
		}
		if (Id.Contains(TEXT("HitMarker")))
		{
			// Two-tone: sharp tick then a slightly higher confirmation.
			FBuffer Buffer(0.09f);
			FNoise Noise;
			float Lp = 0.f;
			for (int32 Index = 0; Index < Buffer.Num(); ++Index)
			{
				const float Time = static_cast<float>(Index) / static_cast<float>(SampleRate);
				const float Tone = FMath::Sin(2.f * PI * 1450.f * Time) * DecayGain(Time, 0.02f)
					+ FMath::Sin(2.f * PI * 2200.f * Time) * DecayGain(FMath::Max(0.f, Time - 0.012f), 0.02f) * 0.8f;
				Buffer.Add(Index, Tone + LowPass(Noise.Next(), Lp, 0.5f) * DecayGain(Time, 0.006f) * 0.5f);
			}
			return Bake(Buffer, 0.34f, false);
		}
		if (Id.Contains(TEXT("Kill")))
		{
			FBuffer Buffer(0.26f);
			for (int32 Index = 0; Index < Buffer.Num(); ++Index)
			{
				const float Time = static_cast<float>(Index) / static_cast<float>(SampleRate);
				const float First = FMath::Sin(2.f * PI * 740.f * Time) * DecayGain(Time, 0.04f);
				const float Second = FMath::Sin(2.f * PI * 1110.f * FMath::Max(0.f, Time - 0.05f))
					* DecayGain(FMath::Max(0.f, Time - 0.05f), 0.06f);
				Buffer.Add(Index, First + Second);
			}
			return Bake(Buffer, 0.32f, false);
		}
		if (Id.Contains(TEXT("Click")))
		{
			return Bake(MakeClickBuffer(0.035f, 0.45f, 2600.f, 0.25f), 0.28f, false);
		}

		return nullptr;
	}

	bool HasSynthesisedSound(FName SoundId)
	{
		static const TSet<FName> Known = {
			TEXT("SFX_FuseBeep"), TEXT("SFX_FuseBeep_Close"), TEXT("SFX_UtilityBounce"),
			TEXT("SFX_Flashbang_Ring"), TEXT("SFX_Flashbang_Bang"), TEXT("SFX_Frag_Explosion"),
			TEXT("SFX_Breach_Explosion"), TEXT("SFX_Breach_Plant"), TEXT("SFX_Smoke_Deploy"),
			TEXT("SFX_Wall_Collapse"), TEXT("SFX_Shutter_Close"),
			TEXT("SFX_Gunshot_Suppressed"),
			TEXT("SFX_Helicopter_Rotor"), TEXT("SFX_FireAlarm"), TEXT("SFX_Lockdown_Siren"),
			TEXT("SFX_Melee_Thud"), TEXT("SFX_Reload_Mag_Out"), TEXT("SFX_Reload_Mag_In"),
			TEXT("SFX_Reload_Bolt"), TEXT("SFX_Revive_Complete"), TEXT("SFX_Downed_Tinnitus"),
			TEXT("SFX_Extraction_Start"), TEXT("SFX_Override_Hum"), TEXT("SFX_Terminal_Accept"),
			TEXT("SFX_Terminal_Deny"), TEXT("SFX_Keycard_Use"), TEXT("SFX_Footstep_Carpet"),
			TEXT("SFX_Footstep_Hard"), TEXT("SFX_Footstep_Metal"), TEXT("SFX_Footstep_Water"),
			TEXT("UI_Click"), TEXT("UI_Hover"), TEXT("UI_Accept"), TEXT("UI_Deny"),
			TEXT("UI_ScoreTick")
		};

		if (Known.Contains(SoundId))
		{
			return true;
		}

		// The eleven ambience beds. Answering yes here is what stops the bank
		// validator reporting every room in the building as a silent cue - which
		// it did, because beds were briefs and nothing else.
		if (IsSynthesisedAmbience(SoundId))
		{
			return true;
		}

		// The rest of the UI set is named by what it does rather than by an
		// entry here: UI_HitMarker, UI_HitMarker_Headshot, UI_HitMarker_Armour,
		// UI_KillConfirm, UI_LowAmmo_Click, UI_Damage_Taken, UI_Banner and the
		// voice-channel blips all resolve through MakeUISound's substring
		// branches, which is exactly what this asks.
		//
		// SFX_LowAmmo_Click, SFX_HitMarker and SFX_KillConfirm used to be listed
		// here instead. Nothing in the project has ever asked for them - the UI
		// feedback goes through the UI_ names - so three of the four sounds a
		// player hears most often were reported as silent by the validator while
		// being perfectly audible.
		return IsSynthesisedUICue(SoundId.ToString());
	}

	USoundWaveProcedural* GetSynthesisedSound(FName SoundId)
	{
		if (const TStrongObjectPtr<USoundWaveProcedural>* Found = GSynthesisedCache.Find(SoundId))
		{
			return Found->Get();
		}

		USoundWaveProcedural* Wave = nullptr;
		const FString Id = SoundId.ToString();

		if (Id == TEXT("SFX_FuseBeep"))					{ Wave = MakeTone(2350.f, 0.055f, EOneBrahamToneShape::Square, 0.30f, 0.04f); }
		else if (Id == TEXT("SFX_FuseBeep_Close"))		{ Wave = MakeTone(2350.f, 0.065f, EOneBrahamToneShape::Square, 0.44f, 0.05f); }
		else if (Id == TEXT("SFX_UtilityBounce"))		{ Wave = MakeNoiseBurst(0.14f, 0.86f, 0.34f, 0.0008f, 0.008f); }
		else if (Id == TEXT("SFX_Flashbang_Ring"))		{ Wave = MakeTinnitusRinging(5.f); }
		else if (Id == TEXT("SFX_Flashbang_Bang"))		{ Wave = MakeNoiseBurst(0.55f, 0.55f, 0.85f, 0.0005f, 0.02f); }
		else if (Id == TEXT("SFX_Frag_Explosion"))		{ Wave = MakeNoiseBurst(0.95f, 0.70f, 0.90f, 0.0006f, 0.03f); }
		else if (Id == TEXT("SFX_Breach_Explosion"))	{ Wave = MakeNoiseBurst(1.25f, 0.80f, 0.95f, 0.0006f, 0.04f); }
		// A partition falling over is not an explosion: it is a long, dark, slow
		// collapse with a debris tail. Low-passed hard, so it reads as weight.
		else if (Id == TEXT("SFX_Wall_Collapse"))		{ Wave = MakeNoiseBurst(2.30f, 0.22f, 0.62f, 0.010f, 0.75f); }
		else if (Id == TEXT("SFX_Shutter_Close"))		{ Wave = MakeNoiseBurst(1.10f, 0.34f, 0.70f, 0.004f, 0.16f); }
		else if (Id == TEXT("SFX_Breach_Plant"))		{ Wave = MakeTone(1480.f, 0.28f, EOneBrahamToneShape::Square, 0.30f, 0.26f); }
		else if (Id == TEXT("SFX_Smoke_Deploy"))		{ Wave = MakeNoiseBurst(1.30f, 0.90f, 0.42f, 0.02f, 0.35f); }
		// SFX_Gunshot and SFX_LowAmmo_Click used to have branches here. Neither is
		// a bank row and nothing in the project plays either: weapons ask for
		// SFX_Gunshot_<Calibre> per category, and the low-ammo cue is
		// UI_LowAmmo_Click, which the substring synthesiser below answers. Both
		// were recipes for a cue that could not be requested.
		else if (Id == TEXT("SFX_Gunshot_Pistol"))		{ Wave = MakeGunshot(0.55f, false); }
		else if (Id == TEXT("SFX_Gunshot_SMG"))			{ Wave = MakeGunshot(0.75f, false); }
		else if (Id == TEXT("SFX_Gunshot_Shotgun"))		{ Wave = MakeGunshot(1.5f, false); }
		else if (Id == TEXT("SFX_Gunshot_Rifle"))		{ Wave = MakeGunshot(1.0f, false); }
		else if (Id == TEXT("SFX_Gunshot_Marksman"))	{ Wave = MakeGunshot(1.7f, false); }
		else if (Id == TEXT("SFX_Gunshot_Suppressed"))	{ Wave = MakeGunshot(0.7f, true); }
		else if (Id == TEXT("SFX_Helicopter_Rotor"))	{ Wave = MakeHelicopterRotorLoop(2.0f); }
		else if (Id == TEXT("SFX_FireAlarm"))			{ Wave = MakeFireAlarmLoop(3.0f); }
		else if (Id == TEXT("SFX_Lockdown_Siren"))		{ Wave = MakeTone(620.f, 1.6f, EOneBrahamToneShape::SweepUp, 0.35f, 0.f); }
		else if (Id == TEXT("SFX_Melee_Thud"))			{ Wave = MakeNoiseBurst(0.20f, 0.93f, 0.55f, 0.001f, 0.02f); }
		else if (Id == TEXT("SFX_Reload_Mag_Out"))		{ Wave = Bake(MakeClickBuffer(0.10f, 0.70f, 1450.f, 0.35f), 0.30f, false); }
		else if (Id == TEXT("SFX_Reload_Mag_In"))		{ Wave = Bake(MakeClickBuffer(0.13f, 0.62f, 1180.f, 0.40f), 0.34f, false); }
		else if (Id == TEXT("SFX_Reload_Bolt"))			{ Wave = Bake(MakeClickBuffer(0.16f, 0.48f, 2100.f, 0.55f), 0.38f, false); }
		else if (Id == TEXT("SFX_Revive_Complete"))		{ Wave = MakeTone(420.f, 0.55f, EOneBrahamToneShape::SweepUp, 0.34f, 0.35f); }
		else if (Id == TEXT("SFX_Downed_Tinnitus"))		{ Wave = MakeTone(300.f, 1.8f, EOneBrahamToneShape::Sine, 0.20f, 1.5f); }
		else if (Id == TEXT("SFX_Extraction_Start"))	{ Wave = MakeTone(140.f, 1.9f, EOneBrahamToneShape::SweepUp, 0.42f, 1.0f); }
		else if (Id == TEXT("SFX_Override_Hum"))		{ Wave = MakeTone(96.f, 1.0f, EOneBrahamToneShape::Sine, 0.16f, 0.f); }
		else if (Id == TEXT("SFX_Terminal_Accept"))		{ Wave = MakeTone(1240.f, 0.14f, EOneBrahamToneShape::Triangle, 0.32f, 0.11f); }
		else if (Id == TEXT("SFX_Terminal_Deny"))		{ Wave = MakeTone(220.f, 0.22f, EOneBrahamToneShape::Square, 0.28f, 0.18f); }
		else if (Id == TEXT("SFX_Keycard_Use"))			{ Wave = Bake(MakeClickBuffer(0.22f, 0.55f, 3120.f, 0.60f), 0.30f, false); }
		else if (Id == TEXT("SFX_Footstep_Carpet"))		{ Wave = MakeNoiseBurst(0.12f, 0.94f, 0.22f, 0.002f, 0.012f); }
		else if (Id == TEXT("SFX_Footstep_Hard"))		{ Wave = MakeNoiseBurst(0.11f, 0.55f, 0.28f, 0.001f, 0.008f); }
		else if (Id == TEXT("SFX_Footstep_Metal"))		{ Wave = Bake(MakeClickBuffer(0.20f, 0.35f, 2740.f, 0.45f), 0.30f, false); }
		else if (Id == TEXT("SFX_Footstep_Water"))		{ Wave = MakeNoiseBurst(0.22f, 0.80f, 0.34f, 0.003f, 0.05f); }
		// Ambience beds: the room you are standing in, before any wave is
		// authored. Eleven of these cover the whole building.
		else if (IsSynthesisedAmbience(SoundId))	{ Wave = MakeAmbienceLoop(SoundId); }
		else											{ Wave = MakeUISound(SoundId); }

		if (Wave == nullptr)
		{
			return nullptr;
		}

		GSynthesisedCache.Add(SoundId, TStrongObjectPtr<USoundWaveProcedural>(Wave));
		return Wave;
	}

	void ReleaseSynthesisedSounds()
	{
		GSynthesisedCache.Empty();
	}
}
