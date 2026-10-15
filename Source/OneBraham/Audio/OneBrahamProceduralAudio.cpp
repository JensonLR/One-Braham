// Copyright (c) One Braham. All Rights Reserved.

#include "Audio/OneBrahamProceduralAudio.h"

#include "OneBraham.h"
#include "Math/RandomStream.h"
#include "Misc/Crc.h"
#include "Sound/SoundGenerator.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectGlobals.h"

// ===========================================================================
//  Generators
//
//  One generator per play. This is the whole reason the module has its own
//  wave class: the mixer asks a procedural wave for a fresh ISoundGenerator
//  every time a source starts, so a cached cue can be fired by six rifles in
//  the same frame and each of them hears it from sample zero.
// ===========================================================================
namespace
{
	/** Reads an immutable PCM buffer with a private cursor. */
	class FOneBrahamPcmGenerator final : public ISoundGenerator
	{
	public:
		FOneBrahamPcmGenerator(TSharedPtr<const TArray<int16>, ESPMode::ThreadSafe> InPcm, bool bInLoop)
			: Pcm(MoveTemp(InPcm))
			, bLoop(bInLoop)
		{
		}

		virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) override
		{
			const int32 Total = Pcm.IsValid() ? Pcm->Num() : 0;
			const int16* Data = Total > 0 ? Pcm->GetData() : nullptr;

			int32 Written = 0;
			while (Written < NumSamples && Total > 0)
			{
				if (Cursor >= Total)
				{
					if (!bLoop)
					{
						break;
					}
					Cursor = 0;
				}

				const int32 Chunk = FMath::Min(NumSamples - Written, Total - Cursor);
				for (int32 Index = 0; Index < Chunk; ++Index)
				{
					OutAudio[Written + Index] = static_cast<float>(Data[Cursor + Index]) * (1.f / 32768.f);
				}
				Written += Chunk;
				Cursor += Chunk;
			}

			if (Written < NumSamples)
			{
				FMemory::Memzero(OutAudio + Written, (NumSamples - Written) * sizeof(float));
				bFinished = !bLoop;
			}

			return NumSamples;
		}

		// A one-shot that reports finished is a voice the mixer can reuse; one
		// that never does would hold a source until the component was destroyed.
		virtual bool IsFinished() const override { return bFinished; }
		virtual int32 GetNumChannels() const override { return 1; }

	private:
		TSharedPtr<const TArray<int16>, ESPMode::ThreadSafe> Pcm;
		int32 Cursor = 0;
		bool bLoop = false;
		bool bFinished = false;
	};

	/**
	 * The live heartbeat. Rendered sample by sample because its tempo is the
	 * player's health: a pre-baked loop can only be sped up by pitching it, and
	 * a pitched heartbeat at 150 bpm sounds like a cartoon rather than a body.
	 */
	class FOneBrahamHeartbeatGenerator final : public ISoundGenerator
	{
	public:
		FOneBrahamHeartbeatGenerator(TSharedPtr<FOneBrahamHeartbeatControl, ESPMode::ThreadSafe> InControl, float InSampleRate)
			: Control(MoveTemp(InControl))
			, Rate(FMath::Max(8000.f, InSampleRate))
		{
			// Start just before a beat so the first one lands immediately.
			BeatClock = 10.f;
		}

		virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) override
		{
			const float Target = Control.IsValid() ? FMath::Clamp(Control->Intensity.load(std::memory_order_relaxed), 0.f, 1.f) : 0.f;

			// ~0.6 s glide: the body does not change tempo between two samples.
			const float Smooth = 1.f - FMath::Exp(-1.f / (0.6f * Rate));
			const float Dt = 1.f / Rate;

			for (int32 Index = 0; Index < NumSamples; ++Index)
			{
				Intensity += (Target - Intensity) * Smooth;

				const float Bpm = FMath::Lerp(64.f, 152.f, Intensity);
				const float Interval = 60.f / Bpm;

				BeatClock += Dt;
				if (BeatClock >= Interval)
				{
					BeatClock = FMath::Fmod(BeatClock, Interval);
					LubPhase = 0.f;
					DubPhase = 0.f;
					// A heart is not a metronome: a few percent of level jitter per
					// beat is what keeps the loop from reading as a drum machine.
					BeatJitter = 0.9f + 0.2f * (static_cast<float>((NoiseState >> 8) & 0xFF) / 255.f);
				}

				// Systole shortens as the rate climbs, which is the tell of panic.
				const float DubOffset = FMath::Lerp(0.30f, 0.19f, Intensity);

				const float Lub = Thump(BeatClock, 62.f, 40.f, 0.075f, LubPhase);
				const float Dub = Thump(BeatClock - DubOffset, 78.f, 52.f, 0.055f, DubPhase) * 0.7f;

				NoiseState ^= NoiseState << 13;
				NoiseState ^= NoiseState >> 17;
				NoiseState ^= NoiseState << 5;
				const float White = (static_cast<float>(NoiseState) / 4294967295.f) * 2.f - 1.f;
				ChestLp += 0.02f * (White - ChestLp);

				const float Env = BeatEnv(BeatClock) + BeatEnv(BeatClock - DubOffset) * 0.6f;
				const float Level = FMath::Pow(Intensity, 0.75f) * BeatJitter;

				OutAudio[Index] = FMath::Tanh((Lub + Dub + ChestLp * 6.f * Env) * Level * 0.9f);
			}

			return NumSamples;
		}

		virtual int32 GetNumChannels() const override { return 1; }

	private:
		/** One valve closing: a low sine that sags in pitch as it decays. */
		float Thump(float Time, float StartHz, float EndHz, float Decay, float& Phase) const
		{
			if (Time < 0.f || Time > Decay * 4.f)
			{
				return 0.f;
			}
			const float Hz = EndHz + (StartHz - EndHz) * FMath::Exp(-Time / 0.04f);
			Phase = FMath::Fmod(Phase + Hz / Rate, 1.f);
			const float Attack = FMath::Min(1.f, Time / 0.006f);
			return FMath::Sin(2.f * PI * Phase) * Attack * FMath::Exp(-Time / Decay);
		}

		static float BeatEnv(float Time)
		{
			return Time < 0.f ? 0.f : FMath::Exp(-Time / 0.05f) * FMath::Min(1.f, Time / 0.004f);
		}

		TSharedPtr<FOneBrahamHeartbeatControl, ESPMode::ThreadSafe> Control;
		float Rate = 44100.f;
		float Intensity = 0.f;
		float BeatClock = 0.f;
		float BeatJitter = 1.f;
		float LubPhase = 0.f;
		float DubPhase = 0.f;
		float ChestLp = 0.f;
		uint32 NoiseState = 0x6D2B79F5u;
	};
}

void UOneBrahamSynthWave::InitFromPcm(TSharedPtr<const TArray<int16>, ESPMode::ThreadSafe> InPcm, int32 InSampleRate, bool bLoop)
{
	Pcm = MoveTemp(InPcm);
	HeartbeatControl.Reset();
	bLoopPcm = bLoop;

	const int32 Count = Pcm.IsValid() ? Pcm->Num() : 0;

	SetSampleRate(InSampleRate);
	NumChannels = 1;
	Duration = bLoop ? INDEFINITELY_LOOPING_DURATION : static_cast<float>(Count) / static_cast<float>(FMath::Max(1, InSampleRate));
	bLooping = bLoop;
	SoundGroup = SOUNDGROUP_Default;
	Volume = 1.f;
	bCanProcessAsync = true;
}

void UOneBrahamSynthWave::InitAsHeartbeat(TSharedPtr<FOneBrahamHeartbeatControl, ESPMode::ThreadSafe> InControl, int32 InSampleRate)
{
	Pcm.Reset();
	HeartbeatControl = MoveTemp(InControl);
	bLoopPcm = true;

	SetSampleRate(InSampleRate);
	NumChannels = 1;
	Duration = INDEFINITELY_LOOPING_DURATION;
	bLooping = true;
	SoundGroup = SOUNDGROUP_Default;
	Volume = 1.f;
	bCanProcessAsync = true;

	// Silent between beats and at zero intensity, but it must keep its clock:
	// a virtualised heartbeat would restart on the wrong beat when it returns.
	VirtualizationMode = EVirtualizationMode::PlayWhenSilent;
}

ISoundGeneratorPtr UOneBrahamSynthWave::CreateSoundGenerator(const FSoundGeneratorInitParams& InParams)
{
	if (HeartbeatControl.IsValid())
	{
		return MakeShared<FOneBrahamHeartbeatGenerator, ESPMode::ThreadSafe>(HeartbeatControl,
			static_cast<float>(OneBrahamProceduralAudio::SampleRate));
	}

	return MakeShared<FOneBrahamPcmGenerator, ESPMode::ThreadSafe>(Pcm, bLoopPcm);
}

namespace OneBrahamProceduralAudio
{
	// -----------------------------------------------------------------------
	//  Internal helpers
	// -----------------------------------------------------------------------
	namespace
	{
		/**
		 * Seed for the variant currently being rendered. Every noise source picks
		 * it up by default, so even the older single-layer recipes (footsteps,
		 * reload clicks) come out a little different per variant without each of
		 * them having to thread a seed through.
		 */
		uint32 GCurrentSeed = 0x1F2E3D4Cu;

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

		/** Deterministic white noise so the same variant sounds identical every build. */
		struct FNoise
		{
			uint32 State;

			explicit FNoise(uint32 Seed = GCurrentSeed)
				: State(Seed != 0u ? Seed : 0x1F2E3D4Cu)
			{
			}

			FORCEINLINE float Next()
			{
				// xorshift32 - cheap, deterministic, no shared RNG state.
				State ^= State << 13;
				State ^= State >> 17;
				State ^= State << 5;
				return (static_cast<float>(State) / static_cast<float>(0xFFFFFFFFu)) * 2.f - 1.f;
			}
		};

		FORCEINLINE float TimeOf(int32 Index)
		{
			return static_cast<float>(Index) / static_cast<float>(SampleRate);
		}

		FORCEINLINE int32 IndexOf(float Seconds)
		{
			return FMath::Max(0, FMath::RoundToInt(Seconds * SampleRate));
		}

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

		/** Per-sample multiplier that reaches -60 dB after DecaySeconds. */
		FORCEINLINE float DecayStep(float DecaySeconds)
		{
			return FMath::Pow(10.f, -3.f / (FMath::Max(0.0005f, DecaySeconds) * SampleRate));
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

		/** RBJ biquad. Band-pass (constant 0 dB peak) is all the resonant work needs. */
		struct FBiquad
		{
			float B0 = 1.f, B1 = 0.f, B2 = 0.f, A1 = 0.f, A2 = 0.f;
			float Z1 = 0.f, Z2 = 0.f;

			void SetBandPass(float Hz, float Q)
			{
				const float W = 2.f * PI * FMath::Clamp(Hz, 20.f, SampleRate * 0.45f) / SampleRate;
				const float Alpha = FMath::Sin(W) / (2.f * FMath::Max(0.1f, Q));
				const float A0 = 1.f + Alpha;
				B0 = Alpha / A0;
				B1 = 0.f;
				B2 = -Alpha / A0;
				A1 = -2.f * FMath::Cos(W) / A0;
				A2 = (1.f - Alpha) / A0;
			}

			FORCEINLINE float Process(float X)
			{
				const float Y = B0 * X + Z1;
				Z1 = B1 * X - A1 * Y + Z2;
				Z2 = B2 * X - A2 * Y;
				return Y;
			}
		};

		// --- building blocks -------------------------------------------------
		//
		// Every recipe below is a sum of these. They are deliberately few: a
		// struck object is a click plus modes, a blast is filtered noise plus a
		// falling sine, a scatter is many of either. What makes a cue convincing
		// is the numbers, not the number of building blocks.

		/** A decaying sine - one resonant mode of something struck. */
		void AddPing(FBuffer& Buffer, float Start, float Hz, float Decay, float Amp)
		{
			if (Hz <= 0.f || Hz >= SampleRate * 0.48f || Amp == 0.f)
			{
				return;
			}

			const int32 First = IndexOf(Start);
			const int32 Last = FMath::Min(Buffer.Num(), First + IndexOf(Decay * 1.05f) + 1);

			// Rotating phasor rather than a sin() per sample: a glass scatter is a
			// few hundred of these and it should cost nothing to render.
			const float W = 2.f * PI * Hz / SampleRate;
			const float Cw = FMath::Cos(W);
			const float Sw = FMath::Sin(W);
			const float Step = DecayStep(Decay);

			float Re = 1.f;
			float Im = 0.f;
			float Gain = Amp;

			for (int32 Index = First; Index < Last; ++Index)
			{
				// 0.2 ms ramp: an instant onset is a click the strike already has.
				const float Ramp = FMath::Min(1.f, static_cast<float>(Index - First) / 9.f);
				Buffer.Add(Index, Im * Gain * Ramp);

				const float NewRe = Re * Cw - Im * Sw;
				Im = Re * Sw + Im * Cw;
				Re = NewRe;
				Gain *= Step;
			}
		}

		/**
		 * The modes of a struck body. Higher modes die faster than lower ones,
		 * which is what distinguishes a bell from a sine chord.
		 */
		void AddModes(FBuffer& Buffer, FRandomStream& Rng, float Start, float BaseHz,
			const float* Ratios, const float* Gains, int32 Count, float Decay, float Amp)
		{
			for (int32 Mode = 0; Mode < Count; ++Mode)
			{
				const float Hz = BaseHz * Ratios[Mode] * Rng.FRandRange(0.985f, 1.015f);
				const float ModeDecay = Decay / FMath::Sqrt(FMath::Max(1.f, Ratios[Mode]));
				AddPing(Buffer, Start, Hz, ModeDecay, Amp * Gains[Mode] * Rng.FRandRange(0.8f, 1.f));
			}
		}

		/** Filtered noise with an attack and an exponential tail. */
		void AddNoise(FBuffer& Buffer, FNoise& Noise, float Start, float Attack, float Decay, float Amp,
			float LowAlpha, float HighAlpha = 0.f)
		{
			const int32 First = IndexOf(Start);
			const int32 Last = FMath::Min(Buffer.Num(), First + IndexOf(Attack + Decay * 1.05f) + 1);
			const float Step = DecayStep(Decay);
			const int32 AttackSamples = FMath::Max(1, IndexOf(Attack));

			float Lp = 0.f;
			float HpLp = 0.f;
			float Gain = Amp;

			for (int32 Index = First; Index < Last; ++Index)
			{
				const int32 Local = Index - First;
				float Sample = Noise.Next();

				if (LowAlpha > 0.f)
				{
					Sample = LowPass(Sample, Lp, LowAlpha);
				}
				if (HighAlpha > 0.f)
				{
					// High-pass as "signal minus its own low-pass".
					Sample -= LowPass(Sample, HpLp, HighAlpha);
				}

				if (Local < AttackSamples)
				{
					Buffer.Add(Index, Sample * Amp * (static_cast<float>(Local) / AttackSamples));
				}
				else
				{
					Buffer.Add(Index, Sample * Gain);
					Gain *= Step;
				}
			}
		}

		/** Band-passed noise: the "chip", the "squelch", the radio hiss. */
		void AddBandNoise(FBuffer& Buffer, FNoise& Noise, float Start, float Attack, float Decay, float Amp, float CentreHz, float Q)
		{
			const int32 First = IndexOf(Start);
			const int32 Last = FMath::Min(Buffer.Num(), First + IndexOf(Attack + Decay * 1.05f) + 1);
			const float Step = DecayStep(Decay);
			const int32 AttackSamples = FMath::Max(1, IndexOf(Attack));

			FBiquad Filter;
			Filter.SetBandPass(CentreHz, Q);

			// A band-pass at Q>1 loses most of white noise's energy; this keeps the
			// requested amplitude roughly honest across centre frequencies.
			const float MakeUp = FMath::Sqrt(FMath::Max(1.f, Q)) * 1.6f;
			float Gain = Amp;

			for (int32 Index = First; Index < Last; ++Index)
			{
				const int32 Local = Index - First;
				const float Sample = Filter.Process(Noise.Next()) * MakeUp;

				if (Local < AttackSamples)
				{
					Buffer.Add(Index, Sample * Amp * (static_cast<float>(Local) / AttackSamples));
				}
				else
				{
					Buffer.Add(Index, Sample * Gain);
					Gain *= Step;
				}
			}
		}

		/** A sine that falls in pitch as it decays: every thump, boom and body. */
		void AddThump(FBuffer& Buffer, float Start, float StartHz, float EndHz, float Glide, float Decay, float Amp,
			float Attack = 0.001f)
		{
			const int32 First = IndexOf(Start);
			const int32 Last = FMath::Min(Buffer.Num(), First + IndexOf(Attack + Decay * 1.05f) + 1);
			const float Step = DecayStep(Decay);

			float Phase = 0.f;
			float Gain = Amp;

			for (int32 Index = First; Index < Last; ++Index)
			{
				const float Local = TimeOf(Index - First);
				const float Hz = EndHz + (StartHz - EndHz) * FMath::Exp(-Local / FMath::Max(0.001f, Glide));
				Phase = FMath::Fmod(Phase + Hz / SampleRate, 1.f);

				const float Env = Local < Attack ? (Local / Attack) * Amp : Gain;
				if (Local >= Attack)
				{
					Gain *= Step;
				}

				Buffer.Add(Index, FMath::Sin(2.f * PI * Phase) * Env);
			}
		}

		/**
		 * The supersonic N-wave: a near-instant overpressure falling linearly to
		 * an underpressure and snapping back. Under a millisecond long, and the
		 * reason a rifle round past your head is a crack rather than a whoosh.
		 */
		void AddNWave(FBuffer& Buffer, float Start, float LengthSeconds, float Amp)
		{
			const int32 First = IndexOf(Start);
			const int32 Length = FMath::Max(4, IndexOf(LengthSeconds));

			for (int32 Local = 0; Local < Length; ++Local)
			{
				const float Alpha = static_cast<float>(Local) / static_cast<float>(Length);
				const float Edge = FMath::Min(1.f, Local / 2.f);
				Buffer.Add(First + Local, Amp * (1.f - 2.f * Alpha) * Edge);
			}
		}

		/** A bright 1-2 ms contact click: the moment two hard things meet. */
		void AddClick(FBuffer& Buffer, FNoise& Noise, float Start, float Amp, float Decay = 0.0015f)
		{
			AddNoise(Buffer, Noise, Start, 0.0001f, Decay, Amp, 0.f, 0.7f);
		}

		/** Packs a mixed float buffer into immutable PCM, normalised to Gain. */
		TSharedPtr<const TArray<int16>, ESPMode::ThreadSafe> Pack(const FBuffer& Buffer, float Gain)
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

			TSharedPtr<TArray<int16>, ESPMode::ThreadSafe> Pcm = MakeShared<TArray<int16>, ESPMode::ThreadSafe>();
			Pcm->SetNumUninitialized(Count);

			for (int32 Index = 0; Index < Count; ++Index)
			{
				const float Value = SoftClip(Buffer.Samples[Index] * Normalise);
				(*Pcm)[Index] = static_cast<int16>(FMath::Clamp(Value, -1.f, 1.f) * 32767.f);
			}

			return Pcm;
		}

		/** Wraps a mixed buffer as a playable wave. */
		USoundWaveProcedural* Bake(const FBuffer& Buffer, float Gain, bool bLooping)
		{
			UOneBrahamSynthWave* Wave = NewObject<UOneBrahamSynthWave>(GetTransientPackage());
			Wave->InitFromPcm(Pack(Buffer, Gain), SampleRate, bLooping);
			return Wave;
		}

		/** Crossfade the tail into the head so a loop has no tick at its seam. */
		void MakeSeamless(FBuffer& Buffer, float FadeSeconds)
		{
			const int32 FadeCount = FMath::Min(Buffer.Num() / 4, FMath::RoundToInt(FadeSeconds * SampleRate));
			for (int32 Index = 0; Index < FadeCount; ++Index)
			{
				const float Alpha = static_cast<float>(Index) / static_cast<float>(FadeCount);
				const int32 TailIndex = Buffer.Num() - FadeCount + Index;
				Buffer.Samples[TailIndex] = FMath::Lerp(Buffer.Samples[Index], Buffer.Samples[TailIndex], Alpha);
			}
		}

		/** Every variant of every cue, by id. Released on module shutdown. */
		struct FCueVariants
		{
			TArray<TStrongObjectPtr<USoundWaveProcedural>> Waves;
			int32 LastIndex = INDEX_NONE;
		};

		TMap<FName, FCueVariants> GSynthesisedCache;

		/** A metallic click: a short bright noise transient plus two ringing partials. */
		FBuffer MakeClickBuffer(float DurationSeconds, float LowAlpha, float RingHz, float RingGain)
		{
			FBuffer Buffer(DurationSeconds);
			FNoise Noise;
			float LpState = 0.f;

			for (int32 Index = 0; Index < Buffer.Num(); ++Index)
			{
				const float Time = TimeOf(Index);

				const float Env = PercussiveEnv(Time, 0.0008f, 0.004f, DurationSeconds - 0.005f);
				const float Body = LowPass(Noise.Next(), LpState, LowAlpha) * Env;

				const float Ring = (FMath::Sin(2.f * PI * RingHz * Time) * 0.5f
					+ FMath::Sin(2.f * PI * RingHz * 1.63f * Time) * 0.3f)
					* DecayGain(Time, DurationSeconds * 0.6f) * RingGain;

				Buffer.Add(Index, Body + Ring);
			}

			return Buffer;
		}

		// ===================================================================
		//  Weapons
		// ===================================================================

		/**
		 * The bolt carrier cycling: back, a spring, forward into battery. Two
		 * metal-on-metal strikes 30-50 ms apart. This is the close layer that
		 * tells you a weapon is *right there*, and the whole of what a suppressed
		 * weapon sounds like past a few metres.
		 */
		void AddActionCycle(FBuffer& Buffer, FNoise& Noise, FRandomStream& Rng, float Start, float Amp)
		{
			static const float Ratios[] = { 1.f, 1.47f, 2.63f, 3.91f };
			static const float Gains[] = { 1.f, 0.6f, 0.45f, 0.25f };

			const float Base = Rng.FRandRange(2100.f, 2900.f);
			const float Forward = Start + Rng.FRandRange(0.032f, 0.048f);

			AddClick(Buffer, Noise, Start, Amp * 0.9f);
			AddModes(Buffer, Rng, Start, Base, Ratios, Gains, 4, Rng.FRandRange(0.025f, 0.04f), Amp * 0.5f);
			AddThump(Buffer, Start + 0.004f, 420.f, 330.f, 0.02f, 0.02f, Amp * 0.25f);

			AddClick(Buffer, Noise, Forward, Amp);
			AddModes(Buffer, Rng, Forward, Base * Rng.FRandRange(0.82f, 0.94f), Ratios, Gains, 4, Rng.FRandRange(0.035f, 0.055f), Amp * 0.6f);
			AddBandNoise(Buffer, Noise, Forward, 0.0005f, 0.012f, Amp * 0.3f, 4200.f, 1.2f);
		}

		FBuffer RenderGunshot(float CalibreScale, bool bSuppressed, FRandomStream& Rng)
		{
			const float Scale = FMath::Clamp(CalibreScale, 0.4f, 2.2f);
			FNoise Noise;

			if (bSuppressed)
			{
				// Gas behind a baffle stack: a dull cough, a little "pfft" of
				// escaping gas, and the action - which past ten metres is the
				// loudest part of a suppressed shot, and the part that gives it away.
				FBuffer Buffer(0.24f);
				AddNoise(Buffer, Noise, 0.f, 0.0015f, Rng.FRandRange(0.03f, 0.045f), 0.9f, 0.82f);
				AddThump(Buffer, 0.f, 170.f, 70.f, 0.02f, 0.045f, 0.75f);
				AddBandNoise(Buffer, Noise, 0.001f, 0.002f, 0.022f, 0.32f, Rng.FRandRange(1500.f, 2100.f), 0.8f);
				AddActionCycle(Buffer, Noise, Rng, Rng.FRandRange(0.004f, 0.008f), 0.55f);
				return Buffer;
			}

			const float Duration = 0.30f + 0.16f * Scale;
			FBuffer Buffer(Duration);

			// 1. The crack: the bullet's own shock wave plus the muzzle's first
			//    edge. Under a millisecond, and it is what sets the peak.
			AddNWave(Buffer, 0.f, (0.35f + 0.25f * Scale) * 0.001f, 1.05f);
			AddNoise(Buffer, Noise, 0.f, 0.0001f, 0.0025f * Scale + 0.001f, 0.9f, 0.f, 0.6f);

			// 2. The blast: propellant gas leaving the barrel. Bright first, then
			//    a darker rumble that lasts longer the bigger the cartridge.
			const float BodyDecay = (0.08f + 0.06f * Scale) * Rng.FRandRange(0.9f, 1.1f);
			AddNoise(Buffer, Noise, 0.0003f, 0.0008f, BodyDecay, 1.0f, FMath::Clamp(0.5f + 0.1f * Scale, 0.45f, 0.72f));
			AddNoise(Buffer, Noise, 0.001f, 0.004f, BodyDecay * 1.9f, 0.55f, 0.86f);

			// 3. The pressure thump you feel in the chest, falling in pitch.
			AddThump(Buffer, 0.f, 140.f / Scale + 30.f, 45.f, 0.025f, 0.10f * Scale, 0.9f);

			// 4. A cavity "pop" that gives a pistol its bark and a shotgun its
			//    boom: band-passed, centred lower for bigger calibres.
			AddBandNoise(Buffer, Noise, 0.0005f, 0.001f, 0.04f * Scale, 0.45f, 700.f / Scale, 1.6f);

			return Buffer;
		}

		/** The action alone, played under every shot heard from close range. */
		FBuffer RenderGunMechanism(FRandomStream& Rng)
		{
			FBuffer Buffer(0.12f);
			FNoise Noise;
			AddActionCycle(Buffer, Noise, Rng, 0.002f, 1.f);
			return Buffer;
		}

		/**
		 * A gunshot from the far end of a floor or another building: no crack
		 * (the high frequencies died in the air), a rolling boom, and the city
		 * answering it a few times as it bounces off glass and concrete.
		 */
		FBuffer RenderGunDistant(bool bHeavy, FRandomStream& Rng)
		{
			const float Duration = bHeavy ? 1.9f : 1.3f;
			FBuffer Buffer(Duration);
			FNoise Noise;

			AddNoise(Buffer, Noise, 0.f, 0.006f, bHeavy ? 0.35f : 0.22f, 1.f, bHeavy ? 0.955f : 0.94f);
			AddThump(Buffer, 0.f, bHeavy ? 75.f : 95.f, bHeavy ? 34.f : 45.f, 0.06f, bHeavy ? 0.28f : 0.17f, 0.85f, 0.004f);

			const int32 Echoes = Rng.RandRange(4, 7);
			for (int32 Echo = 0; Echo < Echoes; ++Echo)
			{
				const float At = Rng.FRandRange(0.09f, Duration * 0.55f);
				const float Level = Rng.FRandRange(0.18f, 0.45f) * FMath::Exp(-At / 0.6f);
				AddNoise(Buffer, Noise, At, 0.015f, Rng.FRandRange(0.18f, 0.3f), Level, 0.955f);
				AddThump(Buffer, At, 70.f, 40.f, 0.05f, 0.15f, Level * 0.5f, 0.01f);
			}

			// The rumble that sits under all of it.
			AddNoise(Buffer, Noise, 0.01f, 0.03f, Duration * 0.55f, 0.35f, 0.975f);
			return Buffer;
		}

		/**
		 * A room answering a shot. Early reflections from the nearest walls, a
		 * diffuse tail whose top end dies first, and per-room character: the
		 * stairwell's flutter between parallel concrete, the plaza's slap off the
		 * buildings opposite, the server floor's metal ring.
		 */
		struct FRoomRecipe
		{
			FName Id;
			float Decay;
			float Colour;		// low-pass alpha of the diffuse tail: higher = deader room
			int32 Taps;
			float TapMin;
			float TapMax;
			float TapGain;
			float FlutterSec;	// 0 = no flutter echo
			float RingHz;		// 0 = no metallic ring
		};

		const FRoomRecipe* GetRoomRecipe(FName SoundId)
		{
			// Decays match REVERB_PROFILES in Tools/Python/onebraham/build_audio.py,
			// which is the mix document; the wet level is applied by the manager.
			static const FRoomRecipe Rooms[] = {
				{ TEXT("SFX_Gunshot_Tail_Lobby"),       2.6f, 0.45f, 6, 0.030f, 0.120f, 0.50f, 0.f,    0.f },
				{ TEXT("SFX_Gunshot_Tail_Mezzanine"),   1.8f, 0.70f, 4, 0.025f, 0.090f, 0.35f, 0.f,    0.f },
				{ TEXT("SFX_Gunshot_Tail_OpenPlan"),    0.9f, 0.78f, 3, 0.012f, 0.050f, 0.30f, 0.f,    0.f },
				{ TEXT("SFX_Gunshot_Tail_ServerComms"), 1.4f, 0.40f, 5, 0.010f, 0.060f, 0.45f, 0.f,    1180.f },
				{ TEXT("SFX_Gunshot_Tail_Executive"),   1.1f, 0.80f, 3, 0.020f, 0.070f, 0.30f, 0.f,    0.f },
				{ TEXT("SFX_Gunshot_Tail_Canteen"),     2.2f, 0.55f, 5, 0.020f, 0.100f, 0.45f, 0.f,    0.f },
				{ TEXT("SFX_Gunshot_Tail_Skydeck"),     0.4f, 0.75f, 2, 0.120f, 0.350f, 0.25f, 0.f,    0.f },
				{ TEXT("SFX_Gunshot_Tail_Plaza"),       1.8f, 0.80f, 4, 0.150f, 0.550f, 0.60f, 0.f,    0.f },
				{ TEXT("SFX_Gunshot_Tail_Stairwell"),   3.4f, 0.50f, 4, 0.010f, 0.050f, 0.50f, 0.019f, 0.f }
			};

			for (const FRoomRecipe& Room : Rooms)
			{
				if (Room.Id == SoundId)
				{
					return &Room;
				}
			}
			return nullptr;
		}

		FBuffer RenderRoomTail(const FRoomRecipe& Room, FRandomStream& Rng)
		{
			const float Decay = Room.Decay * Rng.FRandRange(0.94f, 1.06f);
			FBuffer Buffer(FMath::Min(Decay * 1.05f + 0.12f, 4.f));
			FNoise Noise;

			const float PreDelay = 0.008f;
			const float Step = DecayStep(Decay);
			float Gain = 1.f;
			float Bright = 0.f;
			float Dark = 0.f;

			for (int32 Index = IndexOf(PreDelay); Index < Buffer.Num(); ++Index)
			{
				const float Time = TimeOf(Index) - PreDelay;
				const float Rise = FMath::Min(1.f, Time / 0.03f);

				Bright = LowPass(Noise.Next(), Bright, Room.Colour);
				Dark = LowPass(Bright, Dark, 0.9f);

				// Air and soft furnishings eat the top end first, so the tail gets
				// darker as it decays. A tail that stays bright reads as a plate
				// reverb on a vocal, not a room.
				const float Darkening = FMath::Clamp(Time / Decay * 1.6f, 0.f, 1.f);
				Buffer.Add(Index, FMath::Lerp(Bright, Dark * 2.2f, Darkening) * Rise * Gain * 0.55f);
				Gain *= Step;
			}

			for (int32 Tap = 0; Tap < Room.Taps; ++Tap)
			{
				const float At = Rng.FRandRange(Room.TapMin, Room.TapMax);
				AddNoise(Buffer, Noise, PreDelay + At, 0.0005f, Rng.FRandRange(0.006f, 0.012f),
					Room.TapGain * Rng.FRandRange(0.5f, 1.f) * FMath::Exp(-At * 3.f / Decay), 0.45f);
			}

			if (Room.FlutterSec > 0.f)
			{
				// Parallel concrete walls three metres apart: the shot bouncing back
				// and forth every 19 ms is the "zing" of a stairwell.
				for (float At = Room.FlutterSec; At < Decay * 0.6f; At += Room.FlutterSec * Rng.FRandRange(0.97f, 1.03f))
				{
					AddNoise(Buffer, Noise, PreDelay + At, 0.0003f, 0.004f, 0.45f * DecayGain(At, Decay * 0.45f), 0.35f);
				}
			}

			if (Room.RingHz > 0.f)
			{
				static const float Ratios[] = { 1.f, 1.41f, 2.23f };
				static const float Gains[] = { 1.f, 0.7f, 0.4f };
				AddModes(Buffer, Rng, PreDelay, Room.RingHz * Rng.FRandRange(0.9f, 1.1f), Ratios, Gains, 3, Decay * 0.35f, 0.06f);
			}

			return Buffer;
		}

		// ===================================================================
		//  Explosions
		// ===================================================================
		enum class EBlast : uint8 { Frag, Breach, Flash };

		FBuffer RenderExplosion(EBlast Kind, FRandomStream& Rng)
		{
			const float Duration = Kind == EBlast::Breach ? 3.6f : (Kind == EBlast::Flash ? 1.7f : 3.2f);
			FBuffer Buffer(Duration);
			FNoise Noise;

			// The crack: the shock front. A flashbang is mostly this.
			const float CrackAmp = Kind == EBlast::Flash ? 1.4f : 1.1f;
			AddNWave(Buffer, 0.f, Kind == EBlast::Flash ? 0.0009f : 0.0013f, CrackAmp);
			AddNoise(Buffer, Noise, 0.f, 0.0001f, Kind == EBlast::Flash ? 0.010f : 0.006f, CrackAmp, 0.f, 0.5f);

			// The blast: bright, then a dark mid-body.
			switch (Kind)
			{
			case EBlast::Flash:
				AddNoise(Buffer, Noise, 0.0005f, 0.001f, 0.12f, 1.1f, 0.45f);
				AddThump(Buffer, 0.f, 110.f, 40.f, 0.05f, 0.5f, 0.7f, 0.003f);
				AddNoise(Buffer, Noise, 0.01f, 0.03f, 0.9f, 0.3f, 0.97f);
				break;

			case EBlast::Breach:
				AddNoise(Buffer, Noise, 0.0005f, 0.001f, 0.35f, 1.2f, 0.7f);
				AddNoise(Buffer, Noise, 0.002f, 0.006f, 0.8f, 0.8f, 0.86f);
				AddThump(Buffer, 0.f, 75.f, 24.f, 0.12f, 2.0f, 1.4f, 0.004f);
				AddNoise(Buffer, Noise, 0.02f, 0.05f, 2.8f, 0.6f, 0.975f);

				// Plaster and stud giving way: a crunch of small dark bursts.
				for (int32 Grain = 0; Grain < 40; ++Grain)
				{
					const float At = 0.03f + 0.6f * FMath::Pow(Rng.FRand(), 1.6f);
					AddNoise(Buffer, Noise, At, 0.001f, Rng.FRandRange(0.01f, 0.03f), Rng.FRandRange(0.15f, 0.45f) * FMath::Exp(-At / 0.4f), 0.7f);
				}
				break;

			case EBlast::Frag:
			default:
				AddNoise(Buffer, Noise, 0.0005f, 0.001f, 0.25f, 1.2f, 0.6f);
				AddNoise(Buffer, Noise, 0.002f, 0.005f, 0.6f, 0.8f, 0.85f);
				AddThump(Buffer, 0.f, 85.f, 28.f, 0.1f, 1.6f, 1.3f, 0.003f);
				AddNoise(Buffer, Noise, 0.02f, 0.04f, 2.4f, 0.6f, 0.97f);
				break;
			}

			// The sub tail: a long low sine you feel more than hear, swelling in
			// slightly after the blast. This is what makes a grenade in the next
			// room shake the floor of this one.
			if (Kind != EBlast::Flash)
			{
				AddThump(Buffer, 0.01f, 36.f, 28.f, 0.4f, Kind == EBlast::Breach ? 3.0f : 2.6f, 0.4f, 0.025f);
			}

			return Buffer;
		}

		/**
		 * What actually sells a blast inside a building is the stuff that falls
		 * afterwards: ceiling tile, plaster, glass, a fitting. Dense to begin
		 * with, thinning to the odd last piece.
		 */
		FBuffer RenderDebris(FRandomStream& Rng)
		{
			FBuffer Buffer(2.4f);
			FNoise Noise;

			const int32 Grains = Rng.RandRange(60, 90);
			for (int32 Grain = 0; Grain < Grains; ++Grain)
			{
				const float At = 0.03f + 2.2f * FMath::Pow(Rng.FRand(), 1.8f);
				const float Fade = FMath::Exp(-At / 1.1f);
				const float Roll = Rng.FRand();

				if (Roll < 0.55f)
				{
					// Plaster and concrete chips.
					AddNoise(Buffer, Noise, At, 0.0005f, Rng.FRandRange(0.008f, 0.025f), Rng.FRandRange(0.2f, 1.f) * Fade, Rng.FRandRange(0.5f, 0.75f));
				}
				else if (Roll < 0.8f)
				{
					// Glass from a light fitting.
					AddPing(Buffer, At, Rng.FRandRange(3000.f, 8000.f), Rng.FRandRange(0.01f, 0.04f), 0.4f * Fade);
				}
				else
				{
					// Something heavier: a chunk of ceiling tile, a bracket.
					AddThump(Buffer, At, Rng.FRandRange(180.f, 260.f), 110.f, 0.02f, 0.03f, 0.6f * Fade);
					AddNoise(Buffer, Noise, At, 0.0005f, 0.02f, 0.4f * Fade, 0.75f);
				}
			}

			return Buffer;
		}

		FBuffer RenderExplosionDistant(FRandomStream& Rng)
		{
			FBuffer Buffer(4.f);
			FNoise Noise;

			AddNoise(Buffer, Noise, 0.f, 0.015f, 0.8f, 1.f, 0.965f);
			AddThump(Buffer, 0.f, 55.f, 22.f, 0.15f, 2.2f, 1.2f, 0.01f);

			const int32 Echoes = Rng.RandRange(5, 8);
			for (int32 Echo = 0; Echo < Echoes; ++Echo)
			{
				const float At = Rng.FRandRange(0.2f, 2.0f);
				const float Level = Rng.FRandRange(0.2f, 0.5f) * FMath::Exp(-At / 1.2f);
				AddNoise(Buffer, Noise, At, 0.03f, Rng.FRandRange(0.4f, 0.7f), Level, 0.97f);
			}

			AddNoise(Buffer, Noise, 0.02f, 0.05f, 3.f, 0.5f, 0.98f);
			return Buffer;
		}

		// ===================================================================
		//  Bullet impacts, by surface
		//
		//  Each one is the same anatomy - contact, body, aftermath - with
		//  numbers chosen so that the material is identifiable with your eyes
		//  shut, because in this game it often is: a round into plasterboard
		//  and a round into a server rack tell you two different things about
		//  what is on the other side of the wall.
		// ===================================================================
		FBuffer RenderImpact(const FString& Surface, FRandomStream& Rng)
		{
			FNoise Noise;

			if (Surface == TEXT("Metal"))
			{
				const bool bRicochet = Rng.FRand() < 0.34f;
				FBuffer Buffer(bRicochet ? 0.7f : 0.55f);
				static const float Ratios[] = { 1.f, 2.32f, 4.25f, 6.63f, 9.38f };
				static const float Gains[] = { 1.f, 0.7f, 0.5f, 0.3f, 0.2f };

				AddClick(Buffer, Noise, 0.f, 1.f);
				AddBandNoise(Buffer, Noise, 0.f, 0.0003f, 0.01f, 0.5f, 4200.f, 1.f);
				AddModes(Buffer, Rng, 0.f, Rng.FRandRange(900.f, 1900.f), Ratios, Gains, 5, Rng.FRandRange(0.18f, 0.32f), 0.9f);

				if (bRicochet)
				{
					// The round leaving again: a whine falling as it goes.
					const float From = Rng.FRandRange(3200.f, 4200.f);
					const float To = Rng.FRandRange(1400.f, 1900.f);
					float Phase = 0.f;
					for (int32 Index = IndexOf(0.01f); Index < Buffer.Num(); ++Index)
					{
						const float Local = TimeOf(Index) - 0.01f;
						const float Hz = FMath::Lerp(From, To, FMath::Min(1.f, Local / 0.35f));
						Phase = FMath::Fmod(Phase + Hz / SampleRate, 1.f);
						const float Env = FMath::Min(1.f, Local / 0.02f) * DecayGain(Local, 0.4f);
						Buffer.Add(Index, FMath::Sin(2.f * PI * Phase) * Env * 0.35f);
					}
				}
				return Buffer;
			}

			if (Surface == TEXT("ServerRack"))
			{
				// Sheet steel over a box of electronics: a hollow boom, a panel
				// rattle, and a ring that is duller than solid metal.
				FBuffer Buffer(0.6f);
				static const float BoomRatios[] = { 1.f, 1.58f, 2.13f, 2.9f };
				static const float BoomGains[] = { 1.f, 0.7f, 0.45f, 0.3f };
				static const float RingRatios[] = { 1.f, 2.7f, 5.1f };
				static const float RingGains[] = { 1.f, 0.5f, 0.25f };

				AddClick(Buffer, Noise, 0.f, 0.7f);
				AddModes(Buffer, Rng, 0.f, Rng.FRandRange(110.f, 180.f), BoomRatios, BoomGains, 4, 0.12f, 0.8f);
				AddModes(Buffer, Rng, 0.f, Rng.FRandRange(1500.f, 2600.f), RingRatios, RingGains, 3, 0.1f, 0.3f);

				// Panel rattle: band noise chopped at ~35 Hz.
				FBiquad Filter;
				Filter.SetBandPass(900.f, 2.f);
				const float RattleHz = Rng.FRandRange(30.f, 42.f);
				for (int32 Index = IndexOf(0.005f); Index < IndexOf(0.25f) && Index < Buffer.Num(); ++Index)
				{
					const float Local = TimeOf(Index) - 0.005f;
					const float Chop = FMath::Sin(2.f * PI * RattleHz * Local) > 0.2f ? 1.f : 0.f;
					Buffer.Add(Index, Filter.Process(Noise.Next()) * 2.2f * Chop * DecayGain(Local, 0.15f) * 0.4f);
				}

				for (int32 Crackle = 0; Crackle < 4; ++Crackle)
				{
					AddClick(Buffer, Noise, Rng.FRandRange(0.01f, 0.12f), 0.15f);
				}
				return Buffer;
			}

			if (Surface == TEXT("Glass"))
			{
				// Crack, then shards: dozens of tiny high pings scattering, and a
				// second wave as pieces land on the floor.
				FBuffer Buffer(0.75f);
				AddClick(Buffer, Noise, 0.f, 1.f, 0.002f);
				AddBandNoise(Buffer, Noise, 0.f, 0.0003f, 0.006f, 0.6f, 5200.f, 1.2f);
				AddNoise(Buffer, Noise, 0.f, 0.001f, 0.08f, 0.3f, 0.3f, 0.6f);

				const int32 Shards = Rng.RandRange(25, 40);
				for (int32 Shard = 0; Shard < Shards; ++Shard)
				{
					const float At = 0.002f + 0.45f * FMath::Square(Rng.FRand());
					AddPing(Buffer, At, Rng.FRandRange(2800.f, 9500.f), Rng.FRandRange(0.006f, 0.03f),
						Rng.FRandRange(0.2f, 0.8f) * FMath::Exp(-At / 0.3f));
				}

				const int32 Landing = Rng.RandRange(8, 14);
				for (int32 Shard = 0; Shard < Landing; ++Shard)
				{
					AddPing(Buffer, Rng.FRandRange(0.18f, 0.55f), Rng.FRandRange(2200.f, 7000.f), Rng.FRandRange(0.008f, 0.02f), 0.3f);
				}
				return Buffer;
			}

			if (Surface == TEXT("Wood"))
			{
				// A desk or a door: a short knock with the wood's own resonance,
				// and a few splinters.
				FBuffer Buffer(0.4f);
				static const float Ratios[] = { 1.f, 2.1f, 3.4f, 5.2f };
				static const float Gains[] = { 1.f, 0.6f, 0.35f, 0.2f };

				AddClick(Buffer, Noise, 0.f, 0.6f);
				AddModes(Buffer, Rng, 0.f, Rng.FRandRange(180.f, 320.f), Ratios, Gains, 4, Rng.FRandRange(0.04f, 0.07f), 1.f);
				AddNoise(Buffer, Noise, 0.f, 0.001f, 0.03f, 0.4f, 0.7f);

				const int32 Splinters = Rng.RandRange(6, 10);
				for (int32 Splinter = 0; Splinter < Splinters; ++Splinter)
				{
					AddNoise(Buffer, Noise, Rng.FRandRange(0.005f, 0.08f), 0.0003f, Rng.FRandRange(0.002f, 0.005f), 0.35f, 0.f, 0.5f);
				}
				return Buffer;
			}

			if (Surface == TEXT("Plaster"))
			{
				// Plasterboard: a soft puff and a crumble. The quiet one, which is
				// why shooting through a partition is a decision.
				FBuffer Buffer(0.4f);
				AddClick(Buffer, Noise, 0.f, 0.35f);
				AddNoise(Buffer, Noise, 0.f, 0.001f, Rng.FRandRange(0.05f, 0.08f), 1.f, 0.72f);
				AddThump(Buffer, 0.f, 140.f, 70.f, 0.02f, 0.05f, 0.5f);

				const int32 Crumbs = Rng.RandRange(10, 16);
				for (int32 Crumb = 0; Crumb < Crumbs; ++Crumb)
				{
					const float At = 0.02f + 0.33f * Rng.FRand();
					AddNoise(Buffer, Noise, At, 0.0005f, Rng.FRandRange(0.004f, 0.01f), 0.2f * FMath::Exp(-At / 0.15f), 0.6f);
				}
				return Buffer;
			}

			if (Surface == TEXT("Carpet"))
			{
				FBuffer Buffer(0.25f);
				AddClick(Buffer, Noise, 0.f, 0.15f);
				AddThump(Buffer, 0.f, 130.f, 70.f, 0.015f, 0.03f, 0.8f);
				AddNoise(Buffer, Noise, 0.f, 0.001f, 0.03f, 0.6f, 0.9f);
				return Buffer;
			}

			if (Surface == TEXT("Flesh"))
			{
				// Wet, close and low: a slap, a thump, a squelch. It must be
				// unmistakeable from armour and from a wall.
				FBuffer Buffer(0.32f);
				AddBandNoise(Buffer, Noise, 0.f, 0.0003f, 0.006f, 0.6f, 1200.f, 0.8f);
				AddThump(Buffer, 0.f, Rng.FRandRange(110.f, 130.f), 55.f, 0.02f, 0.06f, 1.f);
				AddBandNoise(Buffer, Noise, 0.003f, 0.002f, Rng.FRandRange(0.03f, 0.05f), 0.55f, Rng.FRandRange(500.f, 900.f), 1.5f);
				AddNoise(Buffer, Noise, 0.f, 0.001f, 0.05f, 0.5f, 0.92f);
				return Buffer;
			}

			if (Surface == TEXT("Water"))
			{
				// A plop, a spray, a few droplets coming back down.
				FBuffer Buffer(0.5f);
				AddThump(Buffer, 0.005f, 450.f, Rng.FRandRange(900.f, 1300.f), 0.01f, 0.025f, 0.7f);
				AddNoise(Buffer, Noise, 0.f, 0.003f, Rng.FRandRange(0.12f, 0.2f), 0.6f, 0.3f);
				AddNoise(Buffer, Noise, 0.f, 0.002f, 0.06f, 0.5f, 0.8f);

				const int32 Drops = Rng.RandRange(6, 10);
				for (int32 Drop = 0; Drop < Drops; ++Drop)
				{
					const float From = Rng.FRandRange(1200.f, 2500.f);
					AddThump(Buffer, Rng.FRandRange(0.05f, 0.35f), From, From * 1.6f, 0.004f, Rng.FRandRange(0.008f, 0.015f), 0.2f);
				}
				return Buffer;
			}

			// Concrete, and anything unknown: a London office is mostly concrete.
			FBuffer Buffer(0.45f);
			AddClick(Buffer, Noise, 0.f, 0.8f);
			AddBandNoise(Buffer, Noise, 0.f, 0.0003f, Rng.FRandRange(0.025f, 0.04f), 1.f, Rng.FRandRange(1800.f, 3200.f), 1.2f);
			AddThump(Buffer, 0.f, 180.f, 90.f, 0.01f, 0.03f, 0.4f);

			const int32 Dust = Rng.RandRange(8, 14);
			for (int32 Grain = 0; Grain < Dust; ++Grain)
			{
				const float At = 0.03f + 0.37f * FMath::Pow(Rng.FRand(), 1.5f);
				AddNoise(Buffer, Noise, At, 0.0005f, Rng.FRandRange(0.003f, 0.008f), 0.15f * FMath::Exp(-At / 0.2f), 0.5f);
			}
			return Buffer;
		}

		/** A round striking a helmet: a short bell. Dry, and played completely straight. */
		FBuffer RenderHelmetPing(FRandomStream& Rng)
		{
			FBuffer Buffer(0.7f);
			FNoise Noise;
			static const float Ratios[] = { 1.f, 1.52f, 2.07f, 2.76f, 3.58f };
			static const float Gains[] = { 1.f, 0.75f, 0.5f, 0.35f, 0.2f };

			AddClick(Buffer, Noise, 0.f, 0.8f);
			AddModes(Buffer, Rng, 0.f, Rng.FRandRange(2150.f, 2650.f), Ratios, Gains, 5, Rng.FRandRange(0.35f, 0.5f), 1.f);
			AddThump(Buffer, 0.f, 320.f, 260.f, 0.01f, 0.02f, 0.3f);
			return Buffer;
		}

		/**
		 * A subsonic round going past: band-passed air whose centre falls as the
		 * round passes (Doppler), with a faint whistle. Asymmetric: it arrives
		 * slowly and leaves quickly, which is what makes it read as a pass-by.
		 */
		FBuffer RenderWhizz(FRandomStream& Rng)
		{
			FBuffer Buffer(0.32f);
			FNoise Noise;
			FBiquad Filter;

			const float Peak = Rng.FRandRange(0.12f, 0.16f);
			const float From = Rng.FRandRange(2800.f, 3600.f);
			const float To = Rng.FRandRange(800.f, 1100.f);
			float Phase = 0.f;

			for (int32 Index = 0; Index < Buffer.Num(); ++Index)
			{
				const float Time = TimeOf(Index);
				const float Sigma = Time < Peak ? 0.06f : 0.035f;
				const float Env = FMath::Exp(-FMath::Square((Time - Peak) / Sigma));

				// Centre slides through the pass, steepest at the moment of closest approach.
				const float Pass = 0.5f + 0.5f * FMath::Tanh((Time - Peak) / 0.025f);
				const float Hz = FMath::Lerp(From, To, Pass);

				if ((Index & 31) == 0)
				{
					Filter.SetBandPass(Hz, 3.f);
				}

				Phase = FMath::Fmod(Phase + Hz * 0.8f / SampleRate, 1.f);
				Buffer.Add(Index, (Filter.Process(Noise.Next()) * 2.8f + FMath::Sin(2.f * PI * Phase) * 0.25f) * Env);
			}
			return Buffer;
		}

		/**
		 * A supersonic round past your head: the N-wave of its shock cone, a
		 * flick of air, and the slap of that crack off the nearest wall.
		 */
		FBuffer RenderBulletCrack(FRandomStream& Rng)
		{
			FBuffer Buffer(0.14f);
			FNoise Noise;

			AddNWave(Buffer, 0.f, Rng.FRandRange(0.00035f, 0.0006f), 1.f);
			AddNoise(Buffer, Noise, 0.f, 0.0001f, 0.0015f, 0.6f, 0.f, 0.6f);

			const float Slap = Rng.FRandRange(0.006f, 0.014f);
			AddNWave(Buffer, Slap, 0.0006f, 0.35f);
			AddNoise(Buffer, Noise, Slap, 0.0002f, 0.004f, 0.2f, 0.4f);
			return Buffer;
		}

		/**
		 * A spent case hitting the floor and bouncing. Brass is a short tube, so
		 * it rings high and inharmonic; the floor decides how much of that ring
		 * survives - carpet swallows it, a steel deck gives it away.
		 */
		FBuffer RenderShellDrop(const FString& Floor, FRandomStream& Rng)
		{
			const bool bCarpet = Floor == TEXT("Carpet");
			const bool bMetal = Floor == TEXT("Metal");

			FBuffer Buffer(bCarpet ? 0.3f : (bMetal ? 0.8f : 0.6f));
			FNoise Noise;
			static const float Ratios[] = { 1.f, 2.76f, 5.40f };
			static const float Gains[] = { 1.f, 0.5f, 0.25f };

			const float Base = Rng.FRandRange(3600.f, 5200.f);
			const float Ring = bCarpet ? 0.012f : (bMetal ? Rng.FRandRange(0.12f, 0.2f) : Rng.FRandRange(0.05f, 0.09f));
			const int32 Bounces = bCarpet ? 2 : (bMetal ? Rng.RandRange(4, 5) : Rng.RandRange(3, 4));

			float At = 0.f;
			float Gap = Rng.FRandRange(0.07f, 0.11f);
			float Level = 1.f;

			for (int32 Bounce = 0; Bounce < Bounces; ++Bounce)
			{
				AddClick(Buffer, Noise, At, (bCarpet ? 0.15f : 0.35f) * Level);
				AddModes(Buffer, Rng, At, Base, Ratios, Gains, 3, Ring, (bCarpet ? 0.25f : 1.f) * Level);
				if (bCarpet)
				{
					AddNoise(Buffer, Noise, At, 0.001f, 0.015f, 0.4f * Level, 0.85f);
				}

				At += Gap;
				Gap *= Rng.FRandRange(0.55f, 0.7f);
				Level *= Rng.FRandRange(0.45f, 0.6f);
			}

			if (!bCarpet)
			{
				// The roll: a few tiny ticks as it settles.
				const int32 Ticks = Rng.RandRange(6, 10);
				for (int32 Tick = 0; Tick < Ticks; ++Tick)
				{
					const float TickAt = At + Rng.FRandRange(0.f, 0.1f);
					AddPing(Buffer, TickAt, Base * Rng.FRandRange(0.95f, 1.05f), 0.01f, 0.06f);
				}
			}

			return Buffer;
		}

		/** A whole office window giving way, and the floor catching it. */
		FBuffer RenderWindowBreak(FRandomStream& Rng)
		{
			FBuffer Buffer(1.8f);
			FNoise Noise;

			AddNWave(Buffer, 0.f, 0.0005f, 1.1f);
			AddClick(Buffer, Noise, 0.f, 1.2f, 0.004f);
			AddThump(Buffer, 0.f, 160.f, 80.f, 0.02f, 0.05f, 0.5f);
			AddNoise(Buffer, Noise, 0.f, 0.002f, 0.25f, 0.45f, 0.35f, 0.5f);

			const int32 Shards = Rng.RandRange(180, 260);
			for (int32 Shard = 0; Shard < Shards; ++Shard)
			{
				const float At = 0.003f + 1.3f * FMath::Pow(Rng.FRand(), 2.2f);
				const float Fade = FMath::Exp(-At / 0.5f);

				if (Rng.FRand() < 0.3f)
				{
					AddNoise(Buffer, Noise, At, 0.0002f, Rng.FRandRange(0.003f, 0.008f), 0.4f * Fade, 0.f, 0.5f);
				}
				else
				{
					AddPing(Buffer, At, Rng.FRandRange(2500.f, 11000.f), Rng.FRandRange(0.004f, 0.035f), Rng.FRandRange(0.15f, 0.9f) * Fade);
				}
			}

			// The floor wave: the big pieces landing a beat later.
			for (int32 Shard = 0; Shard < 50; ++Shard)
			{
				AddPing(Buffer, Rng.FRandRange(0.35f, 1.1f), Rng.FRandRange(2000.f, 7000.f), Rng.FRandRange(0.008f, 0.03f), 0.45f * Rng.FRand());
			}

			return Buffer;
		}

		/** A body going down: one heavy thump, limbs after it, kit rattling. */
		FBuffer RenderBodyFall(FRandomStream& Rng)
		{
			FBuffer Buffer(1.f);
			FNoise Noise;

			AddThump(Buffer, 0.f, Rng.FRandRange(85.f, 100.f), 45.f, 0.03f, 0.09f, 1.f, 0.003f);
			AddNoise(Buffer, Noise, 0.f, 0.002f, 0.07f, 0.8f, 0.9f);

			const int32 Limbs = Rng.RandRange(2, 3);
			for (int32 Limb = 0; Limb < Limbs; ++Limb)
			{
				const float At = Rng.FRandRange(0.07f, 0.25f);
				AddThump(Buffer, At, 140.f, 70.f, 0.015f, 0.04f, 0.45f);
				AddNoise(Buffer, Noise, At, 0.001f, 0.03f, 0.35f, 0.8f);
			}

			const int32 Kit = Rng.RandRange(5, 9);
			for (int32 Piece = 0; Piece < Kit; ++Piece)
			{
				AddPing(Buffer, Rng.FRandRange(0.02f, 0.3f), Rng.FRandRange(2500.f, 4500.f), Rng.FRandRange(0.01f, 0.03f), 0.12f);
			}

			// Cloth settling.
			AddNoise(Buffer, Noise, 0.02f, 0.02f, 0.15f, 0.12f, 0.6f);
			return Buffer;
		}

		/** One lub-dub, used by the fixed-tempo heartbeat loop. */
		void AddHeartbeat(FBuffer& Buffer, FNoise& Noise, float At, float DubOffset, float Level)
		{
			AddThump(Buffer, At, 62.f, 40.f, 0.04f, 0.45f, Level, 0.006f);
			AddNoise(Buffer, Noise, At, 0.004f, 0.08f, Level * 0.3f, 0.97f);
			AddThump(Buffer, At + DubOffset, 78.f, 52.f, 0.04f, 0.35f, Level * 0.7f, 0.006f);
			AddNoise(Buffer, Noise, At + DubOffset, 0.004f, 0.06f, Level * 0.2f, 0.97f);
		}

		/** The 110 bpm heartbeat loop the bank names for the downed state. */
		FBuffer RenderHeartbeatLoop()
		{
			const float Interval = 60.f / 110.f;
			FBuffer Buffer(Interval * 4.f);
			FNoise Noise;

			for (int32 Beat = 0; Beat < 4; ++Beat)
			{
				AddHeartbeat(Buffer, Noise, Beat * Interval, 0.24f, Beat % 2 == 0 ? 1.f : 0.92f);
			}
			return Buffer;
		}

		/**
		 * Being hit, heard from inside: a low thud in the head rather than a
		 * sting. It has to be unmistakeable from your own hit marker, which is
		 * high and bright.
		 */
		FBuffer RenderDamageTaken(FRandomStream& Rng)
		{
			FBuffer Buffer(0.25f);
			FNoise Noise;
			AddThump(Buffer, 0.f, Rng.FRandRange(80.f, 92.f), 40.f, 0.03f, 0.09f, 1.f, 0.002f);
			AddNoise(Buffer, Noise, 0.f, 0.002f, 0.06f, 0.6f, 0.93f);
			AddBandNoise(Buffer, Noise, 0.f, 0.0005f, 0.006f, 0.2f, 2000.f, 1.f);
			return Buffer;
		}

		// ===================================================================
		//  Weapon handling and player foley
		// ===================================================================
		FBuffer RenderDryFire(FRandomStream& Rng)
		{
			FBuffer Buffer(0.1f);
			FNoise Noise;
			static const float Ratios[] = { 1.f, 1.8f, 3.1f };
			static const float Gains[] = { 1.f, 0.5f, 0.3f };
			AddClick(Buffer, Noise, 0.f, 1.f);
			AddModes(Buffer, Rng, 0.f, Rng.FRandRange(2800.f, 3300.f), Ratios, Gains, 3, 0.015f, 0.6f);
			AddThump(Buffer, 0.001f, 650.f, 550.f, 0.01f, 0.01f, 0.25f);
			return Buffer;
		}

		FBuffer RenderFireModeSwitch(FRandomStream& Rng)
		{
			FBuffer Buffer(0.14f);
			FNoise Noise;
			static const float Ratios[] = { 1.f, 2.2f };
			static const float Gains[] = { 1.f, 0.4f };
			const float Second = Rng.FRandRange(0.05f, 0.07f);
			AddClick(Buffer, Noise, 0.f, 0.7f);
			AddModes(Buffer, Rng, 0.f, Rng.FRandRange(2600.f, 3000.f), Ratios, Gains, 2, 0.012f, 0.5f);
			AddClick(Buffer, Noise, Second, 0.8f);
			AddModes(Buffer, Rng, Second, Rng.FRandRange(2700.f, 3100.f), Ratios, Gains, 2, 0.012f, 0.55f);
			return Buffer;
		}

		FBuffer RenderWeaponDraw(FRandomStream& Rng)
		{
			FBuffer Buffer(0.38f);
			FNoise Noise;
			static const float Ratios[] = { 1.f, 1.7f, 2.9f };
			static const float Gains[] = { 1.f, 0.5f, 0.3f };

			// Sling and fabric: a rising then falling rustle.
			float Lp = 0.f;
			for (int32 Index = 0; Index < IndexOf(0.3f) && Index < Buffer.Num(); ++Index)
			{
				const float Time = TimeOf(Index);
				const float Env = Time < 0.15f ? Time / 0.15f : FMath::Max(0.f, 1.f - (Time - 0.15f) / 0.15f);
				Buffer.Add(Index, LowPass(Noise.Next(), Lp, 0.5f) * Env * 0.5f);
			}

			const float Clack = Rng.FRandRange(0.22f, 0.27f);
			AddClick(Buffer, Noise, Clack, 0.8f);
			AddModes(Buffer, Rng, Clack, Rng.FRandRange(1600.f, 2000.f), Ratios, Gains, 3, 0.03f, 0.6f);
			return Buffer;
		}

		FBuffer RenderLanding(bool bHard, FRandomStream& Rng)
		{
			FBuffer Buffer(bHard ? 0.4f : 0.25f);
			FNoise Noise;

			AddThump(Buffer, 0.f, bHard ? 110.f : 90.f, bHard ? 50.f : 55.f, 0.02f, bHard ? 0.08f : 0.05f, 1.f, 0.002f);
			AddNoise(Buffer, Noise, 0.f, 0.002f, bHard ? 0.06f : 0.04f, 0.6f, bHard ? 0.8f : 0.93f);

			if (bHard)
			{
				// Two feet rarely land together, and the kit lands after them.
				AddThump(Buffer, Rng.FRandRange(0.015f, 0.03f), 100.f, 55.f, 0.02f, 0.05f, 0.6f);
				for (int32 Piece = 0; Piece < 4; ++Piece)
				{
					AddPing(Buffer, Rng.FRandRange(0.02f, 0.15f), Rng.FRandRange(2500.f, 4000.f), 0.015f, 0.1f);
				}
			}
			return Buffer;
		}

		// ===================================================================
		//  Voice and interface
		// ===================================================================
		FBuffer RenderVoiceCue(const FString& Id, FRandomStream& Rng)
		{
			FNoise Noise;

			if (Id == TEXT("UI_Voice_Radio_Open"))
			{
				// Push to talk: a click and a short "tsk" of band-limited hiss,
				// small enough to sit under the first syllable.
				FBuffer Buffer(0.12f);
				AddClick(Buffer, Noise, 0.f, 0.6f);
				AddBandNoise(Buffer, Noise, 0.002f, 0.002f, 0.04f, 0.5f, 1800.f, 1.2f);
				return Buffer;
			}
			if (Id == TEXT("UI_Voice_Radio_Close"))
			{
				// Release: the squelch tail every radio has.
				FBuffer Buffer(0.18f);
				AddClick(Buffer, Noise, 0.f, 0.5f);
				AddBandNoise(Buffer, Noise, 0.004f, 0.004f, 0.09f, 0.7f, 2400.f, 0.9f);
				return Buffer;
			}
			if (Id == TEXT("UI_Voice_ChannelSwitch"))
			{
				// One detent. The caller pitches it up for squad and down for
				// proximity, so the channel is readable without looking.
				FBuffer Buffer(0.09f);
				AddClick(Buffer, Noise, 0.f, 0.7f);
				AddPing(Buffer, 0.f, 1250.f, 0.02f, 0.6f);
				AddPing(Buffer, 0.f, 2500.f, 0.01f, 0.25f);
				return Buffer;
			}
			if (Id == TEXT("UI_Voice_Connected") || Id == TEXT("UI_Voice_Disconnected"))
			{
				const bool bUp = Id == TEXT("UI_Voice_Connected");
				FBuffer Buffer(0.2f);
				AddClick(Buffer, Noise, 0.f, 0.3f);
				AddPing(Buffer, 0.f, bUp ? 880.f : 1320.f, 0.05f, 0.7f);
				AddPing(Buffer, 0.07f, bUp ? 1320.f : 880.f, 0.06f, 0.7f);
				return Buffer;
			}
			if (Id == TEXT("UI_Voice_Muted"))
			{
				FBuffer Buffer(0.06f);
				AddNoise(Buffer, Noise, 0.f, 0.0002f, 0.008f, 0.6f, 0.5f);
				AddPing(Buffer, 0.f, 400.f, 0.012f, 0.5f);
				return Buffer;
			}

			// UI_Banner: the Tannoy's two-tone chime before an announcement.
			FBuffer Buffer(0.75f);
			AddPing(Buffer, 0.f, 784.f, 0.4f, 0.8f);
			AddPing(Buffer, 0.f, 1568.f, 0.2f, 0.2f);
			AddPing(Buffer, 0.22f, 659.f, 0.45f, 0.8f);
			AddPing(Buffer, 0.22f, 1318.f, 0.22f, 0.2f);
			return Buffer;
		}

		/** How many variants a cue gets. One for anything that should be identical every time. */
		int32 GetVariantCount(const FString& Id)
		{
			if (Id.StartsWith(TEXT("SFX_Bullet_Impact_")) || Id.StartsWith(TEXT("SFX_Shell_Drop"))
				|| Id == TEXT("SFX_Gunshot_Mech"))
			{
				return 5;
			}
			if (Id.StartsWith(TEXT("SFX_Gunshot_Tail_")) || Id == TEXT("SFX_Explosion_Distant"))
			{
				return 2;
			}
			if (Id.StartsWith(TEXT("SFX_Gunshot_")) || Id.StartsWith(TEXT("SFX_Footstep_")) || Id == TEXT("SFX_Helmet_Ping")
				|| Id == TEXT("SFX_Bullet_Whizz") || Id == TEXT("SFX_Bullet_Crack") || Id == TEXT("SFX_Body_Fall")
				|| Id == TEXT("SFX_UtilityBounce"))
			{
				return 4;
			}
			if (Id == TEXT("SFX_Frag_Explosion") || Id == TEXT("SFX_Breach_Explosion") || Id == TEXT("SFX_Flashbang_Bang")
				|| Id == TEXT("SFX_Explosion_Debris") || Id == TEXT("SFX_Window_Break") || Id == TEXT("UI_Damage_Taken")
				|| Id == TEXT("SFX_DryFire") || Id == TEXT("SFX_FireMode_Switch") || Id == TEXT("SFX_Weapon_Draw")
				|| Id == TEXT("SFX_Land_Hard") || Id == TEXT("SFX_Land_Soft") || Id == TEXT("SFX_Melee_Thud")
				|| Id.StartsWith(TEXT("SFX_Reload_")))
			{
				return 3;
			}
			return 1;
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
			const float Time = TimeOf(Index);
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
			const float Env = PercussiveEnv(TimeOf(Index), AttackSeconds, BodySeconds, Release);
			Buffer.Add(Index, LowPass(Noise.Next(), LpState, LowPassAlpha) * Env);
		}

		return Bake(Buffer, Amplitude, false);
	}

	USoundWaveProcedural* MakeGunshot(float CalibreScale, bool bSuppressed)
	{
		FRandomStream Rng(static_cast<int32>(GCurrentSeed));
		return Bake(RenderGunshot(CalibreScale, bSuppressed, Rng), bSuppressed ? 0.42f : 0.85f, false);
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
			const float Time = TimeOf(Index);
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
			const float Time = TimeOf(Index);
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

		MakeSeamless(Buffer, 0.04f);
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
			const float Time = TimeOf(Index);
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

	USoundWaveProcedural* MakeHeartbeatWave(TSharedPtr<FOneBrahamHeartbeatControl, ESPMode::ThreadSafe> Control)
	{
		UOneBrahamSynthWave* Wave = NewObject<UOneBrahamSynthWave>(GetTransientPackage());
		Wave->InitAsHeartbeat(MoveTemp(Control), SampleRate);
		return Wave;
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
			const float Time = TimeOf(Index);

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
		MakeSeamless(Buffer, 0.05f);
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
				const float Time = TimeOf(Index);
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
				const float Time = TimeOf(Index);
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
			// The gunshot layers: close report per calibre, the action, the
			// distant report, and one tail per room the building has.
			TEXT("SFX_Gunshot_Pistol"), TEXT("SFX_Gunshot_SMG"), TEXT("SFX_Gunshot_Shotgun"),
			TEXT("SFX_Gunshot_Rifle"), TEXT("SFX_Gunshot_Marksman"), TEXT("SFX_Gunshot_Mech"),
			TEXT("SFX_Gunshot_Distant_Light"), TEXT("SFX_Gunshot_Distant_Heavy"),
			TEXT("SFX_Gunshot_Tail_Lobby"), TEXT("SFX_Gunshot_Tail_Mezzanine"), TEXT("SFX_Gunshot_Tail_OpenPlan"),
			TEXT("SFX_Gunshot_Tail_ServerComms"), TEXT("SFX_Gunshot_Tail_Executive"), TEXT("SFX_Gunshot_Tail_Canteen"),
			TEXT("SFX_Gunshot_Tail_Skydeck"), TEXT("SFX_Gunshot_Tail_Plaza"), TEXT("SFX_Gunshot_Tail_Stairwell"),
			// What a round does when it arrives, or goes past.
			TEXT("SFX_Bullet_Impact_Concrete"), TEXT("SFX_Bullet_Impact_Metal"), TEXT("SFX_Bullet_Impact_Wood"),
			TEXT("SFX_Bullet_Impact_Glass"), TEXT("SFX_Bullet_Impact_Carpet"), TEXT("SFX_Bullet_Impact_Flesh"),
			TEXT("SFX_Bullet_Impact_Plaster"), TEXT("SFX_Bullet_Impact_Water"), TEXT("SFX_Bullet_Impact_ServerRack"),
			TEXT("SFX_Helmet_Ping"), TEXT("SFX_Bullet_Whizz"), TEXT("SFX_Bullet_Crack"),
			TEXT("SFX_Shell_Drop"), TEXT("SFX_Shell_Drop_Carpet"), TEXT("SFX_Shell_Drop_Metal"),
			// Explosion layers, glass, bodies.
			TEXT("SFX_Explosion_Debris"), TEXT("SFX_Explosion_Distant"), TEXT("SFX_Window_Break"),
			TEXT("SFX_Body_Fall"), TEXT("SFX_Downed_Heartbeat"),
			// Handling and landing foley.
			TEXT("SFX_DryFire"), TEXT("SFX_FireMode_Switch"), TEXT("SFX_Weapon_Draw"),
			TEXT("SFX_Land_Hard"), TEXT("SFX_Land_Soft"),
			// Interface cues the substring synthesiser does not answer.
			TEXT("UI_Damage_Taken"), TEXT("UI_Banner"), TEXT("UI_Voice_ChannelSwitch"),
			TEXT("UI_Voice_Radio_Open"), TEXT("UI_Voice_Radio_Close"), TEXT("UI_Voice_Connected"),
			TEXT("UI_Voice_Disconnected"), TEXT("UI_Voice_Muted"),
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
		// UI_KillConfirm and UI_LowAmmo_Click all resolve through MakeUISound's
		// substring branches, which is exactly what this asks.
		//
		// SFX_LowAmmo_Click, SFX_HitMarker and SFX_KillConfirm used to be listed
		// here instead. Nothing in the project has ever asked for them - the UI
		// feedback goes through the UI_ names - so three of the four sounds a
		// player hears most often were reported as silent by the validator while
		// being perfectly audible.
		return IsSynthesisedUICue(SoundId.ToString());
	}

	namespace
	{
		/**
		 * Renders one variant of a cue. GCurrentSeed is set by the caller, so
		 * every noise source in every recipe picks up this variant's seed.
		 */
		USoundWaveProcedural* BuildCue(FName SoundId, uint32 Seed)
		{
			const FString Id = SoundId.ToString();
			FRandomStream Rng(static_cast<int32>(Seed));

			if (Id == TEXT("SFX_FuseBeep"))					{ return MakeTone(2350.f, 0.055f, EOneBrahamToneShape::Square, 0.30f, 0.04f); }
			if (Id == TEXT("SFX_FuseBeep_Close"))			{ return MakeTone(2350.f, 0.065f, EOneBrahamToneShape::Square, 0.44f, 0.05f); }
			if (Id == TEXT("SFX_UtilityBounce"))			{ return MakeNoiseBurst(0.14f, 0.86f, 0.34f, 0.0008f, 0.008f); }
			if (Id == TEXT("SFX_Flashbang_Ring"))			{ return MakeTinnitusRinging(5.f); }

			// Explosions are layered by the manager: this is the blast itself
			// (crack, body, sub tail); debris and the distant report are their
			// own cues so they can be delayed and filtered by distance.
			if (Id == TEXT("SFX_Flashbang_Bang"))			{ return Bake(RenderExplosion(EBlast::Flash, Rng), 0.95f, false); }
			if (Id == TEXT("SFX_Frag_Explosion"))			{ return Bake(RenderExplosion(EBlast::Frag, Rng), 0.95f, false); }
			if (Id == TEXT("SFX_Breach_Explosion"))			{ return Bake(RenderExplosion(EBlast::Breach, Rng), 0.97f, false); }
			if (Id == TEXT("SFX_Explosion_Debris"))			{ return Bake(RenderDebris(Rng), 0.55f, false); }
			if (Id == TEXT("SFX_Explosion_Distant"))		{ return Bake(RenderExplosionDistant(Rng), 0.7f, false); }

			// A partition falling over is not an explosion: it is a long, dark, slow
			// collapse with a debris tail. Low-passed hard, so it reads as weight.
			if (Id == TEXT("SFX_Wall_Collapse"))			{ return MakeNoiseBurst(2.30f, 0.22f, 0.62f, 0.010f, 0.75f); }
			if (Id == TEXT("SFX_Shutter_Close"))			{ return MakeNoiseBurst(1.10f, 0.34f, 0.70f, 0.004f, 0.16f); }
			if (Id == TEXT("SFX_Breach_Plant"))				{ return MakeTone(1480.f, 0.28f, EOneBrahamToneShape::Square, 0.30f, 0.26f); }
			if (Id == TEXT("SFX_Smoke_Deploy"))				{ return MakeNoiseBurst(1.30f, 0.90f, 0.42f, 0.02f, 0.35f); }

			// The close report per calibre. Weapons ask for SFX_Gunshot_<Calibre>
			// per category; the manager adds the action, the distant report and
			// the room around it.
			if (Id == TEXT("SFX_Gunshot_Pistol"))			{ return Bake(RenderGunshot(0.55f, false, Rng), 0.82f, false); }
			if (Id == TEXT("SFX_Gunshot_SMG"))				{ return Bake(RenderGunshot(0.75f, false, Rng), 0.82f, false); }
			if (Id == TEXT("SFX_Gunshot_Shotgun"))			{ return Bake(RenderGunshot(1.5f, false, Rng), 0.9f, false); }
			if (Id == TEXT("SFX_Gunshot_Rifle"))			{ return Bake(RenderGunshot(1.0f, false, Rng), 0.86f, false); }
			if (Id == TEXT("SFX_Gunshot_Marksman"))			{ return Bake(RenderGunshot(1.7f, false, Rng), 0.9f, false); }
			if (Id == TEXT("SFX_Gunshot_Suppressed"))		{ return Bake(RenderGunshot(0.7f, true, Rng), 0.42f, false); }
			if (Id == TEXT("SFX_Gunshot_Mech"))				{ return Bake(RenderGunMechanism(Rng), 0.38f, false); }
			if (Id == TEXT("SFX_Gunshot_Distant_Light"))	{ return Bake(RenderGunDistant(false, Rng), 0.6f, false); }
			if (Id == TEXT("SFX_Gunshot_Distant_Heavy"))	{ return Bake(RenderGunDistant(true, Rng), 0.66f, false); }

			if (const FRoomRecipe* Room = GetRoomRecipe(SoundId))
			{
				return Bake(RenderRoomTail(*Room, Rng), 0.5f, false);
			}

			if (Id.StartsWith(TEXT("SFX_Bullet_Impact_")))
			{
				const FString Surface = Id.RightChop(18);
				const float Gain = Surface == TEXT("Carpet") ? 0.45f : (Surface == TEXT("Metal") || Surface == TEXT("ServerRack") ? 0.72f : 0.65f);
				return Bake(RenderImpact(Surface, Rng), Gain, false);
			}
			if (Id == TEXT("SFX_Helmet_Ping"))				{ return Bake(RenderHelmetPing(Rng), 0.6f, false); }
			if (Id == TEXT("SFX_Bullet_Whizz"))				{ return Bake(RenderWhizz(Rng), 0.5f, false); }
			if (Id == TEXT("SFX_Bullet_Crack"))				{ return Bake(RenderBulletCrack(Rng), 0.8f, false); }
			if (Id == TEXT("SFX_Shell_Drop"))				{ return Bake(RenderShellDrop(TEXT("Hard"), Rng), 0.45f, false); }
			if (Id == TEXT("SFX_Shell_Drop_Carpet"))		{ return Bake(RenderShellDrop(TEXT("Carpet"), Rng), 0.25f, false); }
			if (Id == TEXT("SFX_Shell_Drop_Metal"))			{ return Bake(RenderShellDrop(TEXT("Metal"), Rng), 0.5f, false); }
			if (Id == TEXT("SFX_Window_Break"))				{ return Bake(RenderWindowBreak(Rng), 0.75f, false); }
			if (Id == TEXT("SFX_Body_Fall"))				{ return Bake(RenderBodyFall(Rng), 0.6f, false); }

			if (Id == TEXT("SFX_Helicopter_Rotor"))			{ return MakeHelicopterRotorLoop(2.0f); }
			if (Id == TEXT("SFX_FireAlarm"))				{ return MakeFireAlarmLoop(3.0f); }
			if (Id == TEXT("SFX_Lockdown_Siren"))			{ return MakeTone(620.f, 1.6f, EOneBrahamToneShape::SweepUp, 0.35f, 0.f); }
			if (Id == TEXT("SFX_Melee_Thud"))				{ return MakeNoiseBurst(0.20f, 0.93f, 0.55f, 0.001f, 0.02f); }
			if (Id == TEXT("SFX_Reload_Mag_Out"))			{ return Bake(MakeClickBuffer(0.10f, 0.70f, 1450.f * Rng.FRandRange(0.96f, 1.04f), 0.35f), 0.30f, false); }
			if (Id == TEXT("SFX_Reload_Mag_In"))			{ return Bake(MakeClickBuffer(0.13f, 0.62f, 1180.f * Rng.FRandRange(0.96f, 1.04f), 0.40f), 0.34f, false); }
			if (Id == TEXT("SFX_Reload_Bolt"))				{ return Bake(MakeClickBuffer(0.16f, 0.48f, 2100.f * Rng.FRandRange(0.96f, 1.04f), 0.55f), 0.38f, false); }
			if (Id == TEXT("SFX_DryFire"))					{ return Bake(RenderDryFire(Rng), 0.4f, false); }
			if (Id == TEXT("SFX_FireMode_Switch"))			{ return Bake(RenderFireModeSwitch(Rng), 0.32f, false); }
			if (Id == TEXT("SFX_Weapon_Draw"))				{ return Bake(RenderWeaponDraw(Rng), 0.35f, false); }
			if (Id == TEXT("SFX_Land_Hard"))				{ return Bake(RenderLanding(true, Rng), 0.6f, false); }
			if (Id == TEXT("SFX_Land_Soft"))				{ return Bake(RenderLanding(false, Rng), 0.35f, false); }
			if (Id == TEXT("SFX_Revive_Complete"))			{ return MakeTone(420.f, 0.55f, EOneBrahamToneShape::SweepUp, 0.34f, 0.35f); }
			if (Id == TEXT("SFX_Downed_Tinnitus"))			{ return MakeTone(300.f, 1.8f, EOneBrahamToneShape::Sine, 0.20f, 1.5f); }
			if (Id == TEXT("SFX_Downed_Heartbeat"))			{ return Bake(RenderHeartbeatLoop(), 0.55f, true); }
			if (Id == TEXT("SFX_Extraction_Start"))			{ return MakeTone(140.f, 1.9f, EOneBrahamToneShape::SweepUp, 0.42f, 1.0f); }
			if (Id == TEXT("SFX_Override_Hum"))				{ return MakeTone(96.f, 1.0f, EOneBrahamToneShape::Sine, 0.16f, 0.f); }
			if (Id == TEXT("SFX_Terminal_Accept"))			{ return MakeTone(1240.f, 0.14f, EOneBrahamToneShape::Triangle, 0.32f, 0.11f); }
			if (Id == TEXT("SFX_Terminal_Deny"))			{ return MakeTone(220.f, 0.22f, EOneBrahamToneShape::Square, 0.28f, 0.18f); }
			if (Id == TEXT("SFX_Keycard_Use"))				{ return Bake(MakeClickBuffer(0.22f, 0.55f, 3120.f, 0.60f), 0.30f, false); }
			if (Id == TEXT("SFX_Footstep_Carpet"))			{ return MakeNoiseBurst(0.12f, 0.94f, 0.22f, 0.002f, 0.012f); }
			if (Id == TEXT("SFX_Footstep_Hard"))			{ return MakeNoiseBurst(0.11f, 0.55f, 0.28f, 0.001f, 0.008f); }
			if (Id == TEXT("SFX_Footstep_Metal"))			{ return Bake(MakeClickBuffer(0.20f, 0.35f, 2740.f * Rng.FRandRange(0.95f, 1.05f), 0.45f), 0.30f, false); }
			if (Id == TEXT("SFX_Footstep_Water"))			{ return MakeNoiseBurst(0.22f, 0.80f, 0.34f, 0.003f, 0.05f); }

			if (Id == TEXT("UI_Damage_Taken"))				{ return Bake(RenderDamageTaken(Rng), 0.5f, false); }
			if (Id == TEXT("UI_Banner") || Id.StartsWith(TEXT("UI_Voice_")))
			{
				return Bake(RenderVoiceCue(Id, Rng), Id == TEXT("UI_Banner") ? 0.32f : 0.3f, false);
			}

			// Ambience beds: the room you are standing in, before any wave is
			// authored. Eleven of these cover the whole building.
			if (IsSynthesisedAmbience(SoundId))				{ return MakeAmbienceLoop(SoundId); }

			return MakeUISound(SoundId);
		}

		/** Renders and caches every variant of a cue. False when the id is not ours. */
		FCueVariants* RenderVariants(FName SoundId)
		{
			const FString Id = SoundId.ToString();
			const int32 Count = GetVariantCount(Id);
			const uint32 BaseSeed = FCrc::StrCrc32(*Id);

			FCueVariants Variants;
			for (int32 Variant = 0; Variant < Count; ++Variant)
			{
				const uint32 Seed = (BaseSeed ^ (0x9E3779B9u * static_cast<uint32>(Variant + 1))) | 1u;
				GCurrentSeed = Seed;

				USoundWaveProcedural* Wave = BuildCue(SoundId, Seed);
				if (Wave == nullptr)
				{
					break;
				}
				Variants.Waves.Add(TStrongObjectPtr<USoundWaveProcedural>(Wave));
			}
			GCurrentSeed = 0x1F2E3D4Cu;

			if (Variants.Waves.Num() == 0)
			{
				return nullptr;
			}

			return &GSynthesisedCache.Add(SoundId, MoveTemp(Variants));
		}

		/**
		 * Rendered ahead of the first fight. Ordered by how soon a match needs
		 * them: the first thing anyone hears is a gunshot, the last is a breach.
		 */
		const TCHAR* const CombatCues[] = {
			TEXT("SFX_Gunshot_Rifle"), TEXT("SFX_Gunshot_Mech"), TEXT("SFX_Gunshot_Pistol"), TEXT("SFX_Gunshot_SMG"),
			TEXT("SFX_Gunshot_Shotgun"), TEXT("SFX_Gunshot_Marksman"), TEXT("SFX_Gunshot_Suppressed"),
			TEXT("SFX_Gunshot_Distant_Light"), TEXT("SFX_Gunshot_Distant_Heavy"),
			TEXT("SFX_Bullet_Impact_Concrete"), TEXT("SFX_Bullet_Impact_Plaster"), TEXT("SFX_Bullet_Impact_Flesh"),
			TEXT("SFX_Bullet_Impact_Metal"), TEXT("SFX_Bullet_Impact_Glass"), TEXT("SFX_Bullet_Impact_Wood"),
			TEXT("SFX_Bullet_Impact_Carpet"), TEXT("SFX_Bullet_Impact_ServerRack"), TEXT("SFX_Bullet_Impact_Water"),
			TEXT("SFX_Bullet_Whizz"), TEXT("SFX_Bullet_Crack"), TEXT("SFX_Helmet_Ping"),
			TEXT("SFX_Shell_Drop"), TEXT("SFX_Shell_Drop_Carpet"), TEXT("SFX_Shell_Drop_Metal"),
			TEXT("UI_Damage_Taken"), TEXT("SFX_Body_Fall"),
			TEXT("SFX_Gunshot_Tail_OpenPlan"), TEXT("SFX_Gunshot_Tail_Stairwell"), TEXT("SFX_Gunshot_Tail_Lobby"),
			TEXT("SFX_Gunshot_Tail_ServerComms"), TEXT("SFX_Gunshot_Tail_Executive"), TEXT("SFX_Gunshot_Tail_Mezzanine"),
			TEXT("SFX_Gunshot_Tail_Canteen"), TEXT("SFX_Gunshot_Tail_Plaza"), TEXT("SFX_Gunshot_Tail_Skydeck"),
			TEXT("SFX_Frag_Explosion"), TEXT("SFX_Explosion_Debris"), TEXT("SFX_Explosion_Distant"),
			TEXT("SFX_Flashbang_Bang"), TEXT("SFX_Breach_Explosion"), TEXT("SFX_Window_Break")
		};

		int32 GPrewarmCursor = 0;
	}

	USoundWaveProcedural* GetSynthesisedSound(FName SoundId)
	{
		FCueVariants* Variants = GSynthesisedCache.Find(SoundId);
		if (Variants == nullptr)
		{
			Variants = RenderVariants(SoundId);
			if (Variants == nullptr)
			{
				return nullptr;
			}
		}

		const int32 Count = Variants->Waves.Num();
		int32 Pick = 0;

		if (Count > 1)
		{
			// Never the same variant twice running: two identical impacts in a
			// row is the one repeat the ear always catches.
			Pick = FMath::RandRange(0, Count - 1);
			if (Pick == Variants->LastIndex)
			{
				Pick = (Pick + 1) % Count;
			}
		}

		Variants->LastIndex = Pick;
		return Variants->Waves[Pick].Get();
	}

	bool PrewarmNextCombatCue()
	{
		const int32 Total = UE_ARRAY_COUNT(CombatCues);

		while (GPrewarmCursor < Total)
		{
			const FName SoundId(CombatCues[GPrewarmCursor++]);
			if (!GSynthesisedCache.Contains(SoundId))
			{
				RenderVariants(SoundId);
				break;
			}
		}

		return GPrewarmCursor < Total;
	}

	void PrewarmCombatCues()
	{
		while (PrewarmNextCombatCue())
		{
		}
	}

	void ReleaseSynthesisedSounds()
	{
		GSynthesisedCache.Empty();
		GPrewarmCursor = 0;
	}
}
