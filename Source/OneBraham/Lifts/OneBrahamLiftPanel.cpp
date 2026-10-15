// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamLiftPanel.cpp

#include "Lifts/OneBrahamLiftPanel.h"

#include "OneBraham.h"
#include "Lifts/OneBrahamLift.h"
#include "Core/OneBrahamGameplayTags.h"
#include "Core/OneBrahamPlayerController.h"
#include "Core/OneBrahamPlayerState.h"
#include "Data/OneBrahamBuildingLayoutAsset.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/** Stand this close to press a lift button. A hair tighter than a door, because
	 *  the car buttons sit beside the door and a looser reach would let somebody
	 *  in the lobby press the car's Up through the open doors. */
	constexpr float OBLiftPanelInteractionRange = 200.f;
}

AOneBrahamLiftPanel::AOneBrahamLiftPanel()
{
	PrimaryActorTick.bCanEverTick = false;

	bReplicates = true;

	// Movement replication carries the attachment, which is how the car buttons
	// arrive on a client already riding the car. The landing panels never move,
	// so for them this is one initial location and silence.
	SetReplicateMovement(true);
	SetNetUpdateFrequency(2.f);
	SetMinNetUpdateFrequency(1.f);

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	PlateMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PlateMesh"));
	PlateMesh->SetupAttachment(Root);
	PlateMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PlateMesh->SetCanEverAffectNavigation(false);
	PlateMesh->SetRelativeScale3D(FVector(0.03f, 0.14f, 0.22f));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeFinder.Succeeded())
	{
		PlateMesh->SetStaticMesh(CubeFinder.Object);
	}

	// The trace target. Interactable only: a button must never stop a bullet or a
	// pawn, and nothing else in the game should know it is there.
	UseVolume = CreateDefaultSubobject<UBoxComponent>(TEXT("UseVolume"));
	UseVolume->SetupAttachment(Root);
	UseVolume->SetBoxExtent(FVector(4.f, 10.f, 14.f));
	UseVolume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	UseVolume->SetCollisionObjectType(ECC_WorldDynamic);
	UseVolume->SetCollisionResponseToAllChannels(ECR_Ignore);
	UseVolume->SetCollisionResponseToChannel(ECC_GameTraceChannel3 /*Interactable*/, ECR_Block);
	UseVolume->SetCanEverAffectNavigation(false);
	UseVolume->SetGenerateOverlapEvents(false);
}

void AOneBrahamLiftPanel::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AOneBrahamLiftPanel, Lift);
	DOREPLIFETIME(AOneBrahamLiftPanel, PanelRole);
	DOREPLIFETIME(AOneBrahamLiftPanel, PanelFloor);
}

void AOneBrahamLiftPanel::InitPanel(AOneBrahamLift* InLift, EOneBrahamLiftPanelRole InRole, int32 InFloor)
{
	Lift = InLift;
	PanelRole = InRole;
	PanelFloor = InFloor;

	// The car buttons are the ones a rider cares about; give them a different
	// faceplate size so they read as a pair rather than as a single call button.
	if (PlateMesh != nullptr && InRole != EOneBrahamLiftPanelRole::Landing)
	{
		PlateMesh->SetRelativeScale3D(FVector(0.03f, 0.12f, 0.14f));
		UseVolume->SetBoxExtent(FVector(4.f, 9.f, 10.f));
	}

	ForceNetUpdate();
}

bool AOneBrahamLiftPanel::IsInteractorAble(const AOneBrahamPlayerController* Interactor)
{
	if (Interactor == nullptr || Interactor->GetPawn() == nullptr)
	{
		return false;
	}

	const AOneBrahamPlayerState* PS = Interactor->GetPlayerState<AOneBrahamPlayerState>();
	return PS == nullptr || !PS->IsEliminated();
}

// ===========================================================================
//  IOneBrahamInteractionInterface
// ===========================================================================

FText AOneBrahamLiftPanel::GetInteractionPrompt_Implementation(const AOneBrahamPlayerController* Interactor) const
{
	if (Lift == nullptr)
	{
		return NSLOCTEXT("OneBraham", "Lift_Offline", "Lift offline");
	}

	if (Lift->IsLockedDown())
	{
		return NSLOCTEXT("OneBraham", "Lift_Lockdown", "Lift out of service - lockdown");
	}

	const UOneBrahamBuildingLayoutAsset* Building = UOneBrahamBuildingLayoutAsset::Get(this);

	if (PanelRole == EOneBrahamLiftPanelRole::Landing)
	{
		if (Lift->IsCarOpenAtFloor(PanelFloor))
		{
			return NSLOCTEXT("OneBraham", "Lift_HoldDoors", "Hold the doors");
		}

		if (Lift->IsFloorCalled(PanelFloor))
		{
			return FText::Format(NSLOCTEXT("OneBraham", "Lift_Called", "Lift called - car at floor {0}"),
				FText::AsNumber(Lift->GetCarFloor()));
		}

		return NSLOCTEXT("OneBraham", "Lift_Call", "Call lift");
	}

	const int32 Direction = PanelRole == EOneBrahamLiftPanelRole::CarUp ? 1 : -1;
	const int32 Next = Lift->GetNextCarSelection(Direction);

	if (Next == INDEX_NONE)
	{
		return Direction > 0
			? NSLOCTEXT("OneBraham", "Lift_TopFloor", "Top floor")
			: NSLOCTEXT("OneBraham", "Lift_BottomFloor", "Bottom floor");
	}

	const FText Where = Building != nullptr ? Building->GetFloorAnnouncement(Next) : FText::AsNumber(Next);

	return FText::Format(Direction > 0
		? NSLOCTEXT("OneBraham", "Lift_Up", "Up - floor {0}: {1}")
		: NSLOCTEXT("OneBraham", "Lift_Down", "Down - floor {0}: {1}"),
		FText::AsNumber(Next), Where);
}

FGameplayTag AOneBrahamLiftPanel::GetInteractionTag_Implementation(const AOneBrahamPlayerController* Interactor) const
{
	return OneBrahamTags::Interaction_Lift;
}

bool AOneBrahamLiftPanel::CanInteract_Implementation(const AOneBrahamPlayerController* Interactor) const
{
	if (Lift == nullptr || Lift->IsLockedDown() || !IsInteractorAble(Interactor))
	{
		return false;
	}

	if (PanelRole == EOneBrahamLiftPanelRole::Landing)
	{
		return true;
	}

	return Lift->GetNextCarSelection(PanelRole == EOneBrahamLiftPanelRole::CarUp ? 1 : -1) != INDEX_NONE;
}

float AOneBrahamLiftPanel::GetInteractionRange_Implementation(const AOneBrahamPlayerController* Interactor) const
{
	return OBLiftPanelInteractionRange;
}

bool AOneBrahamLiftPanel::OnInteractionStart_Implementation(AOneBrahamPlayerController* Interactor)
{
	// Accept the press; the lift decides on release, which is when a tap is
	// complete and when the interact component has re-checked range and facing.
	return HasAuthority() && Lift != nullptr && !Lift->IsLockedDown();
}

void AOneBrahamLiftPanel::OnInteractionEnd_Implementation(AOneBrahamPlayerController* Interactor, bool bCompleted)
{
	if (!bCompleted || !HasAuthority() || Lift == nullptr)
	{
		return;
	}

	switch (PanelRole)
	{
	case EOneBrahamLiftPanelRole::Landing:
		Lift->ServerCallToFloor(PanelFloor, Interactor);
		break;
	case EOneBrahamLiftPanelRole::CarUp:
		Lift->ServerCycleCarDestination(1, Interactor);
		break;
	case EOneBrahamLiftPanelRole::CarDown:
		Lift->ServerCycleCarDestination(-1, Interactor);
		break;
	default:
		break;
	}
}

void AOneBrahamLiftPanel::SetInteractionHighlight_Implementation(bool bHighlighted)
{
	if (PlateMesh != nullptr)
	{
		PlateMesh->SetRenderCustomDepth(bHighlighted);
	}
}
