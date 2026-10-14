// Copyright (c) One Braham. All Rights Reserved.

#include "Access/OneBrahamBreachableWall.h"

#include "OneBraham.h"
#include "Access/OneBrahamAccessZoneComponent.h"
#include "Audio/OneBrahamAudioManagerComponent.h"
#include "Characters/OneBrahamCharacter.h"
#include "Core/OneBrahamGameState.h"
#include "Core/OneBrahamGameplayTags.h"
#include "Core/OneBrahamPlayerController.h"
#include "Core/OneBrahamPlayerState.h"
#include "VFX/OneBrahamVFXStatics.h"
#include "Weapons/OneBrahamInventoryComponent.h"
#include "Weapons/OneBrahamWeaponComponent.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "NavigationSystem.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/**
	 * How close a player has to be to plant a charge on a wall, in centimetres.
	 *
	 * Named rather than written into the return statement because it is quoted in
	 * the interaction table in Docs/13 section 9, and check_interaction.py compares
	 * the two. A number a document quotes is a number that needs a name.
	 */
	constexpr float OBBreachInteractionRange = 260.f;

	/** Utility slot index of the breaching charge; see UOneBrahamInventoryComponent. */
	constexpr int32 BreachChargeUtilityIndex = 3;

	/** Minimum hole a player can walk through without it feeling like a bug. */
	constexpr float MinOpeningWidthCm = 120.f;
	constexpr float MinOpeningHeightCm = 190.f;

	/** Wall material that survives the breach; a small margin either side. */
	constexpr float FramingMarginCm = 20.f;

	FORCEINLINE ECollisionEnabled::Type BlockingCollision()
	{
		return ECollisionEnabled::QueryAndPhysics;
	}
}

// ===========================================================================
//  Construction
// ===========================================================================

AOneBrahamBreachableWall::AOneBrahamBreachableWall()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetReplicateMovement(false);
	SetCanBeDamaged(false);

	// A breached wall is part of the building's fiction for the rest of the
	// match; it is never garbage collected mid-round.
	SetNetCullDistanceSquared(400000000.f);

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Static);
	SetRootComponent(Root);

	auto MakeWallPiece = [this, Root](const TCHAR* Name) -> UStaticMeshComponent*
	{
		UStaticMeshComponent* Piece = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Piece->SetupAttachment(Root);
		Piece->SetMobility(EComponentMobility::Static);
		Piece->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Piece->SetCollisionObjectType(ECC_WorldStatic);
		Piece->SetCollisionResponseToAllChannels(ECR_Block);
		Piece->SetGenerateOverlapEvents(false);
		Piece->SetCanEverAffectNavigation(true);
		Piece->SetCastShadow(true);
		return Piece;
	};

	WallMesh = MakeWallPiece(TEXT("Wall"));
	FrameWest = MakeWallPiece(TEXT("FrameWest"));
	FrameEast = MakeWallPiece(TEXT("FrameEast"));
	FrameLintel = MakeWallPiece(TEXT("FrameLintel"));

	// The frame pieces exist from construction so that the map's navigation mesh
	// and the packaging pass both know about them, but they do not block, do not
	// render and do not affect navigation until the wall opens.
	for (UStaticMeshComponent* Frame : { FrameWest, FrameEast, FrameLintel })
	{
		Frame->SetVisibility(false);
		Frame->SetHiddenInGame(true);
		Frame->SetCanEverAffectNavigation(false);
	}

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeFinder.Succeeded())
	{
		WallMesh->SetStaticMesh(CubeFinder.Object);
		FrameWest->SetStaticMesh(CubeFinder.Object);
		FrameEast->SetStaticMesh(CubeFinder.Object);
		FrameLintel->SetStaticMesh(CubeFinder.Object);
	}

	// Sensible defaults for a 600 x 25 x 400 partition with a 140 cm door in it.
	WallMesh->SetRelativeScale3D(ScaleForCm(WallDimensionsCm));
}

void AOneBrahamBreachableWall::BeginPlay()
{
	Super::BeginPlay();

	RemainingIntegrity = BreachIntegrity;

	if (WallMaterial != nullptr)
	{
		for (UStaticMeshComponent* Piece : { WallMesh, FrameWest, FrameEast, FrameLintel })
		{
			Piece->SetMaterial(0, WallMaterial);
		}
	}

	RebuildGeometry();

	// A wall that starts breached is a wall somebody opened before we got here.
	if (bStartsBreached)
	{
		bBreached = true;
	}

	ApplyBreachedState();

	OB_ACCESS_LOG("Breachable wall '%s' online: floor %d, %s, integrity %.0f at %.2f resistance.",
		*WallId.ToString(), FloorIndex, *SurfaceId.ToString(), BreachIntegrity, BreachResistance);
}

void AOneBrahamBreachableWall::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AOneBrahamBreachableWall, bBreached);
	DOREPLIFETIME(AOneBrahamBreachableWall, RemainingIntegrity);
}

// ===========================================================================
//  Geometry
// ===========================================================================

void AOneBrahamBreachableWall::SetWallDimensions(const FVector& InDimensionsCm, bool bApplyImmediately)
{
	WallDimensionsCm = FVector(
		FMath::Max(40.f, InDimensionsCm.X),
		FMath::Max(8.f, InDimensionsCm.Y),
		FMath::Max(120.f, InDimensionsCm.Z));

	if (bApplyImmediately)
	{
		RebuildGeometry();
	}
}

FVector2D AOneBrahamBreachableWall::GetClampedOpeningSize() const
{
	// Leave at least FramingMarginCm of wall on each side of the hole, and never
	// make a hole smaller than a person: the clamp is the difference between a
	// shortcut and a bug report.
	const float MaxWidth = FMath::Max(MinOpeningWidthCm, WallDimensionsCm.X - FramingMarginCm * 2.f);
	const float MaxHeight = FMath::Max(MinOpeningHeightCm, WallDimensionsCm.Z - FramingMarginCm);

	return FVector2D(
		FMath::Clamp(static_cast<float>(OpeningSizeCm.X), FMath::Min(MinOpeningWidthCm, MaxWidth), MaxWidth),
		FMath::Clamp(static_cast<float>(OpeningSizeCm.Y), FMath::Min(MinOpeningHeightCm, MaxHeight), MaxHeight));
}

void AOneBrahamBreachableWall::RebuildGeometry()
{
	const float Thickness = WallDimensionsCm.Y;
	const FVector2D Opening = GetClampedOpeningSize();

	WallMesh->SetRelativeLocation(FVector::ZeroVector);
	WallMesh->SetRelativeScale3D(ScaleForCm(WallDimensionsCm));

	const float JambWidth = FMath::Max(0.f, (WallDimensionsCm.X - static_cast<float>(Opening.X)) * 0.5f);
	const float LintelHeight = FMath::Max(0.f, WallDimensionsCm.Z - static_cast<float>(Opening.Y));
	const float JambOffset = static_cast<float>(Opening.X) * 0.5f + JambWidth * 0.5f;

	// Two jambs, full height, either side of the hole.
	FrameWest->SetRelativeLocation(FVector(-JambOffset, 0.f, 0.f));
	FrameWest->SetRelativeScale3D(ScaleForCm(FVector(JambWidth, Thickness, WallDimensionsCm.Z)));

	FrameEast->SetRelativeLocation(FVector(JambOffset, 0.f, 0.f));
	FrameEast->SetRelativeScale3D(ScaleForCm(FVector(JambWidth, Thickness, WallDimensionsCm.Z)));

	// The lintel: everything above the hole. Its centre sits at half the opening
	// height, which is where the wall's own origin puts the top of the opening.
	FrameLintel->SetRelativeLocation(FVector(0.f, 0.f, static_cast<float>(Opening.Y) * 0.5f));
	FrameLintel->SetRelativeScale3D(ScaleForCm(FVector(static_cast<float>(Opening.X), Thickness, LintelHeight)));
}

FVector AOneBrahamBreachableWall::GetWallNormal() const
{
	// The wall's local Y is its thickness, so the normal is the actor's Y axis.
	return GetActorRotation().RotateVector(FVector(0.f, 1.f, 0.f)).GetSafeNormal();
}

FVector AOneBrahamBreachableWall::GetWallCentre() const
{
	return GetActorLocation();
}

// ===========================================================================
//  Breaching
// ===========================================================================

void AOneBrahamBreachableWall::ApplyBreachDamage(float Damage, AActor* Breacher, const FVector& Origin)
{
	if (!HasAuthority() || bBreached || Damage <= 0.f)
	{
		return;
	}

	const float Effective = Damage * FMath::Clamp(1.f - BreachResistance, 0.05f, 1.f);
	RemainingIntegrity = FMath::Max(0.f, RemainingIntegrity - Effective);

	const FVector Direction = (GetWallCentre() - Origin).GetSafeNormal();
	const FVector DebrisOrigin = FMath::IsNearlyZero(Direction.SizeSquared())
		? GetWallCentre()
		: Origin + Direction * FMath::Min(FVector::Dist(Origin, GetWallCentre()), 120.f);

	// A charge that does not open the wall still takes a bite out of it: the
	// player can see the progress on the surface, which is how they learn that
	// one charge is not always enough.
	UOneBrahamVFXStatics::SpawnBreachDebris(this, DebrisOrigin, Direction);
	UOneBrahamVFXStatics::SpawnEffect(this, EOneBrahamVFXEffect::DustPuff, DebrisOrigin, FRotator::ZeroRotator, FVector(1.4f));

	OB_ACCESS_LOG("Breach on wall '%s' (floor %d): %.0f raw, %.0f effective, %.0f of %.0f left.",
		*WallId.ToString(), FloorIndex, Damage, Effective, RemainingIntegrity, BreachIntegrity);

	ForceNetUpdate();

	if (RemainingIntegrity > 0.f)
	{
		return;
	}

	ServerBreach(Breacher);
}

void AOneBrahamBreachableWall::ServerBreach(AActor* Breacher)
{
	if (!HasAuthority() || bBreached)
	{
		return;
	}

	bBreached = true;
	RemainingIntegrity = 0.f;

	ApplyBreachedState();

	// Every client sees and hears the hole appear, and the local player hears it
	// on their own mix so the noise is positioned rather than a 2D stinger.
	MulticastPlayBreachEffects(GetWallCentre());

	if (bAnnounceOnBreach)
	{
		if (AOneBrahamGameState* GS = GetWorld() != nullptr ? GetWorld()->GetGameState<AOneBrahamGameState>() : nullptr)
		{
			// Deliberately flat: this is a building notice, not a kill feed line.
			GS->MulticastAnnounce(
				FText::Format(
					NSLOCTEXT("OneBraham", "BreachAnnounce", "Structural alarm: floor {0}, partition breached."),
					FText::AsNumber(FloorIndex)),
				/*bPlayTannoySound*/ true);
		}
	}

	OnWallBreached.Broadcast(this, Breacher);
	OnWallOpenedLocally(Breacher);

	ForceNetUpdate();

	OB_ACCESS_LOG("Wall '%s' on floor %d is open; navigable hole is active.",
		*WallId.ToString(), FloorIndex);
}

void AOneBrahamBreachableWall::ApplyBreachedState()
{
	const bool bWhole = !bBreached;

	// The whole wall: visible and blocking only while it is whole.
	WallMesh->SetVisibility(bWhole);
	WallMesh->SetHiddenInGame(!bWhole);
	WallMesh->SetCollisionEnabled(bWhole ? BlockingCollision() : ECollisionEnabled::NoCollision);
	WallMesh->SetCanEverAffectNavigation(bWhole);

	// The frame: the opposite, and it is what actually blocks once opened.
	for (UStaticMeshComponent* Frame : { FrameWest, FrameEast, FrameLintel })
	{
		Frame->SetVisibility(!bWhole);
		Frame->SetHiddenInGame(bWhole);
		Frame->SetCollisionEnabled(bWhole ? ECollisionEnabled::NoCollision : BlockingCollision());
		Frame->SetCanEverAffectNavigation(!bWhole);
	}

	// Navigation has to be told, because the geometry changed after the mesh was
	// built. Without this the AI walk into a wall that is no longer there, which
	// is the single most-reported bug in every game that has ever shipped a
	// breachable wall.
	if (UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
	{
		FBox Bounds = FBox::BuildAABB(GetWallCentre(), FVector::ZeroVector);
		Bounds += WallMesh->Bounds.GetBox();
		Bounds += FrameLintel->Bounds.GetBox();

		Nav->AddDirtyArea(Bounds.ExpandBy(80.f), ENavigationDirtyFlag::All);
	}

	ForceNetUpdate();
}

void AOneBrahamBreachableWall::MulticastPlayBreachEffects_Implementation(const FVector& Origin)
{
	// Server-side callers already produced the debris during ApplyBreachDamage;
	// on a dedicated server there is nothing to see, so the effects only run
	// where there is a viewer.
	if (UOneBrahamVFXStatics::ShouldSpawnForLocalViewer(this, Origin, 25000.f) == false)
	{
		return;
	}

	UOneBrahamVFXStatics::SpawnExplosion(this, Origin, 320.f, /*bBreach*/ true);
	UOneBrahamVFXStatics::SpawnEffect(this, EOneBrahamVFXEffect::BreachExplosion, Origin);
	UOneBrahamVFXStatics::SpawnEffect(this, EOneBrahamVFXEffect::BreachDebris, Origin, FRotator::ZeroRotator, FVector(1.6f));

	// A multicast can arrive while the world is being torn down - the extraction
	// completes, the server starts travelling, and a charge that was already in
	// the air detonates into a dying world. GetPlayerControllerIterator() derefs
	// the world, so the guard is load-bearing rather than defensive.
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	// Audio through the local player's manager so the mix, the duck and the
	// occlusion all apply exactly as they do for a door being breached.
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		AOneBrahamPlayerController* PC = Cast<AOneBrahamPlayerController>(It->Get());
		if (PC == nullptr || !PC->IsLocalController())
		{
			continue;
		}

		// The charge that did this played its own blast; the wall adds its fall.
		if (UOneBrahamAudioManagerComponent* Audio = PC->GetAudioManager())
		{
			Audio->PlaySoundAtLocation(FName(TEXT("SFX_Wall_Collapse")), Origin, 1.1f, 0.96f, this);
		}
	}
}

void AOneBrahamBreachableWall::OnRep_Breached()
{
	ApplyBreachedState();
	OnWallOpenedLocally(nullptr);
}

// ===========================================================================
//  Interaction: "you need a charge, not a card"
// ===========================================================================

FText AOneBrahamBreachableWall::GetInteractionPrompt_Implementation(const AOneBrahamPlayerController* Interactor) const
{
	if (bBreached)
	{
		// An open hole is scenery. Prompting on it would be noise.
		return FText::GetEmpty();
	}

	const AOneBrahamCharacter* Character = Interactor != nullptr ? Interactor->GetOneBrahamCharacter() : nullptr;
	const UOneBrahamInventoryComponent* Inventory = Character != nullptr ? Character->GetInventoryComponent() : nullptr;

	if (Inventory != nullptr && Inventory->Utility.BreachChargeCount > 0)
	{
		return NSLOCTEXT("OneBraham", "BreachWall_Plant", "Plant breaching charge");
	}

	return NSLOCTEXT("OneBraham", "BreachWall_NoCharge", "Partition - breaching charge required");
}

FGameplayTag AOneBrahamBreachableWall::GetInteractionTag_Implementation(const AOneBrahamPlayerController* Interactor) const
{
	return OneBrahamTags::Interaction_Door_Breachable;
}

bool AOneBrahamBreachableWall::CanInteract_Implementation(const AOneBrahamPlayerController* Interactor) const
{
	if (bBreached || Interactor == nullptr)
	{
		return false;
	}

	const AOneBrahamCharacter* Character = Interactor->GetOneBrahamCharacter();
	const UOneBrahamInventoryComponent* Inventory = Character != nullptr ? Character->GetInventoryComponent() : nullptr;

	return Inventory != nullptr && Inventory->Utility.BreachChargeCount > 0;
}

bool AOneBrahamBreachableWall::RequiresHold_Implementation(const AOneBrahamPlayerController* Interactor) const
{
	// Planting is a hold: a player who taps and walks off has not committed, and
	// the charge is not spent.
	return true;
}

float AOneBrahamBreachableWall::GetHoldDuration_Implementation(const AOneBrahamPlayerController* Interactor) const
{
	return PlantSeconds;
}

float AOneBrahamBreachableWall::GetInteractionRange_Implementation(const AOneBrahamPlayerController* Interactor) const
{
	// Longer than a door: a wall is a wall, and the player is usually standing
	// back from it slightly to see the whole thing.
	return OBBreachInteractionRange;
}

bool AOneBrahamBreachableWall::OnInteractionStart_Implementation(AOneBrahamPlayerController* Interactor)
{
	if (!HasAuthority() || bBreached || !CanInteract_Implementation(Interactor))
	{
		return false;
	}

	OB_ACCESS_LOG("Player %d is planting a charge on wall '%s'.",
		Interactor->GetPlayerState<AOneBrahamPlayerState>() != nullptr
			? Interactor->GetPlayerState<AOneBrahamPlayerState>()->GetPlayerId()
			: INDEX_NONE,
		*WallId.ToString());

	return true;
}

void AOneBrahamBreachableWall::OnInteractionEnd_Implementation(AOneBrahamPlayerController* Interactor, bool bCompleted)
{
	if (!bCompleted || !HasAuthority() || bBreached || Interactor == nullptr)
	{
		return;
	}

	AOneBrahamCharacter* Character = Interactor->GetOneBrahamCharacter();
	UOneBrahamInventoryComponent* Inventory = Character != nullptr ? Character->GetInventoryComponent() : nullptr;
	UOneBrahamWeaponComponent* Weapons = Character != nullptr ? Character->GetWeaponComponent() : nullptr;

	if (Inventory == nullptr || Weapons == nullptr)
	{
		return;
	}

	// ServerPlaceUtility does its own trace, its own cost check and its own
	// spawn: the wall only asks for the place to happen. If the player has moved
	// so that the charge would land somewhere else, the inventory refuses and
	// nothing is spent.
	if (!Inventory->ServerPlaceUtility(Weapons, BreachChargeUtilityIndex))
	{
		OB_ACCESS_WARN("Charge placement refused on wall '%s' for player %d.",
			*WallId.ToString(),
			Interactor->GetPlayerState<AOneBrahamPlayerState>() != nullptr
				? Interactor->GetPlayerState<AOneBrahamPlayerState>()->GetPlayerId()
				: INDEX_NONE);
	}
}

void AOneBrahamBreachableWall::SetInteractionHighlight_Implementation(bool bHighlightedIn)
{
	bHighlighted = bHighlightedIn;

	// Only the whole wall gets an outline: outlining an open hole would suggest
	// the hole is a thing that can be used, and it cannot.
	const bool bOutline = bHighlightedIn && !bBreached;

	for (UStaticMeshComponent* Piece : { WallMesh, FrameWest, FrameEast, FrameLintel })
	{
		Piece->SetRenderCustomDepth(bOutline);
		Piece->SetCustomDepthStencilValue(1);
	}
}
