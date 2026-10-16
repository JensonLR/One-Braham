// Copyright (c) One Braham. All Rights Reserved.

#include "Characters/OneBrahamFirstPersonFeelComponent.h"

#include "OneBraham.h"
#include "Characters/OneBrahamCharacter.h"
#include "Characters/OneBrahamCharacterMovementComponent.h"
#include "Characters/OneBrahamDownedComponent.h"
#include "Characters/OneBrahamHealthComponent.h"
#include "Core/OneBrahamGameUserSettings.h"
#include "Weapons/OneBrahamWeaponComponent.h"
#include "VFX/OneBrahamCameraShake.h"

#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

namespace OneBrahamFeel
{
	/** The engine's default vignette, used when nothing else in the scene sets one. */
	constexpr float DefaultVignette = 0.4f;

	/** The colour the screen bleeds towards on a hit: a dark arterial red, not a cartoon one. */
	const FLinearColor DamageTintColour(1.f, 0.52f, 0.48f, 1.f);

	/** Below this, a screen effect is "off" and its override is handed back to the level. */
	constexpr float EffectEpsilon = 0.004f;

	/** Reduce-motion keeps the cue but takes most of the movement out of it. */
	constexpr float ReducedMotionScale = 0.3f;

	/** A frame longer than this is a hitch, not motion, and must not launch a spring. */
	constexpr float MaxFeelDeltaSeconds = 0.1f;

	float SmoothStep01(float X)
	{
		const float T = FMath::Clamp(X, 0.f, 1.f);
		return T * T * (3.f - 2.f * T);
	}

	FRotator VectorToRotator(const FVector& V)
	{
		// The rotation springs store (Pitch, Yaw, Roll) in (X, Y, Z).
		return FRotator(V.X, V.Y, V.Z);
	}
}

// ==========================================================================
//  Spring
// ==========================================================================
void FOneBrahamFeelSpring::Step(const FVector& Target, float FrequencyHz, float DampingRatio, float DeltaSeconds)
{
	if (DeltaSeconds <= 0.f)
	{
		return;
	}

	// RK4 in fixed sub-steps. Sub-steps because a stiff spring integrated in one
	// 30 Hz step explodes on a frame hitch; RK4 rather than semi-implicit Euler
	// because Euler bleeds energy from the first frames of an impulse - a 2.4
	// degree punch landed at 2.1 - and the punch is sized analytically
	// (ImpulseForPeak), so the integrator has to honour the analytic peak.
	const float Omega = 2.f * PI * FMath::Max(0.01f, FrequencyHz);
	const float Zeta = FMath::Max(0.f, DampingRatio);
	const float Clamped = FMath::Min(DeltaSeconds, OneBrahamFeel::MaxFeelDeltaSeconds);
	const int32 Steps = FMath::Clamp(FMath::CeilToInt(Clamped * 120.f), 1, 12);
	const float H = Clamped / static_cast<float>(Steps);
	const float K = Omega * Omega;
	const float C = 2.f * Zeta * Omega;

	auto Accel = [&Target, K, C](const FVector& X, const FVector& V)
	{
		return (Target - X) * K - V * C;
	};

	for (int32 Index = 0; Index < Steps; ++Index)
	{
		const FVector X1 = Value, V1 = Velocity, A1 = Accel(X1, V1);
		const FVector X2 = Value + V1 * (H * 0.5f), V2 = Velocity + A1 * (H * 0.5f), A2 = Accel(X2, V2);
		const FVector X3 = Value + V2 * (H * 0.5f), V3 = Velocity + A2 * (H * 0.5f), A3 = Accel(X3, V3);
		const FVector X4 = Value + V3 * H, V4 = Velocity + A3 * H, A4 = Accel(X4, V4);
		Value += (V1 + V2 * 2.f + V3 * 2.f + V4) * (H / 6.f);
		Velocity += (A1 + A2 * 2.f + A3 * 2.f + A4) * (H / 6.f);
	}
}

float FOneBrahamFeelSpring::ImpulseForPeak(float PeakValue, float FrequencyHz, float DampingRatio)
{
	// For x'' = -w^2 x - 2zw x' released from rest with velocity v0, the first
	// peak is v0 / w * exp(-z / sqrt(1 - z^2) * atan(sqrt(1 - z^2) / z)). Solved
	// for v0. Critically damped and over-damped springs peak at about v0 / (e w).
	const float Omega = 2.f * PI * FMath::Max(0.01f, FrequencyHz);
	const float Zeta = FMath::Clamp(DampingRatio, 0.01f, 10.f);

	float PeakFactor = 1.f / UE_EULERS_NUMBER;
	if (Zeta < 0.999f)
	{
		const float Root = FMath::Sqrt(1.f - Zeta * Zeta);
		PeakFactor = FMath::Exp(-Zeta / Root * FMath::Atan2(Root, Zeta));
	}

	return PeakValue * Omega / FMath::Max(0.05f, PeakFactor);
}

// ==========================================================================
//  Lifecycle
// ==========================================================================
UOneBrahamFirstPersonFeelComponent::UOneBrahamFirstPersonFeelComponent()
{
	// Ticked by the character, explicitly, after it has placed the lean: the
	// additive offset has to be computed from this frame's camera, not last frame's.
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(false);
}

void UOneBrahamFirstPersonFeelComponent::BeginPlay()
{
	Super::BeginPlay();

	if (const AOneBrahamCharacter* Character = GetOBCharacter())
	{
		if (const USkeletalMeshComponent* Arms = Character->GetFirstPersonMesh())
		{
			BaseMeshLocation = Arms->GetRelativeLocation();
			BaseMeshRotation = Arms->GetRelativeRotation();
			bCapturedBaseMesh = true;
		}
	}
}

AOneBrahamCharacter* UOneBrahamFirstPersonFeelComponent::GetOBCharacter() const
{
	return Cast<AOneBrahamCharacter>(GetOwner());
}

float UOneBrahamFirstPersonFeelComponent::GetMotionScale() const
{
	const UOneBrahamGameUserSettings* Settings = UOneBrahamGameUserSettings::GetOneBrahamSettings();
	return Settings != nullptr && Settings->bReduceMotion ? OneBrahamFeel::ReducedMotionScale : 1.f;
}

// ==========================================================================
//  Pure helpers
// ==========================================================================
float UOneBrahamFirstPersonFeelComponent::ComputeHeartbeatBpm(float HealthFraction01)
{
	// Resting at the low-health line, racing at zero: 72 to 132 beats a minute.
	// Above the line the heart is not something the player is asked to notice.
	const float Alpha = FMath::Clamp(1.f - FMath::Clamp(HealthFraction01, 0.f, 1.f) / 0.3f, 0.f, 1.f);
	return FMath::Lerp(72.f, 132.f, Alpha);
}

float UOneBrahamFirstPersonFeelComponent::ComputeHeartbeatPulse(float TimeSeconds, float HealthFraction01)
{
	// A "lub-dub": a strong beat and a weaker one a sixth of a cycle later, each
	// a narrow bump. A sine reads as breathing; this reads as a heart.
	const float BeatsPerSecond = ComputeHeartbeatBpm(HealthFraction01) / 60.f;
	const float Phase = FMath::Frac(TimeSeconds * BeatsPerSecond);

	auto Bump = [](float X, float Centre, float Width)
	{
		const float D = (X - Centre) / Width;
		return FMath::Exp(-D * D);
	};

	const float Lub = Bump(Phase, 0.08f, 0.05f);
	const float Dub = Bump(Phase, 0.26f, 0.05f) * 0.6f;
	return FMath::Clamp(Lub + Dub, 0.f, 1.f);
}

FRotator UOneBrahamFirstPersonFeelComponent::ComputeDirectionalPunch(const FVector& ViewLocation, const FRotator& ViewRotation,
	const FVector& InstigatorLocation, float Strength01, float PitchDegrees, float YawDegrees, float RollDegrees)
{
	const float Strength = FMath::Max(0.f, Strength01);

	// No instigator (a fall, the building, the lockdown purge) arrives as the zero
	// vector: a plain downward flinch, because there is no side to be hit from.
	const FVector Local = InstigatorLocation.IsNearlyZero()
		? FVector::ZeroVector
		: ViewRotation.UnrotateVector(InstigatorLocation - ViewLocation);
	const FVector Flat = FVector(Local.X, Local.Y, 0.f).GetSafeNormal();

	if (Flat.IsNearlyZero())
	{
		return FRotator(-0.5f * PitchDegrees * Strength, 0.f, 0.f);
	}

	const float Forward = Flat.X;	// +1 = the shot came from in front
	const float Right = Flat.Y;		// +1 = the shot came from the right

	// From the front the head is knocked back (view up); from behind it is
	// shoved forward (view down). From either side the view turns and rolls
	// away from the shooter - with the round, which is the direction a body goes.
	return FRotator(
		PitchDegrees * Strength * (0.35f + 0.65f * Forward),
		-Right * YawDegrees * Strength,
		-Right * RollDegrees * Strength);
}

float UOneBrahamFirstPersonFeelComponent::ComputeCameraShakeScale(const UOneBrahamGameUserSettings* Settings)
{
	if (Settings == nullptr)
	{
		return 1.f;
	}

	// "Screen shake reduction" is a flat 30% (OneBrahamCameraShake.h promises
	// it), and it switches bCameraShake off as it does so - so the reduction is
	// checked first or a player who asked for less would get none at all.
	if (Settings->bScreenShakeReduction)
	{
		return 0.3f;
	}

	return Settings->bCameraShake ? FMath::Max(0.f, Settings->CameraShakeIntensity) : 0.f;
}

float UOneBrahamFirstPersonFeelComponent::ComputeExplosionShakeFalloff(float DistanceCm, float RadiusCm)
{
	if (RadiusCm <= 0.f)
	{
		return 0.f;
	}

	// Squared, not linear: a frag in the next room should rattle the teeth, one
	// at the far end of the open-plan floor should be a nudge.
	const float Linear = FMath::Clamp(1.f - FMath::Max(0.f, DistanceCm) / RadiusCm, 0.f, 1.f);
	return Linear * Linear;
}

// ==========================================================================
//  Events
// ==========================================================================
void UOneBrahamFirstPersonFeelComponent::NotifyDamageTaken(float Damage, FVector InstigatorLocation)
{
	AOneBrahamCharacter* Character = GetOBCharacter();
	if (Character == nullptr || Damage <= 0.f || bDeathCamActive)
	{
		return;
	}

	const float Strength = FMath::Clamp(Damage / FMath::Max(1.f, PunchReferenceDamage), 0.15f, 1.5f);

	FVector ViewLocation = Character->GetEyeLocation();
	FRotator ViewRotation = Character->GetEyeRotation();

	if (const UCameraComponent* Camera = Character->GetFirstPersonCamera())
	{
		ViewLocation = Camera->GetComponentLocation();
	}

	FRotator Punch = ComputeDirectionalPunch(ViewLocation, ViewRotation, InstigatorLocation, Strength,
		PunchPitchDegrees, PunchYawDegrees, PunchRollDegrees);

	// A little randomness so a burst does not punch the same way three times;
	// small enough that the direction still reads.
	Punch.Pitch += FMath::FRandRange(-0.25f, 0.25f) * Strength;
	Punch.Yaw += FMath::FRandRange(-0.2f, 0.2f) * Strength;

	AddCameraPunch(Punch);

	// The screen: a pulse whose size is the size of the hit.
	DamagePulse = FMath::Clamp(DamagePulse + Damage / 40.f, 0.f, 1.f);
}

void UOneBrahamFirstPersonFeelComponent::AddCameraPunch(FRotator PeakDegrees)
{
	const float Scale = GetMotionScale();

	PunchSpring.Velocity += FVector(
		FOneBrahamFeelSpring::ImpulseForPeak(PeakDegrees.Pitch * Scale, PunchSpringFrequency, PunchSpringDamping),
		FOneBrahamFeelSpring::ImpulseForPeak(PeakDegrees.Yaw * Scale, PunchSpringFrequency, PunchSpringDamping),
		FOneBrahamFeelSpring::ImpulseForPeak(PeakDegrees.Roll * Scale, PunchSpringFrequency, PunchSpringDamping));

	// The weapon takes the hit too, a beat behind the eyes: the arms are on the
	// same body, and a gun that ignores the round that hit you looks bolted on.
	WeaponRotationSpring.Velocity += FVector(
		FOneBrahamFeelSpring::ImpulseForPeak(-PeakDegrees.Pitch * 0.6f * Scale, WeaponSpringFrequency, WeaponSpringDamping),
		FOneBrahamFeelSpring::ImpulseForPeak(-PeakDegrees.Yaw * 0.6f * Scale, WeaponSpringFrequency, WeaponSpringDamping),
		FOneBrahamFeelSpring::ImpulseForPeak(PeakDegrees.Roll * 0.8f * Scale, WeaponSpringFrequency, WeaponSpringDamping));
}

void UOneBrahamFirstPersonFeelComponent::NotifyLanded(float FallSpeed)
{
	AOneBrahamCharacter* Character = GetOBCharacter();
	if (Character == nullptr || FallSpeed < LandingMinSpeed || bDeathCamActive)
	{
		return;
	}

	const float Range = FMath::Max(1.f, LandingHardSpeed - LandingMinSpeed);
	const float Alpha = FMath::Clamp((FallSpeed - LandingMinSpeed) / Range, 0.f, 1.6f);
	const bool bHard = FallSpeed >= LandingHardSpeed;

	// A landing that hurt is heavier than its speed alone says: the knees gave.
	const float Weight = Alpha * (bHard ? 1.35f : 1.f) * GetMotionScale();

	const float Dip = LandingDipCm * Weight;
	const float Nod = LandingPitchDegrees * Weight;

	LandingSpring.Velocity += FVector(
		FOneBrahamFeelSpring::ImpulseForPeak(Dip, LandingSpringFrequency, LandingSpringDamping),
		FOneBrahamFeelSpring::ImpulseForPeak(Nod, LandingSpringFrequency, LandingSpringDamping),
		0.f);

	// The weapon drops further than the eyes and comes back later.
	WeaponLocationSpring.Velocity.Z -= FOneBrahamFeelSpring::ImpulseForPeak(Dip * 0.8f, WeaponSpringFrequency, WeaponSpringDamping);
	WeaponRotationSpring.Velocity.X -= FOneBrahamFeelSpring::ImpulseForPeak(Nod * 0.7f, WeaponSpringFrequency, WeaponSpringDamping);

	// The shake: the one GetLandingShakeClass was written for and nothing ever
	// played. An ordinary drop off a desk is barely a tremor; a fall that cost
	// health is a jolt.
	const float ShakeScale = ComputeCameraShakeScale(UOneBrahamGameUserSettings::GetOneBrahamSettings())
		* FMath::Min(Alpha, 1.f) * (bHard ? HardLandingShakeScale : LandingShakeScale);

	if (ShakeScale > 0.01f)
	{
		if (APlayerController* PC = Cast<APlayerController>(Character->GetController()))
		{
			if (PC->PlayerCameraManager != nullptr)
			{
				PC->PlayerCameraManager->StartCameraShake(UOneBrahamCameraShake::GetLandingShakeClass(), ShakeScale);
			}
		}
	}
}

// ==========================================================================
//  Tick
// ==========================================================================
void UOneBrahamFirstPersonFeelComponent::TickFeel(float DeltaSeconds)
{
	AOneBrahamCharacter* Character = GetOBCharacter();
	if (Character == nullptr || DeltaSeconds <= 0.f)
	{
		return;
	}

	if (bDeathCamActive)
	{
		TickDeathCam(DeltaSeconds, *Character);
		TickScreenEffects(DeltaSeconds, *Character);
		return;
	}

	// Only the person behind these eyes. IsLocallyControlled() alone is true for
	// a bot on the server, and a bot has no screen to shake.
	const APlayerController* PC = Cast<APlayerController>(Character->GetController());
	if (PC == nullptr || !PC->IsLocalController())
	{
		return;
	}

	TickCameraLayer(DeltaSeconds, *Character);
	TickWeaponLayer(DeltaSeconds, *Character);
	TickScreenEffects(DeltaSeconds, *Character);
}

void UOneBrahamFirstPersonFeelComponent::TickCameraLayer(float DeltaSeconds, AOneBrahamCharacter& Character)
{
	UCameraComponent* Camera = Character.GetFirstPersonCamera();
	if (Camera == nullptr)
	{
		return;
	}

	const UOneBrahamGameUserSettings* Settings = UOneBrahamGameUserSettings::GetOneBrahamSettings();
	const float MotionScale = GetMotionScale();
	const UOneBrahamCharacterMovementComponent* Movement = Character.GetOBCharacterMovement();

	// --- Punch and landing springs ------------------------------------------
	PunchSpring.Step(FVector::ZeroVector, PunchSpringFrequency, PunchSpringDamping, DeltaSeconds);
	PunchSpring.Value = PunchSpring.Value.BoundToCube(PunchMaxDegrees);
	LandingSpring.Step(FVector::ZeroVector, LandingSpringFrequency, LandingSpringDamping, DeltaSeconds);

	// --- Movement: strafe roll and bob ---------------------------------------
	const FVector Velocity = Character.GetVelocity();
	const float Speed2D = Velocity.Size2D();
	const bool bOnGround = Movement != nullptr && Movement->IsMovingOnGround();
	const float JogSpeed = Movement != nullptr ? FMath::Max(1.f, Movement->JogSpeed) : 400.f;
	const bool bSprinting = Movement != nullptr && Movement->IsSprinting() && Speed2D > JogSpeed * 1.05f;

	const FRotator YawOnly(0.f, Character.GetEyeRotation().Yaw, 0.f);
	const float RightSpeed = FVector::DotProduct(Velocity, FRotationMatrix(YawOnly).GetUnitAxis(EAxis::Y));
	const float StrafeTarget = bOnGround ? FMath::Clamp(RightSpeed / JogSpeed, -1.f, 1.f) * StrafeRollDegrees * MotionScale : 0.f;
	StrafeRoll = FMath::FInterpTo(StrafeRoll, StrafeTarget, DeltaSeconds, StrafeRollInterpSpeed);

	// Half a cycle of StepPhase is one footfall. The phase keeps running while
	// the weight fades, so stopping eases out instead of freezing mid-stride.
	const float StepLength = StepLengthCm * (bSprinting ? 1.25f : 1.f);
	StepPhase = FMath::Fmod(StepPhase + (bOnGround ? Speed2D / FMath::Max(1.f, StepLength) * PI * DeltaSeconds : 0.f), 2.f * PI);
	BobWeight = FMath::FInterpTo(BobWeight, bOnGround ? FMath::Clamp(Speed2D / JogSpeed, 0.f, 1.f) : 0.f, DeltaSeconds, 6.f);

	const bool bBobEnabled = Settings == nullptr || (Settings->bViewBobbing && !Settings->bReduceMotion);
	const float BobScale = bBobEnabled ? (Settings != nullptr ? Settings->ViewBobbingAmount : 1.f) * BobWeight * (bSprinting ? SprintBobMultiplier : 1.f) : 0.f;

	const float BobZ = -CameraBobCm * BobScale * (0.5f - 0.5f * FMath::Cos(2.f * StepPhase));
	const float BobRoll = CameraBobRollDegrees * BobScale * FMath::Sin(StepPhase);

	// --- Lean: the roll that used to be written to the relative rotation, where
	// pawn control rotation erased it every frame. The sideways slide stays on
	// the component (the character places it) because the aim ray uses it.
	const float LeanRoll = Character.LeanAmount * Character.GetLeanCameraRoll();

	const FRotator OffsetRotation(
		PunchSpring.Value.X - LandingSpring.Value.Y,
		PunchSpring.Value.Y,
		PunchSpring.Value.Z + StrafeRoll + BobRoll + LeanRoll);

	const FVector OffsetLocation(0.f, 0.f, BobZ - LandingSpring.Value.X);

	Camera->ClearAdditiveOffset();
	Camera->AddAdditiveOffset(FTransform(OffsetRotation, OffsetLocation), 0.f);
}

void UOneBrahamFirstPersonFeelComponent::TickWeaponLayer(float DeltaSeconds, AOneBrahamCharacter& Character)
{
	USkeletalMeshComponent* Arms = Character.GetFirstPersonMesh();
	if (Arms == nullptr)
	{
		return;
	}

	if (!bCapturedBaseMesh)
	{
		BaseMeshLocation = Arms->GetRelativeLocation();
		BaseMeshRotation = Arms->GetRelativeRotation();
		bCapturedBaseMesh = true;
	}

	const UOneBrahamGameUserSettings* Settings = UOneBrahamGameUserSettings::GetOneBrahamSettings();
	const UOneBrahamCharacterMovementComponent* Movement = Character.GetOBCharacterMovement();
	const UOneBrahamWeaponComponent* Weapons = Character.GetWeaponComponent();
	const float MotionScale = GetMotionScale();

	// --- Look rate -----------------------------------------------------------
	const FRotator ControlRotation = Character.GetEyeRotation();
	if (!bHasLastControlRotation)
	{
		LastControlRotation = ControlRotation;
		bHasLastControlRotation = true;
	}

	const FRotator LookDelta = (ControlRotation - LastControlRotation).GetNormalized();
	LastControlRotation = ControlRotation;

	const float SafeDelta = FMath::Max(DeltaSeconds, 0.001f);
	const float YawRate = FMath::Clamp(LookDelta.Yaw / SafeDelta, -900.f, 900.f);
	const float PitchRate = FMath::Clamp(LookDelta.Pitch / SafeDelta, -900.f, 900.f);

	// --- Multipliers -----------------------------------------------------------
	const float ADSAlpha = Weapons != nullptr ? Weapons->GetADSAlpha() : 0.f;
	const float MovementScale = Weapons != nullptr ? Weapons->GetSwayMovementScale() : 1.f;
	const float LoopScale = Weapons != nullptr ? Weapons->GetSwayLoopScale() : 1.f;
	const bool bCrouched = Character.bIsCrouched;

	const float SwayScale = FMath::Lerp(1.f, ADSSwayMultiplier, ADSAlpha)
		* (bCrouched ? CrouchSwayMultiplier : 1.f)
		* FMath::Lerp(1.f, LowHealthSwayMultiplier, LowHealthAlpha)
		* MotionScale;

	// --- Inertia: the gun lags the look and settles with a little overshoot ----
	const float MaxInertia = WeaponLookInertiaMaxDegrees;
	FVector RotationTarget(
		FMath::Clamp(-PitchRate * WeaponLookInertia, -MaxInertia, MaxInertia),
		FMath::Clamp(-YawRate * WeaponLookInertia, -MaxInertia, MaxInertia),
		FMath::Clamp(-YawRate * WeaponLookInertia * 0.5f, -MaxInertia, MaxInertia));

	FVector LocationTarget(
		0.f,
		FMath::Clamp(-YawRate * WeaponLookLagCm, -1.5f, 1.5f),
		FMath::Clamp(-PitchRate * WeaponLookLagCm * 0.6f, -1.f, 1.f));

	// --- Movement sway ----------------------------------------------------------
	const FVector Velocity = Character.GetVelocity();
	const float JogSpeed = Movement != nullptr ? FMath::Max(1.f, Movement->JogSpeed) : 400.f;
	const FRotator YawOnly(0.f, ControlRotation.Yaw, 0.f);
	const FVector LocalVelocity = YawOnly.UnrotateVector(Velocity);
	const float ForwardAlpha = FMath::Clamp(LocalVelocity.X / JogSpeed, -1.5f, 1.5f);
	const float RightAlpha = FMath::Clamp(LocalVelocity.Y / JogSpeed, -1.5f, 1.5f);

	LocationTarget.X += -ForwardAlpha * WeaponMoveSwayCm * 0.5f * MovementScale;
	LocationTarget.Y += -RightAlpha * WeaponMoveSwayCm * MovementScale;
	RotationTarget.Z += RightAlpha * WeaponMoveRollDegrees * MovementScale;

	// Falling: the weapon floats up as the body drops away under it.
	if (Movement != nullptr && Movement->IsFalling())
	{
		LocationTarget.Z += FMath::Clamp(-Velocity.Z / 1000.f, -1.f, 1.f) * 1.4f;
	}

	WeaponRotationSpring.Step(RotationTarget * SwayScale, WeaponSpringFrequency, WeaponSpringDamping, DeltaSeconds);
	WeaponLocationSpring.Step(LocationTarget * SwayScale, WeaponSpringFrequency, WeaponSpringDamping, DeltaSeconds);

	// --- Bob ------------------------------------------------------------------
	const bool bSprinting = Movement != nullptr && Movement->IsSprinting() && Velocity.Size2D() > JogSpeed * 1.05f;
	const bool bBobEnabled = Settings == nullptr || (Settings->bViewBobbing && !Settings->bReduceMotion);
	const float BobAmount = bBobEnabled
		? (Settings != nullptr ? Settings->ViewBobbingAmount : 1.f) * BobWeight * (bSprinting ? SprintWeaponBobMultiplier : 1.f) * MovementScale
			* FMath::Lerp(1.f, ADSSwayMultiplier, ADSAlpha)
		: 0.f;

	const FVector BobLocation(
		0.f,
		WeaponBobCm * 0.6f * BobAmount * FMath::Sin(StepPhase),
		-WeaponBobCm * BobAmount * (0.5f - 0.5f * FMath::Cos(2.f * StepPhase)));
	const FRotator BobRotation(0.f, 0.4f * BobAmount * FMath::Sin(StepPhase), 0.8f * BobAmount * FMath::Sin(StepPhase));

	// --- Breathing --------------------------------------------------------------
	// A slow figure-of-eight (yaw at half the pitch rate). It quickens when the
	// player is hurt, which is a cue the player feels before they read it.
	const float BreathRate = BreathingRateHz * (1.f + 1.4f * LowHealthAlpha + (bSprinting ? 0.8f : 0.f));
	BreathingPhase = FMath::Fmod(BreathingPhase + 2.f * PI * BreathRate * DeltaSeconds, 4.f * PI);
	const float BreathScale = LoopScale * SwayScale;
	const FRotator BreathRotation(
		BreathingPitchDegrees * BreathScale * FMath::Sin(BreathingPhase),
		BreathingYawDegrees * BreathScale * FMath::Sin(0.5f * BreathingPhase + 0.7f),
		0.f);

	// --- Recoil kick ------------------------------------------------------------
	// Computed by the weapon component on every predicted shot and, until now,
	// applied to nothing. X is back along the barrel, Y is up.
	const FVector2D Kick = Weapons != nullptr ? Weapons->GetWeaponKickOffset() : FVector2D::ZeroVector;
	const float KickRoll = Weapons != nullptr ? Weapons->GetWeaponKickRoll() : 0.f;
	const FVector KickLocation(Kick.X, 0.f, Kick.Y * 0.6f);
	const FRotator KickRotation(Kick.Y * KickPitchDegreesPerCm, 0.f, KickRoll);

	// --- Compose ----------------------------------------------------------------
	const FVector OffsetLocation = WeaponLocationSpring.Value + BobLocation + KickLocation
		- FVector(0.f, 0.f, LandingSpring.Value.X * 0.35f);

	const FRotator OffsetRotation = OneBrahamFeel::VectorToRotator(WeaponRotationSpring.Value)
		+ BobRotation + BreathRotation + KickRotation;

	// The arms are parented to the camera, so camera space is their parent space:
	// the offset rotation goes on the outside of the mesh's own (-90 yaw) basis,
	// and turns the mesh about the hand (WeaponPivotCm), not about its origin.
	const FQuat OffsetQuat(OffsetRotation);
	const FQuat Composed = OffsetQuat * FQuat(BaseMeshRotation);
	const FVector PivotedLocation = WeaponPivotCm + OffsetQuat.RotateVector(BaseMeshLocation - WeaponPivotCm);
	Arms->SetRelativeLocationAndRotation(PivotedLocation + OffsetLocation, Composed.Rotator());
}

void UOneBrahamFirstPersonFeelComponent::TickScreenEffects(float DeltaSeconds, AOneBrahamCharacter& Character)
{
	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	const UOneBrahamGameUserSettings* Settings = UOneBrahamGameUserSettings::GetOneBrahamSettings();
	const bool bReduceMotion = Settings != nullptr && Settings->bReduceMotion;
	const bool bAllowFringe = Settings == nullptr || Settings->bChromaticAberration;

	const UOneBrahamHealthComponent* Health = Character.GetHealthComponent();
	const UOneBrahamDownedComponent* Downed = Character.GetDownedComponent();

	const float HealthFraction = Health != nullptr && Health->GetMaxHealth() > 0.f
		? FMath::Clamp(Health->GetHealth() / Health->GetMaxHealth(), 0.f, 1.f)
		: 1.f;
	const bool bAlive = Health == nullptr || Health->IsAlive();
	const bool bDowned = Downed != nullptr && Downed->IsDowned();

	// --- Targets --------------------------------------------------------------
	const float Threshold = FMath::Max(0.01f, LowHealthThreshold);
	const float LowTarget = (bAlive && !bDowned && !bDeathCamActive && HealthFraction < Threshold)
		? FMath::Clamp((Threshold - HealthFraction) / Threshold, 0.f, 1.f)
		: 0.f;

	// Slow both ways: a heal should feel like the world coming back, not a cut.
	LowHealthAlpha = FMath::FInterpTo(LowHealthAlpha, LowTarget, DeltaSeconds, ScreenBlendSpeed);
	DownedAlpha = FMath::FInterpTo(DownedAlpha, bDowned && !bDeathCamActive ? 1.f : 0.f, DeltaSeconds, ScreenBlendSpeed);
	DeathAlpha = FMath::FInterpTo(DeathAlpha, bDeathCamActive ? 1.f : 0.f, DeltaSeconds, bDeathCamActive ? 1.1f : 4.f);
	DamagePulse *= FMath::Exp(-DamagePulseDecay * DeltaSeconds);

	// The heartbeat: the same function the HUD's crosshair pulse uses, so the
	// edge of the screen and the middle of it beat together. Reduce-motion keeps
	// the tunnel but takes the throb out of it.
	const float Heartbeat = bReduceMotion ? 0.f
		: ComputeHeartbeatPulse(World->GetTimeSeconds(), HealthFraction) * FMath::Max(LowHealthAlpha, DownedAlpha * 0.6f);

	const float Pulse = DamagePulse * (bReduceMotion ? 0.5f : 1.f);

	// --- Compose ----------------------------------------------------------------
	float Saturation = FMath::Lerp(1.f, LowHealthSaturation, LowHealthAlpha) - 0.06f * Heartbeat;
	Saturation = FMath::Lerp(Saturation, DownedSaturation, DownedAlpha);
	Saturation = FMath::Lerp(Saturation, DeathCamSaturation, DeathAlpha);

	const float Vignette = Pulse * DamageVignette
		+ LowHealthAlpha * LowHealthVignette
		+ Heartbeat * HeartbeatVignette
		+ DownedAlpha * DownedVignette
		+ DeathAlpha * DeathCamVignette;

	const float Fringe = bAllowFringe
		? (LowHealthAlpha * LowHealthFringe + DownedAlpha * LowHealthFringe * 0.6f + Pulse * 0.6f)
		: 0.f;

	const float Tint = FMath::Clamp(Pulse * DamageTint + Heartbeat * 0.05f, 0.f, 1.f);

	ApplyPostProcess(Character, FMath::Clamp(Saturation, 0.f, 1.f), Vignette, Fringe, Tint, DownedAlpha);
}

void UOneBrahamFirstPersonFeelComponent::ApplyPostProcess(AOneBrahamCharacter& Character, float Saturation, float VignetteAdd,
	float Fringe, float TintAlpha, float BlurAlpha)
{
	UCameraComponent* Camera = Character.GetFirstPersonCamera();
	if (Camera == nullptr)
	{
		return;
	}

	FPostProcessSettings& PP = Camera->PostProcessSettings;

	const bool bSaturation = Saturation < 1.f - OneBrahamFeel::EffectEpsilon;
	const bool bVignette = VignetteAdd > OneBrahamFeel::EffectEpsilon;
	const bool bFringe = Fringe > OneBrahamFeel::EffectEpsilon;
	const bool bTint = TintAlpha > OneBrahamFeel::EffectEpsilon;
	const bool bBlur = BlurAlpha > 0.02f;
	const bool bAny = bSaturation || bVignette || bFringe || bTint || bBlur;

	if (!bAny && !bPostProcessDirty)
	{
		// Nothing to say: leave the level's grade alone entirely. The overrides
		// below only exist while an effect is on screen, so a healthy player sees
		// exactly the post-process volume the environment artist set.
		return;
	}

	PP.bOverride_ColorSaturation = bSaturation;
	PP.ColorSaturation = FVector4(Saturation, Saturation, Saturation, 1.f);

	PP.bOverride_VignetteIntensity = bVignette;
	PP.VignetteIntensity = FMath::Clamp(OneBrahamFeel::DefaultVignette + VignetteAdd, 0.f, 1.f);

	PP.bOverride_SceneFringeIntensity = bFringe;
	PP.SceneFringeIntensity = FMath::Clamp(Fringe, 0.f, 5.f);

	PP.bOverride_SceneColorTint = bTint;
	PP.SceneColorTint = FMath::Lerp(FLinearColor::White, OneBrahamFeel::DamageTintColour, TintAlpha);

	// The downed blur rides on depth of field: a focal plane at arm's length and
	// a wide aperture leaves the hands sharp and the room smeared. If the player
	// has depth of field switched off in their settings this does nothing, which
	// is their choice to make.
	PP.bOverride_DepthOfFieldFocalDistance = bBlur;
	PP.bOverride_DepthOfFieldFstop = bBlur;
	PP.DepthOfFieldFocalDistance = FMath::Lerp(10000.f, DownedFocalDistanceCm, BlurAlpha);
	PP.DepthOfFieldFstop = FMath::Lerp(22.f, DownedFstop, BlurAlpha);

	bPostProcessDirty = bAny;
}

// ==========================================================================
//  Death camera
// ==========================================================================
void UOneBrahamFirstPersonFeelComponent::BeginDeathCam(AActor* Killer, const FVector& FocusLocation, bool bHasFocus, float DurationSeconds)
{
	AOneBrahamCharacter* Character = GetOBCharacter();
	UCameraComponent* Camera = Character != nullptr ? Character->GetFirstPersonCamera() : nullptr;
	if (Camera == nullptr || bDeathCamActive)
	{
		return;
	}

	bDeathCamActive = true;
	DeathCamElapsed = 0.f;
	DeathCamDuration = FMath::Max(0.5f, DurationSeconds);
	DeathCamKiller = (Killer != nullptr && Killer != Character) ? Killer : nullptr;
	bDeathCamHasFocus = bHasFocus || DeathCamKiller.IsValid();
	DeathCamFocus = FocusLocation;

	DeathCamStartLocation = Camera->GetComponentLocation();
	DeathCamStartRotation = Character->GetEyeRotation();
	DeathCamLookRotation = DeathCamStartRotation;

	// Fall away from the shot: a round from the right puts you down on your left.
	DeathCamRollSign = FMath::RandBool() ? 1.f : -1.f;
	if (bDeathCamHasFocus)
	{
		const FVector Local = DeathCamStartRotation.UnrotateVector(
			(DeathCamKiller.IsValid() ? DeathCamKiller->GetActorLocation() : DeathCamFocus) - DeathCamStartLocation);
		if (FMath::Abs(Local.Y) > 1.f)
		{
			DeathCamRollSign = Local.Y > 0.f ? -1.f : 1.f;
		}
	}

	// Off the body: the capsule is about to stop colliding and the ragdoll will
	// flop it about, and a camera attached to that is a camera in the carpet.
	Camera->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
	Camera->bUsePawnControlRotation = false;
	Camera->ClearAdditiveOffset();

	if (USkeletalMeshComponent* Arms = Character->GetFirstPersonMesh())
	{
		Arms->SetVisibility(false, true);
	}

	PunchSpring.Reset();
	LandingSpring.Reset();
	WeaponRotationSpring.Reset();
	WeaponLocationSpring.Reset();
}

void UOneBrahamFirstPersonFeelComponent::EndDeathCam()
{
	if (!bDeathCamActive)
	{
		return;
	}

	bDeathCamActive = false;
	DeathCamKiller = nullptr;
	bHasLastControlRotation = false;

	if (AOneBrahamCharacter* Character = GetOBCharacter())
	{
		Character->RestoreFirstPersonView();
	}
}

void UOneBrahamFirstPersonFeelComponent::TickDeathCam(float DeltaSeconds, AOneBrahamCharacter& Character)
{
	UCameraComponent* Camera = Character.GetFirstPersonCamera();
	UWorld* World = GetWorld();
	if (Camera == nullptr || World == nullptr)
	{
		return;
	}

	DeathCamElapsed += DeltaSeconds;

	// Once the shot has landed and spectating has taken the view, the camera
	// just holds: no traces every frame for a body nobody is looking through.
	if (DeathCamElapsed > DeathCamDuration + 1.5f)
	{
		return;
	}

	const float T = FMath::Clamp(DeathCamElapsed / DeathCamDuration, 0.f, 1.f);
	const float MotionScale = GetMotionScale();

	FCollisionQueryParams Params(SCENE_QUERY_STAT(OBDeathCam), false, &Character);
	if (DeathCamKiller.IsValid())
	{
		Params.AddIgnoredActor(DeathCamKiller.Get());
	}

	// --- The collapse: fast, over the first 0.7 s, eased out --------------------
	const float DropAlpha = 1.f - FMath::Pow(1.f - FMath::Clamp(DeathCamElapsed / 0.7f, 0.f, 1.f), 3.f);
	float Drop = DeathCamDropCm * MotionScale;

	// Never through the floor.
	FHitResult FloorHit;
	if (World->LineTraceSingleByChannel(FloorHit, DeathCamStartLocation,
		DeathCamStartLocation - FVector(0.f, 0.f, Drop + 20.f), ECC_Camera, Params))
	{
		Drop = FMath::Max(0.f, FloorHit.Distance - 20.f);
	}

	const FVector Sagged = DeathCamStartLocation - FVector(0.f, 0.f, Drop * DropAlpha);

	// --- The look: a beat of nothing, then a slow turn to whoever did it --------
	FRotator Desired = DeathCamStartRotation;
	FVector PushDirection = FVector::ZeroVector;

	if (bDeathCamHasFocus)
	{
		// Aim at the head, not the feet: the killer's actor location is the middle
		// of their capsule.
		const FVector Focus = DeathCamKiller.IsValid()
			? DeathCamKiller->GetActorLocation() + FVector(0.f, 0.f, 55.f)
			: DeathCamFocus;
		const FVector ToFocus = Focus - Sagged;
		Desired = ToFocus.Rotation();
		PushDirection = ToFocus.GetSafeNormal();

		// Stop the push well short of the person: the shot is of them, not of
		// the inside of their jacket.
		const float Room = FMath::Max(0.f, ToFocus.Size() - 150.f);
		PushDirection *= FMath::Min(Room, DeathCamPushInCm * MotionScale);
	}
	else
	{
		// Nobody to look at (the building, a fall): the view sinks towards the floor.
		Desired.Pitch = FMath::Clamp(DeathCamStartRotation.Pitch - 25.f, -60.f, 10.f);
	}

	const float TurnDelay = 0.35f;
	const float TurnRamp = FMath::Clamp((DeathCamElapsed - TurnDelay) * 2.f, 0.f, 1.f);
	DeathCamLookRotation = FMath::RInterpTo(DeathCamLookRotation, Desired, DeltaSeconds, DeathCamLookInterpSpeed * TurnRamp);

	// --- The push-in: slow, eased, and never into a wall -------------------------
	FVector Location = Sagged + PushDirection * OneBrahamFeel::SmoothStep01(T);

	FHitResult PushHit;
	if (!PushDirection.IsNearlyZero() &&
		World->LineTraceSingleByChannel(PushHit, Sagged, Location, ECC_Camera, Params))
	{
		Location = PushHit.Location - PushDirection.GetSafeNormal() * 20.f;
	}

	// The head tips over with the collapse and partly rights itself as the eyes
	// find the killer: the moment the player realises what happened.
	const float Roll = DeathCamRollDegrees * DeathCamRollSign * MotionScale * DropAlpha
		* (1.f - 0.4f * OneBrahamFeel::SmoothStep01(T));

	Camera->SetWorldLocationAndRotation(Location,
		FRotator(DeathCamLookRotation.Pitch, DeathCamLookRotation.Yaw, Roll));
}
