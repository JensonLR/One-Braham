// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamAccessZone.cpp

#include "Access/OneBrahamAccessZone.h"

#include "OneBraham.h"
#include "Access/OneBrahamAccessZoneComponent.h"
#include "Audio/OneBrahamAudioManagerComponent.h"
#include "Characters/OneBrahamCharacter.h"
#include "Core/OneBrahamGameplayTags.h"
#include "Core/OneBrahamGameState.h"
#include "Core/OneBrahamPlayerController.h"
#include "Core/OneBrahamPlayerState.h"
#include "Lockdown/OneBrahamLockdownComponent.h"
#include "VFX/OneBrahamVFXStatics.h"
#include "Weapons/OneBrahamInventoryComponent.h"

#include "Components/BoxComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "NavigationSystem.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/** Reader light colours. Red is the only one that has to be unmistakable. */
	const FLinearColor OBReaderLocked(1.0f, 0.04f, 0.03f);
	const FLinearColor OBReaderOverride(1.0f, 0.62f, 0.02f);
	const FLinearColor OBReaderOpen(0.05f, 0.85f, 0.32f);
	const FLinearColor OBReaderBreached(0.55f, 0.05f, 0.55f);

	/** How long a refusal stays on the reader before it goes back to red. */
	constexpr float OBReaderRefusalSeconds = 1.6f;

	/** Distance the interaction trace can reach the reader from. */
	constexpr float OBDoorInteractionRange = 240.f;

	/** A door that is 2 cm from open is not open. This is the blocking threshold. */
	constexpr float OBDoorBlockingThreshold = 0.12f;
}

AOneBrahamAccessZone::AOneBrahamAccessZone()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.f;

	bReplicates = true;
	SetReplicateMovement(false);

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	FrameMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FrameMesh"));
	FrameMesh->SetupAttachment(Root);
	FrameMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FrameMesh->SetCanEverAffectNavigation(false);

	LeafLeftMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LeafLeftMesh"));
	LeafLeftMesh->SetupAttachment(Root);
	LeafLeftMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	LeafLeftMesh->SetCanEverAffectNavigation(false);

	LeafRightMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LeafRightMesh"));
	LeafRightMesh->SetupAttachment(Root);
	LeafRightMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	LeafRightMesh->SetCanEverAffectNavigation(false);

	BarrierVolume = CreateDefaultSubobject<UBoxComponent>(TEXT("BarrierVolume"));
	BarrierVolume->SetupAttachment(Root);
	BarrierVolume->SetBoxExtent(FVector(110.f, 15.f, 110.f));
	BarrierVolume->SetRelativeLocation(FVector(0.f, 0.f, 110.f));
	BarrierVolume->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	BarrierVolume->SetCollisionObjectType(ECC_WorldStatic);
	BarrierVolume->SetCollisionResponseToAllChannels(ECR_Ignore);
	BarrierVolume->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	BarrierVolume->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	// Bullets and grenade traces are handled by the trace channels, not by this
	// box: a closed door stops people, not lead, unless the door is armour. The
	// gate that does stop lead is a separate mesh with its own profile.
	BarrierVolume->SetCanEverAffectNavigation(true);

	ReaderMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ReaderMesh"));
	ReaderMesh->SetupAttachment(Root);
	ReaderMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ReaderMesh->SetRelativeLocation(FVector(115.f, -20.f, 125.f));
	ReaderMesh->SetRelativeScale3D(FVector(0.18f, 0.06f, 0.28f));

	ReaderLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("ReaderLight"));
	ReaderLight->SetupAttachment(ReaderMesh);
	ReaderLight->SetRelativeLocation(FVector(-6.f, -10.f, 0.f));
	ReaderLight->SetIntensity(320.f);
	ReaderLight->SetAttenuationRadius(260.f);
	ReaderLight->SetLightColor(OBReaderLocked);
	ReaderLight->SetCastShadows(false);
	ReaderLight->SetMobility(EComponentMobility::Movable);

	Zone = CreateDefaultSubobject<UOneBrahamAccessZoneComponent>(TEXT("Zone"));
	Zone->SetupAttachment(Root);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeFinder.Succeeded())
	{
		FrameMesh->SetStaticMesh(CubeFinder.Object);
		LeafLeftMesh->SetStaticMesh(CubeFinder.Object);
		LeafRightMesh->SetStaticMesh(CubeFinder.Object);
		ReaderMesh->SetStaticMesh(CubeFinder.Object);
	}

	FrameMesh->SetRelativeScale3D(FVector(2.4f, 0.22f, 0.12f));
	FrameMesh->SetRelativeLocation(FVector(0.f, 0.f, 222.f));

	// Two leaves, meeting in the middle of a 220 cm opening.
	LeafLeftMesh->SetRelativeScale3D(FVector(1.06f, 0.06f, 2.1f));
	LeafLeftMesh->SetRelativeLocation(FVector(-55.f, 0.f, 105.f));
	LeafRightMesh->SetRelativeScale3D(FVector(1.06f, 0.06f, 2.1f));
	LeafRightMesh->SetRelativeLocation(FVector(55.f, 0.f, 105.f));
}

void AOneBrahamAccessZone::BeginPlay()
{
	Super::BeginPlay();

	ClosedLeftLocation = LeafLeftMesh->GetRelativeLocation();
	ClosedRightLocation = LeafRightMesh->GetRelativeLocation();
	ClosedLeftRotation = LeafLeftMesh->GetRelativeRotation();
	ClosedRightRotation = LeafRightMesh->GetRelativeRotation();

	RemainingIntegrity = BreachIntegrity;

	bOpen = bStartsOpen || (Zone != nullptr && Zone->IsUnlocked());
	OpenAlpha = bOpen ? 1.f : 0.f;
	ApplyOpenAlpha(OpenAlpha);

	if (Zone != nullptr)
	{
		ApplyPlacementToZone();
		Zone->OnZoneChanged.AddDynamic(this, &AOneBrahamAccessZone::HandleZoneChanged);
	}

	// The reader's state has to be right on the very first frame, including for a
	// client that joins a match where this door is already open.
	ReaderLight->SetLightColor(bBreached ? OBReaderBreached : (bOpen ? OBReaderOpen : OBReaderLocked));

	OB_ACCESS_LOG("Door '%s' online: floor %d, tier %s, %s",
		*GetZoneId().ToString(),
		Zone != nullptr ? Zone->FloorIndex : INDEX_NONE,
		Zone != nullptr ? *UEnum::GetValueAsString(Zone->RequiredTier) : TEXT("?"),
		bOpen ? "open" : "locked");
}

void AOneBrahamAccessZone::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AOneBrahamAccessZone, bOpen);
	DOREPLIFETIME(AOneBrahamAccessZone, bBreached);
}

FName AOneBrahamAccessZone::GetZoneId() const
{
	if (Zone != nullptr && !Zone->ZoneId.IsNone())
	{
		return Zone->ZoneId;
	}

	return ZoneId;
}

void AOneBrahamAccessZone::ApplyPlacementToZone()
{
	if (Zone == nullptr)
	{
		return;
	}

	// The actor's placement values win on BeginPlay, because they are what the
	// map (and the map builder) authored. A zone with no id set on either object
	// gets one derived from the actor's name, so a stray door in a test map is
	// still identifiable in the logs and on the HUD.
	if (!ZoneId.IsNone())
	{
		Zone->ZoneId = ZoneId;
	}
	else if (Zone->ZoneId.IsNone())
	{
		Zone->ZoneId = GetFName();
	}

	if (!ZoneDisplayName.IsEmpty())
	{
		Zone->ZoneDisplayName = ZoneDisplayName;
	}

	Zone->RequiredTier = RequiredTier;
	Zone->FloorIndex = FloorIndex;
	Zone->bEscalationOpens = bEscalationOpens;
	Zone->bBreachable = bBreachable;
	Zone->bUnlockedAtStart = bUnlockedAtStart;

	if (!PairedZoneId.IsNone())
	{
		Zone->PairedZoneId = PairedZoneId;
	}

	// The door is the reader: the component's own override timing is what the
	// panel uses, and the two-person rule is expressed as a pair of zone ids.
	OB_ACCESS_LOG("Door '%s' placement applied: tier %s, floor %d, pair '%s'.",
		*Zone->ZoneId.ToString(), *UEnum::GetValueAsString(Zone->RequiredTier),
		Zone->FloorIndex, *Zone->PairedZoneId.ToString());
}

void AOneBrahamAccessZone::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	ReaderStateTime += DeltaSeconds;

	const float Target = bOpen ? 1.f : 0.f;
	if (!FMath::IsNearlyEqual(OpenAlpha, Target, 0.001f))
	{
		const float Step = DeltaSeconds / FMath::Max(0.05f, DoorOpenSeconds);
		OpenAlpha = (Target > OpenAlpha)
			? FMath::Min(OpenAlpha + Step, Target)
			: FMath::Max(OpenAlpha - Step, Target);

		ApplyOpenAlpha(OpenAlpha);
	}

	// Reader light: idle red, amber while a panel is being held, green when open,
	// and a short bright flash on a refusal so the player knows it was the door
	// saying no and not their input.
	if (ReaderLight != nullptr)
	{
		FLinearColor Desired = bBreached ? OBReaderBreached : (bOpen ? OBReaderOpen : OBReaderLocked);
		float Intensity = bOpen ? 420.f : 320.f;

		if (!bOpen && Zone != nullptr && (Zone->IsOverrideInProgress() || IsPairedPanelHeld()))
		{
			Desired = OBReaderOverride;
			Intensity = 900.f;
		}

		if (bHighlighted && !bOpen)
		{
			Intensity *= 1.6f;
		}

		ReaderLight->SetLightColor(Desired);
		ReaderLight->SetIntensity(FMath::FInterpTo(ReaderLight->Intensity, Intensity, DeltaSeconds, 10.f));
	}
}

// ===========================================================================
//  Presentation
// ===========================================================================

void AOneBrahamAccessZone::ApplyOpenAlpha(float Alpha)
{
	const float Sign = bSwingDoors ? 1.f : 0.f;
	const float Swing = bSwingDoors ? DoorSwingAngle * Alpha : 0.f;

	if (bSwingDoors)
	{
		// Swing: the leaves rotate about their outer edges. The mesh origin is the
		// hinge, which is why the leaves are positioned at the frame edges above.
		LeafLeftMesh->SetRelativeRotation(ClosedLeftRotation + FRotator(0.f, -Swing, 0.f));
		LeafRightMesh->SetRelativeRotation(ClosedRightRotation + FRotator(0.f, Swing, 0.f));
	}
	else
	{
		// Slide: the standard for lifts and stairwell fire doors in this building.
		const float Travel = 96.f * Alpha;
		LeafLeftMesh->SetRelativeLocation(ClosedLeftLocation - FVector(Travel, 0.f, 0.f));
		LeafRightMesh->SetRelativeLocation(ClosedRightLocation + FVector(Travel, 0.f, 0.f));
	}

	// Collision leads the animation slightly on close and lags it on open, so a
	// player cannot clip through the last centimetres of a closing door and the
	// AI does not path into a doorway that is still physically shut.
	const bool bShouldBlock = Alpha < (1.f - OBDoorBlockingThreshold);
	const ECollisionEnabled::Type Desired = bShouldBlock
		? ECollisionEnabled::QueryAndPhysics
		: ECollisionEnabled::NoCollision;

	if (BarrierVolume->GetCollisionEnabled() != Desired)
	{
		BarrierVolume->SetCollisionEnabled(Desired);

		if (UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
		{
			Nav->AddDirtyArea(BarrierVolume->Bounds.GetBox(), ENavigationDirtyFlag::All);
		}
	}
}

void AOneBrahamAccessZone::HandleZoneChanged(FName InZoneId, bool bUnlocked, EOneBrahamUnlockMethod Method)
{
	// This fires on every machine, from the zone component's own OnRep/Multicast,
	// so one path drives the leaves, the reader and the audio everywhere.
	bOpen = bUnlocked;
	ForceNetUpdate();

	if (bUnlocked)
	{
		PlayDoorEffects(Method, true);
	}

	OB_ACCESS_LOG("Door '%s' -> %s (%s)",
		*InZoneId.ToString(), bUnlocked ? "open" : "shut", *UEnum::GetValueAsString(Method));
}

void AOneBrahamAccessZone::OnRep_DoorState()
{
	PlayDoorEffects(EOneBrahamUnlockMethod::None, bOpen);
	OnDoorStateChanged.Broadcast(GetZoneId(), bOpen);
}

void AOneBrahamAccessZone::PlayDoorEffects(EOneBrahamUnlockMethod Method, bool bUnlocked)
{
	if (ReaderLight != nullptr)
	{
		ReaderLight->SetLightColor(bUnlocked ? OBReaderOpen : OBReaderLocked);
	}

	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	const FVector Location = GetBarrierCentre();

	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		AOneBrahamPlayerController* PC = Cast<AOneBrahamPlayerController>(It->Get());
		UOneBrahamAudioManagerComponent* Audio = GetAudioFor(PC);
		if (Audio == nullptr)
		{
			continue;
		}

		if (bUnlocked)
		{
			switch (Method)
			{
			case EOneBrahamUnlockMethod::Keycard:
				Audio->PlaySoundAtLocation(FName(TEXT("SFX_Keycard_Use")), Location, 0.9f, 1.f, this);
				break;
			case EOneBrahamUnlockMethod::Breach:
				// The charge that opened it played the blast where it went off.
				break;
			case EOneBrahamUnlockMethod::Escalation:
				// The building does it for you: no click, just the mechanism.
				break;
			default:
				Audio->PlaySoundAtLocation(FName(TEXT("SFX_Terminal_Accept")), Location, 0.8f, 1.f, this);
				break;
			}

			Audio->PlaySoundAtLocation(FName(TEXT("SFX_Door_Open")), Location, 0.85f, 1.f, this);
		}
		else
		{
			Audio->PlaySoundAtLocation(FName(TEXT("SFX_Door_Close")), Location, 0.8f, 1.f, this);
		}
	}

	// Dust falls out of the frame when a door that has been shut since the start of
	// the match finally moves. It is one of those details that makes the building
	// feel like a building rather than a level.
	UOneBrahamVFXStatics::SpawnEffect(this, EOneBrahamVFXEffect::DustPuff, Location,
		FRotator::ZeroRotator, FVector(0.8f));
}

UOneBrahamAudioManagerComponent* AOneBrahamAccessZone::GetAudioFor(const AOneBrahamPlayerController* PC)
{
	return PC != nullptr ? PC->GetAudioManager() : nullptr;
}

FVector AOneBrahamAccessZone::GetBarrierCentre() const
{
	return BarrierVolume != nullptr
		? BarrierVolume->GetComponentLocation()
		: GetActorLocation();
}

// ===========================================================================
//  Zones
// ===========================================================================

AOneBrahamAccessZone* AOneBrahamAccessZone::FindZoneById(const UObject* WorldContextObject, FName InZoneId)
{
	const UWorld* World = GEngine != nullptr && WorldContextObject != nullptr
		? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
		: nullptr;

	if (World == nullptr || InZoneId.IsNone())
	{
		return nullptr;
	}

	for (TActorIterator<AOneBrahamAccessZone> It(const_cast<UWorld*>(World)); It; ++It)
	{
		if (It->GetZoneId() == InZoneId)
		{
			return *It;
		}
	}

	return nullptr;
}

void AOneBrahamAccessZone::GetAllZones(const UObject* WorldContextObject, TArray<AOneBrahamAccessZone*>& OutZones)
{
	OutZones.Reset();

	const UWorld* World = GEngine != nullptr && WorldContextObject != nullptr
		? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
		: nullptr;

	if (World == nullptr)
	{
		return;
	}

	for (TActorIterator<AOneBrahamAccessZone> It(const_cast<UWorld*>(World)); It; ++It)
	{
		OutZones.Add(*It);
	}
}

AOneBrahamAccessZone* AOneBrahamAccessZone::FindPairedZone() const
{
	if (Zone == nullptr || Zone->PairedZoneId.IsNone())
	{
		return nullptr;
	}

	return FindZoneById(this, Zone->PairedZoneId);
}

bool AOneBrahamAccessZone::HasBadgeFor(const AOneBrahamPlayerController* Interactor) const
{
	if (Interactor == nullptr || Zone == nullptr)
	{
		return false;
	}

	const AOneBrahamCharacter* Character = Cast<AOneBrahamCharacter>(Interactor->GetPawn());
	if (Character == nullptr)
	{
		return false;
	}

	const UOneBrahamInventoryComponent* Inventory = Character->GetInventoryComponent();
	return Inventory != nullptr && Inventory->HasKeycardForTier(Zone->RequiredTier);
}

bool AOneBrahamAccessZone::IsInteractorEliminated(const AOneBrahamPlayerController* Interactor)
{
	if (Interactor == nullptr)
	{
		return true;
	}

	const AOneBrahamPlayerState* PS = Interactor->GetPlayerState<AOneBrahamPlayerState>();
	return PS != nullptr && PS->IsEliminated();
}

bool AOneBrahamAccessZone::IsPairedPanelHeld() const
{
	const AOneBrahamAccessZone* Partner = FindPairedZone();
	if (Partner == nullptr || Partner->Zone == nullptr)
	{
		return false;
	}

	return Partner->Zone->IsOverrideInProgress();
}

// ===========================================================================
//  Interaction
// ===========================================================================

FText AOneBrahamAccessZone::GetInteractionPrompt_Implementation(const AOneBrahamPlayerController* Interactor) const
{
	if (Zone == nullptr)
	{
		return FText::GetEmpty();
	}

	if (bOpen)
	{
		return FText::GetEmpty(); // An open door is furniture.
	}

	return Zone->GetInteractionPrompt(Interactor);
}

FGameplayTag AOneBrahamAccessZone::GetInteractionTag_Implementation(const AOneBrahamPlayerController* Interactor) const
{
	if (Zone == nullptr)
	{
		return OneBrahamTags::Interaction_Door;
	}

	// A badge door and a forced door are different verbs, and the HUD shows a
	// different icon for each. The tag is what tells it which.
	if (Zone->RequiredTier == EOneBrahamAccessTier::Override)
	{
		return OneBrahamTags::Interaction_Terminal;
	}

	if (Zone->RequiredTier == EOneBrahamAccessTier::Secure ||
		Zone->RequiredTier == EOneBrahamAccessTier::Executive ||
		Zone->RequiredTier == EOneBrahamAccessTier::Skydeck)
	{
		return OneBrahamTags::Interaction_Door_Secure;
	}

	return bBreachable
		? OneBrahamTags::Interaction_Door_Breachable
		: OneBrahamTags::Interaction_Door;
}

bool AOneBrahamAccessZone::CanInteract_Implementation(const AOneBrahamPlayerController* Interactor) const
{
	if (bOpen || Zone == nullptr || Interactor == nullptr || IsInteractorEliminated(Interactor))
	{
		return false;
	}

	// Always allowed to *try*: the refusal is part of the information the player
	// needs. The prompt tells them which badge they are missing, and that is how
	// they learn to go and find the person who has it.
	return true;
}

bool AOneBrahamAccessZone::RequiresHold_Implementation(const AOneBrahamPlayerController* Interactor) const
{
	if (bOpen || Zone == nullptr || !bHasOnboardPanel)
	{
		return false;
	}

	// Tap if you have the badge; hold if you are forcing it. That difference is
	// the whole point: taking a badge off a body is instant, forcing a door is 12
	// seconds of standing still in a corridor.
	return !HasBadgeFor(Interactor);
}

float AOneBrahamAccessZone::GetHoldDuration_Implementation(const AOneBrahamPlayerController* Interactor) const
{
	if (Zone == nullptr)
	{
		return 0.f;
	}

	return Zone->GetOverrideSecondsFor(Interactor);
}

float AOneBrahamAccessZone::GetInteractionRange_Implementation(const AOneBrahamPlayerController* Interactor) const
{
	// A reader panel is reachable from a step further away than a pickup, because
	// players approach a door at a diagonal and expecting pixel-perfect aim on a
	// card reader is a bad night.
	return OBDoorInteractionRange;
}

bool AOneBrahamAccessZone::OnInteractionStart_Implementation(AOneBrahamPlayerController* Interactor)
{
	if (Interactor == nullptr || !HasAuthority() || Zone == nullptr || bOpen)
	{
		return false;
	}

	if (HasBadgeFor(Interactor))
	{
		const AOneBrahamPlayerState* PS = Interactor->GetPlayerState<AOneBrahamPlayerState>();
		const int32 PlayerId = PS != nullptr ? PS->GetPlayerId() : INDEX_NONE;

		const bool bUnlocked = Zone->ServerUnlock(EOneBrahamUnlockMethod::Keycard, PlayerId);
		if (bUnlocked)
		{
			OB_ACCESS_LOG("Door '%s' opened by badge (player %d).", *GetZoneId().ToString(), PlayerId);
			return true;
		}

		return false;
	}

	if (!bHasOnboardPanel)
	{
		// No panel on this door: the refusal is the whole interaction. The prompt
		// has already told the player what they need, and the log records who tried.
		OB_ACCESS_LOG("Door '%s' refused: tier %s, no onboard panel. %s",
			*GetZoneId().ToString(),
			*UEnum::GetValueAsString(Zone->RequiredTier),
			*Zone->GetRefusalText(Interactor).ToString());

		if (UOneBrahamAudioManagerComponent* Audio = GetAudioFor(Interactor))
		{
			Audio->PlaySoundAtLocation(FName(TEXT("SFX_Door_Locked")), GetBarrierCentre(), 0.8f, 1.f, this);
			Audio->PlaySoundAtLocation(FName(TEXT("SFX_Terminal_Deny")), GetBarrierCentre(), 0.7f, 1.f, this);
		}

		return false;
	}

	// Two-person rule: if the partner panel is held right now, this one takes 3
	// seconds. The check is re-evaluated every tick of the hold, not once here, so
	// a partner who arrives late still speeds it up.
	HoldInteractor = Interactor;
	HoldLastSeconds = 0.f;

	UOneBrahamAudioManagerComponent* Audio = GetAudioFor(Interactor);
	if (Audio != nullptr)
	{
		Audio->PlaySoundAtLocation(FName(TEXT("SFX_Terminal_Hum")), GetBarrierCentre(), 0.7f, 1.f, this);
	}

	return true;
}

void AOneBrahamAccessZone::OnInteractionHold_Implementation(AOneBrahamPlayerController* Interactor, float HeldSeconds, float Progress01)
{
	if (Interactor == nullptr || !HasAuthority() || Zone == nullptr || bOpen)
	{
		return;
	}

	// The interact component reports absolute held seconds, so the delta for the
	// zone's own accumulator comes from the change since the last report.
	float Delta = GetWorld() != nullptr ? GetWorld()->GetDeltaSeconds() : 0.1f;
	if (HoldInteractor.Get() == Interactor)
	{
		Delta = FMath::Clamp(HeldSeconds - HoldLastSeconds, 0.f, 0.5f);
	}
	else
	{
		HoldInteractor = Interactor;
	}

	HoldLastSeconds = HeldSeconds;

	const bool bPairedHeld = IsPairedPanelHeld();
	const float Progress = Zone->ServerHoldOverride(Interactor, bPairedHeld, Delta);

	if (Progress >= 1.f)
	{
		HoldInteractor.Reset();
		HoldLastSeconds = 0.f;
	}
}

void AOneBrahamAccessZone::OnInteractionEnd_Implementation(AOneBrahamPlayerController* Interactor, bool bCompleted)
{
	if (Interactor == nullptr || !HasAuthority() || Zone == nullptr)
	{
		return;
	}

	if (!bCompleted || !bOpen)
	{
		Zone->ServerCancelOverride(Interactor);
	}

	HoldInteractor.Reset();
	HoldLastSeconds = 0.f;
}

void AOneBrahamAccessZone::OnInteractionCancelled_Implementation(AOneBrahamPlayerController* Interactor)
{
	if (Interactor != nullptr && HasAuthority() && Zone != nullptr)
	{
		Zone->ServerCancelOverride(Interactor);
	}

	HoldInteractor.Reset();
	HoldLastSeconds = 0.f;
}

void AOneBrahamAccessZone::SetInteractionHighlight_Implementation(bool bHighlightedIn)
{
	bHighlighted = bHighlightedIn;
}

// ===========================================================================
//  Breaching
// ===========================================================================

void AOneBrahamAccessZone::ApplyBreachDamage(float Damage, AActor* Breacher, const FVector& Origin)
{
	if (!HasAuthority() || bBreached || !bBreachable)
	{
		return;
	}

	const float Effective = Damage * FMath::Clamp(1.f - BreachResistance, 0.05f, 1.f);
	RemainingIntegrity = FMath::Max(0.f, RemainingIntegrity - Effective);

	const FVector Direction = (GetBarrierCentre() - Origin).GetSafeNormal();

	// A charge on a door that is still holding makes a hole in the plaster and a
	// lot of noise; the door itself needs to be defeated before it opens.
	UOneBrahamVFXStatics::SpawnBreachDebris(this, Origin, -Direction);

	OB_ACCESS_LOG("Breach on door '%s': %.0f raw, %.0f effective, %.0f left.",
		*GetZoneId().ToString(), Damage, Effective, RemainingIntegrity);

	if (RemainingIntegrity > 0.f)
	{
		return;
	}

	bBreached = true;

	const int32 PlayerId = [Breacher]()
	{
		const AOneBrahamCharacter* Character = Cast<AOneBrahamCharacter>(Breacher);
		const AOneBrahamPlayerState* PS = Character != nullptr
			? Character->GetPlayerState<AOneBrahamPlayerState>()
			: Cast<AOneBrahamPlayerState>(Breacher);
		return PS != nullptr ? PS->GetPlayerId() : INDEX_NONE;
	}();

	if (Zone != nullptr)
	{
		// Breaching is permanent and it does not need the card, the terminal or
		// permission. It also cannot be undone: the runners are gone.
		Zone->ServerUnlock(EOneBrahamUnlockMethod::Breach, PlayerId);
	}

	bOpen = true;
	bBreachable = false;
	ForceNetUpdate();

	if (ReaderLight != nullptr)
	{
		ReaderLight->SetLightColor(OBReaderBreached);
	}

	// The doorway is now permanently passable, including for navmesh.
	BarrierVolume->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BarrierVolume->SetCanEverAffectNavigation(false);

	if (UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
	{
		Nav->AddDirtyArea(BarrierVolume->Bounds.GetBox(), ENavigationDirtyFlag::All);
	}

	UOneBrahamVFXStatics::SpawnExplosion(this, GetBarrierCentre(), 320.f, /*bBreach*/ true);

	// Everyone on the floor hears it, and the charge is what they hear: the
	// projectile plays the blast through every listener's mix
	// (AOneBrahamProjectileBase::MulticastDetonationFeedback). This played a
	// second blast once per player controller, so a listen host heard it twelve
	// times over and the clients heard it from the charge only.
}

float AOneBrahamAccessZone::GetBreachIntegrity01() const
{
	return BreachIntegrity > 0.f ? FMath::Clamp(RemainingIntegrity / BreachIntegrity, 0.f, 1.f) : 1.f;
}

int32 AOneBrahamAccessZone::GetBreachFloor() const
{
	return Zone != nullptr ? Zone->FloorIndex : 1;
}
