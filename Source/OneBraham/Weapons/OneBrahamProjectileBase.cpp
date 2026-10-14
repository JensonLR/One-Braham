// Copyright (c) One Braham. All Rights Reserved.

#include "Weapons/OneBrahamProjectileBase.h"

#include "OneBraham.h"
#include "Core/OneBrahamGameState.h"
#include "Core/OneBrahamPlayerController.h"
#include "Core/OneBrahamPlayerState.h"
#include "Characters/OneBrahamCharacter.h"
#include "Characters/OneBrahamHealthComponent.h"
#include "Access/OneBrahamBreachableInterface.h"
#include "Access/OneBrahamBreakableGlass.h"
#include "Audio/OneBrahamAudioManagerComponent.h"
#include "Data/OneBrahamMatchSettingsAsset.h"
#include "Voice/OneBrahamVoiceSubsystem.h"

#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/AudioComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "Engine/DamageEvents.h"
#include "Engine/OverlapResult.h"
#include "EngineUtils.h"
#include "DrawDebugHelpers.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "PhysicalMaterials/PhysicalMaterial.h"

AOneBrahamProjectileBase::AOneBrahamProjectileBase()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.05f;
	bReplicates = true;
	SetReplicateMovement(true);
	SetNetUpdateFrequency(30.f);

	CollisionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("Collision"));
	CollisionSphere->InitSphereRadius(7.f);
	CollisionSphere->SetCollisionProfileName(TEXT("OneBrahamPickup"));
	CollisionSphere->SetCollisionResponseToChannel(ECC_GameTraceChannel10 /*ProjectileBlocker*/, ECR_Block);
	CollisionSphere->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
	CollisionSphere->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
	CollisionSphere->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore); // grenades pass through people
	CollisionSphere->SetNotifyRigidBodyCollision(true);
	SetRootComponent(CollisionSphere);

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(CollisionSphere);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCastShadow(true);

	ProjectileMovement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("ProjectileMovement"));
	ProjectileMovement->SetUpdatedComponent(CollisionSphere);
	ProjectileMovement->bShouldBounce = true;
	ProjectileMovement->Bounciness = 0.32f;
	ProjectileMovement->Friction = 0.55f;
	ProjectileMovement->ProjectileGravityScale = 1.f;
	ProjectileMovement->bRotationFollowsVelocity = true;
	ProjectileMovement->bAutoActivate = false;
	ProjectileMovement->InitialSpeed = 0.f;
	ProjectileMovement->MaxSpeed = 4000.f;

	TrailFX = CreateDefaultSubobject<UNiagaraComponent>(TEXT("Trail"));
	TrailFX->SetupAttachment(CollisionSphere);
	TrailFX->SetAutoActivate(false);

	DetonationFX = CreateDefaultSubobject<UNiagaraComponent>(TEXT("Detonation"));
	DetonationFX->SetupAttachment(CollisionSphere);
	DetonationFX->SetAutoActivate(false);

	BeepAudio = CreateDefaultSubobject<UAudioComponent>(TEXT("Beep"));
	BeepAudio->SetupAttachment(CollisionSphere);
	BeepAudio->bAutoActivate = false;
	BeepAudio->bAllowSpatialization = true;
}

void AOneBrahamProjectileBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AOneBrahamProjectileBase, UtilityType);
	DOREPLIFETIME(AOneBrahamProjectileBase, FuseRemaining);
	DOREPLIFETIME(AOneBrahamProjectileBase, bDetonated);
	DOREPLIFETIME(AOneBrahamProjectileBase, bIsPlanted);
	DOREPLIFETIME(AOneBrahamProjectileBase, ThrowerName);
	DOREPLIFETIME(AOneBrahamProjectileBase, ThrowerPlayerId);
}

void AOneBrahamProjectileBase::BeginPlay()
{
	Super::BeginPlay();

	if (ProjectileMovement != nullptr)
	{
		ProjectileMovement->OnProjectileBounce.AddDynamic(this, &AOneBrahamProjectileBase::OnProjectileBounce);
		ProjectileMovement->OnProjectileStop.AddDynamic(this, &AOneBrahamProjectileBase::OnProjectileStop);
	}

	// The fuse ticks on the server only; clients interpolate from the replicated
	// FuseRemaining (which is what the beep interval is driven from).
	if (HasAuthority())
	{
		FuseRemaining = FuseSeconds;
		GetWorldTimerManager().SetTimer(FuseTimerHandle, this, &AOneBrahamProjectileBase::Detonate, FuseSeconds, false);
	}
}

void AOneBrahamProjectileBase::InitialiseUtility(FName InUtilityId, const FVector& InThrowDirection, AOneBrahamCharacter* InThrower)
{
	Thrower = InThrower;

	if (InThrower != nullptr)
	{
		ThrowerName = InThrower->GetPlayerState() ? InThrower->GetPlayerState()->GetPlayerName() : InThrower->GetName();
		ThrowerPlayerId = InThrower->GetPlayerState() ? InThrower->GetPlayerState()->GetPlayerId() : -1;
	}

	// --- Type + tuning ----------------------------------------------------
	if (InUtilityId == TEXT("Utility_Flashbang"))
	{
		UtilityType = EOneBrahamUtilityType::Flashbang;
		FuseSeconds = 1.8f;          // flashes must be nearly instant to be useful
		BlastRadius = 0.f;
	}
	else if (InUtilityId == TEXT("Utility_Smoke"))
	{
		UtilityType = EOneBrahamUtilityType::Smoke;
		FuseSeconds = 1.2f;
		BlastRadius = 0.f;
		ProjectileMovement->Bounciness = 0.15f; // smoke lands where it lands
	}
	else if (InUtilityId == TEXT("Utility_BreachCharge"))
	{
		UtilityType = EOneBrahamUtilityType::BreachCharge;
		FuseSeconds = 3.f;
		ProjectileMovement->bShouldBounce = false;
	}
	else
	{
		UtilityType = EOneBrahamUtilityType::Frag;
		FuseSeconds = 3.5f;

		// Match-settings override so a designer can retune frags per mode.
		if (const AOneBrahamGameState* GameState = GetWorld()->GetGameState<AOneBrahamGameState>())
		{
			if (const UOneBrahamMatchSettingsAsset* Settings = GameState->GetMatchSettings())
			{
				MaxDamage = FMath::Max(MaxDamage, Settings->BreachChargeDamage * 0.5f);
			}
		}
	}

	FuseRemaining = FuseSeconds;

	// The utility type is known from here on, and it is what decides which
	// detonation effect this actor plays. Resolved on every machine, because the
	// visual is client-side and the Blueprint may have assigned nothing.
	ResolveDetonationFX();

	// --- Server fuse ------------------------------------------------------
	if (HasAuthority())
	{
		GetWorldTimerManager().ClearTimer(FuseTimerHandle);
		GetWorldTimerManager().SetTimer(FuseTimerHandle, this, &AOneBrahamProjectileBase::Detonate, FuseSeconds, false);
	}
}

void AOneBrahamProjectileBase::LaunchWithVelocity(const FVector& Velocity)
{
	if (ProjectileMovement != nullptr)
	{
		ProjectileMovement->Velocity = Velocity;
		ProjectileMovement->Activate(true);
	}

	// Throw audio: the pin, the grunt, the clatter. Handled on every client.
	if (TrailFX != nullptr)
	{
		TrailFX->Activate(true);
	}
}

void AOneBrahamProjectileBase::ServerPlantAsBreachCharge(AActor* HostActor, const FVector& SurfacePoint, const FVector& SurfaceNormal)
{
	if (!HasAuthority())
	{
		return;
	}

	bIsPlanted = true;
	ProjectileMovement->StopMovementImmediately();
	ProjectileMovement->Deactivate();
	SetActorLocation(SurfacePoint + SurfaceNormal * 3.f);

	// Attach to the surface so a moving lift door carries the charge with it.
	if (HostActor != nullptr && HostActor->GetRootComponent() != nullptr)
	{
		AttachToComponent(HostActor->GetRootComponent(), FAttachmentTransformRules::KeepWorldTransform);
	}

	// Restart the fuse from the plant.
	GetWorldTimerManager().ClearTimer(FuseTimerHandle);
	GetWorldTimerManager().SetTimer(FuseTimerHandle, this, &AOneBrahamProjectileBase::Detonate, FuseSeconds, false);

	OB_WEAPON_LOG("Breach charge planted on '%s' by '%s' (%.1fs fuse).",
		HostActor ? *HostActor->GetName() : TEXT("world"), *ThrowerName, FuseSeconds);
}

void AOneBrahamProjectileBase::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Beep interval accelerates as the fuse runs down: the single best audio cue
	// in the game, and it is generated procedurally rather than authored.
	if (!bDetonated && UtilityType == EOneBrahamUtilityType::Frag && BeepAudio != nullptr)
	{
		const float Alpha = FuseSeconds > 0.f ? FMath::Clamp(FuseRemaining / FuseSeconds, 0.f, 1.f) : 0.f;
		const float Interval = FMath::Lerp(0.12f, 0.85f, Alpha);

		if (GetWorld()->GetTimeSeconds() - LastBeepTime >= Interval)
		{
			LastBeepTime = GetWorld()->GetTimeSeconds();

			if (AOneBrahamPlayerController* LocalPC = GetWorld()->GetFirstPlayerController<AOneBrahamPlayerController>())
			{
				if (UOneBrahamAudioManagerComponent* Audio = LocalPC->GetAudioManager())
				{
					// 2D beep if it is close to the local player (you can hear your own
					// fuse in your head), 3D otherwise. This is a genuinely important
					// gameplay affordance, not just flavour.
					const float DistanceToLocal = LocalPC->GetPawn() != nullptr
						? FVector::Dist(LocalPC->GetPawn()->GetActorLocation(), GetActorLocation())
						: 99999.f;

					Audio->PlayFuseBeep(GetActorLocation(), DistanceToLocal < 300.f);
				}
			}
		}
	}
}

void AOneBrahamProjectileBase::OnProjectileBounce(const FHitResult& ImpactResult, const FVector& ImpactVelocity)
{
	++BounceCount;

	// Bounce audio/vfx: the surface matters (a grenade bouncing off a metal lift
	// door sounds completely different from one bouncing off carpet). The manager
	// classifies the surface itself from the impact point so this stays cheap.
	if (AOneBrahamPlayerController* LocalPC = GetWorld()->GetFirstPlayerController<AOneBrahamPlayerController>())
	{
		if (UOneBrahamAudioManagerComponent* Audio = LocalPC->GetAudioManager())
		{
			Audio->PlayUtilityBounce(GetActorLocation());
		}
	}
	if (HasAuthority() && BounceCount >= 3 && UtilityType == EOneBrahamUtilityType::Frag)
	{
		// Real grenades do not shorten their fuse, but three bounces means the
		// throw was bad enough that the player has earned a faster resolution.
		const float NewFuse = FMath::Min(FuseRemaining, 1.2f);
		if (NewFuse < FuseRemaining)
		{
			FuseRemaining = NewFuse;
			GetWorldTimerManager().ClearTimer(FuseTimerHandle);
			GetWorldTimerManager().SetTimer(FuseTimerHandle, this, &AOneBrahamProjectileBase::Detonate, FuseRemaining, false);
		}
	}

	if (AActor* HitActor = ImpactResult.GetActor())
	{
		if (AOneBrahamCharacter* Character = Cast<AOneBrahamCharacter>(HitActor))
		{
			// Grenades do not damage on impact - the design forbids "cooking" and
			// ricochet kills because they are unreadable in a tactical shooter.
			(void)Character;
		}
	}
}

EOneBrahamVFXEffect AOneBrahamProjectileBase::GetDetonationEffect() const
{
	switch (UtilityType)
	{
	case EOneBrahamUtilityType::Flashbang:		return EOneBrahamVFXEffect::FlashbangBurst;
	case EOneBrahamUtilityType::Smoke:			return EOneBrahamVFXEffect::SmokeCloud;
	case EOneBrahamUtilityType::BreachCharge:	return EOneBrahamVFXEffect::BreachExplosion;
	case EOneBrahamUtilityType::Frag:
	default:									return EOneBrahamVFXEffect::FragExplosion;
	}
}

void AOneBrahamProjectileBase::ResolveDetonationFX()
{
	if (DetonationFX == nullptr)
	{
		return;
	}

	// Already pointed somewhere by the Blueprint: leave the artist's work alone.
	if (DetonationFX->GetAsset() != nullptr)
	{
		return;
	}

#if WITH_EDITOR
	// Reached the fallback, which in the editor means somebody opened this
	// Blueprint and did not pick a system. Loud once, then it just works.
	UE_LOG(LogOneBraham, Warning,
		TEXT("[OB.VFX] '%s' has no detonation FX assigned; using the generated "
			 "NS_OB_* fallback. Assign the authored system in the Blueprint to remove this."),
		*GetClass()->GetName());
#endif

	if (UNiagaraSystem* Fallback = UOneBrahamVFXStatics::LoadEffectSystem(GetDetonationEffect()))
	{
		DetonationFX->SetAsset(Fallback);
	}
}

void AOneBrahamProjectileBase::MulticastDetonationFeedback_Implementation(FVector_NetQuantize DetonationLocation)
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	const float Magnitude = UtilityType == EOneBrahamUtilityType::BreachCharge ? 1.35f
		: (UtilityType == EOneBrahamUtilityType::Frag ? 1.f : 0.45f);

	// Camera shake for everyone who can feel it, not just for whoever is hosting.
	// This used to be `GetFirstPlayerController` -> `ApplyExplosionShakeLocally`,
	// which meant a player who threw a grenade on a dedicated server felt it and
	// nobody else did. A Client RPC on a locally controlled controller runs on
	// that machine immediately, so the host is covered by the same loop.
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		if (AOneBrahamPlayerController* PC = Cast<AOneBrahamPlayerController>(It->Get()))
		{
			PC->ClientApplyExplosionShake(DetonationLocation, Magnitude);
		}
	}

	// The two vision-breaking utilities are spawned here, and not by the Niagara
	// component hanging off the grenade, because this is the one function that
	// runs on every machine *with the parameters*: a client that activates the
	// smoke system by itself draws it at the system's default radius, which is a
	// smoke grenade the size of a bin bag, and the flash is scaled by its own
	// radius. See Docs/11 sections 3.9 and 3.10 - this is the spawner those rows
	// name.
	switch (UtilityType)
	{
	case EOneBrahamUtilityType::Flashbang:
		UOneBrahamVFXStatics::SpawnFlashbang(this, DetonationLocation, FlashRadius);
		break;

	case EOneBrahamUtilityType::Smoke:
		UOneBrahamVFXStatics::SpawnSmokeCloud(this, DetonationLocation, SmokeRadius, SmokeDuration);
		break;

	default:
		// Frag and breach: the burst is the projectile's own detonation system,
		// activated on every machine by `OnRep_Detonated` with the authored
		// Blueprint able to override it (`Docs/11` section 6).
		break;
	}
}

void AOneBrahamProjectileBase::OnProjectileStop(const FHitResult& ImpactResult)
{
	// Keep the fuse running: a stopped grenade is still a live grenade.
}

void AOneBrahamProjectileBase::Detonate()
{
	if (!HasAuthority() || bDetonated)
	{
		return;
	}

	bDetonated = true;
	OnRep_Detonated();

	switch (UtilityType)
	{
	case EOneBrahamUtilityType::Frag:			ApplyFragDamage(); break;
	case EOneBrahamUtilityType::Flashbang:		ApplyFlashEffect(); break;
	case EOneBrahamUtilityType::Smoke:			ApplySmokeEffect(); break;
	case EOneBrahamUtilityType::BreachCharge:	ApplyBreachDamage(); break;
	default:									ApplyFragDamage(); break;
	}

	OnDetonated.Broadcast(this);

	MulticastDetonationFeedback(GetActorLocation());

	// "Go on then." - the thrower gets a dry line 40% of the time when a frag
	// actually hurts someone. Never after a flash: gloating over a blind player
	// reads as a taunt emote, which the tone brief forbids.
	if (UtilityType == EOneBrahamUtilityType::Frag && ThrowerPlayerId >= 0 && FMath::FRand() < 0.4f)
	{
		if (AGameStateBase* GS = GetWorld()->GetGameState())
		{
			for (APlayerState* PS : GS->PlayerArray)
			{
				if (AOneBrahamPlayerState* Ours = Cast<AOneBrahamPlayerState>(PS))
				{
					if (Ours->GetPlayerId() == ThrowerPlayerId)
					{
						if (UOneBrahamVoiceSubsystem* Voice = UOneBrahamVoiceSubsystem::Get(Ours))
						{
							Voice->SpeakContextualLine(Ours, TEXT("V_FRAG_HIT"), 0.35f);
						}
						break;
					}
				}
			}
		}
	}

	// The actor lingers for the effect duration (smoke especially) then cleans up.
	const float LifeAfterDetonation = UtilityType == EOneBrahamUtilityType::Smoke ? SmokeDuration + 2.f : 1.5f;
	SetLifeSpan(LifeAfterDetonation);

	if (ProjectileMovement != nullptr)
	{
		ProjectileMovement->StopMovementImmediately();
		ProjectileMovement->Deactivate();
	}
}

void AOneBrahamProjectileBase::ApplyFragDamage()
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	const FVector Origin = GetActorLocation();

	// --- Line of sight + falloff ------------------------------------------
	FCollisionQueryParams Params(SCENE_QUERY_STAT(OBFragLOS), false, this);
	Params.AddIgnoredActor(this);
	if (Thrower.IsValid())
	{
		Params.AddIgnoredActor(Thrower.Get());
	}

	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		AOneBrahamPlayerController* PC = Cast<AOneBrahamPlayerController>(It->Get());
		if (PC == nullptr)
		{
			continue;
		}

		AOneBrahamCharacter* Character = Cast<AOneBrahamCharacter>(PC->GetPawn());
		if (Character == nullptr)
		{
			continue;
		}

		UOneBrahamHealthComponent* Health = Character->GetHealthComponent();
		if (Health == nullptr || !Health->IsAlive())
		{
			continue;
		}

		const FVector ToTarget = Health->GetOwner()->GetActorLocation() - Origin;
		const float Distance = ToTarget.Size();
		if (Distance > BlastRadius)
		{
			continue;
		}

		// Wall check: frags do not damage through a fire shutter or a lift shaft
		// wall. This is the difference between a frag being a tool and a frag
		// being an area-denial cheat.
		FHitResult OcclusionHit;
		const bool bOccluded = World->LineTraceSingleByChannel(OcclusionHit, Origin, Health->GetOwner()->GetActorLocation(),
			ECC_Visibility, Params);

		if (bOccluded && OcclusionHit.GetActor() != Character)
		{
			// Half damage through a thin penetrable surface, none through solid
			// architecture: consistent with the bullet penetration rules.
			const bool bPenetrable = OcclusionHit.GetActor() != nullptr &&
				OcclusionHit.GetActor()->GetRootComponent() != nullptr &&
				OcclusionHit.GetActor()->GetRootComponent()->GetCollisionObjectType() == ECC_GameTraceChannel4;

			if (!bPenetrable)
			{
				continue;
			}
		}

		// Falloff: full damage in the middle third, dropping to MinDamage at the edge.
		const float Alpha = FMath::Clamp(Distance / BlastRadius, 0.f, 1.f);
		const float Damage = FMath::Lerp(MaxDamage, MinDamage, Alpha * Alpha);

		FOneBrahamDamageReport Report;
		Report.BaseDamage = Damage;
		Report.FinalDamage = Damage;
		Report.HitZone = EOneBrahamHitZone::Torso;
		Report.HitLocation = Health->GetOwner()->GetActorLocation();
		Report.ShotDirection = (Report.HitLocation - Origin).GetSafeNormal();
		Report.Distance = Distance;
		Report.bLethal = Damage >= Health->GetHealth();

		Health->ApplyDamage(Damage, Thrower.Get(), Report, Thrower.IsValid() ? Thrower->GetController() : nullptr);
	}

	// --- Physical push ----------------------------------------------------
	// Grenades shove desks, trays, mugs and chairs around. This is a huge part of
	// how the game feels, and it is Chaos doing it, not bespoke code.
	for (TActorIterator<AActor> ActorIt(World); ActorIt; ++ActorIt)
	{
		AActor* Actor = *ActorIt;
		if (Actor == nullptr || Actor == this || Actor->IsA(AOneBrahamCharacter::StaticClass()))
		{
			continue;
		}

		UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Actor->GetRootComponent());
		if (Primitive == nullptr || !Primitive->IsSimulatingPhysics())
		{
			continue;
		}

		const FVector ToActor = Actor->GetActorLocation() - Origin;
		const float Distance = ToActor.Size();
		const float PushRadius = BlastRadius * 1.4f;

		if (Distance > PushRadius || Distance < KINDA_SMALL_NUMBER)
		{
			continue;
		}

		const float Force = FMath::Lerp(2800.f, 300.f, Distance / PushRadius);
		Primitive->AddRadialImpulse(Origin, PushRadius, Force, ERadialImpulseFalloff::RIF_Linear, /*bVelChange*/ true);
	}
	ApplyBlastToGlass(GetActorLocation(), BlastRadius);
}

void AOneBrahamProjectileBase::ApplyFlashEffect()
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	const FVector Origin = GetActorLocation();

	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		AOneBrahamPlayerController* PC = Cast<AOneBrahamPlayerController>(It->Get());
		if (PC == nullptr || !PC->IsLocalController())
		{
			continue;
		}

		// The flash is applied on the AFFECTED CLIENT so the blinding is instant and
		// cannot be desynced; the server decides who is affected and for how long
		// via a multicast.
		continue;
	}

	// Server: decide who is flashed, multicast the duration to those clients.
	TArray<AOneBrahamPlayerController*> Affected;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(OBFlashLOS), false, this);
	Params.AddIgnoredActor(this);

	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		AOneBrahamPlayerController* PC = Cast<AOneBrahamPlayerController>(It->Get());
		AOneBrahamCharacter* Character = PC != nullptr ? Cast<AOneBrahamCharacter>(PC->GetPawn()) : nullptr;
		const UOneBrahamHealthComponent* Health = Character != nullptr ? Character->GetHealthComponent() : nullptr;

		if (Character == nullptr || Health == nullptr || !Health->IsAlive())
		{
			continue;
		}

		const FVector EyeLocation = Character->GetEyeLocation();
		const float Distance = FVector::Dist(EyeLocation, Origin);
		if (Distance > FlashRadius)
		{
			continue;
		}

		// Looking away reduces the effect dramatically - this is the skill in
		// dealing with flashes, and it must be rewarded.
		const FVector ToFlash = (Origin - EyeLocation).GetSafeNormal();
		const float Facing = FVector::DotProduct(Character->GetEyeRotation().Vector(), ToFlash);
		if (Facing < -0.25f)
		{
			continue; // fully turned away: no effect at all
		}

		// Line of sight: a flash through a fire shutter does not blind you.
		FHitResult Hit;
		if (World->LineTraceSingleByChannel(Hit, EyeLocation, Origin, ECC_Visibility, Params))
		{
			if (Hit.GetActor() != this && Hit.GetActor() != Character)
			{
				continue;
			}
		}

		const float DistanceAlpha = 1.f - FMath::Clamp(Distance / FlashRadius, 0.f, 1.f);
		const float FacingAlpha = FMath::Clamp(Facing, 0.f, 1.f);
		const float Duration = MaxFlashDuration * DistanceAlpha * FacingAlpha;

		if (Duration > 0.15f)
		{
			PC->ClientApplyFlash(Duration, Facing > 0.85f);
		}
	}
}

void AOneBrahamProjectileBase::ApplySmokeEffect()
{
	// Server side of a smoke grenade, and deliberately almost empty. The cloud
	// itself is spawned on every machine by `MulticastDetonationFeedback`, which
	// is where the radius and the lifetime are known; the actor's own lifespan is
	// set in `Detonate`. Smoke does NOT block bullets (a design decision:
	// bullet-blocking smoke makes every fight a coin flip); it blocks vision,
	// which is what a smoke grenade is actually for.
	//
	// Collision: players can walk through smoke and bullets pass through it, so
	// the only physics work left is the haze volume that tints the Lumen scene so
	// the smoke darkens the room it is in. That is a level-authoring job on the
	// cloud's own system, not something the throw can do after the fact.
}

void AOneBrahamProjectileBase::ApplyBlastToGlass(const FVector& Origin, float RadiusCm)
{
	UWorld* World = GetWorld();
	if (World == nullptr || RadiusCm <= 0.f)
	{
		return;
	}

	// There are eight panes in the building and no registry of them, so this
	// walks the class list. That is the right cost: a detonation is once per
	// grenade, the list is short, and an index would be one more thing to keep in
	// step with the map builder.
	TArray<AActor*> Panes;
	UGameplayStatics::GetAllActorsOfClass(World, AOneBrahamBreakableGlass::StaticClass(), Panes);

	FCollisionQueryParams Params(SCENE_QUERY_STAT(OBGlassBlast), false, this);

	for (AActor* Actor : Panes)
	{
		AOneBrahamBreakableGlass* Pane = Cast<AOneBrahamBreakableGlass>(Actor);
		if (Pane == nullptr || Pane->IsBroken())
		{
			continue;
		}

		const FVector Centre = Pane->GetPaneCentre();
		const float Distance = FVector::Dist(Centre, Origin);
		if (Distance > RadiusCm)
		{
			continue;
		}

		// Line of sight, so a charge in one room does not blow out the glazing of
		// the room next to it. The trace is allowed to end on the pane itself -
		// that is the hit this is looking for - and on nothing else.
		FHitResult Hit;
		if (World->LineTraceSingleByChannel(Hit, Origin, Centre, ECollisionChannel::ECC_Visibility, Params) &&
			Hit.GetActor() != Pane)
		{
			continue;
		}

		// Falloff, then the pane's own threshold decides. A frag that lands on the
		// carpet three metres away still takes the window out; one that goes off in
		// the far corner of the floor does not.
		const float Alpha = 1.f - FMath::Clamp(Distance / RadiusCm, 0.f, 1.f);
		Pane->TakeDamage(RadiusCm * 1.2f * Alpha, FDamageEvent(), Thrower.IsValid() ? Thrower->GetController() : nullptr, this);
	}
}

void AOneBrahamProjectileBase::ApplyBreachDamage()
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	const FVector Origin = GetActorLocation();

	// 1. Damage any breachable actor in range (doors, shutters, weak walls).
	TArray<AActor*> Overlapping;
	CollisionSphere->GetOverlappingActors(Overlapping);

	for (AActor* Actor : Overlapping)
	{
		if (Actor == nullptr)
		{
			continue;
		}

		if (IOneBrahamBreachableInterface* Breachable = Cast<IOneBrahamBreachableInterface>(Actor))
		{
			Breachable->ApplyBreachDamage(BreachDamage, Thrower.Get(), Origin);
		}
	}

	// 2. Also look slightly further out: a charge stuck to a doorframe should
	//    still blow the door.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(OBBreachQuery), false, this);
	TArray<FOverlapResult> Overlaps;
	World->OverlapMultiByChannel(Overlaps, Origin, FQuat::Identity, ECC_GameTraceChannel3 /*Interactable*/,
		FCollisionShape::MakeSphere(BreachRadius), Params);

	for (const FOverlapResult& Overlap : Overlaps)
	{
		AActor* Actor = Overlap.GetActor();
		if (Actor == nullptr)
		{
			continue;
		}

		if (IOneBrahamBreachableInterface* Breachable = Cast<IOneBrahamBreachableInterface>(Actor))
		{
			Breachable->ApplyBreachDamage(BreachDamage * 0.75f, Thrower.Get(), Origin);
		}
	}

	// 3. Anyone standing on the other side of that door takes a hit.
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		AOneBrahamPlayerController* PC = Cast<AOneBrahamPlayerController>(It->Get());
		AOneBrahamCharacter* Character = PC != nullptr ? Cast<AOneBrahamCharacter>(PC->GetPawn()) : nullptr;
		UOneBrahamHealthComponent* Health = Character != nullptr ? Character->GetHealthComponent() : nullptr;

		if (Health == nullptr || !Health->IsAlive())
		{
			continue;
		}

		const float Distance = FVector::Dist(Health->GetOwner()->GetActorLocation(), Origin);
		if (Distance > BreachRadius * 1.5f)
		{
			continue;
		}

		const float Damage = FMath::Lerp(70.f, 15.f, FMath::Clamp(Distance / (BreachRadius * 1.5f), 0.f, 1.f));

		FOneBrahamDamageReport Report;
		Report.BaseDamage = Damage;
		Report.FinalDamage = Damage;
		Report.HitZone = EOneBrahamHitZone::Torso;
		Report.HitLocation = Health->GetOwner()->GetActorLocation();
		Report.ShotDirection = (Report.HitLocation - Origin).GetSafeNormal();

		Health->ApplyDamage(Damage, Thrower.Get(), Report, Thrower.IsValid() ? Thrower->GetController() : nullptr);
	}
	ApplyBlastToGlass(GetActorLocation(), FMath::Max(BreachRadius, BlastRadius));
}

void AOneBrahamProjectileBase::OnRep_Detonated()
{
	// The component owns the burst for the two explosives only. The flash and the
	// smoke are spawned by `MulticastDetonationFeedback` instead: this component
	// rides on the grenade, and the grenade is not always where the cloud should
	// be - a smoke that lands on a desk would otherwise draw inside the desk.
	const bool bComponentOwnsBurst = UtilityType == EOneBrahamUtilityType::Frag
		|| UtilityType == EOneBrahamUtilityType::BreachCharge;

	if (bDetonated && DetonationFX != nullptr && bComponentOwnsBurst)
	{
		DetonationFX->Activate(true);
	}

	// Smoke keeps its cloud alive after detonation; the FX component handles that.
	if (bDetonated && UtilityType == EOneBrahamUtilityType::Smoke)
	{
		SetLifeSpan(SmokeDuration + 2.f);
	}
}

void AOneBrahamProjectileBase::OnRep_FuseRemaining()
{
	// Clients drive their beep from the replicated fuse value; the server's value
	// is authoritative so a modified client cannot silence the beep (it only
	// silences it locally, which hurts nobody but themselves).
}

void AOneBrahamProjectileBase::ClearTimers()
{
	GetWorldTimerManager().ClearTimer(FuseTimerHandle);
	GetWorldTimerManager().ClearTimer(CleanupTimerHandle);
}
