// Copyright (c) One Braham. All Rights Reserved.

#include "Characters/OneBrahamAnimInstance.h"

#include "Characters/OneBrahamCharacter.h"
#include "Characters/OneBrahamCharacterMovementComponent.h"
#include "Data/OneBrahamWeaponDataAsset.h"
#include "Weapons/OneBrahamWeaponBase.h"
#include "Weapons/OneBrahamWeaponComponent.h"

void UOneBrahamAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();
	Character = Cast<AOneBrahamCharacter>(TryGetPawnOwner());
	RefreshPersonality();
}

EOneBrahamWeaponPose UOneBrahamAnimInstance::PoseForCategory(EOneBrahamWeaponCategory Category)
{
	switch (Category)
	{
	case EOneBrahamWeaponCategory::Pistol:
	case EOneBrahamWeaponCategory::CompactPistol:
	case EOneBrahamWeaponCategory::SuppressedPistol:
		return EOneBrahamWeaponPose::Pistol;

	case EOneBrahamWeaponCategory::PumpShotgun:
	case EOneBrahamWeaponCategory::SemiAutoShotgun:
		return EOneBrahamWeaponPose::Shotgun;

	case EOneBrahamWeaponCategory::SMG:
	case EOneBrahamWeaponCategory::PDW:
	case EOneBrahamWeaponCategory::Carbine556:
	case EOneBrahamWeaponCategory::CompactAssaultRifle:
	case EOneBrahamWeaponCategory::MarksmanRifle:
		return EOneBrahamWeaponPose::Rifle;

	default:
		return EOneBrahamWeaponPose::Unarmed;
	}
}

float UOneBrahamAnimInstance::DirectionOf(const FVector& Velocity, const FRotator& Facing)
{
	const FVector Flat = FVector(Velocity.X, Velocity.Y, 0.f);
	if (Flat.SizeSquared() < 1.f)
	{
		return 0.f;
	}

	const FRotator Heading = Flat.Rotation();
	return FRotator::NormalizeAxis(Heading.Yaw - Facing.Yaw);
}

void UOneBrahamAnimInstance::RefreshPersonality()
{
	AOneBrahamCharacter* Owner = Character.Get();
	if (Owner == nullptr || Owner->Appearance.ArchetypeTag == PersonalityFor)
	{
		return;
	}

	PersonalityFor = Owner->Appearance.ArchetypeTag;
	Personality = FOneBrahamAnimationPersonality();

	if (const UOneBrahamCharacterDataAsset* Registry = UOneBrahamCharacterDataAsset::LoadRegistry())
	{
		if (const FOneBrahamArchetypeDefinition* Definition = Registry->FindArchetype(PersonalityFor))
		{
			Personality = Definition->Personality;
		}
	}
}

void UOneBrahamAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	AOneBrahamCharacter* Owner = Character.Get();
	if (Owner == nullptr)
	{
		Owner = Cast<AOneBrahamCharacter>(TryGetPawnOwner());
		Character = Owner;
		if (Owner == nullptr)
		{
			return;
		}
	}

	RefreshPersonality();

	// --- Locomotion -------------------------------------------------------
	const FVector Velocity = Owner->GetVelocity();
	Speed = FVector(Velocity.X, Velocity.Y, 0.f).Size();
	Direction = DirectionOf(Velocity, Owner->GetActorRotation());

	const UOneBrahamCharacterMovementComponent* Movement = Owner->GetOBCharacterMovement();
	const EOneBrahamMovementMode Mode = Movement != nullptr ? Movement->GetOBMovementMode() : EOneBrahamMovementMode::Normal;
	bIsInAir = Movement != nullptr && Movement->IsFalling();
	bIsCrouched = Mode == EOneBrahamMovementMode::Crouched || Owner->bIsCrouched;
	bIsSprinting = Movement != nullptr && Movement->IsSprinting() && Speed > 50.f;
	bIsSliding = Mode == EOneBrahamMovementMode::Sliding;
	bIsMantling = Mode == EOneBrahamMovementMode::Mantling;

	// The clips were cut at a walk and a run; the play rate matches the feet to
	// the ground at any speed between, and the personality's cadence on top.
	const float Authored = bIsSprinting ? AuthoredRunSpeed : AuthoredWalkSpeed;
	GaitPlayRate = Speed > 10.f
		? FMath::Clamp(Speed / Authored, 0.5f, 1.6f) * Personality.WalkRateScale
		: 1.f;

	// --- Life -------------------------------------------------------------
	bIsDowned = Owner->LifeState == EOneBrahamLifeState::Downed;
	bIsDead = Owner->LifeState == EOneBrahamLifeState::Dead;

	// --- Aim and weapon ---------------------------------------------------
	// The base aim rotation is replicated for simulated proxies, so other
	// players' bodies look where those players are looking.
	const FRotator Aim = (Owner->GetBaseAimRotation() - Owner->GetActorRotation()).GetNormalized();
	AimPitch = FMath::Clamp(Aim.Pitch, -90.f, 90.f);
	AimYaw = FMath::Clamp(Aim.Yaw, -120.f, 120.f);
	Lean = Owner->LeanAmount;

	const UOneBrahamWeaponComponent* Weapons = Owner->GetWeaponComponent();
	const AOneBrahamWeaponBase* Weapon = Weapons != nullptr ? Weapons->GetCurrentWeapon() : nullptr;
	ADSAlpha = Weapons != nullptr ? Weapons->GetADSAlpha() : 0.f;
	if (Weapons != nullptr && !Owner->IsLocallyControlled())
	{
		// The ADS blend only runs locally; a remote body reads the flag.
		ADSAlpha = Weapons->IsAimingDownSights() ? 1.f : 0.f;
	}
	bIsReloading = Weapons != nullptr && Weapons->IsReloading();
	WeaponPose = (Weapon != nullptr && Weapon->Definition != nullptr)
		? PoseForCategory(Weapon->Definition->Category)
		: EOneBrahamWeaponPose::Unarmed;

	// --- Personality ------------------------------------------------------
	ArmSwing = Personality.ArmSwingScale * (1.f - 0.8f * ADSAlpha) * (WeaponPose == EOneBrahamWeaponPose::Unarmed ? 1.f : 0.6f);
	SprintHunchAlpha = bIsSprinting ? Personality.SprintHunch : 0.f;
}
