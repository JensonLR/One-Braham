// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamFirstPersonFeelComponent.h
// ---------------------------------------------------------------------------
// Everything that makes the first-person view feel like a body rather than a
// camera on a stick, owned in one place:
//
//   * The additive camera layer. Lean roll, the hit punch, the landing dip,
//     strafe roll and view bob are summed into ONE camera-space offset and
//     handed to UCameraComponent::AddAdditiveOffset every frame. That is the
//     only correct place for them: the camera uses pawn control rotation, so
//     GetCameraView() overwrites the component's world rotation every frame and
//     any SetRelativeRotation() on it - which is how the hit flinch and the lean
//     roll used to be written - never reaches the screen.
//   * The weapon layer: look inertia (the gun lags a fast turn and settles with
//     a small overshoot), movement sway, walking and sprinting bob, a breathing
//     loop, and the visual recoil kick, all applied to the first-person arms
//     mesh the weapon is attached to. Aiming down sights shrinks all of it.
//   * The post-process driver: a red-edged vignette pulse on damage, low-health
//     tunnel vision with a heartbeat, the downed blur, the death-cam grade.
//   * The death camera: the camera leaves the body, sags, turns to the person
//     who did it and pushes in, before the controller hands over to spectating.
//
// Purely cosmetic and purely local. Nothing here is replicated, nothing here
// touches the aim ray (GetEyeLocation reads the component location, which the
// additive offset never moves), and on a dedicated server none of it runs.
//
// The character ticks this explicitly, after it has placed the lean, so the
// order of the two is a line of code rather than a tick-group accident.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "OneBrahamFirstPersonFeelComponent.generated.h"

class AOneBrahamCharacter;
class UOneBrahamGameUserSettings;

/** A damped spring on three axes. Plain data, no reflection, so the maths can be tested. */
struct ONEBRAHAM_API FOneBrahamFeelSpring
{
	FVector Value = FVector::ZeroVector;
	FVector Velocity = FVector::ZeroVector;

	/** Advances towards Target. Frequency in Hz, DampingRatio < 1 overshoots. */
	void Step(const FVector& Target, float FrequencyHz, float DampingRatio, float DeltaSeconds);

	void Reset() { Value = FVector::ZeroVector; Velocity = FVector::ZeroVector; }

	/**
	 * The velocity impulse that makes a spring at rest peak at PeakValue. Lets
	 * the tuning speak in degrees and centimetres ("a 2.4 degree punch") rather
	 * than in velocities nobody can picture.
	 */
	static float ImpulseForPeak(float PeakValue, float FrequencyHz, float DampingRatio);
};

UCLASS(ClassGroup = (OneBraham), meta = (BlueprintSpawnableComponent))
class ONEBRAHAM_API UOneBrahamFirstPersonFeelComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UOneBrahamFirstPersonFeelComponent();

	virtual void BeginPlay() override;

	/** Called by AOneBrahamCharacter::Tick after the lean has placed the camera. */
	void TickFeel(float DeltaSeconds);

	// ======================================================================
	//  Events (owning client only; every one of these is cosmetic)
	// ======================================================================
	/** A hit landed on this player. Directional: a shot from the left turns and rolls the view right. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Feel")
	void NotifyDamageTaken(float Damage, FVector InstigatorLocation);

	/** The feet touched the floor at FallSpeed cm/s (positive). */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Feel")
	void NotifyLanded(float FallSpeed);

	/** Adds a raw camera punch, in degrees at its peak. */
	UFUNCTION(BlueprintCallable, Category = "One Braham|Feel")
	void AddCameraPunch(FRotator PeakDegrees);

	/** Starts the death camera. Killer may be null; FocusLocation is used when it is. */
	void BeginDeathCam(AActor* Killer, const FVector& FocusLocation, bool bHasFocus, float DurationSeconds);

	/** Ends the death camera and puts the first-person view back on the body. */
	void EndDeathCam();

	UFUNCTION(BlueprintPure, Category = "One Braham|Feel")
	bool IsDeathCamActive() const { return bDeathCamActive; }

	// ======================================================================
	//  Pure helpers (static so the automation tests can assert them)
	// ======================================================================
	/** Heart rate for a health fraction: resting above the low-health line, racing at zero. */
	static float ComputeHeartbeatBpm(float HealthFraction01);

	/** 0..1 "lub-dub" pulse at TimeSeconds. Shared with the HUD so the edge and the crosshair beat together. */
	static float ComputeHeartbeatPulse(float TimeSeconds, float HealthFraction01);

	/** Peak punch, in degrees, for a hit from InstigatorLocation seen from ViewLocation/ViewRotation. */
	static FRotator ComputeDirectionalPunch(const FVector& ViewLocation, const FRotator& ViewRotation,
		const FVector& InstigatorLocation, float Strength01, float PitchDegrees, float YawDegrees, float RollDegrees);

	/** The player's shake multiplier: off, the 30% "reduction" level, or the intensity slider. */
	static float ComputeCameraShakeScale(const UOneBrahamGameUserSettings* Settings);

	/** Distance falloff for an explosion shake: squared, so the far end of a floor is a nudge. */
	static float ComputeExplosionShakeFalloff(float DistanceCm, float RadiusCm);

	// ======================================================================
	//  Tuning: hit punch
	// ======================================================================
	/** Pitch kick for a hit from straight ahead at the reference damage (a shot from behind tips the head forward instead). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Feel|Punch")
	float PunchPitchDegrees = 2.4f;

	/** Yaw turn away from a hit from the side. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Feel|Punch")
	float PunchYawDegrees = 1.6f;

	/** Roll away from a hit from the side: the head goes with the round. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Feel|Punch")
	float PunchRollDegrees = 2.2f;

	/** Damage that produces the full punch; a graze is a fraction of it, a shotgun up to 1.5x. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Feel|Punch")
	float PunchReferenceDamage = 35.f;

	/** Ceiling on the accumulated punch so a full magazine does not spin the view. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Feel|Punch")
	float PunchMaxDegrees = 6.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Feel|Punch")
	float PunchSpringFrequency = 2.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Feel|Punch")
	float PunchSpringDamping = 0.45f;

	// ======================================================================
	//  Tuning: landing
	// ======================================================================
	/** Below this fall speed a landing is just a step. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Feel|Landing")
	float LandingMinSpeed = 350.f;

	/** At and above this a landing is hard: the character's safe-fall line, where damage starts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Feel|Landing")
	float LandingHardSpeed = 1150.f;

	/** Camera dip at the hard-landing speed, in cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Feel|Landing")
	float LandingDipCm = 7.f;

	/** Pitch nod at the hard-landing speed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Feel|Landing")
	float LandingPitchDegrees = 2.8f;

	/** Camera shake scale for an ordinary landing at the hard speed... */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Feel|Landing")
	float LandingShakeScale = 0.3f;

	/** ...and for a landing that hurt. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Feel|Landing")
	float HardLandingShakeScale = 0.9f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Feel|Landing")
	float LandingSpringFrequency = 2.4f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Feel|Landing")
	float LandingSpringDamping = 0.55f;

	// ======================================================================
	//  Tuning: camera motion while moving
	// ======================================================================
	/** Roll into a full-speed strafe. Small: enough to sell weight, not enough to tilt the horizon. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Feel|Movement")
	float StrafeRollDegrees = 0.9f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Feel|Movement")
	float StrafeRollInterpSpeed = 6.f;

	/** Vertical camera bob per step at a jog, in cm. Scaled by ViewBobbingAmount and off with bViewBobbing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Feel|Movement")
	float CameraBobCm = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Feel|Movement")
	float CameraBobRollDegrees = 0.15f;

	/** Bob amplitude multiplier while sprinting. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Feel|Movement")
	float SprintBobMultiplier = 1.9f;

	/** Distance covered per step at a jog; a sprint lengthens it by a quarter. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Feel|Movement")
	float StepLengthCm = 72.f;

	// ======================================================================
	//  Tuning: weapon
	// ======================================================================
	/** Degrees of weapon lag per degree-per-second of look rate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Feel|Weapon")
	float WeaponLookInertia = 0.011f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Feel|Weapon")
	float WeaponLookInertiaMaxDegrees = 4.5f;

	/** Sideways drift of the weapon per degree-per-second of yaw rate, in cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Feel|Weapon")
	float WeaponLookLagCm = 0.005f;

	/** Below 1 the weapon overshoots and settles after a turn stops: that is the weight. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Feel|Weapon")
	float WeaponSpringFrequency = 3.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Feel|Weapon")
	float WeaponSpringDamping = 0.42f;

	/** Lateral lag at a jog, in cm (scaled by the weapon's SwayMovementScale). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Feel|Weapon")
	float WeaponMoveSwayCm = 0.9f;

	/** Roll into a strafe, at a jog. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Feel|Weapon")
	float WeaponMoveRollDegrees = 2.4f;

	/** Weapon bob per step at a jog, in cm vertical (lateral is 60% of it). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Feel|Weapon")
	float WeaponBobCm = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Feel|Weapon")
	float SprintWeaponBobMultiplier = 2.3f;

	/** Breathing loop amplitude at rest (scaled by the weapon's SwayLoopScale). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Feel|Weapon")
	float BreathingPitchDegrees = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Feel|Weapon")
	float BreathingYawDegrees = 0.38f;

	/** Breaths per second at rest; the rate climbs as health falls. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Feel|Weapon")
	float BreathingRateHz = 0.24f;

	/** Everything above multiplied by this at full ADS: the sight has to stay on the target. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Feel|Weapon")
	float ADSSwayMultiplier = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Feel|Weapon")
	float CrouchSwayMultiplier = 0.65f;

	/** Below the low-health line the hands are not steady. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Feel|Weapon")
	float LowHealthSwayMultiplier = 1.8f;

	/** Muzzle climb of the weapon mesh per cm of upward kick. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Feel|Weapon")
	float KickPitchDegreesPerCm = 2.2f;

	// ======================================================================
	//  Tuning: post-process
	// ======================================================================
	/** Health fraction below which tunnel vision, desaturation and the heartbeat begin. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Feel|Screen")
	float LowHealthThreshold = 0.3f;

	/** Vignette added at the peak of a damage pulse. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Feel|Screen")
	float DamageVignette = 0.55f;

	/** How far the screen tints towards red at the peak of a damage pulse (0..1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Feel|Screen")
	float DamageTint = 0.32f;

	/** Damage pulse decay, per second. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Feel|Screen")
	float DamagePulseDecay = 2.4f;

	/** Saturation at zero health (1 is normal). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Feel|Screen")
	float LowHealthSaturation = 0.45f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Feel|Screen")
	float LowHealthVignette = 0.5f;

	/** Chromatic aberration at zero health. Respects the player's chromatic aberration toggle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Feel|Screen")
	float LowHealthFringe = 1.4f;

	/** Extra vignette at the top of each heartbeat. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Feel|Screen")
	float HeartbeatVignette = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Feel|Screen")
	float DownedSaturation = 0.18f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Feel|Screen")
	float DownedVignette = 0.9f;

	/** The downed blur: a very short focal distance at a wide aperture. Needs depth of field enabled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Feel|Screen")
	float DownedFocalDistanceCm = 45.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Feel|Screen")
	float DownedFstop = 1.2f;

	/** How fast the screen state blends towards its target, per second. Slow on purpose: recovery should be felt. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Feel|Screen")
	float ScreenBlendSpeed = 1.6f;

	// ======================================================================
	//  Tuning: death camera
	// ======================================================================
	/** How far the view sags as the body goes down. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Feel|Death")
	float DeathCamDropCm = 55.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Feel|Death")
	float DeathCamRollDegrees = 12.f;

	/** The slow push towards the killer over the whole death cam. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Feel|Death")
	float DeathCamPushInCm = 45.f;

	/** How quickly the view turns to the killer. Low: it is a realisation, not a snap. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Feel|Death")
	float DeathCamLookInterpSpeed = 2.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Feel|Death")
	float DeathCamSaturation = 0.08f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "One Braham|Feel|Death")
	float DeathCamVignette = 1.f;

protected:
	AOneBrahamCharacter* GetOBCharacter() const;

	/** Effective motion scale from the reduce-motion setting (1 or a fraction). */
	float GetMotionScale() const;

	void TickCameraLayer(float DeltaSeconds, AOneBrahamCharacter& Character);
	void TickWeaponLayer(float DeltaSeconds, AOneBrahamCharacter& Character);
	void TickScreenEffects(float DeltaSeconds, AOneBrahamCharacter& Character);
	void TickDeathCam(float DeltaSeconds, AOneBrahamCharacter& Character);

	/** Writes the screen state into the camera's post-process, overriding only what is in use. */
	void ApplyPostProcess(AOneBrahamCharacter& Character, float Saturation, float VignetteAdd, float Fringe,
		float TintAlpha, float BlurAlpha);

	// --- Camera layer state ----------------------------------------------
	/** Punch rotation as (Pitch, Yaw, Roll) in degrees. */
	FOneBrahamFeelSpring PunchSpring;

	/** Landing: X = vertical dip in cm, Y = pitch nod in degrees. */
	FOneBrahamFeelSpring LandingSpring;

	float StrafeRoll = 0.f;
	float StepPhase = 0.f;
	float BobWeight = 0.f;

	// --- Weapon layer state ----------------------------------------------
	/** Inertia rotation (Pitch, Yaw, Roll) in degrees. */
	FOneBrahamFeelSpring WeaponRotationSpring;

	/** Inertia translation in camera space, cm. */
	FOneBrahamFeelSpring WeaponLocationSpring;

	FRotator LastControlRotation = FRotator::ZeroRotator;
	bool bHasLastControlRotation = false;
	float BreathingPhase = 0.f;

	/** The arms mesh transform the character was built with; every offset is relative to it. */
	FVector BaseMeshLocation = FVector::ZeroVector;
	FRotator BaseMeshRotation = FRotator::ZeroRotator;
	bool bCapturedBaseMesh = false;

	// --- Screen state ------------------------------------------------------
	float DamagePulse = 0.f;
	float LowHealthAlpha = 0.f;
	float DownedAlpha = 0.f;
	float DeathAlpha = 0.f;
	bool bPostProcessDirty = false;

	// --- Death cam ---------------------------------------------------------
	bool bDeathCamActive = false;
	float DeathCamElapsed = 0.f;
	float DeathCamDuration = 3.5f;
	float DeathCamRollSign = 1.f;
	bool bDeathCamHasFocus = false;
	FVector DeathCamFocus = FVector::ZeroVector;
	FVector DeathCamStartLocation = FVector::ZeroVector;
	FRotator DeathCamStartRotation = FRotator::ZeroRotator;
	FRotator DeathCamLookRotation = FRotator::ZeroRotator;
	TWeakObjectPtr<AActor> DeathCamKiller;
};
