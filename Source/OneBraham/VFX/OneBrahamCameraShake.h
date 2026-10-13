// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamCameraShake.h
// ---------------------------------------------------------------------------
// ONE BRAHAM ships exactly three camera shakes, all built procedurally so no
// binary Curves asset is required:
//   * Explosion  - frag / breach charge, scaled by distance
//   * Landing    - a heavy landing after a fall
//   * Extraction - the helicopter: sustained rotor vibration on the Skydeck
//
// Accessibility: every shake is scaled by UOneBrahamGameUserSettings::
// CameraShakeIntensity, and a player who sets "Screen shake reduction" receives
// a flat 30% instead of 100%.

#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraShakeBase.h"
#include "OneBrahamCameraShake.generated.h"

UCLASS(BlueprintType, EditInlineNew)
class ONEBRAHAM_API UOneBrahamCameraShake : public UCameraShakeBase
{
	GENERATED_BODY()

public:
	UOneBrahamCameraShake();

	/** Shake class used for explosions; resolved lazily and cached. */
	static TSubclassOf<UCameraShakeBase> GetExplosionShakeClass();
	static TSubclassOf<UCameraShakeBase> GetLandingShakeClass();
	static TSubclassOf<UCameraShakeBase> GetExtractionShakeClass();

	/** Peak translation amplitude in cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Shake")
	float TranslationAmplitude = 2.2f;

	/** Peak rotation amplitude in degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Shake")
	float RotationAmplitude = 1.1f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Shake")
	float Frequency = 22.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Shake")
	float Duration = 0.45f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Shake")
	float BlendInTime = 0.02f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Shake")
	float BlendOutTime = 0.35f;
};
