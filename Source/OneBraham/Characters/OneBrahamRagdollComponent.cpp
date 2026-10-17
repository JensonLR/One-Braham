// Copyright (c) One Braham. All Rights Reserved.

#include "Characters/OneBrahamRagdollComponent.h"

#include "Audio/OneBrahamAudioManagerComponent.h"
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
	ApplyCorpseMeshCollision(Mesh);

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
		// A flinch in progress hands over to the ragdoll rather than being
		// unwound under it: every body is about to simulate anyway.
		bReactionActive = false;
		bReactionEnabledPhysics = false;

		// Arm the local corpse bookkeeping on every machine, not just the server:
		// the pool and the landing puff are drawn by clients.
		LocalRagdollStartTime = GetWorld() != nullptr ? GetWorld()->GetTimeSeconds() : 0.f;
		bCorpsePoolSpawned = false;
		if (!Character->HasAuthority())
		{
			bCorpseFalling = true;
			LastCorpseFallSpeed = 0.f;
			CorpseAirTime = 0.f;
		}

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

		// The capsule goes on this machine too, not only on the server: LifeState
		// carries the same rule, but whichever of the two notifies lands first, a
		// client never walks its own prediction into a body that is no longer there.
		SwapToCorpseCollision();

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
	LocalRagdollStartTime = -1.f;
	bCorpsePoolSpawned = false;

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
	AOneBrahamCharacter::ApplyCapsuleCollisionForLifeState(Character->GetCapsuleComponent(), EOneBrahamLifeState::Dead);

	ApplyCorpseMeshCollision(GetTargetMesh());
}

void UOneBrahamRagdollComponent::ApplyCorpseMeshCollision(USkeletalMeshComponent* Mesh)
{
	if (Mesh == nullptr)
	{
		return;
	}

	// The profile says all of this already (OneBrahamRagdoll: PhysicsBody that
	// blocks Weapon, ignores Pawn and Interactable). The two responses are set
	// again after it because the profile resets every override and is edited in
	// an .ini: a body in a doorway must stay scenery even if the profile drifts.
	Mesh->SetCollisionProfileName(TEXT("OneBrahamRagdoll"));
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Mesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	Mesh->SetCollisionResponseToChannel((ECollisionChannel)EOneBrahamTraceChannel::Interactable, ECR_Ignore);
}

void UOneBrahamRagdollComponent::RestoreLivingCollision()
{
	AOneBrahamCharacter* Character = CachedCharacter.Get();
	if (Character == nullptr)
	{
		return;
	}

	// Whatever the character's life state now says (Alive on a respawn): the
	// one rule, not a second copy of the living capsule here.
	AOneBrahamCharacter::ApplyCapsuleCollisionForLifeState(Character->GetCapsuleComponent(), Character->LifeState);

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
	PlayHitReaction(HitBone, ShotDir, Damage, /*bHeadshot*/ false);
}

FName UOneBrahamRagdollComponent::FindSimulatedBone(USkeletalMeshComponent* Mesh, FName Bone) const
{
	// Hits land on bones without bodies (fingers, twist bones, the face): walk
	// up the hierarchy to the first bone the physics asset actually simulates.
	int32 Guard = 0;
	while (Mesh != nullptr && Bone != NAME_None && Guard++ < 32)
	{
		if (Mesh->GetBodyInstance(Bone) != nullptr)
		{
			return Bone;
		}
		Bone = Mesh->GetParentBone(Bone);
	}
	return NAME_None;
}

void UOneBrahamRagdollComponent::PlayHitReaction(FName HitBone, const FVector& ShotDir, float Damage, bool bHeadshot)
{
	// Non-lethal hit reactions use a physics blend below the hit bone: the arm
	// moves, the player keeps control. This is what makes gunfights read without
	// shipping a hundred bespoke flinch animations. It is cosmetic and local -
	// every machine plays it from the weapon's hit multicast - so a dedicated
	// server, which nobody watches, skips it.
	if (GetNetMode() == NM_DedicatedServer || bRagdolling)
	{
		return;
	}

	AOneBrahamCharacter* Character = CachedCharacter.Get();
	USkeletalMeshComponent* Mesh = GetTargetMesh();
	if (Character == nullptr || Mesh == nullptr)
	{
		return;
	}

	const bool bStagger = bHeadshot || Damage >= StaggerDamage;

	// The victim does not watch their own third-person body; what they feel is
	// the camera. Heavier hits kick harder.
	if (Character->IsLocallyControlled())
	{
		UOneBrahamVFXStatics::PlayLocalCameraFlinch(this, bStagger ? 0.55f : FMath::GetMappedRangeValueClamped(FVector2D(10.f, 40.f), FVector2D(0.18f, 0.4f), Damage));
	}

	if (Mesh->GetPhysicsAsset() == nullptr || !Mesh->IsVisible())
	{
		return;
	}

	// A stagger rocks everything above the waist: the spine takes the hit and
	// the head and arms follow. A light hit moves only the limb it landed on.
	FName Root = NAME_None;
	if (bStagger)
	{
		static const FName SpineBones[] = { TEXT("spine_02"), TEXT("spine_01"), TEXT("spine_03") };
		for (const FName& Spine : SpineBones)
		{
			if (Mesh->GetBodyInstance(Spine) != nullptr)
			{
				Root = Spine;
				break;
			}
		}
	}
	if (Root == NAME_None)
	{
		Root = FindSimulatedBone(Mesh, HitBone);
	}
	if (Root == NAME_None)
	{
		return;
	}

	// A new hit replaces the old reaction rather than stacking on it.
	if (bReactionActive && Root != ReactionBone)
	{
		EndHitReaction(/*bRestoreCollision*/ false);
	}

	// The living body is query-only (it is the hitbox). Partial simulation needs
	// physics collision on the mesh for the duration of the flinch; pawns are
	// ignored so the limb cannot shove its own capsule or a teammate.
	if (Mesh->GetCollisionEnabled() == ECollisionEnabled::QueryOnly)
	{
		Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Mesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
		bReactionEnabledPhysics = true;
	}

	ReactionBone = Root;
	ReactionDuration = bStagger ? 0.65f : FMath::GetMappedRangeValueClamped(FVector2D(10.f, 40.f), FVector2D(0.28f, 0.45f), Damage);
	ReactionRemaining = ReactionDuration;
	ReactionPeakWeight = bStagger ? 0.8f : FMath::Clamp(Damage / 45.f, 0.25f, 0.65f);
	bReactionActive = true;

	Mesh->SetAllBodiesBelowSimulatePhysics(Root, true, /*bIncludeSelf*/ true);
	Mesh->SetAllBodiesBelowPhysicsBlendWeight(Root, ReactionPeakWeight, /*bSkipCustomPhysicsType*/ false, /*bIncludeSelf*/ true);

	// Velocity change rather than impulse, so a heavy torso and a light forearm
	// both move by an amount that reads, not by their mass ratio.
	const FVector Push = ShotDir.GetSafeNormal() + FVector(0.f, 0.f, bStagger ? 0.05f : 0.15f);
	const float Speed = bStagger
		? FMath::GetMappedRangeValueClamped(FVector2D(40.f, 100.f), FVector2D(220.f, 420.f), Damage)
		: FMath::GetMappedRangeValueClamped(FVector2D(10.f, 40.f), FVector2D(120.f, 300.f), Damage);
	Mesh->AddImpulseToAllBodiesBelow(Push.GetSafeNormal() * Speed, Root, /*bVelChange*/ true, /*bIncludeSelf*/ true);

	// The bone that was actually hit gets an extra shove: on a stagger the
	// spine rocks, and the shoulder that took the round still snaps back.
	const FName HitBody = FindSimulatedBone(Mesh, HitBone);
	if (HitBody != NAME_None && HitBody != Root && Mesh->GetBodyInstance(HitBody) != nullptr && Mesh->GetBodyInstance(HitBody)->IsInstanceSimulatingPhysics())
	{
		Mesh->AddImpulse(Push.GetSafeNormal() * Speed * 0.6f, HitBody, /*bVelChange*/ true);
	}

	// Blend-out needs a per-frame tick for the next fraction of a second.
	SetComponentTickInterval(0.f);
}

void UOneBrahamRagdollComponent::TickHitReaction(float DeltaTime)
{
	if (!bReactionActive)
	{
		return;
	}

	USkeletalMeshComponent* Mesh = GetTargetMesh();
	if (Mesh == nullptr || bRagdolling)
	{
		bReactionActive = false;
		return;
	}

	ReactionRemaining -= DeltaTime;
	if (ReactionRemaining <= 0.f)
	{
		EndHitReaction(/*bRestoreCollision*/ true);
		return;
	}

	// Hold the peak for the first fifth, then ease back into the animation.
	const float Alpha = FMath::Clamp(ReactionRemaining / (ReactionDuration * 0.8f), 0.f, 1.f);
	Mesh->SetAllBodiesBelowPhysicsBlendWeight(ReactionBone, ReactionPeakWeight * Alpha * Alpha, false, true);
}

void UOneBrahamRagdollComponent::EndHitReaction(bool bRestoreCollision)
{
	USkeletalMeshComponent* Mesh = GetTargetMesh();
	if (Mesh != nullptr && !bRagdolling && ReactionBone != NAME_None)
	{
		Mesh->SetAllBodiesBelowPhysicsBlendWeight(ReactionBone, 0.f, false, true);
		Mesh->SetAllBodiesBelowSimulatePhysics(ReactionBone, false, true);

		if (bRestoreCollision && bReactionEnabledPhysics)
		{
			Mesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Mesh->SetCollisionProfileName(TEXT("OneBrahamHitbox"));
			bReactionEnabledPhysics = false;
		}
	}

	bReactionActive = false;
	ReactionBone = NAME_None;
}

void UOneBrahamRagdollComponent::AddWoundShading(float Damage)
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	USkeletalMeshComponent* Mesh = GetTargetMesh();
	if (Mesh == nullptr)
	{
		return;
	}

	// Skeletal meshes cannot take decals, so wounds are a material parameter:
	// the character master reads "BloodAmount" to darken and wet the clothing.
	// A material without the parameter ignores it, so this is safe before the
	// character art pass adds it.
	WoundAmount = UOneBrahamVFXStatics::IsGoreEnabled() ? FMath::Clamp(WoundAmount + Damage / 120.f, 0.f, 1.f) : 0.f;
	Mesh->SetScalarParameterValueOnMaterials(TEXT("BloodAmount"), WoundAmount);
}

void UOneBrahamRagdollComponent::ClientNearMiss_Implementation(FVector_NetQuantize PassLocation)
{
	UOneBrahamVFXStatics::PlayNearMiss(this, PassLocation);
}

// ==========================================================================
//  Blood pools
// ==========================================================================
bool UOneBrahamRagdollComponent::FindFloorBelow(const FVector& From, FVector& OutFloor) const
{
	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return false;
	}

	FCollisionQueryParams Params(SCENE_QUERY_STAT(OBBloodPool), false, GetOwner());
	FCollisionObjectQueryParams Objects(ECC_WorldStatic);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);

	FHitResult Hit;
	if (World->LineTraceSingleByObjectType(Hit, From + FVector(0.f, 0.f, 20.f), From - FVector(0.f, 0.f, 160.f), Objects, Params)
		&& Hit.ImpactNormal.Z > 0.6f)
	{
		OutFloor = Hit.ImpactPoint;
		return true;
	}
	return false;
}

void UOneBrahamRagdollComponent::TickBlood(float DeltaTime)
{
	if (GetNetMode() == NM_DedicatedServer || !UOneBrahamVFXStatics::IsGoreEnabled())
	{
		return;
	}

	AOneBrahamCharacter* Character = CachedCharacter.Get();
	USkeletalMeshComponent* Mesh = GetTargetMesh();
	const UWorld* World = GetWorld();
	if (Character == nullptr || Mesh == nullptr || World == nullptr)
	{
		return;
	}

	const float Now = World->GetTimeSeconds();

	// --- Corpse -----------------------------------------------------------
	// The pool starts once the body has had time to settle, and spreads over
	// fifteen seconds. Arriving with a ready-made puddle is what a placed prop
	// does; spreading is what a body does.
	if (bRagdolling)
	{
		if (!bCorpsePoolSpawned && LocalRagdollStartTime >= 0.f && Now - LocalRagdollStartTime > 1.6f && !bFadingOut)
		{
			bCorpsePoolSpawned = true;

			FVector Floor;
			if (FindFloorBelow(Mesh->GetBoneLocation(TEXT("pelvis")), Floor))
			{
				const float Hold = CorpseLifetimeSeconds > 0.f ? FMath::Min(CorpseLifetimeSeconds, 240.f) : 240.f;
				UOneBrahamVFXStatics::SpawnBloodPool(this, Floor, FMath::FRandRange(42.f, 64.f), 15.f, Hold);
			}
		}
		return;
	}

	// --- Downed -----------------------------------------------------------
	const UOneBrahamDownedComponent* Downed = Character->GetDownedComponent();
	const bool bDowned = Downed != nullptr && Downed->IsDowned();

	if (!bDowned)
	{
		bWasDowned = false;
		DownedPools.Reset();
		return;
	}

	const FVector Here = Mesh->GetBoneLocation(TEXT("pelvis"));

	if (!bWasDowned)
	{
		bWasDowned = true;
		DownedSince = Now;
		StationarySince = Now;
		LastBleedLocation = Here;
		LastDripLocation = Here;
		DownedPools.Reset();
	}

	// Crawling leaves a trail of drops; staying put for a few seconds leaves a
	// pool. Both are how the squad coming up the stairs finds a wounded player,
	// and how the enemy does.
	if (FVector::DistSquared2D(Here, LastBleedLocation) > FMath::Square(25.f))
	{
		LastBleedLocation = Here;
		StationarySince = Now;
	}

	if (FVector::DistSquared2D(Here, LastDripLocation) > FMath::Square(35.f))
	{
		LastDripLocation = Here;
		UOneBrahamVFXStatics::SpawnBloodDrip(this, Character, Here + FMath::VRand() * 8.f, FMath::FRandRange(4.f, 8.f));
	}

	const bool bSettled = Now - StationarySince > (DownedPools.Num() == 0 ? 1.5f : 3.f);
	const bool bNewSpot = !DownedPools.ContainsByPredicate([&Here](const FVector& Pool) { return FVector::DistSquared2D(Pool, Here) < FMath::Square(80.f); });

	if (bSettled && bNewSpot && DownedPools.Num() < 3)
	{
		FVector Floor;
		if (FindFloorBelow(Here, Floor))
		{
			DownedPools.Add(Here);
			UOneBrahamVFXStatics::SpawnBloodPool(this, Floor, FMath::FRandRange(22.f, 32.f), 20.f, 120.f);
		}
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

			// The weight of a body hitting the floor.
			if (UOneBrahamAudioManagerComponent* Audio = UOneBrahamAudioManagerComponent::GetLocal(this))
			{
				Audio->PlayBodyFall(ImpactLocation, LastCorpseFallSpeed);
			}
		}
	}

	LastCorpseFallSpeed = FallSpeed;
}

void UOneBrahamRagdollComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// --- Cosmetic, every state ----------------------------------------------
	TickHitReaction(DeltaTime);

	// Back to bookkeeping rate once the flinch is over.
	if (!bReactionActive && GetComponentTickInterval() < 0.5f)
	{
		SetComponentTickInterval(0.5f);
	}

	BloodTickAccumulator += DeltaTime;
	if (BloodTickAccumulator >= 0.45f)
	{
		TickBlood(BloodTickAccumulator);
		BloodTickAccumulator = 0.f;
	}

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
