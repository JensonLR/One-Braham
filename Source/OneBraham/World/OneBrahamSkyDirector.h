// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamSkyDirector.h
// ---------------------------------------------------------------------------
// The day going down over Aldgate. The match is played under an overcast
// London afternoon; when the lockdown reaches its last stage - the Purge, the
// stage that sends everyone up to the Skydeck - the sky turns to dusk over
// twenty seconds: the sun sinks to the horizon and warms, the dome's
// photograph crossfades to the Belfast sunset, the haze goes amber and
// thickens, and the skylight follows the dome. Docs/10 §3 rule 4: the Skydeck
// at dusk is the lighting showcase and the frame on the store page.
//
// The two looks are the two presets build_district.py builds (SKY_PRESETS):
//   day  - kloofendal_48d_partly_cloudy_puresky, dome x2500, sun 47.8 deg up
//   dusk - belfast_sunset_puresky, dome x900, sun 2.1 deg up, light 255/196/140
// The day end is read from the placed actors at BeginPlay, so the director
// starts from whatever the content build left in the map; the dusk end is
// this actor's DuskState.
//
// NETWORKING
//   None of its own. The lockdown stage already replicates (OnRep_Stage on
//   UOneBrahamLockdownComponent), so every machine watches the same stage and
//   runs the same blend locally. A player joining mid-Purge snaps to dusk
//   rather than watching a sunset that everyone else saw minutes ago. A
//   dedicated server has no one to show a sky to and does not tick.
//
// THE SKY DOME
//   M_OB_SkyDome samples one cubemap, SkyCube. When the material also carries
//   a second cube (DuskCubeParameter) and a crossfade scalar (DuskBlendParameter)
//   the photographs crossfade with the light. On the single-cube material the
//   dusk photograph is swapped in at the half-way point, where the luminance
//   and haze are already half-way between the two and the change is softest.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Core/OneBrahamTypes.h"
#include "OneBrahamSkyDirector.generated.h"

class ADirectionalLight;
class ASkyLight;
class AExponentialHeightFog;
class AOneBrahamGameState;
class UMaterialInstanceDynamic;
class UOneBrahamLockdownComponent;
class UStaticMeshComponent;
class UTextureCube;

/** One look of the outdoor sky: everything the director blends. */
USTRUCT(BlueprintType)
struct ONEBRAHAM_API FOneBrahamSkyState
{
	GENERATED_BODY()

	/** The sun's rotation: the light travels along its forward vector. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Sky")
	FRotator SunRotation = FRotator(-47.81f, 124.1f, 0.f);

	/** Lux. 8,000 is overcast London daylight. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Sky", meta = (ClampMin = "0"))
	float SunIntensityLux = 8000.f;

	/** Linear colour (the preset's sRGB light colour, decoded). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Sky")
	FLinearColor SunColour = FLinearColor(FColor(255, 246, 235));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Sky", meta = (ClampMin = "0"))
	float SkyLightIntensity = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Sky", meta = (ClampMin = "0"))
	float FogDensity = 0.0035f;

	/** Physical units: the haze is as bright as the dome at the horizon (HazeColour x SkyLuminance). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Sky")
	FLinearColor FogInscatteringLuminance = FLinearColor(1900.f, 2050.f, 2250.f);

	/** M_OB_SkyDome's SkyLuminance: the photograph's scale to physical luminance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Sky", meta = (ClampMin = "0"))
	float DomeLuminance = 2500.f;

	/** M_OB_SkyDome's HazeColour: the horizon band the fog matches. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Sky")
	FLinearColor DomeHaze = FLinearColor(0.78f, 0.84f, 0.92f);
};

UCLASS()
class ONEBRAHAM_API AOneBrahamSkyDirector : public AActor
{
	GENERATED_BODY()

public:
	AOneBrahamSkyDirector();

	virtual void Tick(float DeltaSeconds) override;

	// ======================================================================
	//  The scene (set by the content build; found in the world if left empty)
	// ======================================================================
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Sky|Scene")
	TObjectPtr<ADirectionalLight> Sun = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Sky|Scene")
	TObjectPtr<ASkyLight> SkyLight = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Sky|Scene")
	TObjectPtr<AExponentialHeightFog> HeightFog = nullptr;

	/** The dome: the actor whose mesh wears M_OB_SkyDome (or an instance of it). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Sky|Scene")
	TObjectPtr<AActor> SkyDome = nullptr;

	// ======================================================================
	//  The finale
	// ======================================================================
	/** The lockdown stage that brings the dusk. The Purge sends everyone to the Skydeck. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Sky")
	EOneBrahamLockdownStage FinaleStage = EOneBrahamLockdownStage::Purge;

	/** Seconds from day to dusk. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Sky", meta = (ClampMin = "0.1"))
	float BlendSeconds = 20.f;

	/**
	 * The dusk end. The sun is the Belfast photograph's (azimuth 217.62, 2.11
	 * degrees up) placed the way build_sky places it: yaw = azimuth + 90 + 180.
	 * 2,500 lux is direct sun two degrees above a hazy horizon. The haze is the
	 * dusk preset's (0.95, 0.62, 0.42) at the dome's x900, as the day's fog is
	 * its haze at x2500. The evening haze is a little denser as the air cools.
	 * The skylight is lifted slightly: with the sun nearly level it lights the
	 * walls and not the deck, and the shaded side is the dome's to carry.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Sky")
	FOneBrahamSkyState DuskState;

	/** The dusk photograph, imported by build_district.build_sky's dusk preset. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Sky")
	TSoftObjectPtr<UTextureCube> DuskSkyCube;

	/** M_OB_SkyDome's parameter names. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Sky|Material")
	FName SkyCubeParameter = TEXT("SkyCube");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Sky|Material")
	FName DuskCubeParameter = TEXT("SkyCubeDusk");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Sky|Material")
	FName DuskBlendParameter = TEXT("DuskBlend");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Sky|Material")
	FName LuminanceParameter = TEXT("SkyLuminance");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "One Braham|Sky|Material")
	FName HazeParameter = TEXT("HazeColour");

	// ======================================================================
	//  Queries
	// ======================================================================
	/** 0 = day, 1 = dusk, eased: where the sky is right now. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Sky")
	float GetDuskAlpha() const { return EaseProgress(Progress); }

	/** The day end as captured from the scene at BeginPlay. */
	UFUNCTION(BlueprintPure, Category = "One Braham|Sky")
	FOneBrahamSkyState GetDayState() const { return DayState; }

	// ======================================================================
	//  Pure blend maths, exposed for tests
	// ======================================================================
	/** Moves linear progress toward 1 (dusk) or 0 (day) at 1 / BlendSeconds per second, clamped. */
	static float AdvanceProgress(float Progress, bool bTowardDusk, float DeltaSeconds, float BlendSeconds);

	/**
	 * One tick of the director's progress for the stage it sees. The first look
	 * snaps (a late joiner arrives at dusk), a return to Normal snaps back to
	 * day (a new match), and anything else blends via AdvanceProgress.
	 */
	static float StepProgress(float Progress, bool bFirstLook, EOneBrahamLockdownStage Stage,
		EOneBrahamLockdownStage Finale, float DeltaSeconds, float BlendSeconds);

	/** Smoothstep: the light starts and settles gently instead of moving at a constant rate. */
	static float EaseProgress(float Progress);

	/**
	 * Light and luminance blend geometrically - 8,000 to 2,500 lux passes
	 * through 4,472 at half-way, an even fall in stops, which is how exposure
	 * and the eye read it. Colours and densities blend linearly; the sun's
	 * rotation is slerped so it sinks along one arc.
	 */
	static FOneBrahamSkyState BlendSkyStates(const FOneBrahamSkyState& Day, const FOneBrahamSkyState& Dusk, float Alpha);

	/** A over B in stops: A * (B / A)^Alpha. Falls back to a linear blend when either end is not positive. */
	static float BlendLuminance(float A, float B, float Alpha);

	/** True once a stage has reached the finale. */
	static bool IsFinaleStage(EOneBrahamLockdownStage Stage, EOneBrahamLockdownStage Finale);

protected:
	virtual void BeginPlay() override;

private:
	/** Fills empty scene references from the world, and creates the dome's MID. */
	void ResolveScene();

	/** Reads the day end from the scene as the content build left it. */
	void CaptureDayState();

	/** The replicated lockdown component, once the game state has arrived. */
	UOneBrahamLockdownComponent* FindLockdown();

	/** Pushes a blended state into the actors and the dome. */
	void ApplyAlpha(float Alpha);

	FOneBrahamSkyState DayState;

	/** Linear 0..1, eased on output. */
	float Progress = 0.f;
	float AppliedAlpha = -1.f;

	/** Until the stage has been seen once, the first look is snapped, not blended. */
	bool bStageObserved = false;

	/** True when the dome material crossfades its two photographs itself. */
	bool bDomeCrossfades = false;
	bool bDomeShowsDusk = false;

	/** Seconds since the skylight last recaptured, when it does not capture in real time. */
	float RecaptureAccumulator = 0.f;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> DomeMID = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UTextureCube> DayCube = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UTextureCube> DuskCube = nullptr;

	TWeakObjectPtr<UOneBrahamLockdownComponent> CachedLockdown;
};
