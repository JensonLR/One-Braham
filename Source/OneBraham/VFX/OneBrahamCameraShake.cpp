// Copyright (c) One Braham. All Rights Reserved.

#include "VFX/OneBrahamCameraShake.h"

#include "OneBraham.h"
#include "Core/OneBrahamGameUserSettings.h"
#include "Shakes/PerlinNoiseCameraShakePattern.h"

UOneBrahamCameraShake::UOneBrahamCameraShake(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UPerlinNoiseCameraShakePattern>(TEXT("RootShakePattern")))
{
	// A single-cycle shake with a short attack and a longer decay, driven through
	// the engine's Perlin-noise shake pattern. No binary curve asset, no
	// editor-only dependency: this compiles and works from a code-only checkout.
	bSingleInstance = true;

	UPerlinNoiseCameraShakePattern* Pattern = Cast<UPerlinNoiseCameraShakePattern>(GetRootShakePattern());
	if (Pattern == nullptr)
	{
		return;
	}

	// The Perlin pattern randomises its initial noise offset per axis on every
	// start, so each shake instance reads differently.
	Pattern->Duration = 0.45f;
	Pattern->BlendInTime = 0.02f;
	Pattern->BlendOutTime = 0.35f;

	Pattern->Pitch.Amplitude = 1.1f;
	Pattern->Pitch.Frequency = 22.f;

	Pattern->Yaw.Amplitude = 0.9f;
	Pattern->Yaw.Frequency = 19.f;

	Pattern->Roll.Amplitude = 1.4f;
	Pattern->Roll.Frequency = 26.f;

	Pattern->Z.Amplitude = 2.2f;
	Pattern->Z.Frequency = 28.f;

	Pattern->X.Amplitude = 1.0f;
	Pattern->X.Frequency = 24.f;

	Pattern->Y.Amplitude = 1.0f;
	Pattern->Y.Frequency = 24.f;

	// FOV kick: a punch to the field of view reads as "concussion" more than
	// camera translation ever does.
	Pattern->FOV.Amplitude = 1.6f;
	Pattern->FOV.Frequency = 30.f;
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
