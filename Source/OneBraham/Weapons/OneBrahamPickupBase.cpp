// Copyright (c) One Braham. All Rights Reserved.

#include "Weapons/OneBrahamPickupBase.h"
#include "Voice/OneBrahamVoiceDirector.h"
#include "Data/OneBrahamWeaponDataAsset.h"

#include "Characters/OneBrahamCharacter.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/OneBrahamGameplayTags.h"
#include "Core/OneBrahamPlayerController.h"
#include "Engine/World.h"
#include "GameFramework/PlayerState.h"
#include "Net/UnrealNetwork.h"
#include "OneBraham.h"
#include "Weapons/OneBrahamWeaponComponent.h"

namespace
{
	/** Height above the settled surface that a dropped weapon sits at, cm. */
	constexpr float OBDropHoverCm = 6.f;

	/** Trace length when settling a drop onto the floor, cm. */
	constexpr float OBDropTraceCm = 400.f;

	/** Fade-out window at the end of a dropped weapon's life, seconds. */
	constexpr float OBDropFadeSeconds = 3.f;
}

AOneBrahamPickupBase::AOneBrahamPickupBase()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.1f;

	// Loot glow: a dim, warm light so a weapon in a dark stairwell is findable
	// without turning the building into a loot-shooter. It is off in the hand.
	GroundGlow = CreateDefaultSubobject<UPointLightComponent>(TEXT("GroundGlow"));
	GroundGlow->SetupAttachment(GetRootComponent());
	GroundGlow->SetMobility(EComponentMobility::Movable);
	GroundGlow->SetCastShadows(false);
	GroundGlow->SetIntensity(0.f);
	GroundGlow->SetAttenuationRadius(180.f);
	GroundGlow->SetLightColor(FLinearColor(1.f, 0.72f, 0.35f));
	GroundGlow->SetVisibility(false);
}

void AOneBrahamPickupBase::BeginPlay()
{
	Super::BeginPlay();

	ApplyGroundVisuals();
}

void AOneBrahamPickupBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AOneBrahamPickupBase, bIsOnGround);
	DOREPLIFETIME(AOneBrahamPickupBase, GroundReserveAmmo);
	DOREPLIFETIME(AOneBrahamPickupBase, bDroppedFromCorpse);
	DOREPLIFETIME(AOneBrahamPickupBase, bPermanentLoot);
}

void AOneBrahamPickupBase::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!bIsOnGround)
	{
		return;
	}

	TimeOnGround += DeltaSeconds;

	// Level loot neither ages nor fades; nothing below applies to it.
	if (bPermanentLoot)
	{
		return;
	}

	// Age out: a floor covered in unclaimed weapons is a performance problem and
	// an information problem (the player cannot tell what is actually there).
	if (HasAuthority() && GroundLifetimeSeconds > 0.f && !bPermanentLoot)
	{
		if (TimeOnGround >= GroundLifetimeSeconds + OBDropFadeSeconds)
		{
			Destroy();
			return;
		}
	}

	// Fade during the last few seconds so the weapon leaving is readable.
	if (GroundLifetimeSeconds > 0.f && !bPermanentLoot)
	{
		const float FadeStart = GroundLifetimeSeconds;
		if (TimeOnGround >= FadeStart)
		{
			const float Alpha = FMath::Clamp(1.f - ((TimeOnGround - FadeStart) / OBDropFadeSeconds), 0.f, 1.f);

			if (PickupMesh != nullptr)
			{
				PickupMesh->SetScalarParameterValueOnMaterials(TEXT("FadeAlpha"), Alpha);
				PickupMesh->SetVisibility(Alpha > 0.02f);
			}
			if (GroundGlow != nullptr)
			{
				GroundGlow->SetIntensity(FMath::Lerp(0.f, 18.f, Alpha));
			}
		}
	}
}

// ===========================================================================
//  Dropping
// ===========================================================================

void AOneBrahamPickupBase::ServerDropAt(const FVector& Location, bool bScatter)
{
	if (!HasAuthority())
	{
		return;
	}

	// The weapon component is dropping this: it no longer owns it.
	SetOwningWeaponComponent(nullptr);
	OnUnequipped();

	SettleOnSurface(Location, bScatter);

	bIsOnGround = true;
	TimeOnGround = 0.f;
	bDroppedFromCorpse = true;

	// A drop is a drop, whatever the gun's history: a rifle first found on a
	// desk and dropped by its dead owner ages out and is locked to the body
	// like any other. (bPermanentLoot used to stick, so it was neither.)
	// ServerPlaceAsLoot sets it again after calling this.
	bPermanentLoot = false;

	// Ground collides with the world so the player can walk up to it, and with
	// nothing else: a dropped gun should never body-block a doorway.
	SetActorEnableCollision(true);

	SetLifeSpan(0.f);	// the Tick handles the fade-and-destroy, not the engine

	ApplyGroundVisuals();
	ForceNetUpdate();

	OB_WEAPON_LOG("Weapon dropped on the ground: %s at %s (%d reserve rounds)",
		Definition != nullptr ? *Definition->WeaponId.ToString() : TEXT("unknown"),
		*GetActorLocation().ToCompactString(), GroundReserveAmmo);
}

void AOneBrahamPickupBase::SettleOnSurface(const FVector& Location, bool bScatter)
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		SetActorLocation(Location);
		return;
	}

	// Detach from whatever was holding it (character socket) without snapping.
	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);

	FVector Target = Location;
	if (bScatter)
	{
		// A small scatter so a corpse with two weapons does not look like a rack.
		Target += FVector(FMath::FRandRange(-28.f, 28.f), FMath::FRandRange(-28.f, 28.f), 0.f);
	}

	// Settle onto the floor: trace down from just above the drop point.
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(OBDropSettle), /*bTraceComplex*/ false, this);
	Params.bReturnPhysicalMaterial = false;

	const FVector TraceStart = Target + FVector(0.f, 0.f, 60.f);
	const FVector TraceEnd = Target - FVector(0.f, 0.f, OBDropTraceCm);

	FVector FinalLocation = Target;
	if (World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_Visibility, Params))
	{
		FinalLocation = Hit.ImpactPoint + FVector(0.f, 0.f, OBDropHoverCm);
	}

	// A dropped weapon lands at a slight, random yaw. Never a spin, never a
	// perfect right angle: it was dropped by a person.
	const FRotator FinalRotation = FRotator(
		FMath::FRandRange(-6.f, 6.f),
		FMath::FRandRange(0.f, 360.f),
		FMath::FRandRange(-4.f, 4.f));

	SetActorLocationAndRotation(FinalLocation, FinalRotation, /*bSweep*/ false, nullptr, ETeleportType::TeleportPhysics);
}

void AOneBrahamPickupBase::ServerTakeOffGround()
{
	if (!HasAuthority())
	{
		return;
	}

	bIsOnGround = false;
	TimeOnGround = 0.f;
	SetActorEnableCollision(false);

	ApplyGroundVisuals();
	ForceNetUpdate();
}

bool AOneBrahamPickupBase::IsPickupLocked() const
{
	// The lock stops a killer's squad mate looting the body in the same frame;
	// loot that was lying on a desk before anyone arrived has no body to protect.
	return bIsOnGround && !bPermanentLoot && TimeOnGround < PickupLockSeconds;
}

void AOneBrahamPickupBase::ServerPlaceAsLoot(const FVector& Location, float Yaw)
{
	if (!HasAuthority())
	{
		return;
	}

	bPermanentLoot = true;
	SetActorRotation(FRotator(0.f, Yaw, 0.f));
	ServerDropAt(Location, /*bScatter*/ false);
	bDroppedFromCorpse = false;
	ApplyGroundVisuals();
}

AOneBrahamPickupBase* AOneBrahamPickupBase::SpawnLoot(UWorld* World, UOneBrahamWeaponDataAsset* Registry, FName WeaponId,
	const FVector& Location, float Yaw)
{
	if (World == nullptr || Registry == nullptr || World->GetNetMode() == NM_Client)
	{
		return nullptr;
	}

	UOneBrahamWeaponDefinition* WeaponDefinition = Registry->FindWeapon(WeaponId);
	if (WeaponDefinition == nullptr)
	{
		OB_WARN("Loot: unknown weapon '%s'.", *WeaponId.ToString());
		return nullptr;
	}

	// The same class rule as a weapon given to a player, so a looted rifle is the
	// same actor with the same meshes as one carried in.
	TSubclassOf<AOneBrahamWeaponBase> SpawnClass = nullptr;
	if (!WeaponDefinition->WeaponActorClass.IsNull())
	{
		SpawnClass = WeaponDefinition->WeaponActorClass.LoadSynchronous();
	}
	if (SpawnClass == nullptr && !Registry->WeaponActorClass.IsNull())
	{
		SpawnClass = Registry->WeaponActorClass.LoadSynchronous();
	}
	if (SpawnClass == nullptr || !SpawnClass->IsChildOf(AOneBrahamPickupBase::StaticClass()))
	{
		SpawnClass = AOneBrahamPickupBase::StaticClass();
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AOneBrahamPickupBase* Pickup = World->SpawnActor<AOneBrahamPickupBase>(SpawnClass,
		FTransform(FRotator(0.f, Yaw, 0.f), Location), Params);
	if (Pickup == nullptr)
	{
		return nullptr;
	}

	Pickup->InitialiseFromDefinition(WeaponDefinition);
	Pickup->SetAmmoInMagazine(Pickup->GetMagazineSize());
	Pickup->SetGroundReserveAmmo(WeaponDefinition->StartingReserve);
	Pickup->ServerPlaceAsLoot(Location, Yaw);
	return Pickup;
}

void AOneBrahamPickupBase::SetGroundReserveAmmo(int32 Reserve)
{
	if (!HasAuthority())
	{
		return;
	}

	GroundReserveAmmo = FMath::Max(0, Reserve);
}

void AOneBrahamPickupBase::OnRep_GroundState()
{
	ApplyGroundVisuals();
}

void AOneBrahamPickupBase::ApplyGroundVisuals()
{
	// Keep the base class's ground flag in agreement with ours: the weapon base
	// uses it for weapon state and pickup-mesh visibility, and two flags that
	// disagree is exactly the kind of bug that ships.
	bIsGroundPickup = bIsOnGround;

	// Ground: the world-pickup static mesh is visible and the animated weapon
	// mesh is hidden. In hand: exactly the reverse. One flag, two meshes, no
	// duplicated weapon actors in the world.
	if (PickupMesh != nullptr)
	{
		PickupMesh->SetVisibility(bIsOnGround);
		PickupMesh->SetCollisionEnabled(bIsOnGround ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
	}

	if (WeaponMesh != nullptr)
	{
		WeaponMesh->SetVisibility(!bIsOnGround);
	}

	if (GroundGlow != nullptr)
	{
		// Warmer glow for loot that came off a body, cooler for floor spawns: a
		// quiet visual language for "this just happened here".
		GroundGlow->SetVisibility(bIsOnGround);
		GroundGlow->SetIntensity(bIsOnGround ? (bDroppedFromCorpse ? 22.f : 14.f) : 0.f);
		GroundGlow->SetLightColor(bDroppedFromCorpse ? FLinearColor(1.f, 0.62f, 0.3f) : FLinearColor(0.72f, 0.82f, 1.f));
	}
}

// ===========================================================================
//  Picking up
// ===========================================================================

bool AOneBrahamPickupBase::ServerPickupBy(AOneBrahamCharacter* Character)
{
	if (!HasAuthority() || Character == nullptr || !bIsOnGround)
	{
		return false;
	}

	if (IsPickupLocked())
	{
		return false;
	}

	UOneBrahamWeaponComponent* Weapons = Character->GetWeaponComponent();
	if (Weapons == nullptr)
	{
		return false;
	}

	// The component owns slot rules, ammo transfer and weapon attachment; this
	// actor only knows that it is being taken off the floor.
	if (Weapons->ServerPickupFromWorld(this) != nullptr)
	{
		ServerTakeOffGround();
		FOneBrahamVoiceDirector::OnLootTaken(Character, /*bWeapon*/ true);
		return true;
	}

	return false;
}

// ===========================================================================
//  Interaction
// ===========================================================================

FText AOneBrahamPickupBase::GetInteractionPrompt_Implementation(const AOneBrahamPlayerController* Interactor) const
{
	if (Definition == nullptr)
	{
		return NSLOCTEXT("OneBraham", "Pickup_Generic", "Pick up");
	}

	if (IsPickupLocked())
	{
		return NSLOCTEXT("OneBraham", "Pickup_Locked", "Loot bag settling...");
	}

	const FText WeaponName = Definition->DisplayName.IsEmpty()
		? FText::FromName(Definition->WeaponId)
		: Definition->DisplayName;

	// Ammo is part of the promise: "pick up a 5.56 carbine" and finding one round
	// in it is a nasty surprise, so the count is on the prompt.
	return FText::Format(
		NSLOCTEXT("OneBraham", "Pickup_Prompt", "Take {0}  ({1} + {2})"),
		WeaponName,
		FText::AsNumber(AmmoInMagazine),
		FText::AsNumber(GroundReserveAmmo));
}

FGameplayTag AOneBrahamPickupBase::GetInteractionTag_Implementation(const AOneBrahamPlayerController* Interactor) const
{
	return OneBrahamTags::Interaction_Weapon;
}

bool AOneBrahamPickupBase::CanInteract_Implementation(const AOneBrahamPlayerController* Interactor) const
{
	if (!bIsOnGround || IsPickupLocked())
	{
		return false;
	}

	return Interactor != nullptr && Interactor->GetPawn() != nullptr;
}

bool AOneBrahamPickupBase::RequiresHold_Implementation(const AOneBrahamPlayerController* Interactor) const
{
	return false;
}

float AOneBrahamPickupBase::GetHoldDuration_Implementation(const AOneBrahamPlayerController* Interactor) const
{
	return 0.f;
}

float AOneBrahamPickupBase::GetInteractionRange_Implementation(const AOneBrahamPlayerController* Interactor) const
{
	return PickupRangeCm;
}

bool AOneBrahamPickupBase::OnInteractionStart_Implementation(AOneBrahamPlayerController* Interactor)
{
	if (Interactor == nullptr)
	{
		return false;
	}

	AOneBrahamCharacter* Character = Cast<AOneBrahamCharacter>(Interactor->GetPawn());
	if (Character == nullptr)
	{
		return false;
	}

	// Server-side distance validation: the interaction component already checked
	// on the client, but the server never trusts that.
	const float Distance = FVector::Dist(Character->GetActorLocation(), GetActorLocation());
	if (Distance > PickupRangeCm + 60.f)
	{
		OB_WARN("Pickup rejected: %s is %.0f cm from '%s' (range %.0f).",
			*Character->GetName(), Distance,
			Definition != nullptr ? *Definition->WeaponId.ToString() : TEXT("?"), PickupRangeCm);
		return false;
	}

	return ServerPickupBy(Character);
}

void AOneBrahamPickupBase::OnInteractionEnd_Implementation(AOneBrahamPlayerController* Interactor, bool bCompleted)
{
	// Tap interaction: nothing to unwind. Present so the base contract is met and
	// a Blueprint subclass cannot accidentally forget it.
}

void AOneBrahamPickupBase::OnInteractionCancelled_Implementation(AOneBrahamPlayerController* Interactor)
{
	SetInteractionHighlight_Implementation(false);
}

void AOneBrahamPickupBase::SetInteractionHighlight_Implementation(bool bHighlightedIn)
{
	if (bHighlighted == bHighlightedIn)
	{
		return;
	}

	bHighlighted = bHighlightedIn;

	// The outline is a custom-depth pass on whatever mesh is showing, which keeps
	// loot and doors using one stencil setup (see the UI style guide, Docs/08).
	if (PickupMesh != nullptr && bIsOnGround)
	{
		PickupMesh->SetRenderCustomDepth(bHighlightedIn);
	}
}
