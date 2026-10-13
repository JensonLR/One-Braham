// Copyright (c) One Braham. All Rights Reserved.

#include "Access/OneBrahamKeycard.h"

#include "Characters/OneBrahamCharacter.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/OneBrahamGameplayTags.h"
#include "Core/OneBrahamPlayerController.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "OneBraham.h"
#include "Weapons/OneBrahamInventoryComponent.h"

namespace
{
	/** Engine cube: the fallback card mesh, scaled thin. No content dependency. */
	const TCHAR* const CardMeshFallback = TEXT("/Engine/BasicShapes/Cube.Cube");
	const TCHAR* const CardMaterialFallback = TEXT("/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial");

	/** Interaction range for a card lying on a desk, cm. */
	constexpr float CardPickupRangeCm = 210.f;
}

AOneBrahamKeycard::AOneBrahamKeycard()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.f;

	bReplicates = true;
	SetCanBeDamaged(false);

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	CardMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CardMesh"));
	CardMesh->SetupAttachment(Root);
	CardMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	CardMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
	CardMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	CardMesh->SetGenerateOverlapEvents(false);

	CardGlow = CreateDefaultSubobject<UPointLightComponent>(TEXT("CardGlow"));
	CardGlow->SetupAttachment(Root);
	CardGlow->SetMobility(EComponentMobility::Movable);
	CardGlow->SetCastShadows(false);
	CardGlow->SetAttenuationRadius(90.f);
	CardGlow->SetIntensity(6.f);
}

void AOneBrahamKeycard::BeginPlay()
{
	Super::BeginPlay();

	// A card with no authored mesh still has to be a visible, findable object:
	// the engine cube at 8.5 x 5.4 x 0.08 cm is a credit card, and it costs
	// nothing. This is also the path used before the art pass lands.
	if (CardMesh != nullptr && CardMesh->GetStaticMesh() == nullptr)
	{
		if (UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, CardMeshFallback))
		{
			CardMesh->SetStaticMesh(Cube);
			CardMesh->SetRelativeScale3D(FVector(0.085f, 0.054f, 0.001f));

			if (UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, CardMaterialFallback))
			{
				CardMesh->SetMaterial(0, Material);
			}
		}
	}

	BaseHeight = GetActorLocation().Z;
	RefreshTierVisuals();
}

void AOneBrahamKeycard::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AOneBrahamKeycard, AccessTier);
	DOREPLIFETIME(AOneBrahamKeycard, IssuedFloor);
	DOREPLIFETIME(AOneBrahamKeycard, bDroppedFromPlayer);
	DOREPLIFETIME(AOneBrahamKeycard, PrintedName);
}

void AOneBrahamKeycard::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	Elapsed += DeltaSeconds;

	// Rotation and bob are local cosmetic motion; nothing about the card's state
	// depends on them, so they are not replicated.
	if (CardMesh != nullptr)
	{
		CardMesh->AddLocalRotation(FRotator(0.f, SpinDegreesPerSecond * DeltaSeconds, 0.f));
	}

	if (BobAmplitudeCm > 0.f)
	{
		const float Bob = FMath::Sin(Elapsed * 1.4f) * BobAmplitudeCm;
		CardMesh->SetRelativeLocation(FVector(0.f, 0.f, Bob));
	}

	// Cards do not stay forever: thirty of them spread across fifteen floors is
	// both a performance and a legibility problem.
	if (HasAuthority() && GroundLifetimeSeconds > 0.f && Elapsed > GroundLifetimeSeconds)
	{
		// Blink out rather than vanish: the player should register that it is gone.
		if (Elapsed > GroundLifetimeSeconds + 2.f)
		{
			Destroy();
		}
		else if (CardGlow != nullptr)
		{
			CardGlow->SetIntensity(FMath::Lerp(6.f, 0.f, (Elapsed - GroundLifetimeSeconds) / 2.f));
		}
	}
}

// ===========================================================================
//  Tier language
// ===========================================================================

FLinearColor AOneBrahamKeycard::GetTierColour(EOneBrahamAccessTier Tier)
{
	// The building's own access-colour scheme, which is what a real office uses:
	// grey for public, blue for staff, navy for secure, amber for executive,
	// yellow for facilities (plus the master ribbon), red for override-only.
	switch (Tier)
	{
	case EOneBrahamAccessTier::Public:		return FLinearColor(0.85f, 0.87f, 0.89f);
	case EOneBrahamAccessTier::Staff:		return FLinearColor(0.08f, 0.52f, 1.f);
	case EOneBrahamAccessTier::Secure:		return FLinearColor(0.05f, 0.22f, 0.55f);
	case EOneBrahamAccessTier::Executive:	return FLinearColor(1.f, 0.66f, 0.1f);
	case EOneBrahamAccessTier::Skydeck:		return FLinearColor(1.f, 0.804f, 0.f);
	case EOneBrahamAccessTier::Override:	return FLinearColor(0.867f, 0.118f, 0.122f);
	default:								return FLinearColor(0.85f, 0.87f, 0.89f);
	}
}

FText AOneBrahamKeycard::GetTierDisplayName(EOneBrahamAccessTier Tier)
{
	switch (Tier)
	{
	case EOneBrahamAccessTier::Public:		return NSLOCTEXT("OneBraham", "Tier_Public", "VISITOR PASS");
	case EOneBrahamAccessTier::Staff:		return NSLOCTEXT("OneBraham", "Tier_Staff", "STAFF BADGE");
	case EOneBrahamAccessTier::Secure:		return NSLOCTEXT("OneBraham", "Tier_Secure", "IT OPERATIONS BADGE");
	case EOneBrahamAccessTier::Executive:	return NSLOCTEXT("OneBraham", "Tier_Executive", "EXECUTIVE BADGE");
	case EOneBrahamAccessTier::Skydeck:		return NSLOCTEXT("OneBraham", "Tier_Skydeck", "FACILITIES MASTER");
	case EOneBrahamAccessTier::Override:	return NSLOCTEXT("OneBraham", "Tier_Override", "SYSTEM OVERRIDE");
	default:								return NSLOCTEXT("OneBraham", "Tier_Unknown", "UNREADABLE BADGE");
	}
}

void AOneBrahamKeycard::ApplyTier(EOneBrahamAccessTier NewTier)
{
	if (!HasAuthority())
	{
		return;
	}

	AccessTier = NewTier;

	// The floor the card was issued on follows the tier: one number, one story.
	switch (NewTier)
	{
	case EOneBrahamAccessTier::Public:		IssuedFloor = 1; break;
	case EOneBrahamAccessTier::Staff:		IssuedFloor = 4; break;
	case EOneBrahamAccessTier::Secure:		IssuedFloor = 9; break;
	case EOneBrahamAccessTier::Executive:	IssuedFloor = 12; break;
	case EOneBrahamAccessTier::Skydeck:		IssuedFloor = 15; break;
	default:								IssuedFloor = 1; break;
	}

	RefreshTierVisuals();
	ForceNetUpdate();
}

void AOneBrahamKeycard::OnRep_AccessTier()
{
	RefreshTierVisuals();
}

void AOneBrahamKeycard::RefreshTierVisuals()
{
	const FLinearColor Colour = GetTierColour(AccessTier);

	if (CardGlow != nullptr)
	{
		CardGlow->SetLightColor(Colour);
		// A Facilities master is brighter than a staff badge: in a dark stairwell
		// the card's own light is the fastest read of "is that worth crossing the
		// floor for".
		CardGlow->SetIntensity(AccessTier >= EOneBrahamAccessTier::Executive ? 11.f : 6.f);
	}

	if (CardMesh != nullptr)
	{
		CardMesh->SetVectorParameterValueOnMaterials(TierColourParameter, FVector(Colour));
	}
}

// ===========================================================================
//  Interaction
// ===========================================================================

FText AOneBrahamKeycard::GetInteractionPrompt_Implementation(const AOneBrahamPlayerController* Interactor) const
{
	const FText TierName = GetTierDisplayName(AccessTier);

	if (bDroppedFromPlayer && !PrintedName.IsEmpty())
	{
		return FText::Format(NSLOCTEXT("OneBraham", "Keycard_Prompt_Owned", "Take {0} ({1})"),
			TierName, PrintedName);
	}

	return FText::Format(NSLOCTEXT("OneBraham", "Keycard_Prompt", "Take {0} - issued floor {1}"),
		TierName, FText::AsNumber(IssuedFloor));
}

FGameplayTag AOneBrahamKeycard::GetInteractionTag_Implementation(const AOneBrahamPlayerController* Interactor) const
{
	return OneBrahamTags::Interaction_Keycard;
}

bool AOneBrahamKeycard::CanInteract_Implementation(const AOneBrahamPlayerController* Interactor) const
{
	return Interactor != nullptr && Interactor->GetPawn() != nullptr;
}

bool AOneBrahamKeycard::RequiresHold_Implementation(const AOneBrahamPlayerController* Interactor) const
{
	// Cards are a tap: the cost of taking one is that somebody might see you do it.
	return false;
}

float AOneBrahamKeycard::GetHoldDuration_Implementation(const AOneBrahamPlayerController* Interactor) const
{
	return 0.f;
}

float AOneBrahamKeycard::GetInteractionRange_Implementation(const AOneBrahamPlayerController* Interactor) const
{
	return CardPickupRangeCm;
}

bool AOneBrahamKeycard::OnInteractionStart_Implementation(AOneBrahamPlayerController* Interactor)
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

	// Server-side range validation, as with every other interactable.
	const float Distance = FVector::Dist(Character->GetActorLocation(), GetActorLocation());
	if (Distance > CardPickupRangeCm + 60.f)
	{
		OB_ACCESS_LOG("Keycard pickup rejected: %s is %.0f cm away (limit %.0f).",
			*Character->GetName(), Distance, CardPickupRangeCm + 60.f);
		return false;
	}

	return ServerGrantTo(Character);
}

bool AOneBrahamKeycard::ServerGrantTo(AOneBrahamCharacter* Character)
{
	UOneBrahamInventoryComponent* Inventory = Character->GetInventoryComponent();
	if (Inventory == nullptr)
	{
		OB_WARN("Keycard pickup failed: %s has no inventory component.", *Character->GetName());
		return false;
	}

	Inventory->ServerGrantKeycard(AccessTier);

	// A card taken off a player is tracked so the kill feed can say what it was
	// worth ("FACILITIES MASTER RECOVERED"), which is one of the few places the
	// game lets a player feel clever.
	OB_ACCESS_LOG("%s took a %s (tier %s, floor %d, from player: %s)",
		*Character->GetName(),
		*GetTierDisplayName(AccessTier).ToString(),
		*UEnum::GetValueAsString(AccessTier),
		IssuedFloor,
		bDroppedFromPlayer ? TEXT("yes") : TEXT("no"));

	Destroy();
	return true;
}

void AOneBrahamKeycard::OnInteractionEnd_Implementation(AOneBrahamPlayerController* Interactor, bool bCompleted)
{
	// Tap interaction: nothing to unwind.
}

void AOneBrahamKeycard::OnInteractionCancelled_Implementation(AOneBrahamPlayerController* Interactor)
{
	SetInteractionHighlight_Implementation(false);
}

void AOneBrahamKeycard::SetInteractionHighlight_Implementation(bool bHighlightedIn)
{
	bHighlighted = bHighlightedIn;

	// Cards always render to the custom depth buffer so the outline reads through
	// a desk or a body in the way: a card you cannot see is a card you cannot use,
	// and hunting for one under a corpse is not the tension the game is selling.
	if (CardMesh != nullptr)
	{
		CardMesh->SetRenderCustomDepth(true);
		CardMesh->SetCustomDepthStencilValue(bHighlightedIn ? 2 : 1);
	}
}
