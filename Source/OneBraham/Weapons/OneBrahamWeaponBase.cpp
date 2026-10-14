// Copyright (c) One Braham. All Rights Reserved.

#include "Weapons/OneBrahamWeaponBase.h"

#include "OneBraham.h"
#include "Core/OneBrahamGameSettings.h"
#include "Core/OneBrahamGameState.h"
#include "Core/OneBrahamPlayerController.h"
#include "Core/OneBrahamPlayerState.h"
#include "Characters/OneBrahamCharacter.h"
#include "Characters/OneBrahamHealthComponent.h"
#include "Characters/OneBrahamRagdollComponent.h"
#include "Weapons/OneBrahamWeaponComponent.h"
#include "Data/OneBrahamWeaponDataAsset.h"
#include "Audio/OneBrahamAudioManagerComponent.h"
#include "VFX/OneBrahamVFXStatics.h"

#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/AudioComponent.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Kismet/GameplayStatics.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Materials/MaterialInstanceDynamic.h"

AOneBrahamWeaponBase::AOneBrahamWeaponBase()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.05f;
	bReplicates = true;
	SetReplicateMovement(true);
	SetNetUpdateFrequency(20.f);
	SetMinNetUpdateFrequency(5.f);

	// --- Mesh -------------------------------------------------------------
	WeaponMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("WeaponMesh"));
	SetRootComponent(WeaponMesh);
	WeaponMesh->SetCollisionProfileName(TEXT("OneBrahamWeapon"));
	WeaponMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	WeaponMesh->bCastDynamicShadow = true;
	WeaponMesh->SetGenerateOverlapEvents(false);
	WeaponMesh->SetReceivesDecals(false);

	// --- Ground pickup visual --------------------------------------------
	PickupMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PickupMesh"));
	PickupMesh->SetupAttachment(WeaponMesh);
	PickupMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PickupMesh->SetVisibility(false);

	// --- Muzzle FX --------------------------------------------------------
	MuzzleFlashComponent = CreateDefaultSubobject<UNiagaraComponent>(TEXT("MuzzleFlash"));
	MuzzleFlashComponent->SetupAttachment(WeaponMesh, TEXT("Muzzle"));
	MuzzleFlashComponent->SetAutoActivate(false);

	MuzzleLight = CreateDefaultSubobject<USpotLightComponent>(TEXT("MuzzleLight"));
	MuzzleLight->SetupAttachment(WeaponMesh, TEXT("Muzzle"));
	MuzzleLight->Intensity = 0.f;
	MuzzleLight->AttenuationRadius = 800.f;
	MuzzleLight->OuterConeAngle = 45.f;
	MuzzleLight->SetVisibility(false);
	MuzzleLight->SetCastShadows(false);

	// --- Weapon light (flashlight attachment) ----------------------------
	WeaponLight = CreateDefaultSubobject<USpotLightComponent>(TEXT("WeaponLight"));
	WeaponLight->SetupAttachment(WeaponMesh, TEXT("FlashlightMount"));
	WeaponLight->Intensity = 5200.f;
	WeaponLight->AttenuationRadius = 3200.f;
	WeaponLight->InnerConeAngle = 14.f;
	WeaponLight->OuterConeAngle = 30.f;
	WeaponLight->LightColor = FColor(255, 246, 230);
	WeaponLight->SetVisibility(false);

	FireAudioComponent = CreateDefaultSubobject<UAudioComponent>(TEXT("FireAudio"));
	FireAudioComponent->SetupAttachment(WeaponMesh, TEXT("Muzzle"));
	FireAudioComponent->bAutoActivate = false;
	FireAudioComponent->bAllowSpatialization = true;
	FireAudioComponent->bOverrideAttenuation = false;

	SpreadStream.Initialize(FMath::Rand());
}

void AOneBrahamWeaponBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AOneBrahamWeaponBase, WeaponState);
	DOREPLIFETIME(AOneBrahamWeaponBase, AmmoInMagazine);
	DOREPLIFETIME(AOneBrahamWeaponBase, Attachments);
	DOREPLIFETIME(AOneBrahamWeaponBase, SkinId);
	DOREPLIFETIME(AOneBrahamWeaponBase, bFlashlightEnabled);
	DOREPLIFETIME(AOneBrahamWeaponBase, bLaserEnabled);
	DOREPLIFETIME(AOneBrahamWeaponBase, bIsGroundPickup);
	DOREPLIFETIME(AOneBrahamWeaponBase, bSuppressorFitted);
}

void AOneBrahamWeaponBase::BeginPlay()
{
	Super::BeginPlay();

	if (Definition != nullptr)
	{
		InitialiseFromDefinition(Definition);
	}
}

void AOneBrahamWeaponBase::ResolveFxFallbacks()
{
	// Muzzle flash: the component only gets an asset if the definition names one,
	// so the conventional system is loaded when it does not.
	if (MuzzleFlashComponent != nullptr && MuzzleFlashComponent->GetAsset() == nullptr)
	{
		CachedMuzzleFlash = UOneBrahamVFXStatics::LoadEffectSystem(EOneBrahamVFXEffect::MuzzleFlash);

		if (CachedMuzzleFlash != nullptr)
		{
			MuzzleFlashComponent->SetAsset(CachedMuzzleFlash);
		}
	}

	// Shell ejection and tracer are spawned ad hoc rather than from a component,
	// so they are cached and read directly at fire time.
	if (Definition == nullptr || !Definition->ShellEject.IsValid())
	{
		CachedShellEject = UOneBrahamVFXStatics::LoadEffectSystem(EOneBrahamVFXEffect::ShellEject);
	}

	if (Definition == nullptr || !Definition->Tracer.IsValid())
	{
		CachedTracer = UOneBrahamVFXStatics::LoadEffectSystem(EOneBrahamVFXEffect::Tracer);
	}
}

void AOneBrahamWeaponBase::InitialiseFromDefinition(UOneBrahamWeaponDefinition* InDefinition)
{
	if (InDefinition == nullptr)
	{
		return;
	}

	Definition = InDefinition;
	AmmoInMagazine = InDefinition->MagazineSize;
	WeaponState = bIsGroundPickup ? EOneBrahamWeaponState::Idle : EOneBrahamWeaponState::Ready;

	SpreadStream.Initialize(GetUniqueID());

	// Meshes: the 3P mesh is on the actor root, the 1P mesh is swapped in by the
	// weapon component when the local owner equips it.
	if (InDefinition->ThirdPersonMesh.IsValid() && WeaponMesh != nullptr)
	{
		if (USkeletalMesh* Mesh = InDefinition->ThirdPersonMesh.LoadSynchronous())
		{
			WeaponMesh->SetSkeletalMesh(Mesh);
		}
	}

	if (InDefinition->WorldPickupMesh.IsValid() && PickupMesh != nullptr)
	{
		if (UStaticMesh* Mesh = InDefinition->WorldPickupMesh.LoadSynchronous())
		{
			PickupMesh->SetStaticMesh(Mesh);
		}
	}

	if (PickupMesh != nullptr)
	{
		PickupMesh->SetVisibility(bIsGroundPickup);
	}

	// Muzzle flash system on the muzzle socket.
	if (MuzzleFlashComponent != nullptr && InDefinition->MuzzleFlash.IsValid())
	{
		if (UNiagaraSystem* System = InDefinition->MuzzleFlash.LoadSynchronous())
		{
			MuzzleFlashComponent->SetAsset(System);
		}
	}

	ResolveFxFallbacks();

	RefreshMaterialParameters();

	OB_WEAPON_LOG("Weapon '%s' initialised (%d rounds, %s).",
		*InDefinition->WeaponId.ToString(), AmmoInMagazine, bIsGroundPickup ? TEXT("ground") : TEXT("held"));
}

void AOneBrahamWeaponBase::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	AdvanceStateTimer(DeltaSeconds);

	// Muzzle light decay: a one-frame flash reads as a strobe; a short decay reads
	// as an actual muzzle flash.
	if (MuzzleLight != nullptr && MuzzleLight->IsVisible())
	{
		const float NewIntensity = FMath::FInterpTo(MuzzleLight->Intensity, 0.f, DeltaSeconds, 22.f);
		MuzzleLight->SetIntensity(NewIntensity);
		if (NewIntensity < 60.f)
		{
			MuzzleLight->SetVisibility(false);
		}
	}
}

EOneBrahamWeaponState AOneBrahamWeaponBase::SettledState() const
{
	// Reloading is never a settled state: the reload owns its own completion and
	// sets the state when it finishes, which is what makes a reload interruptible
	// by death or by a weapon swap without leaving the gun stuck.
	return AmmoInMagazine > 0 ? EOneBrahamWeaponState::Ready : EOneBrahamWeaponState::Empty;
}

void AOneBrahamWeaponBase::AdvanceStateTimer(float DeltaSeconds)
{
	if (StateSecondsRemaining <= 0.f)
	{
		return;
	}

	StateSecondsRemaining -= DeltaSeconds;

	if (StateSecondsRemaining > 0.f)
	{
		return;
	}

	StateSecondsRemaining = 0.f;

	switch (WeaponState)
	{
	case EOneBrahamWeaponState::Drawing:
	{
		SetWeaponState(SettledState());

		// Tell the owner's component, which is what resets the shot tracking and
		// lets a held trigger resume firing. This is the call the weapon component
		// has always documented and never received.
		if (OwningWeaponComponent.IsValid())
		{
			OwningWeaponComponent->OnWeaponReady();
		}
		break;
	}

	case EOneBrahamWeaponState::Holstering:
		// Nothing to do: the component decides what to draw next, and it will.
		SetWeaponState(EOneBrahamWeaponState::Idle);
		break;

	case EOneBrahamWeaponState::Meleeing:
		SetWeaponState(SettledState());
		break;

	default:
		break;
	}

	OB_WEAPON_LOG("Weapon '%s' settled into %s.", *GetName(),
		*UEnum::GetValueAsString(WeaponState));
}

void AOneBrahamWeaponBase::OnEquippedBy(AOneBrahamCharacter* NewOwnerCharacter)
{
	OwningCharacter = NewOwnerCharacter;
	bIsGroundPickup = false;

	// The draw owns a timer from the moment it starts. See AdvanceStateTimer for
	// what happens when it is missing.
	StateSecondsRemaining = Definition != nullptr ? FMath::Max(0.05f, Definition->DrawTime) : 0.3f;
	SetWeaponState(EOneBrahamWeaponState::Drawing);

	if (PickupMesh != nullptr)
	{
		PickupMesh->SetVisibility(false);
	}

	// Attach to the correct hand/rig socket.
	if (NewOwnerCharacter != nullptr && WeaponMesh != nullptr)
	{
		USkeletalMeshComponent* TargetMesh = NewOwnerCharacter->IsLocallyControlled()
			? NewOwnerCharacter->GetFirstPersonMesh()
			: NewOwnerCharacter->GetThirdPersonMesh();

		if (TargetMesh != nullptr)
		{
			// Weapon root socket: the anim BP drives the hand IK; the socket is
			// only the attach point so that recoil/aim offsets are additive.
			AttachToComponent(TargetMesh, FAttachmentTransformRules::SnapToTargetNotIncludingScale,
				NewOwnerCharacter->IsLocallyControlled() ? TEXT("Weapon_1P") : TEXT("Weapon_3P"));
		}
	}

	// Draw sound + animation.
	if (Definition != nullptr && Definition->DrawSound.IsValid())
	{
		if (USoundBase* Sound = Definition->DrawSound.LoadSynchronous())
		{
			UGameplayStatics::PlaySoundAtLocation(this, Sound, GetActorLocation(), 0.8f, FMath::FRandRange(0.95f, 1.05f));
		}
	}

	ResetShotTracking();
}

void AOneBrahamWeaponBase::OnUnequipped()
{
	if (WeaponMesh != nullptr)
	{
		WeaponMesh->SetGenerateOverlapEvents(false);
	}

	OwningCharacter = nullptr;
	StateSecondsRemaining = Definition != nullptr ? FMath::Max(0.05f, Definition->HolsterTime) : 0.25f;
	SetWeaponState(EOneBrahamWeaponState::Holstering);
}

// ==========================================================================
//  Sockets
// ==========================================================================
FTransform AOneBrahamWeaponBase::GetMuzzleTransform() const
{
	if (WeaponMesh != nullptr && Definition != nullptr && WeaponMesh->DoesSocketExist(Definition->MuzzleSocket))
	{
		return WeaponMesh->GetSocketTransform(Definition->MuzzleSocket, RTS_World);
	}

	return WeaponMesh != nullptr ? WeaponMesh->GetComponentTransform() : GetActorTransform();
}

FVector AOneBrahamWeaponBase::GetMuzzleLocation() const
{
	return GetMuzzleTransform().GetLocation();
}

FTransform AOneBrahamWeaponBase::GetShellEjectTransform() const
{
	if (WeaponMesh != nullptr && Definition != nullptr && WeaponMesh->DoesSocketExist(Definition->ShellEjectSocket))
	{
		return WeaponMesh->GetSocketTransform(Definition->ShellEjectSocket, RTS_World);
	}

	return GetMuzzleTransform();
}

// ==========================================================================
//  Firing - the authoritative path
// ==========================================================================
float AOneBrahamWeaponBase::ServerFireShot(const FVector& StartLocation, const FVector& AimDirection, TArray<FOneBrahamImpactInfo>& OutImpacts, AActor*& OutFirstHitActor)
{
	OutImpacts.Reset();
	OutFirstHitActor = nullptr;

	if (Definition == nullptr || !HasAuthority() || AmmoInMagazine <= 0)
	{
		return 0.f;
	}

	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return 0.f;
	}

	// --- Rate of fire enforcement ----------------------------------------
	const float Now = World->GetTimeSeconds();
	const float MinInterval = Definition->GetSecondsBetweenShots();
	const float Tolerance = UOneBrahamGameSettings::Get().MaxFireRateTolerance;

	if (Now - LastShotTime < MinInterval - Tolerance)
	{
		// The client fired faster than the weapon allows. Rather than disabling the
		// weapon (which punishes lag), the extra shot is dropped and logged.
		if (AOneBrahamPlayerController* PC = Cast<AOneBrahamPlayerController>(OwningCharacter.IsValid() ? OwningCharacter->GetController() : nullptr))
		{
			PC->ReportViolation(FString::Printf(TEXT("Fire rate: %.4fs since last shot (min %.4f) on %s"),
				Now - LastShotTime, MinInterval, *Definition->WeaponId.ToString()), 1.f);
		}
		return 0.f;
	}

	LastShotTime = Now;
	++ShotCounter;

	// --- Consume the round ------------------------------------------------
	AmmoInMagazine = FMath::Max(0, AmmoInMagazine - 1);
	OnRep_Ammo();

	float TotalDamage = 0.f;

	const int32 Pellets = FMath::Max(1, Definition->PelletCount);
	for (int32 PelletIndex = 0; PelletIndex < Pellets; ++PelletIndex)
	{
		// --- Spread -------------------------------------------------------
		FVector Direction = AimDirection;

		if (OwningWeaponComponent.IsValid())
		{
			// The component owns the spread state (bloom, movement, ADS) because it
			// is the thing that knows how the player is currently moving.
			const float SpreadDegrees = OwningWeaponComponent->ComputeCurrentSpreadDegrees();
			if (SpreadDegrees > 0.001f)
			{
				const FVector RandomCone = SpreadStream.VRandCone(Direction, FMath::DegreesToRadians(SpreadDegrees));
				Direction = RandomCone.GetSafeNormal();
			}
		}

		FOneBrahamImpactInfo Impact;
		AActor* HitActor = nullptr;
		TotalDamage += TraceBullet(StartLocation, Direction, Impact, HitActor);

		if (PelletIndex == 0)
		{
			OutFirstHitActor = HitActor;
		}

		OutImpacts.Add(Impact);
	}

	if (AmmoInMagazine <= 0)
	{
		// Leave the weapon in a "needs reload" state; the component decides whether
		// to auto-reload based on the player's preference.
		SetWeaponState(EOneBrahamWeaponState::Empty);
	}

	OnWeaponFired.Broadcast(this, bSuppressorFitted);

	if (OwningWeaponComponent.IsValid())
	{
		OwningWeaponComponent->OnServerShotFired(ShotCounter);
	}

	// FX go to every machine including the shooter (the shooter's own tracer is
	// predicted locally too, but the multicast keeps them in step).
	MulticastPlayFireFX(GetMuzzleLocation(), AimDirection, bSuppressorFitted);
	MulticastPlayImpactFX(OutImpacts);

	return TotalDamage;
}

float AOneBrahamWeaponBase::TraceBullet(const FVector& Start, const FVector& Direction, FOneBrahamImpactInfo& OutImpact, AActor*& OutHitActor)
{
	OutHitActor = nullptr;
	OutImpact.Location = Start + Direction * Definition->Falloff.MinDamageRange;
	OutImpact.Normal = -Direction;

	const UWorld* World = GetWorld();
	if (World == nullptr || Definition == nullptr)
	{
		return 0.f;
	}

	// --- The trace --------------------------------------------------------
	// ONE BRAHAM uses a single trace with a distance, not a multi-bounce
	// penetration simulation: at 60 Hz with 16 players this is the difference
	// between a stable server and a stuttering one. Cover penetration is modelled
	// by the two-surface rule below.
	const float MaxDistance = Definition->Falloff.MinDamageRange * 2.f;
	const FVector End = Start + Direction * MaxDistance;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(OBWeaponTrace), /*bTraceComplex*/ true);
	Params.bReturnPhysicalMaterial = true;
	Params.AddIgnoredActor(this);
	if (OwningCharacter.IsValid())
	{
		Params.AddIgnoredActor(OwningCharacter.Get());
	}
	if (AActor* Owner = GetOwner())
	{
		Params.AddIgnoredActor(Owner);
	}

	FHitResult Hit;
	// The Weapon channel hits world geometry, characters (via their capsule/body)
	// and the Penetrable channel's shoot-through surfaces.
	const bool bHit = World->LineTraceSingleByChannel(Hit, Start, End, ECC_GameTraceChannel1 /*Weapon*/, Params);

	if (!bHit)
	{
		return 0.f;
	}

	OutImpact.Location = Hit.ImpactPoint;
	OutImpact.Normal = Hit.ImpactNormal;
	OutImpact.Surface = ResolveSurface(Hit.PhysMaterial.Get());
	OutImpact.HitBoneName = Hit.BoneName;
	OutHitActor = Hit.GetActor();

	// --- Character hit ----------------------------------------------------
	if (AOneBrahamCharacter* HitCharacter = Cast<AOneBrahamCharacter>(Hit.GetActor()))
	{
		OutImpact.bHitCharacter = true;

		FOneBrahamDamageReport Report;
		Report.BaseDamage = Definition->GetDamageAtDistance(Hit.Distance);
		Report.HitZone = ResolveHitZone(Hit.BoneName);
		Report.HitBoneName = Hit.BoneName;
		Report.HitLocation = Hit.ImpactPoint;
		Report.ShotDirection = Direction;
		Report.Distance = Hit.Distance;
		Report.bWasPenetrated = false;

		AController* InstigatorController = OwningCharacter.IsValid() ? OwningCharacter->GetController() : nullptr;

		if (UOneBrahamHealthComponent* Health = HitCharacter->GetHealthComponent())
		{
			const float Dealt = Health->ApplyDamage(Report.BaseDamage, OwningCharacter.Get(), Report, InstigatorController);

			// Non-lethal hit reaction: the limb takes the physics, the player keeps
			// control. Killing blows go through the health component's OnDeath.
			if (Dealt > 0.f && Health->IsAlive())
			{
				if (UOneBrahamRagdollComponent* Ragdoll = HitCharacter->GetRagdollComponent())
				{
					Ragdoll->ApplyHitReaction(Hit.BoneName, Direction, Dealt);
				}
			}

			// Hit confirmation goes only to the shooter's own machine, and only
			// when damage actually landed: shooting a corpse or a downed player who
			// has already bled out must not reward a hit marker.
			if (Dealt > 0.f && OwningCharacter.IsValid())
			{
				if (UOneBrahamWeaponComponent* ShooterWeapons = OwningCharacter->GetWeaponComponent())
				{
					const bool bHeadshot = Report.HitZone == EOneBrahamHitZone::Head
						|| Report.HitZone == EOneBrahamHitZone::Neck;
					const bool bKilled = !Health->IsAlive();

					ShooterWeapons->NotifyLocalHitConfirmed(bHeadshot, bKilled, Hit.Distance);
				}
			}

			OutImpact.bHitCharacter = true;
			return Dealt;
		}

		return 0.f;
	}

	// --- Breakable glass --------------------------------------------------
	if (Hit.GetComponent() != nullptr && Hit.GetComponent()->GetCollisionObjectType() == ECC_GameTraceChannel2 /*Glass*/)
	{
		OutImpact.bHitGlass = true;
		// Glass is handled by AOneBrahamBreakableGlass implementing the damage
		// interface; the weapon only reports the impact.
		UGameplayStatics::ApplyPointDamage(Hit.GetActor(), Definition->GetDamageAtDistance(Hit.Distance),
			Direction, Hit, OwningCharacter.IsValid() ? OwningCharacter->GetController() : nullptr, this, nullptr);
		return 0.f;
	}

	// --- Penetration: cubicle walls, plasterboard partitions --------------
	if (Definition->bCanPenetrateCover)
	{
		const AActor* HitActor = Hit.GetActor();
		const bool bPenetrable = HitActor != nullptr &&
			HitActor->GetRootComponent() != nullptr &&
			HitActor->GetRootComponent()->GetCollisionObjectType() == ECC_GameTraceChannel4; /*Penetrable*/

		if (bPenetrable)
		{
			// Continue the trace past the surface: the second hit is what actually
			// takes the damage. This is how shooting through a cubicle wall works.
			const FVector SecondStart = Hit.ImpactPoint + Direction * 2.f;
			const FVector SecondEnd = SecondStart + Direction * MaxDistance;

			FCollisionQueryParams SecondParams(SCENE_QUERY_STAT(OBWeaponTracePenetrated), true);
			SecondParams.AddIgnoredActor(this);
			SecondParams.AddIgnoredActor(HitActor);
			if (OwningCharacter.IsValid())
			{
				SecondParams.AddIgnoredActor(OwningCharacter.Get());
			}

			FHitResult SecondHit;
			if (World->LineTraceSingleByChannel(SecondHit, SecondStart, SecondEnd, ECC_GameTraceChannel1, SecondParams))
			{
				if (AOneBrahamCharacter* HitCharacter = Cast<AOneBrahamCharacter>(SecondHit.GetActor()))
				{
					FOneBrahamDamageReport Report;
					Report.BaseDamage = Definition->GetDamageAtDistance(SecondHit.Distance);
					Report.HitZone = ResolveHitZone(SecondHit.BoneName);
					Report.HitBoneName = SecondHit.BoneName;
					Report.HitLocation = SecondHit.ImpactPoint;
					Report.ShotDirection = Direction;
					Report.Distance = SecondHit.Distance;
					Report.bWasPenetrated = true;

					OutImpact.Location = SecondHit.ImpactPoint;
					OutImpact.Normal = SecondHit.ImpactNormal;
					OutImpact.bWasPenetrated = true;
					OutImpact.bHitCharacter = true;
					OutImpact.HitBoneName = SecondHit.BoneName;
					OutHitActor = SecondHit.GetActor();

					if (UOneBrahamHealthComponent* Health = HitCharacter->GetHealthComponent())
					{
						return Health->ApplyDamage(Report.BaseDamage, OwningCharacter.Get(), Report,
							OwningCharacter.IsValid() ? OwningCharacter->GetController() : nullptr);
					}
				}
				else
				{
					// The round went through the wall and hit the far wall.
					OutImpact.Location = SecondHit.ImpactPoint;
					OutImpact.Normal = SecondHit.ImpactNormal;
					OutImpact.Surface = ResolveSurface(SecondHit.PhysMaterial.Get());
				}
			}
		}
	}

	return 0.f;
}

EOneBrahamHitZone AOneBrahamWeaponBase::ResolveHitZone(FName BoneName)
{
	const FString Bone = BoneName.ToString().ToLower();

	// The bone naming matches the ONE BRAHAM MetaHuman-derived skeleton (see
	// Docs/13). We match on substrings so a rig tweak does not silently break
	// headshots - the single worst possible regression in a shooter.
	if (Bone.Contains(TEXT("head")) || Bone.Contains(TEXT("face")) || Bone.Contains(TEXT("jaw")))
	{
		return EOneBrahamHitZone::Head;
	}
	if (Bone.Contains(TEXT("neck")))
	{
		return EOneBrahamHitZone::Neck;
	}
	if (Bone.Contains(TEXT("hand")) || Bone.Contains(TEXT("finger")) || Bone.Contains(TEXT("thumb")))
	{
		return EOneBrahamHitZone::Hand;
	}
	if (Bone.Contains(TEXT("lowerarm")) || Bone.Contains(TEXT("forearm")) || Bone.Contains(TEXT("_l_arm_lower")) || Bone.Contains(TEXT("_r_arm_lower")))
	{
		return EOneBrahamHitZone::LowerArm;
	}
	if (Bone.Contains(TEXT("upperarm")) || Bone.Contains(TEXT("clavicle")) || Bone.Contains(TEXT("shoulder")) || Bone.Contains(TEXT("_arm_upper")))
	{
		return EOneBrahamHitZone::UpperArm;
	}
	if (Bone.Contains(TEXT("lowerleg")) || Bone.Contains(TEXT("calf")) || Bone.Contains(TEXT("_shin")))
	{
		return EOneBrahamHitZone::LowerLeg;
	}
	if (Bone.Contains(TEXT("upperleg")) || Bone.Contains(TEXT("thigh")))
	{
		return EOneBrahamHitZone::UpperLeg;
	}
	if (Bone.Contains(TEXT("foot")) || Bone.Contains(TEXT("ball")) || Bone.Contains(TEXT("toe")))
	{
		return EOneBrahamHitZone::Foot;
	}

	return EOneBrahamHitZone::Torso;
}

EOneBrahamFootstepSurface AOneBrahamWeaponBase::ResolveSurface(const UPhysicalMaterial* PhysicalMaterial)
{
	if (PhysicalMaterial == nullptr)
	{
		return EOneBrahamFootstepSurface::Concrete;
	}

	switch (PhysicalMaterial->SurfaceType)
	{
	case SurfaceType1:  return EOneBrahamFootstepSurface::Marble;
	case SurfaceType2:  return EOneBrahamFootstepSurface::Carpet;
	case SurfaceType3:  return EOneBrahamFootstepSurface::Concrete;
	case SurfaceType4:  return EOneBrahamFootstepSurface::Metal;
	case SurfaceType5:  return EOneBrahamFootstepSurface::Glass;
	case SurfaceType6:  return EOneBrahamFootstepSurface::Wood;
	case SurfaceType7:  return EOneBrahamFootstepSurface::Puddle;
	case SurfaceType8:  return EOneBrahamFootstepSurface::Grating;
	case SurfaceType9:  return EOneBrahamFootstepSurface::Fabric;
	case SurfaceType10: return EOneBrahamFootstepSurface::ServerRack;
	case SurfaceType11: return EOneBrahamFootstepSurface::Plasterboard;
	case SurfaceType12: return EOneBrahamFootstepSurface::Lino;
	case SurfaceType13: return EOneBrahamFootstepSurface::Decking;
	case SurfaceType14: return EOneBrahamFootstepSurface::Flesh;
	case SurfaceType15: return EOneBrahamFootstepSurface::BallisticGlass;
	default:            return EOneBrahamFootstepSurface::Concrete;
	}
}

// ==========================================================================
//  Ammo / reload
// ==========================================================================
int32 AOneBrahamWeaponBase::GetMagazineSize() const
{
	if (Definition == nullptr)
	{
		return 0;
	}

	int32 Size = Definition->MagazineSize;
	if (const FName* ExtendedMag = Attachments.Find(EOneBrahamAttachmentSlot::Magazine))
	{
		// The delta comes from the attachment table, looked up by the component's
		// registry, and is cached into the attachment map at attach time.
		UE_UNUSED(ExtendedMag);
	}
	return Size;
}

void AOneBrahamWeaponBase::SetAmmoInMagazine(int32 NewAmmo)
{
	if (!HasAuthority())
	{
		return;
	}

	AmmoInMagazine = FMath::Clamp(NewAmmo, 0, GetMagazineSize());
	OnRep_Ammo();
	ForceNetUpdate();
}

float AOneBrahamWeaponBase::GetReloadDuration() const
{
	return ComputeReloadDuration();
}

float AOneBrahamWeaponBase::GetReloadTimeRemaining() const
{
	const UWorld* World = GetWorld();
	if (World == nullptr || !World->GetTimerManager().IsTimerActive(ReloadTimerHandle))
	{
		return 0.f;
	}

	// Read the live timer rather than mirroring the value: a reload interrupted by
	// a weapon swap must not leave the HUD ring stuck mid-sweep.
	return FMath::Max(0.f, World->GetTimerManager().GetTimerRemaining(ReloadTimerHandle));
}

float AOneBrahamWeaponBase::ComputeReloadDuration() const
{
	if (Definition == nullptr)
	{
		return 2.f;
	}

	if (Definition->bPerShellReload)
	{
		return Definition->PerShellReloadTime;
	}

	return AmmoInMagazine > 0 ? Definition->ReloadTimeTactical : Definition->ReloadTimeEmpty;
}

float AOneBrahamWeaponBase::ServerStartReload()
{
	if (Definition == nullptr || !HasAuthority())
	{
		return 0.f;
	}

	if (WeaponState == EOneBrahamWeaponState::Reloading)
	{
		return 0.f;
	}

	const float Duration = ComputeReloadDuration();

	SetWeaponState(EOneBrahamWeaponState::Reloading);
	MulticastPlayReloadFX(true);

	GetWorldTimerManager().ClearTimer(ReloadTimerHandle);
	GetWorldTimerManager().SetTimer(ReloadTimerHandle, this, &AOneBrahamWeaponBase::ServerFinishReload, Duration, false);

	return Duration;
}

void AOneBrahamWeaponBase::ServerFinishReload()
{
	if (Definition == nullptr || !HasAuthority())
	{
		return;
	}

	SetWeaponState(EOneBrahamWeaponState::Ready);
	ResetShotTracking();

	MulticastPlayReloadFX(false);

	// The component owns the reserve pool, so it does the actual ammo transfer.
	if (OwningWeaponComponent.IsValid())
	{
		OwningWeaponComponent->ServerCompleteReload();
	}
}

void AOneBrahamWeaponBase::ServerCancelReload()
{
	if (!HasAuthority())
	{
		return;
	}

	GetWorldTimerManager().ClearTimer(ReloadTimerHandle);
	SetWeaponState(AmmoInMagazine > 0 ? EOneBrahamWeaponState::Ready : EOneBrahamWeaponState::Empty);
	MulticastPlayReloadFX(false);
}

float AOneBrahamWeaponBase::ServerMelee()
{
	if (Definition == nullptr || !HasAuthority())
	{
		return 0.f;
	}

	if (AOneBrahamWeaponComponent* Component = OwningWeaponComponent.Get())
	{
		if (!Component->CanMelee())
		{
			return 0.f;
		}
	}

	AOneBrahamCharacter* Character = OwningCharacter.Get();
	if (Character == nullptr)
	{
		return 0.f;
	}

	SetWeaponState(EOneBrahamWeaponState::Meleeing);

	// Melee is a short sphere trace from the eye: a rifle butt is not a long weapon.
	const FVector Start = Character->GetEyeLocation();
	const FVector End = Start + Character->GetEyeRotation().Vector() * Definition->MeleeRange;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(OBMelee), false, Character);
	FHitResult Hit;

	float Damage = 0.f;
	if (GetWorld()->SweepSingleByChannel(Hit, Start, End, FQuat::Identity, ECC_GameTraceChannel1,
		FCollisionShape::MakeSphere(18.f), Params))
	{
		if (AOneBrahamCharacter* HitCharacter = Cast<AOneBrahamCharacter>(Hit.GetActor()))
		{
			if (UOneBrahamHealthComponent* Health = HitCharacter->GetHealthComponent())
			{
				FOneBrahamDamageReport Report;
				Report.BaseDamage = Definition->MeleeDamage;
				Report.HitZone = EOneBrahamHitZone::Torso;
				Report.HitLocation = Hit.ImpactPoint;
				Report.ShotDirection = (End - Start).GetSafeNormal();
				Report.Distance = Hit.Distance;

				Damage = Health->ApplyDamage(Report.BaseDamage, Character, Report, Character->GetController());
			}
		}
	}

	// Return to ready after the cooldown.
	FTimerHandle Handle;
	GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		SetWeaponState(AmmoInMagazine > 0 ? EOneBrahamWeaponState::Ready : EOneBrahamWeaponState::Empty);
	}), Definition->MeleeCooldown, false);

	return Damage;
}

void AOneBrahamWeaponBase::ResetShotTracking()
{
	ShotCounter = 0;
	LastShotTime = -1000.f;
	if (OwningWeaponComponent.IsValid())
	{
		OwningWeaponComponent->ResetRecoilAndSpread();
	}
}

// ==========================================================================
//  Attachments
// ==========================================================================
bool AOneBrahamWeaponBase::AttachAttachment(FName AttachmentId)
{
	// Attachment validation is done by the component (which owns the registry);
	// this function only mutates the replicated map + meshes.
	if (AttachmentId.IsNone())
	{
		return false;
	}

	// The slot is looked up by the component, which passes the resolved slot in
	// via the attachment's own definition. To keep this actor self-contained we
	// store the id and let the component pass the slot through the map key order.
	// (The component calls with a slot-aware overload; this generic entry point is
	// used by Blueprint tools and the loadout screen.)
	if (UOneBrahamWeaponComponent* Component = OwningWeaponComponent.Get())
	{
		const EOneBrahamAttachmentSlot Slot = Component->GetSlotForAttachment(AttachmentId);
		Attachments.Add(Slot, AttachmentId);

		if (Slot == EOneBrahamAttachmentSlot::Muzzle && Component->IsSuppressorAttachment(AttachmentId))
		{
			bSuppressorFitted = true;
		}

		RebuildAttachmentMeshes();
		ForceNetUpdate();
		return true;
	}

	return false;
}

bool AOneBrahamWeaponBase::DetachAttachment(EOneBrahamAttachmentSlot Slot)
{
	const FName* Existing = Attachments.Find(Slot);
	if (Existing == nullptr)
	{
		return false;
	}

	if (Slot == EOneBrahamAttachmentSlot::Muzzle)
	{
		bSuppressorFitted = false;
	}

	Attachments.Remove(Slot);
	RebuildAttachmentMeshes();
	ForceNetUpdate();
	return true;
}

bool AOneBrahamWeaponBase::HasAttachmentInSlot(EOneBrahamAttachmentSlot Slot) const
{
	return Attachments.Contains(Slot);
}

FName AOneBrahamWeaponBase::GetAttachmentInSlot(EOneBrahamAttachmentSlot Slot) const
{
	const FName* Found = Attachments.Find(Slot);
	return Found != nullptr ? *Found : NAME_None;
}

void AOneBrahamWeaponBase::RebuildAttachmentMeshes()
{
	// Attachment meshes are spawned as static mesh components attached to the
	// weapon's sockets. They are recreated wholesale rather than diffed: this runs
	// on a loadout change, not per frame, so clarity beats cleverness.
	TArray<UStaticMeshComponent*> Existing;
	GetComponents<UStaticMeshComponent>(Existing);

	for (UStaticMeshComponent* Component : Existing)
	{
		if (Component != nullptr && Component != PickupMesh)
		{
			Component->DestroyComponent();
		}
	}

	if (UOneBrahamWeaponComponent* Component = OwningWeaponComponent.Get())
	{
		for (const TPair<EOneBrahamAttachmentSlot, FName>& Pair : Attachments)
		{
			const FOneBrahamAttachmentDefinition* Attachment = Component->GetAttachmentDefinition(Pair.Value);
			if (Attachment == nullptr || !Attachment->Mesh.IsValid())
			{
				continue;
			}

			UStaticMesh* Mesh = Attachment->Mesh.LoadSynchronous();
			if (Mesh == nullptr)
			{
				continue;
			}

			UStaticMeshComponent* MeshComponent = NewObject<UStaticMeshComponent>(this);
			MeshComponent->SetStaticMesh(Mesh);
			MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			MeshComponent->SetupAttachment(WeaponMesh, GetAttachmentSocketName(Pair.Key));
			MeshComponent->RegisterComponent();

			// The laser and flashlight attachments drive the weapon lights rather
			// than carrying their own.
			if (Pair.Key == EOneBrahamAttachmentSlot::Laser && WeaponLight != nullptr)
			{
				WeaponLight->SetVisibility(bLaserEnabled || bFlashlightEnabled);
			}
		}
	}

	RefreshMaterialParameters();
}

FName AOneBrahamWeaponBase::GetAttachmentSocketName(EOneBrahamAttachmentSlot Slot) const
{
	if (Definition == nullptr)
	{
		return NAME_None;
	}

	switch (Slot)
	{
	case EOneBrahamAttachmentSlot::Optic:		return Definition->OpticSocket;
	case EOneBrahamAttachmentSlot::Muzzle:		return Definition->MuzzleAttachmentSocket;
	case EOneBrahamAttachmentSlot::Underbarrel: return Definition->GripSocket;
	case EOneBrahamAttachmentSlot::Laser:		return Definition->LaserSocket;
	case EOneBrahamAttachmentSlot::Flashlight:	return Definition->LaserSocket;
	case EOneBrahamAttachmentSlot::Magazine:	return Definition->MagazineWellSocket;
	default:									return NAME_None;
	}
}

void AOneBrahamWeaponBase::ApplySkin(FName InSkinId)
{
	SkinId = InSkinId;
	RefreshMaterialParameters();
	ForceNetUpdate();
}

void AOneBrahamWeaponBase::OnRep_SkinId()
{
	// The value arrived; the paint job did not, because a dynamic material
	// instance lives on the machine that created it. Rebuild it locally from the
	// replicated id - and from the skin table, which every machine has.
	RefreshMaterialParameters();
}

void AOneBrahamWeaponBase::RefreshMaterialParameters()
{
	// M_WeaponMaster exposes: BaseTint (vector), Wear (scalar), Pattern (texture),
	// SkinMask (scalar). Skins are cosmetic only - no gameplay effect whatsoever.
	if (WeaponMesh == nullptr)
	{
		return;
	}

	UMaterialInstanceDynamic* MID = WeaponMesh->CreateAndSetMaterialInstanceDynamic(0);
	if (MID == nullptr)
	{
		return;
	}

	if (SkinId.IsNone())
	{
		MID->SetVectorParameterValue(TEXT("BaseTint"), FLinearColor::White);
		MID->SetScalarParameterValue(TEXT("Wear"), 0.35f);
		return;
	}

	if (UOneBrahamWeaponComponent* Component = OwningWeaponComponent.Get())
	{
		if (const FOneBrahamWeaponSkin* Skin = Component->GetSkinDefinition(SkinId))
		{
			MID->SetVectorParameterValue(TEXT("BaseTint"), Skin->Tint);
			MID->SetScalarParameterValue(TEXT("Wear"), Skin->WearAmount);

			if (Skin->PatternTexture.IsValid())
			{
				if (UTexture2D* Texture = Skin->PatternTexture.LoadSynchronous())
				{
					MID->SetTextureParameterValue(TEXT("Pattern"), Texture);
					MID->SetScalarParameterValue(TEXT("SkinMask"), 1.f);
				}
			}
		}
	}
}

void AOneBrahamWeaponBase::SetFlashlightEnabled(bool bEnabled)
{
	bFlashlightEnabled = bEnabled;

	if (WeaponLight != nullptr)
	{
		WeaponLight->SetVisibility(bEnabled);
	}

	ForceNetUpdate();
}

void AOneBrahamWeaponBase::SetLaserEnabled(bool bEnabled)
{
	bLaserEnabled = bEnabled;

	// The laser is a Niagara beam + a tiny emissive dot; the dot is what enemies
	// actually see, which is the balance point (a laser gives you away).
	if (WeaponMesh != nullptr)
	{
		if (UNiagaraComponent* Laser = Cast<UNiagaraComponent>(WeaponMesh->GetAttachChildren().FindByPredicate([](USceneComponent* Child)
			{
				return Child != nullptr && Child->GetName().Contains(TEXT("Laser"));
			}) ? *WeaponMesh->GetAttachChildren().FindByPredicate([](USceneComponent* Child)
			{
				return Child != nullptr && Child->GetName().Contains(TEXT("Laser"));
			}) : nullptr))
		{
			Laser->SetVisibility(bEnabled);
		}
	}

	ForceNetUpdate();
}

// ==========================================================================
//  Effective stats
// ==========================================================================
float AOneBrahamWeaponBase::GetEffectiveRecoilMultiplier() const
{
	float Multiplier = 1.f;

	if (UOneBrahamWeaponComponent* Component = OwningWeaponComponent.Get())
	{
		for (const TPair<EOneBrahamAttachmentSlot, FName>& Pair : Attachments)
		{
			if (const FOneBrahamAttachmentDefinition* Attachment = Component->GetAttachmentDefinition(Pair.Value))
			{
				Multiplier *= Attachment->RecoilMultiplier;
			}
		}
	}

	return Multiplier;
}

float AOneBrahamWeaponBase::GetEffectiveSpreadMultiplier() const
{
	float Multiplier = 1.f;

	if (UOneBrahamWeaponComponent* Component = OwningWeaponComponent.Get())
	{
		for (const TPair<EOneBrahamAttachmentSlot, FName>& Pair : Attachments)
		{
			if (const FOneBrahamAttachmentDefinition* Attachment = Component->GetAttachmentDefinition(Pair.Value))
			{
				Multiplier *= Attachment->SpreadMultiplier;
			}
		}
	}

	return Multiplier;
}

float AOneBrahamWeaponBase::GetEffectiveADSTime() const
{
	if (Definition == nullptr)
	{
		return 0.25f;
	}

	float Time = Definition->ADSTime;

	if (UOneBrahamWeaponComponent* Component = OwningWeaponComponent.Get())
	{
		for (const TPair<EOneBrahamAttachmentSlot, FName>& Pair : Attachments)
		{
			if (const FOneBrahamAttachmentDefinition* Attachment = Component->GetAttachmentDefinition(Pair.Value))
			{
				Time *= Attachment->ADS_SpeedMultiplier;
			}
		}
	}

	return Time;
}

// ==========================================================================
//  FX
// ==========================================================================
void AOneBrahamWeaponBase::MulticastPlayFireFX_Implementation(const FVector_NetQuantize& MuzzleLocation, const FVector_NetQuantizeNormal& Direction, bool bSuppressed)
{
	// --- Muzzle flash -----------------------------------------------------
	if (!bSuppressed && MuzzleFlashComponent != nullptr)
	{
		MuzzleFlashComponent->Activate(true);
	}

	if (!bSuppressed && MuzzleLight != nullptr)
	{
		MuzzleLight->SetVisibility(true);
		MuzzleLight->SetIntensity(14000.f);
		MuzzleLight->SetAttenuationRadius(900.f);
	}

	// --- Shell ejection ---------------------------------------------------
	// Authored system first, generated fallback second. A suppressed weapon still
	// ejects brass; it just does not throw a flash.
	{
		UNiagaraSystem* System = (Definition != nullptr && Definition->ShellEject.IsValid())
			? Definition->ShellEject.LoadSynchronous()
			: CachedShellEject.Get();

		if (System != nullptr)
		{
			UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, System, GetShellEjectTransform().GetLocation(),
				GetShellEjectTransform().Rotator(), FVector(1.f), /*bAutoDestroy*/ true);
		}
	}

	// --- Audio ------------------------------------------------------------
	UOneBrahamAudioManagerComponent* AudioManager = nullptr;
	if (AOneBrahamPlayerController* PC = Cast<AOneBrahamPlayerController>(
		OwningCharacter.IsValid() ? OwningCharacter->GetController() : nullptr))
	{
		AudioManager = PC->GetAudioManager();
	}

	if (AudioManager != nullptr && Definition != nullptr)
	{
		AudioManager->PlayWeaponShot(Definition, MuzzleLocation, bSuppressed, OwningCharacter.Get());
	}
	else if (Definition != nullptr && FireAudioComponent != nullptr)
	{
		const TSoftObjectPtr<USoundBase>& SoundPtr = bSuppressed && Definition->SuppressedFireSound.IsValid()
			? Definition->SuppressedFireSound
			: Definition->FireSoundClose;

		if (SoundPtr.IsValid())
		{
			if (USoundBase* Sound = SoundPtr.LoadSynchronous())
			{
				FireAudioComponent->SetSound(Sound);
				FireAudioComponent->Play(FMath::FRandRange(0.97f, 1.04f));
			}
		}
	}

	// --- Tracer -----------------------------------------------------------
	// Tracers are subtle: a thin, brief streak, not a laser beam. Suppressed
	// weapons have none, which is exactly why a suppressor changes how a fight
	// plays rather than just how loud it is.
	if (Definition != nullptr && !bSuppressed && !Definition->bHideTracer)
	{
		const FVector End = MuzzleLocation + FVector(Direction) * Definition->Falloff.FullDamageRange * 1.5f;
		PlayTracerLocal(MuzzleLocation, End, bSuppressed);
	}
}

void AOneBrahamWeaponBase::MulticastPlayImpactFX_Implementation(const TArray<FOneBrahamImpactInfo>& Impacts)
{
	UOneBrahamVFXStatics::SpawnImpactsForHits(this, Impacts);
}

void AOneBrahamWeaponBase::MulticastPlayReloadFX_Implementation(bool bStarted)
{
	if (Definition == nullptr)
	{
		return;
	}

	if (USkeletalMeshComponent* Mesh = WeaponMesh)
	{
		if (UAnimInstance* AnimInstance = Mesh->GetAnimInstance())
		{
			if (Definition->ReloadMontage.IsValid())
			{
				if (UAnimMontage* Montage = Definition->ReloadMontage.LoadSynchronous())
				{
					if (bStarted)
					{
						AnimInstance->Montage_Play(Montage);
					}
					else
					{
						AnimInstance->Montage_Stop(0.2f, Montage);
					}
				}
			}
		}
	}

	// Reload sounds: magazine out, magazine in, bolt release. The audio manager
	// handles the three-stage sequence with correct timing.
	if (bStarted && Definition->ReloadSound.IsValid())
	{
		if (USoundBase* Sound = Definition->ReloadSound.LoadSynchronous())
		{
			UGameplayStatics::PlaySoundAtLocation(this, Sound, GetActorLocation(), 0.9f);
		}
	}
}

void AOneBrahamWeaponBase::MulticastPlayDryFire_Implementation()
{
	if (Definition == nullptr || !Definition->DryFireSound.IsValid())
	{
		return;
	}

	if (USoundBase* Sound = Definition->DryFireSound.LoadSynchronous())
	{
		UGameplayStatics::PlaySoundAtLocation(this, Sound, GetActorLocation(), 0.7f);
	}
}

void AOneBrahamWeaponBase::PlayTracerLocal(const FVector& Start, const FVector& End, bool bSuppressed)
{
	if (Definition == nullptr || Definition->bHideTracer || bSuppressed)
	{
		return;
	}

	// Authored tracer, else the generated one. Suppressed weapons returned above.
	UNiagaraSystem* System = Definition->Tracer.IsValid()
		? Definition->Tracer.LoadSynchronous()
		: CachedTracer.Get();

	if (System != nullptr)
	{
		UNiagaraComponent* TracerComponent = UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, System, Start,
			(End - Start).Rotation(), FVector(1.f), /*bAutoDestroy*/ true, /*bAutoActivate*/ true);

		if (TracerComponent != nullptr)
		{
			// The tracer system reads Start/End so the beam is drawn along the
			// actual shot path rather than being a fixed-length streak.
			TracerComponent->SetNiagaraVariableVec3(TEXT("User.Start"), Start);
			TracerComponent->SetNiagaraVariableVec3(TEXT("User.End"), End);
			TracerComponent->SetNiagaraVariableFloat(TEXT("User.Speed"), Definition->MuzzleVelocity);
			TracerComponent->SetNiagaraVariableLinearColor(TEXT("User.Tint"), FLinearColor(1.f, 0.85f, 0.55f, 1.f));
		}
	}
}

// ==========================================================================
//  State / replication callbacks
// ==========================================================================
void AOneBrahamWeaponBase::SetWeaponState(EOneBrahamWeaponState NewState)
{
	if (WeaponState == NewState)
	{
		return;
	}

	WeaponState = NewState;
	OnWeaponStateChanged.Broadcast(NewState);
	ForceNetUpdate();
}

void AOneBrahamWeaponBase::OnRep_State()
{
	OnWeaponStateChanged.Broadcast(WeaponState);

	// Remote clients need the reload montage too: the weapon component tells the
	// character's anim BP, which is replicated through the weapon state.
}

void AOneBrahamWeaponBase::OnRep_Ammo()
{
	if (OwningWeaponComponent.IsValid())
	{
		OwningWeaponComponent->OnWeaponAmmoChanged(AmmoInMagazine);
	}
}

void AOneBrahamWeaponBase::OnRep_Attachments()
{
	RebuildAttachmentMeshes();

	if (WeaponLight != nullptr)
	{
		WeaponLight->SetVisibility(bLaserEnabled || bFlashlightEnabled);
	}
}
