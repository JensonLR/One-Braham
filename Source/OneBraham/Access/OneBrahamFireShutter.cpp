// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamFireShutter.cpp

#include "Access/OneBrahamFireShutter.h"

#include "OneBraham.h"
#include "Audio/OneBrahamAudioManagerComponent.h"
#include "Core/OneBrahamGameState.h"
#include "Core/OneBrahamPlayerController.h"
#include "Characters/OneBrahamCharacter.h"
#include "VFX/OneBrahamVFXStatics.h"

#include "Components/BoxComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "NavigationSystem.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/StaticMesh.h"

namespace
{
	/** How often the safety check runs while a shutter is waiting to close. */
	constexpr float OBShutterSafetyTick = 0.15f;

	/** How far the shutter "looks" for a body in the doorway. */
	constexpr float OBShutterBodyMargin = 24.f;
}

AOneBrahamFireShutter::AOneBrahamFireShutter()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.f; // Smooth slides need every frame.

	bReplicates = true;
	SetReplicateMovement(false); // The slide is driven locally from replicated bClosed.

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	FrameMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FrameMesh"));
	FrameMesh->SetupAttachment(Root);
	FrameMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FrameMesh->SetCanEverAffectNavigation(false);
	FrameMesh->bReceivesDecals = true;

	ShutterMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ShutterMesh"));
	ShutterMesh->SetupAttachment(Root);
	ShutterMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ShutterMesh->SetCanEverAffectNavigation(false);
	ShutterMesh->bReceivesDecals = true;

	SealVolume = CreateDefaultSubobject<UBoxComponent>(TEXT("SealVolume"));
	SealVolume->SetupAttachment(Root);
	SealVolume->SetBoxExtent(FVector(140.f, 15.f, 110.f));
	SealVolume->SetRelativeLocation(FVector(0.f, 0.f, 110.f));
	SealVolume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	SealVolume->SetCollisionObjectType(ECC_WorldStatic);
	SealVolume->SetCollisionResponseToAllChannels(ECR_Ignore);
	SealVolume->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	SealVolume->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	SealVolume->SetCanEverAffectNavigation(true);
	SealVolume->SetGenerateOverlapEvents(true);

	WarningLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("WarningLight"));
	WarningLight->SetupAttachment(Root);
	WarningLight->SetRelativeLocation(FVector(0.f, -20.f, 230.f));
	WarningLight->SetIntensity(0.f);
	WarningLight->SetAttenuationRadius(900.f);
	WarningLight->SetLightColor(FLinearColor(1.f, 0.13f, 0.08f));
	WarningLight->SetCastShadows(false);
	WarningLight->SetMobility(EComponentMobility::Movable);

	// Default assets so a shutter dropped into a level without an art pass still
	// looks like a shutter. The mesh is the engine cube; the scale is set to a
	// doorway in the map builder.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeFinder.Succeeded())
	{
		ShutterMesh->SetStaticMesh(CubeFinder.Object);
		FrameMesh->SetStaticMesh(CubeFinder.Object);
	}

	ShutterMesh->SetRelativeScale3D(FVector(2.8f, 0.08f, 2.2f));
	FrameMesh->SetRelativeScale3D(FVector(2.9f, 0.2f, 0.1f));
	FrameMesh->SetRelativeLocation(FVector(0.f, 0.f, 222.f));
}

void AOneBrahamFireShutter::BeginPlay()
{
	Super::BeginPlay();

	// The curtain's closed position is where the designer put it, plus the small
	// run-down offset; the open position is that plus SlideOffset. Both are in the
	// actor's local space so a rotated shutter works with no special case.
	ClosedRelativeLocation = ShutterMesh->GetRelativeLocation() + ClosedOffset;

	MoveAlpha = bClosed ? 1.f : 0.f;
	TargetAlpha = MoveAlpha;
	ApplyMoveAlpha(MoveAlpha, true);

	if (bStartClosed)
	{
		bClosed = true;
	}

	// Register with the lockdown director. The component owns floor sealing; this
	// actor only knows how to move, so a shutter in a streamed sub-level gets
	// sealed the instant it loads rather than opening a hole in the lockdown.
	if (HasAuthority())
	{
		if (const AOneBrahamGameState* GS = GetWorld() ? GetWorld()->GetGameState<AOneBrahamGameState>() : nullptr)
		{
			if (UOneBrahamLockdownComponent* Lockdown = GS->GetLockdownComponent())
			{
				Lockdown->RegisterShutter(this);

				// Catch up with a lockdown that started before this floor streamed in.
				if (Lockdown->GetStage() == EOneBrahamLockdownStage::Purge ||
					Lockdown->GetStage() == EOneBrahamLockdownStage::Lockdown)
				{
					if (Lockdown->IsFloorSealed(FloorIndex) && bAllowLockdownClose)
					{
						ServerSetClosed(true, 0.4f);
					}
				}
			}
		}
	}

	OB_ACCESS_LOG("Shutter '%s' online: floor %d, %s%s",
		*ShutterId.ToString(), FloorIndex,
		bAllowLockdownClose ? TEXT("lockdown-sealable") : TEXT("never seals"),
		bStartClosed ? TEXT(", starts closed") : TEXT(""));
}

void AOneBrahamFireShutter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AOneBrahamFireShutter, bClosed);
}

void AOneBrahamFireShutter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	StateElapsed += DeltaSeconds;

	if (HasAuthority())
	{
		TickSafetyWait(DeltaSeconds);
	}

	// The slide. Exponential-ish approach with a hard stop: the shutter decelerates
	// into its runners because that is what steel does, and it never overshoots.
	if (!FMath::IsNearlyEqual(MoveAlpha, TargetAlpha, 0.0005f))
	{
		const float Step = DeltaSeconds / FMath::Max(0.05f, CurrentMoveSeconds);
		const float Next = (TargetAlpha > MoveAlpha)
			? FMath::Min(MoveAlpha + Step, TargetAlpha)
			: FMath::Max(MoveAlpha - Step, TargetAlpha);

		ApplyMoveAlpha(Next);

		if (FMath::IsNearlyEqual(MoveAlpha, TargetAlpha, 0.0005f))
		{
			MoveAlpha = TargetAlpha;
			ApplyMoveAlpha(MoveAlpha, true);
			PlayMovementEffects(bClosed);
			OnShutterMoved.Broadcast(ShutterId, bClosed);
		}
	}

	// Warning strobe: 2 Hz, only while armed and moving down.
	if (WarningLight != nullptr)
	{
		const bool bStrobing = !bClosed && MoveAlpha > 0.001f;
		const bool bArmed = HasAuthority() && bWaitingForClear;
		const float TargetIntensity = (bStrobing || bArmed)
			? (FMath::Fmod(StateElapsed, 0.5f) < 0.25f ? 2600.f : 120.f)
			: 0.f;

		const float Current = WarningLight->Intensity;
		WarningLight->SetIntensity(FMath::FInterpTo(Current, TargetIntensity, DeltaSeconds, 14.f));
	}
}

// ===========================================================================
//  Move + collision
// ===========================================================================

void AOneBrahamFireShutter::ApplyMoveAlpha(float Alpha, bool bForce)
{
	MoveAlpha = FMath::Clamp(Alpha, 0.f, 1.f);

	const FVector OpenRelative = ClosedRelativeLocation + SlideOffset;

	ShutterMesh->SetRelativeLocation(FMath::Lerp(OpenRelative, ClosedRelativeLocation, MoveAlpha));

	// Collision follows the *physical* position with a small lead on close (so a
	// player cannot walk through the last 30 cm of a closing shutter) and a small
	// lag on open (so nav does not open a hole before the mesh has moved).
	const bool bBlocking = BlocksWalking();
	const ECollisionEnabled::Type Desired = bBlocking
		? ECollisionEnabled::QueryAndPhysics
		: ECollisionEnabled::NoCollision;

	if (bForce || SealVolume->GetCollisionEnabled() != Desired)
	{
		SealVolume->SetCollisionEnabled(Desired);
		ShutterMesh->SetCollisionEnabled(Desired);

		// Tell the navmesh, so the AI stops pathing through a sealed doorway the
		// frame it seals rather than 0.5 s later when the tile rebuilds.
		if (UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
		{
			Nav->AddDirtyArea(SealVolume->Bounds.GetBox(), ENavigationDirtyFlag::All);
		}
	}
}

FVector AOneBrahamFireShutter::GetMidOpeningLocation() const
{
	return GetActorLocation() + FVector(0.f, 0.f, 110.f);
}

bool AOneBrahamFireShutter::IsDoorwayBlocked() const
{
	if (SealVolume == nullptr)
	{
		return false;
	}

	const FVector Extent = SealVolume->GetUnscaledBoxExtent()
		+ FVector(OBShutterBodyMargin, OBShutterBodyMargin, 0.f);
	const FVector Centre = SealVolume->GetComponentLocation();
	const FQuat Rotation = SealVolume->GetComponentQuat();

	TArray<FOverlapResult> Overlaps;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(OBShutterClearance), false, this);
	Params.bReturnPhysicalMaterial = false;

	const bool bAny = GetWorld()->OverlapMultiByChannel(
		Overlaps, Centre, Rotation, ECC_Pawn, FCollisionShape::MakeBox(Extent), Params);

	if (!bAny)
	{
		return false;
	}

	for (const FOverlapResult& Overlap : Overlaps)
	{
		const APawn* Pawn = Cast<APawn>(Overlap.GetActor());
		if (Pawn == nullptr || Pawn->IsPendingKillPending())
		{
			continue;
		}

		// A dead or downed body in the doorway does not stop the shutter: the
		// building does not care, and neither does the clearing crew.
		if (const AOneBrahamCharacter* Character = Cast<AOneBrahamCharacter>(Pawn))
		{
			if (Character->LifeState != EOneBrahamLifeState::Alive)
			{
				continue;
			}
		}

		return true;
	}

	return false;
}

// ===========================================================================
//  Server
// ===========================================================================

void AOneBrahamFireShutter::ServerSetClosed(bool bNewClosed, float DurationSeconds)
{
	if (!HasAuthority() || bNewClosed == bClosed)
	{
		return;
	}

	if (bNewClosed && !bAllowLockdownClose)
	{
		OB_WARN("Shutter '%s' refused a lockdown close: it is marked never-seal.",
			*ShutterId.ToString());
		return;
	}

	PendingDuration = DurationSeconds > 0.f
		? DurationSeconds
		: (bNewClosed ? CloseSeconds : OpenSeconds);

	if (bNewClosed)
	{
		// Safety first: never close on a living body.
		if (IsDoorwayBlocked())
		{
			bWaitingForClear = true;
			SafetyWaitElapsed = 0.f;
			OB_ACCESS_LOG("Shutter '%s' holding: doorway occupied.", *ShutterId.ToString());
			return;
		}

		if (bWarnBeforeClosing)
		{
			PlayWarning();
		}
	}

	bWaitingForClear = false;
	bClosed = bNewClosed;
	TargetAlpha = bClosed ? 1.f : 0.f;
	CurrentMoveSeconds = PendingDuration;
	ForceNetUpdate();
	OnRep_Closed();
}

void AOneBrahamFireShutter::ServerSlamClosed()
{
	if (!HasAuthority())
	{
		return;
	}

	bWaitingForClear = false;
	bClosed = true;
	TargetAlpha = 1.f;
	CurrentMoveSeconds = 0.35f;
	ForceNetUpdate();
	OnRep_Closed();
}

void AOneBrahamFireShutter::TickSafetyWait(float DeltaSeconds)
{
	if (!bWaitingForClear)
	{
		return;
	}

	SafetyWaitElapsed += DeltaSeconds;

	if (SafetyWaitElapsed < OBShutterSafetyTick)
	{
		return;
	}

	SafetyWaitElapsed = 0.f;

	if (!IsDoorwayBlocked())
	{
		bWaitingForClear = false;
		bClosed = true;
		TargetAlpha = 1.f;
		CurrentMoveSeconds = PendingDuration;
		ForceNetUpdate();
		OnRep_Closed();
		return;
	}

	// Kept waiting too long. The building wins: it closes anyway, but the message
	// is on the record, because a shutter closing on a body is a bug report.
	if (StateElapsed > MaxSafetyDelay)
	{
		OB_WARN("Shutter '%s' force-closing after %.1fs blocked on floor %d.",
			*ShutterId.ToString(), StateElapsed, FloorIndex);

		bWaitingForClear = false;
		bClosed = true;
		TargetAlpha = 1.f;
		CurrentMoveSeconds = PendingDuration;
		ForceNetUpdate();
		OnRep_Closed();
	}
}

void AOneBrahamFireShutter::OnRep_Closed()
{
	TargetAlpha = bClosed ? 1.f : 0.f;
	CurrentMoveSeconds = bClosed ? CloseSeconds : OpenSeconds;

	// A client that receives a state change while mid-slide keeps moving from
	// where it is, which is what a client should do: the actor is the truth, the
	// interpolation is presentation.
	if (bClosed)
	{
		WarningLight->SetIntensity(600.f);
	}

	OB_ACCESS_LOG("Shutter '%s' %s.", *ShutterId.ToString(), bClosed ? TEXT("closing") : TEXT("opening"));
}

// ===========================================================================
//  Interface
// ===========================================================================

int32 AOneBrahamFireShutter::GetShutterFloor_Implementation() const
{
	return FloorIndex;
}

void AOneBrahamFireShutter::SetShutterClosed_Implementation(bool bClose, float DurationSeconds)
{
	if (HasAuthority())
	{
		ServerSetClosed(bClose, DurationSeconds);
	}
	else
	{
		// Clients only mirror the replicated flag; this is here so a designer can
		// call the interface from Blueprint on a listen server's client and get
		// the same presentation.
		TargetAlpha = bClose ? 1.f : 0.f;
		CurrentMoveSeconds = bClose
			? (DurationSeconds > 0.f ? DurationSeconds : CloseSeconds)
			: OpenSeconds;
	}
}

bool AOneBrahamFireShutter::IsShutterClosed_Implementation() const
{
	return bClosed;
}

void AOneBrahamFireShutter::ApplyBreachDamage(float Damage, AActor* Breacher, const FVector& Origin)
{
	// Shutters are not the intended breach targets in One Braham - the four
	// breachable partitions are - but a charge next to one has to do something
	// sensible rather than nothing at all: it buckles the curtain and opens it.
	if (!HasAuthority() || bAllowLockdownClose == false)
	{
		return;
	}

	const float Effective = Damage * FMath::Clamp(1.f - BreachResistance, 0.05f, 1.f);

	OB_ACCESS_LOG("Breach on shutter '%s': %.0f raw, %.0f effective.",
		*ShutterId.ToString(), Damage, Effective);

	if (Effective < 120.f)
	{
		return;
	}

	UOneBrahamVFXStatics::SpawnBreachDebris(this, Origin, FVector::UpVector);

	bAllowLockdownClose = false; // The runner is bent; it will not seal again.
	ServerSetClosed(false, 1.2f);
}

// ===========================================================================
//  Presentation
// ===========================================================================

void AOneBrahamFireShutter::PlayWarning()
{
	if (WarningLight != nullptr)
	{
		WarningLight->SetIntensity(2600.f);
	}

	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	// The chime is a building system, not a prop: it is played on every machine
	// through each player's own audio director, so it is mixed like a building
	// announcement rather than like a prop three floors away.
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		if (AOneBrahamPlayerController* PC = Cast<AOneBrahamPlayerController>(It->Get()))
		{
			if (UOneBrahamAudioManagerComponent* Audio = PC->GetAudioManager())
			{
				Audio->PlaySoundAtLocation(FName(TEXT("UI_Countdown")), GetMidOpeningLocation(), 0.85f, 1.0f, this);
			}
		}
	}
}

void AOneBrahamFireShutter::PlayMovementEffects(bool bNowClosed)
{
	const FVector Location = GetMidOpeningLocation();

	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (AOneBrahamPlayerController* PC = Cast<AOneBrahamPlayerController>(It->Get()))
		{
			if (UOneBrahamAudioManagerComponent* Audio = PC->GetAudioManager())
			{
				Audio->PlaySoundAtLocation(
					bNowClosed ? FName(TEXT("SFX_Shutter_Drop")) : FName(TEXT("SFX_Lift_Doors")),
					Location, bNowClosed ? 1.15f : 0.7f, 1.f, this);
			}
		}
	}

	if (bNowClosed)
	{
		// Dust off the runners, and a small shake for anyone standing under it.
		UOneBrahamVFXStatics::SpawnEffect(this, EOneBrahamVFXEffect::DustPuff, Location, FRotator::ZeroRotator, FVector(1.4f));

		if (WarningLight != nullptr)
		{
			WarningLight->SetIntensity(1400.f);
		}
	}
}
