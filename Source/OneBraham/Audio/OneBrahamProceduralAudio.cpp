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
			TEXT("SFX_Gunshot"), TEXT("SFX_Gunshot_Suppressed"),
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
		else if (Id == TEXT("SFX_Gunshot"))				{ Wave = MakeGunshot(1.0f, false); }
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
		else if (Id == TEXT("SFX_LowAmmo_Click"))		{ Wave = MakeTone(2100.f, 0.05f, EOneBrahamToneShape::Square, 0.20f, 0.04f); }
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
