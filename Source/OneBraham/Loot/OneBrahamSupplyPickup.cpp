// Copyright (c) One Braham. All Rights Reserved.

#include "Loot/OneBrahamSupplyPickup.h"

#include "Characters/OneBrahamCharacter.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/OneBrahamGameplayTags.h"
#include "Core/OneBrahamPlayerController.h"
#include "Engine/StaticMesh.h"
#include "Net/UnrealNetwork.h"
#include "OneBraham.h"
#include "Weapons/OneBrahamInventoryComponent.h"
#include "Weapons/OneBrahamWeaponComponent.h"

namespace
{
	/** The engine cube, scaled per kind: no content dependency for a box on a desk. */
	const TCHAR* const SupplyMeshFallback = TEXT("/Engine/BasicShapes/Cube.Cube");
}

AOneBrahamSupplyPickup::AOneBrahamSupplyPickup()
{
	PrimaryActorTick.bCanEverTick = false;

	bReplicates = true;
	SetCanBeDamaged(false);

	BoxMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BoxMesh"));
	SetRootComponent(BoxMesh);

	// Found by the interaction trace (Visibility), and nothing else: a box of
	// rounds must never stop a bullet or a player.
	BoxMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	BoxMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
	BoxMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	BoxMesh->SetGenerateOverlapEvents(false);

	Glow = CreateDefaultSubobject<UPointLightComponent>(TEXT("Glow"));
	Glow->SetupAttachment(BoxMesh);
	Glow->SetMobility(EComponentMobility::Movable);
	Glow->SetCastShadows(false);
	Glow->SetAttenuationRadius(80.f);
	Glow->SetIntensity(4.f);
}

void AOneBrahamSupplyPickup::BeginPlay()
{
	Super::BeginPlay();

	if (BoxMesh != nullptr && BoxMesh->GetStaticMesh() == nullptr)
	{
		if (UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, SupplyMeshFallback))
		{
			BoxMesh->SetStaticMesh(Cube);
		}
	}

	ApplyContentsVisuals();
}

void AOneBrahamSupplyPickup::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AOneBrahamSupplyPickup, Kind);
	DOREPLIFETIME(AOneBrahamSupplyPickup, UtilityId);
	DOREPLIFETIME(AOneBrahamSupplyPickup, Amount);
}

void AOneBrahamSupplyPickup::ServerConfigure(EOneBrahamSupplyKind InKind, FName InUtilityId, int32 InAmount)
{
	if (!HasAuthority())
	{
		return;
	}

	Kind = InKind;
	UtilityId = InKind == EOneBrahamSupplyKind::Utility ? InUtilityId : NAME_None;
	Amount = FMath::Max(1, InAmount);
	ApplyContentsVisuals();
	ForceNetUpdate();
}

void AOneBrahamSupplyPickup::OnRep_Contents()
{
	ApplyContentsVisuals();
}

void AOneBrahamSupplyPickup::ApplyContentsVisuals()
{
	// An ammo can, a grenade, a first-aid box: readable by shape and colour from
	// across a room before the prompt ever appears.
	FVector Scale(0.30f, 0.16f, 0.18f);
	FLinearColor Colour(0.85f, 0.62f, 0.18f);

	switch (Kind)
	{
	case EOneBrahamSupplyKind::Utility:
		Scale = FVector(0.09f, 0.09f, 0.12f);
		Colour = FLinearColor(0.35f, 0.75f, 0.35f);
		break;

	case EOneBrahamSupplyKind::Medkit:
		Scale = FVector(0.26f, 0.18f, 0.09f);
		Colour = FLinearColor(0.95f, 0.95f, 0.95f);
		break;

	default:
		break;
	}

	if (BoxMesh != nullptr)
	{
		BoxMesh->SetRelativeScale3D(Scale);
		BoxMesh->SetRenderCustomDepth(true);
		BoxMesh->SetCustomDepthStencilValue(1);
	}

	if (Glow != nullptr)
	{
		Glow->SetLightColor(Colour);
	}
}

FText AOneBrahamSupplyPickup::GetInteractionPrompt_Implementation(const AOneBrahamPlayerController* Interactor) const
{
	switch (Kind)
	{
	case EOneBrahamSupplyKind::Utility:
		if (UtilityId == TEXT("Utility_Frag"))
		{
			return NSLOCTEXT("OneBraham", "Supply_Frag", "Take frag grenade");
		}
		if (UtilityId == TEXT("Utility_Flashbang"))
		{
			return NSLOCTEXT("OneBraham", "Supply_Flash", "Take flashbang");
		}
		if (UtilityId == TEXT("Utility_Smoke"))
		{
			return NSLOCTEXT("OneBraham", "Supply_Smoke", "Take smoke grenade");
		}
		return NSLOCTEXT("OneBraham", "Supply_Breach", "Take breaching charge");

	case EOneBrahamSupplyKind::Medkit:
		return NSLOCTEXT("OneBraham", "Supply_Medkit", "Take medkit");

	default:
		return NSLOCTEXT("OneBraham", "Supply_Ammo", "Take ammunition");
	}
}

FGameplayTag AOneBrahamSupplyPickup::GetInteractionTag_Implementation(const AOneBrahamPlayerController* Interactor) const
{
	switch (Kind)
	{
	case EOneBrahamSupplyKind::Utility:
		return OneBrahamTags::Interaction_Utility;
	case EOneBrahamSupplyKind::Medkit:
		return OneBrahamTags::Interaction_Medkit;
	default:
		return OneBrahamTags::Interaction_Ammo;
	}
}

bool AOneBrahamSupplyPickup::CanInteract_Implementation(const AOneBrahamPlayerController* Interactor) const
{
	return Interactor != nullptr && Interactor->GetPawn() != nullptr && !IsActorBeingDestroyed();
}

bool AOneBrahamSupplyPickup::RequiresHold_Implementation(const AOneBrahamPlayerController* Interactor) const
{
	return false;
}

float AOneBrahamSupplyPickup::GetHoldDuration_Implementation(const AOneBrahamPlayerController* Interactor) const
{
	return 0.f;
}

float AOneBrahamSupplyPickup::GetInteractionRange_Implementation(const AOneBrahamPlayerController* Interactor) const
{
	return PickupRangeCm;
}

bool AOneBrahamSupplyPickup::OnInteractionStart_Implementation(AOneBrahamPlayerController* Interactor)
{
	if (Interactor == nullptr || !HasAuthority())
	{
		return false;
	}

	AOneBrahamCharacter* Character = Cast<AOneBrahamCharacter>(Interactor->GetPawn());
	if (Character == nullptr)
	{
		return false;
	}

	const float Distance = FVector::Dist(Character->GetActorLocation(), GetActorLocation());
	if (Distance > PickupRangeCm + 60.f)
	{
		OB_WARN("Supply pickup rejected: %s is %.0f cm away (limit %.0f).",
			*Character->GetName(), Distance, PickupRangeCm + 60.f);
		return false;
	}

	return ServerGrantTo(Character);
}

bool AOneBrahamSupplyPickup::ServerGrantTo(AOneBrahamCharacter* Character)
{
	bool bGranted = false;

	switch (Kind)
	{
	case EOneBrahamSupplyKind::Ammo:
		if (UOneBrahamWeaponComponent* Weapons = Character->GetWeaponComponent())
		{
			bGranted = Weapons->ServerAddReserveShare(static_cast<float>(Amount) / 100.f) > 0;
		}
		break;

	case EOneBrahamSupplyKind::Utility:
		if (UOneBrahamInventoryComponent* Inventory = Character->GetInventoryComponent())
		{
			bGranted = Inventory->ServerAddUtility(UtilityId, Amount) > 0;
		}
		break;

	case EOneBrahamSupplyKind::Medkit:
		if (UOneBrahamInventoryComponent* Inventory = Character->GetInventoryComponent())
		{
			bGranted = Inventory->ServerAddUtility(TEXT("Utility_Medkit"), Amount) > 0;
		}
		break;
	}

	if (bGranted)
	{
		Destroy();
	}

	return bGranted;
}

void AOneBrahamSupplyPickup::OnInteractionEnd_Implementation(AOneBrahamPlayerController* Interactor, bool bCompleted)
{
	// A tap: the grant happened at the start.
}

void AOneBrahamSupplyPickup::OnInteractionCancelled_Implementation(AOneBrahamPlayerController* Interactor)
{
}

void AOneBrahamSupplyPickup::SetInteractionHighlight_Implementation(bool bHighlighted)
{
	if (BoxMesh != nullptr)
	{
		BoxMesh->SetCustomDepthStencilValue(bHighlighted ? 2 : 1);
	}
}
