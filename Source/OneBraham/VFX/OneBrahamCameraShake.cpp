// Copyright (c) One Braham. All Rights Reserved.

#include "VFX/OneBrahamCameraShake.h"

#include "OneBraham.h"
#include "Core/OneBrahamGameUserSettings.h"

UOneBrahamCameraShake::UOneBrahamCameraShake()
{
	// A single-cycle shake with a short attack and a longer decay, driven through
	// the engine's own CameraShakeBase pattern matrix. No binary curve asset, no
	// editor-only dependency: this compiles and works from a code-only checkout.
	bSingleInstance = true;

	// The engine's default oscillation uses a Sine on all axes; we set the
	// amplitudes below and let the base class blend them.
	OscillationDuration = 0.45f;
	OscillationBlendInTime = 0.02f;
	OscillationBlendOutTime = 0.35f;

	RotOscillation.Pitch.Amplitude = 1.1f;
	RotOscillation.Pitch.Frequency = 22.f;
	RotOscillation.Pitch.InitialOffset = EInitialOscillatorOffset::EOO_OffsetRandom;
	RotOscillation.Pitch.Waveform = EOscillatorWaveform::PerlinNoise;

	RotOscillation.Yaw.Amplitude = 0.9f;
	RotOscillation.Yaw.Frequency = 19.f;
	RotOscillation.Yaw.InitialOffset = EInitialOscillatorOffset::EOO_OffsetRandom;
	RotOscillation.Yaw.Waveform = EOscillatorWaveform::PerlinNoise;

	RotOscillation.Roll.Amplitude = 1.4f;
	RotOscillation.Roll.Frequency = 26.f;
	RotOscillation.Roll.InitialOffset = EInitialOscillatorOffset::EOO_OffsetRandom;
	RotOscillation.Roll.Waveform = EOscillatorWaveform::PerlinNoise;

	LocOscillation.Z.Amplitude = 2.2f;
	LocOscillation.Z.Frequency = 28.f;
	LocOscillation.Z.InitialOffset = EInitialOscillatorOffset::EOO_OffsetRandom;
	LocOscillation.Z.Waveform = EOscillatorWaveform::PerlinNoise;

	LocOscillation.X.Amplitude = 1.0f;
	LocOscillation.X.Frequency = 24.f;
	LocOscillation.X.InitialOffset = EInitialOscillatorOffset::EOO_OffsetRandom;
	LocOscillation.X.Waveform = EOscillatorWaveform::PerlinNoise;

	LocOscillation.Y.Amplitude = 1.0f;
	LocOscillation.Y.Frequency = 24.f;
	LocOscillation.Y.InitialOffset = EInitialOscillatorOffset::EOO_OffsetRandom;
	LocOscillation.Y.Waveform = EOscillatorWaveform::PerlinNoise;

	// FOV kick: a punch to the field of view reads as "concussion" more than
	// camera translation ever does.
	FOVOscillation.Amplitude = 1.6f;
	FOVOscillation.Frequency = 30.f;
	FOVOscillation.InitialOffset = EInitialOscillatorOffset::EOO_OffsetRandom;
	FOVOscillation.Waveform = EOscillatorWaveform::PerlinNoise;
}

TSubclassOf<UCameraShakeBase> UOneBrahamCameraShake::GetExplosionShakeClass()
{
	static TWeakObjectPtr<UClass> CachedClass;

	if (!CachedClass.IsValid())
	{
		// Prefer the artist-authored Blueprint (which can add a radial blur via the
		// post-process), fall back to this C++ class.
		UClass* BlueprintClass = LoadClass<UCameraShakeBase>(nullptr,
			TEXT("/Game/OneBraham/VFX/CameraShakes/BP_OBCameraShake_Explosion.BP_OBCameraShake_Explosion_C"));

		CachedClass = BlueprintClass != nullptr ? BlueprintClass : UOneBrahamCameraShake::StaticClass();
	}

	return CachedClass.Get();
}

TSubclassOf<UCameraShakeBase> UOneBrahamCameraShake::GetLandingShakeClass()
{
	static TWeakObjectPtr<UClass> CachedClass;

	if (!CachedClass.IsValid())
	{
		UClass* BlueprintClass = LoadClass<UCameraShakeBase>(nullptr,
			TEXT("/Game/OneBraham/VFX/CameraShakes/BP_OBCameraShake_Landing.BP_OBCameraShake_Landing_C"));

		CachedClass = BlueprintClass != nullptr ? BlueprintClass : UOneBrahamCameraShake::StaticClass();
	}

	return CachedClass.Get();
}

TSubclassOf<UCameraShakeBase> UOneBrahamCameraShake::GetExtractionShakeClass()
{
	static TWeakObjectPtr<UClass> CachedClass;

	if (!CachedClass.IsValid())
	{
		UClass* BlueprintClass = LoadClass<UCameraShakeBase>(nullptr,
			TEXT("/Game/OneBraham/VFX/CameraShakes/BP_OBCameraShake_Extraction.BP_OBCameraShake_Extraction_C"));

		CachedClass = BlueprintClass != nullptr ? BlueprintClass : UOneBrahamCameraShake::StaticClass();
	}

	return CachedClass.Get();
}
