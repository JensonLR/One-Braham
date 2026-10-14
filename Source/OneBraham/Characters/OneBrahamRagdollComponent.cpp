// Copyright (c) One Braham. All Rights Reserved.

#include "Characters/OneBrahamRagdollComponent.h"

#include "OneBraham.h"
#include "Core/OneBrahamGameMode.h"
#include "Characters/OneBrahamCharacter.h"
#include "Characters/OneBrahamHealthComponent.h"
#include "Characters/OneBrahamDownedComponent.h"
#include "Weapons/OneBrahamInventoryComponent.h"
#include "Weapons/OneBrahamWeaponComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

UOneBrahamRagdollComponent::UOneBrahamRagdollComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.5f; // lifecycle bookkeeping only
	SetIsReplicatedByDefault(true);
}

void UOneBrahamRagdollComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UOneBrahamRagdollComponent, bRagdolling);
	DOREPLIFETIME(UOneBrahamRagdollComponent, DeathPose);
	DOREPLIFETIME(UOneBrahamRagdollComponent, RagdollImpulse);
	DOREPLIFETIME(UOneBrahamRagdollComponent, KillingBone);
}

void UOneBrahamRagdollComponent::BeginPlay()
{
	Super::BeginPlay();

	CachedCharacter = Cast<AOneBrahamCharacter>(GetOwner());
}

USkeletalMeshComponent* UOneBrahamRagdollComponent::GetTargetMesh() const
{
	const AOneBrahamCharacter* Character = CachedCharacter.Get();
	return Character != nullptr ? Character->GetMesh() : nullptr;
}

// ==========================================================================
//  Ragdoll in
// ==========================================================================
void UOneBrahamRagdollComponent::EnterRagdoll(FName HitBone, const FVector& ShotDir, float Damage)
{
	AOneBrahamCharacter* Character = CachedCharacter.Get();
	if (Character == nullptr || !Character->HasAuthority() || bRagdolling)
	{
		return;
	}

	USkeletalMeshComponent* Mesh = GetTargetMesh();
	if (Mesh == nullptr)
	{
		return;
	}

	// --- Impulse ----------------------------------------------------------
	// Clamped so a body never launches across the lobby: the humour in ONE
	// BRAHAM comes from realism, and a body that rockets 20 m is a different game.
	FVector ImpulseDirection = ShotDir.GetSafeNormal();
	if (ImpulseDirection.IsNearlyZero())
	{
		ImpulseDirection = Character->GetActorForwardVector() * -1.f;
	}
	ImpulseDirection.Z += UpwardBias;
	ImpulseDirection.Normalize();

	const float ImpulseMagnitude = FMath::Clamp(Damage * ImpulsePerDamage, 0.f, MaxImpulse);
	RagdollImpulse = ImpulseDirection * ImpulseMagnitude;
	KillingBone = HitBone;

	// --- Pose -------------------------------------------------------------
	DeathPose = DetermineDeathPose(ImpulseDirection);

	// --- Collision --------------------------------------------------------
	SwapToCorpseCollision();

	// --- Client-side predict the ragdoll for responsiveness ----------------
	bRagdolling = true;
	OnRep_Ragdolling();

	// Arm the landing detection: a corpse that lands in silence reads as a bug.
	bCorpseFalling = true;
	LastCorpseFallSpeed = 0.f;
	CorpseAirTime = 0.f;

	// --- Server also ragdolls so physics matches --------------------------
	Mesh->SetAllBodiesSimulatePhysics(true);
	Mesh->SetSimulatePhysics(true);
	Mesh->WakeAllRigidBodies();
	Mesh->bBlendPhysics = true;
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Mesh->SetCollisionProfileName(TEXT("OneBrahamRagdoll"));

	// Add the impulse at the bone that was hit: a shot to the shoulder spins the
	// body, a shot to the pelvis drops it.
	Mesh->AddImpulse(RagdollImpulse, HitBone != NAME_None ? HitBone : NAME_None, /*bVelocityChange*/ true);

	// --- Execution pose ---------------------------------------------------
	if (DeathPose == EOneBrahamDeathPose::Executed)
	{
		// Knees first: a short collapse before the physics takes over. Implemented
		// by damping for the first half-second so the body folds rather than flops.
		Mesh->SetLinearDamping(4.f);
		FTimerHandle UnfoldHandle;
		Character->GetWorldTimerManager().SetTimer(UnfoldHandle, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			if (USkeletalMeshComponent* RagdollMesh = GetTargetMesh())
			{
				RagdollMesh->SetLinearDamping(0.05f);
			}
		}), 0.45f, false);
	}

	CorpseSpawnTime = Character->GetWorld()->GetTimeSeconds();
	OnCorpseReady.Broadcast(Character);

	OB_LOG("Corpse: '%s' ragdolled on bone '%s' (impulse %.0f, pose %s).",
		*Character->GetName(), *HitBone.ToString(), ImpulseMagnitude, *UEnum::GetValueAsString(DeathPose));

	// Cleanup timer: bodies must not accumulate. In a 12-player ASCENT every
	// death produces a corpse and a bag, and corpses never despawn naturally.
	if (CorpseLifetimeSeconds > 0.f)
	{
		FTimerHandle CleanupHandle;
		Character->GetWorldTimerManager().SetTimer(CleanupHandle, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			BeginFadeOut();
		}), CorpseLifetimeSeconds, false);
	}

	Character->ForceNetUpdate();
}

void UOneBrahamRagdollComponent::OnRep_Ragdolling()
{
	USkeletalMeshComponent* Mesh = GetTargetMesh();
	AOneBrahamCharacter* Character = CachedCharacter.Get();
	if (Mesh == nullptr || Character == nullptr)
	{
		return;
	}

	if (bRagdolling)
	{
		// Owner's own body is hidden in first person; the ragdoll only needs to
		// exist for everyone else, so we skip it entirely on the owning client.
		if (Character->IsLocallyControlled() && Character->GetController() != nullptr)
		{
			Mesh->SetVisibility(false);
		}

		Mesh->SetSimulatePhysics(true);
		Mesh->SetAllBodiesSimulatePhysics(true);
		Mesh->WakeAllRigidBodies();
		Mesh->bBlendPhysics = true;
		Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Mesh->SetCollisionProfileName(TEXT("OneBrahamRagdoll"));

		// Clients replay the same impulse so the body lands where the server's
		// authority says it did (within Chaos determinism limits).
		if (!RagdollImpulse.IsNearlyZero())
		{
			Mesh->AddImpulse(RagdollImpulse, KillingBone, /*bVelocityChange*/ true);
		}
	}
	else
	{
		Mesh->SetSimulatePhysics(false);
		Mesh->SetAllBodiesSimulatePhysics(false);
		Mesh->bBlendPhysics = false;
		Mesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Mesh->SetCollisionProfileName(TEXT("OneBrahamHitbox"));
		Mesh->SetRelativeLocation(FVector(0.f, 0.f, -89.f));
		Mesh->SetRelativeRotation(FRotator(0.f, -90.f, 0.f));

		if (Character->IsLocallyControlled())
		{
			Mesh->SetVisibility(false);
		}
		else
		{
			Mesh->SetVisibility(true);
		}
	}
}

// ==========================================================================
//  Ragdoll out
// ==========================================================================
void UOneBrahamRagdollComponent::ExitRagdoll()
{
	if (!bRagdolling)
	{
		return;
	}

	bRagdolling = false;
	bFadingOut = false;
	FadeStartTime = -1.f;

	OnRep_Ragdolling();
	RestoreLivingCollision();

	if (AOneBrahamCharacter* Character = CachedCharacter.Get())
	{
		Character->ForceNetUpdate();
	}
}

void UOneBrahamRagdollComponent::DestroyCorpseNow()
{
	AOneBrahamCharacter* Character = CachedCharacter.Get();
	if (Character == nullptr)
	{
		return;
	}

	if (USkeletalMeshComponent* Mesh = GetTargetMesh())
	{
		Mesh->SetSimulatePhysics(false);
		Mesh->SetVisibility(false, true);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	// The pawn itself is destroyed by the GameMode's pawn cleanup pass; destroy
	// the component's visual state here so nothing lingers for a frame.
	CorpseSpawnTime = -1.f;
}

// ==========================================================================
//  Collision swapping
// ==========================================================================
void UOneBrahamRagdollComponent::SwapToCorpseCollision()
{
	AOneBrahamCharacter* Character = CachedCharacter.Get();
	if (Character == nullptr)
	{
		return;
	}

	// The capsule must stop blocking: a corpse blocking a doorway is a trap for
	// the living, and ONE BRAHAM's chokepoints (lift lobbies, stairwell doors)
	// would become permanently unusable.
	if (UCapsuleComponent* Capsule = Character->GetCapsuleComponent())
	{
		Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	if (USkeletalMeshComponent* Mesh = GetTargetMesh())
	{
		Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Mesh->SetCollisionProfileName(TEXT("OneBrahamRagdoll"));
		Mesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	}
}

void UOneBrahamRagdollComponent::RestoreLivingCollision()
{
	AOneBrahamCharacter* Character = CachedCharacter.Get();
	if (Character == nullptr)
	{
		return;
	}

	if (UCapsuleComponent* Capsule = Character->GetCapsuleComponent())
	{
		Capsule->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Capsule->SetCollisionProfileName(TEXT("OneBrahamCharacter"));
	}

	if (USkeletalMeshComponent* Mesh = GetTargetMesh())
	{
		Mesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Mesh->SetCollisionProfileName(TEXT("OneBrahamHitbox"));
	}
}

// ==========================================================================
//  Poses and hit reactions
// ==========================================================================
EOneBrahamDeathPose UOneBrahamRagdollComponent::DetermineDeathPose(const FVector& ImpulseDirection) const
{
	const AOneBrahamCharacter* Character = CachedCharacter.Get();
	if (Character == nullptr)
	{
		return EOneBrahamDeathPose::Physics;
	}

	UWorld* World = Character->GetWorld();
	if (World == nullptr)
	{
		return EOneBrahamDeathPose::Physics;
	}

	// A wall right behind the body produces a slump. This is worth checking
	// because a slumped body reads as "this person died at their desk", which is
	// exactly the tone ONE BRAHAM is going for.
	const FVector Back = -ImpulseDirection.GetSafeNormal2D();
	const FVector Start = Character->GetActorLocation();
	const FVector End = Start + Back * 90.f;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(OBDeathPose), false, Character);
	FHitResult Hit;
	if (World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
	{
		return EOneBrahamDeathPose::Slumped;
	}

	// Falling deaths: no impulse, all bones relaxed, arrives flat.
	if (ImpulseDirection.Z < -0.7f)
	{
		return EOneBrahamDeathPose::Fallen;
	}

	return EOneBrahamDeathPose::Physics;
}

void UOneBrahamRagdollComponent::ApplyHitReaction(FName HitBone, const FVector& ShotDir, float Damage)
{
	// Non-lethal hit reactions use a physics blend on the hit bone only: the arm
	// moves, the player keeps control. This is what makes gunfights read without
	// shipping a hundred bespoke flinch animations.
	USkeletalMeshComponent* Mesh = GetTargetMesh();
	if (Mesh == nullptr || bRagdolling)
	{
		return;
	}

	if (HitBone == NAME_None)
	{
		return;
	}

	const FVector ReactionDirection = ShotDir.GetSafeNormal();
	const float Magnitude = FMath::Clamp(Damage * 40.f, 0.f, 12000.f);

	// Blend weight scales with damage: a pistol round barely moves a shoulder, a
	// 5.56 round swings the whole arm.
	const float BlendWeight = FMath::Clamp(Damage / 45.f, 0.1f, 0.9f);
	Mesh->SetAllBodiesBelowPhysicsBlendWeight(HitBone, BlendWeight, true, true);
	Mesh->AddImpulseToAllBodiesBelow(ReactionDirection * Magnitude, HitBone, true, true);

	// Blend back out so the pose recovers into the animation.
	if (UWorld* World = GetWorld())
	{
		FTimerHandle RestoreHandle;
		World->GetTimerManager().SetTimer(RestoreHandle, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			if (USkeletalMeshComponent* RestoreMesh = GetTargetMesh())
			{
				RestoreMesh->SetAllBodiesPhysicsBlendWeight(0.f, true);
			}
		}), 0.45f, false);
	}
}

// ==========================================================================
//  Fade-out / cleanup
// ==========================================================================
void UOneBrahamRagdollComponent::BeginFadeOut()
{
	if (bFadingOut)
	{
		return;
	}

	bFadingOut = true;
	FadeStartTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;

	// Bodies fade rather than pop: a corpse vanishing in front of a player breaks
	// the tension immediately.
	if (USkeletalMeshComponent* Mesh = GetTargetMesh())
	{
		Mesh->SetRenderCustomDepth(true);
		Mesh->SetCustomDepthStencilValue(0);
	}
}

void UOneBrahamRagdollComponent::TickCorpseLanding(float DeltaTime)
{
	USkeletalMeshComponent* Mesh = GetTargetMesh();
	if (Mesh == nullptr || !bCorpseFalling)
	{
		return;
	}

	CorpseAirTime += DeltaTime;

	// The pelvis is the body's centre of mass; a head or a hand can be resting on
	// a desk while the body is still travelling.
	const float FallSpeed = -Mesh->GetPhysicsLinearVelocity(TEXT("pelvis")).Z;

	// Landing: the body was moving down meaningfully and has stopped. The
	// threshold is 120 cm/s because a body settling onto a carpet moves less than
	// that, and a body arriving from waist height moves far more.
	const float ImpactThreshold = 120.f;
	const bool bLanded = LastCorpseFallSpeed > ImpactThreshold && FallSpeed < ImpactThreshold * 0.5f;

	// Bound the wait: a corpse caught on a railing can hang for the rest of the
	// match, and a body that never lands still needs the effect's cue to be spent
	// rather than pending forever.
	const bool bTimedOut = CorpseAirTime > 4.f;

	if (bLanded || bTimedOut)
	{
		bCorpseFalling = false;

		if (bLanded && LastCorpseFallSpeed > 260.f)
		{
			const FVector ImpactLocation = Mesh->GetBoneLocation(TEXT("pelvis"));

			// The generated effect is spawned directly rather than through a
			// component: this is a one-shot at a bone, and there is no per-character
			// corpse FX component to hold it.
			UOneBrahamVFXStatics::SpawnEffect(this, EOneBrahamVFXEffect::CorpseImpact,
				ImpactLocation, FRotator::ZeroRotator, FVector(1.f));
		}
	}

	LastCorpseFallSpeed = FallSpeed;
}

void UOneBrahamRagdollComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	AOneBrahamCharacter* Character = CachedCharacter.Get();
	if (Character == nullptr || !bRagdolling)
	{
		return;
	}

	// --- Fade -------------------------------------------------------------
	if (bFadingOut && FadeOutSeconds > 0.f)
	{
		USkeletalMeshComponent* Mesh = GetTargetMesh();
		const float Elapsed = GetWorld()->GetTimeSeconds() - FadeStartTime;
		const float Alpha = FMath::Clamp(1.f - (Elapsed / FadeOutSeconds), 0.f, 1.f);

		if (Mesh != nullptr)
		{
			if (UMaterialInstanceDynamic* MID = Mesh->CreateAndSetMaterialInstanceDynamic(0))
			{
				// The character master material exposes an Opacity scalar used by
				// the DitherTemporalAA fade path.
				MID->SetScalarParameterValue(TEXT("Opacity"), Alpha);
			}

			// Dissolve is done in the material via a noise-driven dither; the
			// scalar above is the driver.
		}

		if (Elapsed >= FadeOutSeconds)
		{
			DestroyCorpseNow();

			// Destroy the corpse pawn. Corpses stay pawns (rather than becoming a
			// separate corpse actor) so that hit registration, looting and the
			// existing spectator flow keep working unchanged; the pawn is destroyed
			// once it has faded. FIRE DRILL respawns were already queued at death.
			Character->SetLifeSpan(0.1f);
		}
	}

	// --- Landing ----------------------------------------------------------
	TickCorpseLanding(DeltaTime);
}
