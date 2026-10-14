// Copyright (c) One Braham. All Rights Reserved.

#include "Access/OneBrahamLift.h"
#include "Voice/OneBrahamVoiceDirector.h"

#include "Audio/OneBrahamAudioManagerComponent.h"
#include "Characters/OneBrahamCharacter.h"
#include "Characters/OneBrahamHealthComponent.h"
#include "Components/AudioComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/OneBrahamGameState.h"
#include "Core/OneBrahamGameplayTags.h"
#include "Core/OneBrahamPlayerController.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Lockdown/OneBrahamLockdownComponent.h"
#include "Net/UnrealNetwork.h"
#include "OneBraham.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"
#include "Weapons/OneBrahamInventoryComponent.h"

namespace
{
	const TCHAR* const LiftCubeMesh = TEXT("/Engine/BasicShapes/Cube.Cube");

	/** Inside dimensions of the car, cm: a 220 cm square box, 230 cm tall. */
	constexpr float CarHalf = 110.f;
	constexpr float CarHeight = 230.f;

	/** Seconds the doors take to travel, and the pause before the car moves. */
	constexpr float DoorTravelSeconds = 1.f;

	/** Retry after the doors hit somebody. */
	constexpr float DoorRetrySeconds = 2.f;

	/** Half the doors' height: 215 cm doors, the building's door height. */
	constexpr float DoorHalfHeight = 107.5f;

	/** How far a door leaf slides to open: the width of the opening. */
	constexpr float DoorSlideCm = 2.f * CarHalf - 20.f;

	void ShapeBox(UStaticMeshComponent* Mesh, const FVector& Centre, const FVector& Size)
	{
		if (Mesh != nullptr)
		{
			Mesh->SetRelativeLocation(Centre);
			Mesh->SetRelativeScale3D(Size / 100.f);
		}
	}

	UStaticMeshComponent* MakePart(AActor* Owner, USceneComponent* Parent, const TCHAR* Name)
	{
		UStaticMeshComponent* Part = Owner->CreateDefaultSubobject<UStaticMeshComponent>(Name);
		if (Parent != nullptr)
		{
			Part->SetupAttachment(Parent);
		}
		Part->SetCollisionProfileName(TEXT("OneBrahamInteractable"));
		Part->SetMobility(EComponentMobility::Movable);
		return Part;
	}

	float SmoothStep(float Alpha)
	{
		Alpha = FMath::Clamp(Alpha, 0.f, 1.f);
		return Alpha * Alpha * (3.f - 2.f * Alpha);
	}
}

AOneBrahamLift::AOneBrahamLift()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;

	bReplicates = true;
	bAlwaysRelevant = true;	// it is heard on every floor it passes
	SetReplicatingMovement(false);
	SetCanBeDamaged(false);

	CarRoot = CreateDefaultSubobject<USceneComponent>(TEXT("CarRoot"));
	CarRoot->SetMobility(EComponentMobility::Movable);
	SetRootComponent(CarRoot);

	CarFloor = MakePart(this, CarRoot, TEXT("CarFloor"));
	CarRoof = MakePart(this, CarRoot, TEXT("CarRoof"));
	CarBack = MakePart(this, CarRoot, TEXT("CarBack"));
	CarLeft = MakePart(this, CarRoot, TEXT("CarLeft"));
	CarRight = MakePart(this, CarRoot, TEXT("CarRight"));
	CarDoor = MakePart(this, CarRoot, TEXT("CarDoor"));

	DoorwayVolume = CreateDefaultSubobject<UBoxComponent>(TEXT("DoorwayVolume"));
	DoorwayVolume->SetupAttachment(CarRoot);
	DoorwayVolume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	DoorwayVolume->SetCollisionResponseToAllChannels(ECR_Ignore);
	DoorwayVolume->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	DoorwayVolume->SetGenerateOverlapEvents(true);
}

double AOneBrahamLift::ServerNow() const
{
	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return 0.0;
	}

	if (const AGameStateBase* GameState = World->GetGameState())
	{
		return GameState->GetServerWorldTimeSeconds();
	}

	return World->GetTimeSeconds();
}

float AOneBrahamLift::StopZ(int32 StopIndex) const
{
	return StopFloors.IsValidIndex(StopIndex) ? OneBrahamConstants::FloorZ(StopFloors[StopIndex]) : ShaftBase.Z;
}

void AOneBrahamLift::BeginPlay()
{
	Super::BeginPlay();

	StopFloors.Sort();
	ShaftBase = FVector(GetActorLocation().X, GetActorLocation().Y,
		StopFloors.Num() > 0 ? OneBrahamConstants::FloorZ(StopFloors[0]) : GetActorLocation().Z);

	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, LiftCubeMesh);
	const float Facing = DoorFacingY < 0.f ? -1.f : 1.f;

	for (UStaticMeshComponent* Part : { CarFloor.Get(), CarRoof.Get(), CarBack.Get(), CarLeft.Get(), CarRight.Get(), CarDoor.Get() })
	{
		if (Part != nullptr && Part->GetStaticMesh() == nullptr && Cube != nullptr)
		{
			Part->SetStaticMesh(Cube);
		}
	}

	ShapeBox(CarFloor, FVector(0.f, 0.f, -5.f), FVector(2.f * CarHalf, 2.f * CarHalf, 10.f));
	ShapeBox(CarRoof, FVector(0.f, 0.f, CarHeight + 5.f), FVector(2.f * CarHalf, 2.f * CarHalf, 10.f));
	ShapeBox(CarBack, FVector(0.f, -Facing * CarHalf, CarHeight / 2.f), FVector(2.f * CarHalf, 6.f, CarHeight));
	ShapeBox(CarLeft, FVector(-CarHalf, 0.f, CarHeight / 2.f), FVector(6.f, 2.f * CarHalf, CarHeight));
	ShapeBox(CarRight, FVector(CarHalf, 0.f, CarHeight / 2.f), FVector(6.f, 2.f * CarHalf, CarHeight));
	ShapeBox(CarDoor, FVector(0.f, Facing * CarHalf, DoorHalfHeight), FVector(DoorSlideCm, 4.f, 2.f * DoorHalfHeight));

	DoorwayVolume->SetRelativeLocation(FVector(0.f, Facing * (CarHalf + 15.f), 100.f));
	DoorwayVolume->SetBoxExtent(FVector(CarHalf - 20.f, 18.f, 95.f));

	// A landing door for every stop, fixed in the world on its own floor.
	for (int32 Index = 0; Index < StopFloors.Num(); ++Index)
	{
		UStaticMeshComponent* Door = NewObject<UStaticMeshComponent>(this, *FString::Printf(TEXT("LandingDoor_%d"), Index));
		Door->SetupAttachment(CarRoot);
		Door->SetUsingAbsoluteLocation(true);
		Door->SetUsingAbsoluteRotation(true);
		Door->SetUsingAbsoluteScale(true);
		Door->SetMobility(EComponentMobility::Movable);
		Door->SetCollisionProfileName(TEXT("OneBrahamInteractable"));
		Door->SetStaticMesh(Cube);
		Door->SetWorldScale3D(FVector(DoorSlideCm, 6.f, 2.f * DoorHalfHeight) / 100.f);
		Door->RegisterComponent();
		LandingDoors.Add(Door);
	}

	if (HasAuthority())
	{
		FromStopIndex = 0;
		ToStopIndex = 0;
		bMoving = false;
		bDoorsOpen = true;
		GetWorldTimerManager().SetTimer(DoorTimerHandle, this, &AOneBrahamLift::ServerTryCloseDoors, DoorOpenSeconds, false);

		// The call buttons, one per stop, beside each landing door.
		for (int32 Index = 0; Index < StopFloors.Num(); ++Index)
		{
			const FVector Location(ShaftBase.X + CarHalf + 45.f, ShaftBase.Y + Facing * (CarHalf + 32.f), StopZ(Index) + 120.f);
			FActorSpawnParameters Params;
			Params.Owner = this;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			if (AOneBrahamLiftCallPanel* Panel = GetWorld()->SpawnActor<AOneBrahamLiftCallPanel>(
				AOneBrahamLiftCallPanel::StaticClass(), FTransform(FRotator(0.f, Facing < 0.f ? -90.f : 90.f, 0.f), Location), Params))
			{
				Panel->Configure(this, Index);
				CallPanels.Add(Panel);
			}
		}
	}

	DoorAlpha = bDoorsOpen ? 1.f : 0.f;
	UpdateCarPosition();
	UpdateDoors(0.f);
}

void AOneBrahamLift::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AOneBrahamLift, FromStopIndex);
	DOREPLIFETIME(AOneBrahamLift, ToStopIndex);
	DOREPLIFETIME(AOneBrahamLift, MoveStartTime);
	DOREPLIFETIME(AOneBrahamLift, bMoving);
	DOREPLIFETIME(AOneBrahamLift, bDoorsOpen);
}

void AOneBrahamLift::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	UpdateCarPosition();
	UpdateDoors(DeltaSeconds);

	if (!HasAuthority())
	{
		return;
	}

	if (bMoving)
	{
		const float Duration = FMath::Max(0.5f, FMath::Abs(StopFloors[ToStopIndex] - StopFloors[FromStopIndex]) * SecondsPerFloor);
		if (ServerNow() >= MoveStartTime + Duration)
		{
			ServerArrive();
		}
	}
	else if (IsOutOfService() && !bDoorsOpen)
	{
		// The lockdown has stopped the lifts: open, and stay open.
		bDoorsOpen = true;
		GetWorldTimerManager().ClearTimer(DoorTimerHandle);
		OnRep_Doors();
		ForceNetUpdate();
	}
}

void AOneBrahamLift::UpdateCarPosition()
{
	float Z = StopZ(FromStopIndex);

	if (bMoving && StopFloors.IsValidIndex(ToStopIndex))
	{
		const float Duration = FMath::Max(0.5f, FMath::Abs(StopFloors[ToStopIndex] - StopFloors[FromStopIndex]) * SecondsPerFloor);
		const float Alpha = SmoothStep(static_cast<float>((ServerNow() - MoveStartTime) / Duration));
		Z = FMath::Lerp(StopZ(FromStopIndex), StopZ(ToStopIndex), Alpha);
	}

	const FVector Target(ShaftBase.X, ShaftBase.Y, Z);
	if (!GetActorLocation().Equals(Target, 0.1f))
	{
		// Not a sweep and not a teleport: riders are based on the floor and the
		// movement component carries them with it.
		SetActorLocation(Target, false, nullptr, ETeleportType::None);
	}
}

void AOneBrahamLift::UpdateDoors(float DeltaSeconds)
{
	const float Target = bDoorsOpen ? 1.f : 0.f;
	DoorAlpha = DeltaSeconds > 0.f ? FMath::FInterpConstantTo(DoorAlpha, Target, DeltaSeconds, 1.f / DoorTravelSeconds) : Target;

	const float Facing = DoorFacingY < 0.f ? -1.f : 1.f;
	const float Slide = DoorAlpha * DoorSlideCm;

	// The car door slides along the car; the landing door at the floor the car
	// is standing at slides with it. Every other landing door stays shut.
	if (CarDoor != nullptr)
	{
		CarDoor->SetRelativeLocation(FVector(Slide, Facing * CarHalf, DoorHalfHeight));
	}

	const int32 OpenAt = bMoving ? INDEX_NONE : FromStopIndex;
	for (int32 Index = 0; Index < LandingDoors.Num(); ++Index)
	{
		if (UStaticMeshComponent* Door = LandingDoors[Index])
		{
			const float DoorSlide = Index == OpenAt ? Slide : 0.f;
			Door->SetWorldLocation(FVector(ShaftBase.X + DoorSlide, ShaftBase.Y + Facing * (CarHalf + 22.f),
				StopZ(Index) + DoorHalfHeight));
		}
	}
}

void AOneBrahamLift::OnRep_Doors()
{
	if (UOneBrahamAudioManagerComponent* Audio = UOneBrahamAudioManagerComponent::GetLocal(this))
	{
		Audio->PlaySoundAtLocation(TEXT("SFX_Lift_Doors"), GetActorLocation() + FVector(0.f, 0.f, 100.f), 1.f, 1.f, this);
	}
}

int32 AOneBrahamLift::GetCurrentFloor() const
{
	return StopFloors.IsValidIndex(FromStopIndex) ? StopFloors[FromStopIndex] : INDEX_NONE;
}

bool AOneBrahamLift::IsOutOfService() const
{
	const AOneBrahamGameState* GameState = GetWorld() != nullptr ? GetWorld()->GetGameState<AOneBrahamGameState>() : nullptr;
	const UOneBrahamLockdownComponent* Lockdown = GameState != nullptr ? GameState->GetLockdownComponent() : nullptr;

	if (Lockdown == nullptr)
	{
		return false;
	}

	const EOneBrahamLockdownStage Stage = Lockdown->GetStage();
	return Stage == EOneBrahamLockdownStage::Lockdown || Stage == EOneBrahamLockdownStage::Purge;
}

bool AOneBrahamLift::IsInsideCar(const AActor* Character) const
{
	if (Character == nullptr)
	{
		return false;
	}

	const FVector Local = Character->GetActorLocation() - GetActorLocation();
	return FMath::Abs(Local.X) < CarHalf && FMath::Abs(Local.Y) < CarHalf && Local.Z > 0.f && Local.Z < CarHeight;
}

int32 AOneBrahamLift::NextStopFor(int32 FromIndex) const
{
	return StopFloors.Num() > 0 ? (FromIndex + 1) % StopFloors.Num() : 0;
}

bool AOneBrahamLift::CanOperate(const AOneBrahamPlayerController* Interactor, FText* OutReason) const
{
	const AOneBrahamCharacter* Character = Interactor != nullptr ? Cast<AOneBrahamCharacter>(Interactor->GetPawn()) : nullptr;
	if (Character == nullptr)
	{
		return false;
	}

	if (IsOutOfService())
	{
		if (OutReason != nullptr)
		{
			*OutReason = NSLOCTEXT("OneBraham", "Lift_OutOfService", "Lifts out of service - lockdown");
		}
		return false;
	}

	const UOneBrahamInventoryComponent* Inventory = Character->GetInventoryComponent();
	if (Inventory == nullptr || !Inventory->HasKeycardForTier(RequiredTier))
	{
		if (OutReason != nullptr)
		{
			*OutReason = NSLOCTEXT("OneBraham", "Lift_NeedsBadge", "Lift - staff badge required");
		}
		return false;
	}

	if (bMoving)
	{
		if (OutReason != nullptr)
		{
			*OutReason = NSLOCTEXT("OneBraham", "Lift_InTransit", "Lift in transit");
		}
		return false;
	}

	return true;
}

bool AOneBrahamLift::ServerRequestStop(int32 StopIndex)
{
	if (!HasAuthority() || bMoving || IsOutOfService() || !StopFloors.IsValidIndex(StopIndex))
	{
		return false;
	}

	if (StopIndex == FromStopIndex)
	{
		// Already here: the doors open again, and the close timer restarts.
		if (!bDoorsOpen)
		{
			bDoorsOpen = true;
			OnRep_Doors();
		}
		GetWorldTimerManager().SetTimer(DoorTimerHandle, this, &AOneBrahamLift::ServerTryCloseDoors, DoorOpenSeconds, false);
		ForceNetUpdate();
		return true;
	}

	// The doors have to shut first, and they will not shut on somebody.
	if (bDoorsOpen)
	{
		TArray<AActor*> InDoorway;
		DoorwayVolume->GetOverlappingActors(InDoorway, AOneBrahamCharacter::StaticClass());
		if (InDoorway.Num() > 0)
		{
			ServerTryCloseDoors();
			return false;
		}

		bDoorsOpen = false;
		OnRep_Doors();
	}

	GetWorldTimerManager().ClearTimer(DoorTimerHandle);
	ToStopIndex = StopIndex;
	MoveStartTime = ServerNow() + DoorTravelSeconds;
	bMoving = true;
	ForceNetUpdate();

	if (UOneBrahamAudioManagerComponent* Audio = UOneBrahamAudioManagerComponent::GetLocal(this))
	{
		Audio->PlaySoundAtLocation(TEXT("SFX_Lift_Motor"), GetActorLocation(), 1.f, 1.f, this);
	}

	OB_LOG("Lift %s: floor %d -> %d.", *LiftId.ToString(), StopFloors[FromStopIndex], StopFloors[StopIndex]);
	return true;
}

void AOneBrahamLift::ServerArrive()
{
	FromStopIndex = ToStopIndex;
	bMoving = false;
	bDoorsOpen = true;
	OnRep_Doors();
	ForceNetUpdate();

	MulticastArrived();

	if (!IsOutOfService())
	{
		GetWorldTimerManager().SetTimer(DoorTimerHandle, this, &AOneBrahamLift::ServerTryCloseDoors, DoorOpenSeconds, false);
	}
}

void AOneBrahamLift::MulticastArrived_Implementation()
{
	// The chime is the tell: everybody on the landing knows somebody is here.
	if (UOneBrahamAudioManagerComponent* Audio = UOneBrahamAudioManagerComponent::GetLocal(this))
	{
		Audio->PlaySoundAtLocation(TEXT("SFX_Lift_Arrive"), GetActorLocation() + FVector(0.f, 0.f, 150.f), 1.f, 1.f, this);
	}
}

void AOneBrahamLift::ServerTryCloseDoors()
{
	if (!HasAuthority() || bMoving || IsOutOfService())
	{
		return;
	}

	TArray<AActor*> InDoorway;
	DoorwayVolume->GetOverlappingActors(InDoorway, AOneBrahamCharacter::StaticClass());

	bool bHitSomebody = false;
	for (AActor* Actor : InDoorway)
	{
		AOneBrahamCharacter* Character = Cast<AOneBrahamCharacter>(Actor);
		UOneBrahamHealthComponent* Health = Character != nullptr ? Character->GetHealthComponent() : nullptr;
		if (Health == nullptr || !Health->IsAlive())
		{
			continue;
		}

		// The doors try to close and do not stop for a person: the building's
		// one trap that is always armed. The kill feed names it.
		FOneBrahamDamageReport Report;
		Report.BaseDamage = DoorCrushDamage;
		Report.FinalDamage = DoorCrushDamage;
		Report.HitZone = EOneBrahamHitZone::Torso;
		Report.HitLocation = Character->GetActorLocation();
		Report.CauseId = TEXT("LiftDoors");
		Report.bLethal = DoorCrushDamage >= Health->GetHealth();
		Health->ApplyDamage(DoorCrushDamage, this, Report, nullptr);
		FOneBrahamVoiceDirector::OnLiftCrush(Character);
		bHitSomebody = true;
	}

	if (bHitSomebody)
	{
		// Reopen and try again, like the real thing.
		GetWorldTimerManager().SetTimer(DoorTimerHandle, this, &AOneBrahamLift::ServerTryCloseDoors, DoorRetrySeconds, false);
		return;
	}

	if (bDoorsOpen)
	{
		bDoorsOpen = false;
		OnRep_Doors();
		ForceNetUpdate();
	}
}

// ==========================================================================
//  Interaction: the car's own panel
// ==========================================================================
FText AOneBrahamLift::GetInteractionPrompt_Implementation(const AOneBrahamPlayerController* Interactor) const
{
	FText Reason;
	if (!CanOperate(Interactor, &Reason))
	{
		return Reason;
	}

	const int32 Next = NextStopFor(FromStopIndex);
	return FText::Format(NSLOCTEXT("OneBraham", "Lift_Ride", "Lift to floor {0}"),
		FText::AsNumber(StopFloors.IsValidIndex(Next) ? StopFloors[Next] : 0));
}

FGameplayTag AOneBrahamLift::GetInteractionTag_Implementation(const AOneBrahamPlayerController* Interactor) const
{
	return OneBrahamTags::Interaction_Lift;
}

bool AOneBrahamLift::CanInteract_Implementation(const AOneBrahamPlayerController* Interactor) const
{
	return Interactor != nullptr && IsInsideCar(Interactor->GetPawn()) && CanOperate(Interactor);
}

bool AOneBrahamLift::RequiresHold_Implementation(const AOneBrahamPlayerController* Interactor) const
{
	return false;
}

float AOneBrahamLift::GetHoldDuration_Implementation(const AOneBrahamPlayerController* Interactor) const
{
	return 0.f;
}

float AOneBrahamLift::GetInteractionRange_Implementation(const AOneBrahamPlayerController* Interactor) const
{
	return PanelRangeCm;
}

bool AOneBrahamLift::OnInteractionStart_Implementation(AOneBrahamPlayerController* Interactor)
{
	if (!HasAuthority() || !CanInteract_Implementation(Interactor))
	{
		return false;
	}

	return ServerRequestStop(NextStopFor(FromStopIndex));
}

void AOneBrahamLift::OnInteractionEnd_Implementation(AOneBrahamPlayerController* Interactor, bool bCompleted)
{
}

void AOneBrahamLift::OnInteractionCancelled_Implementation(AOneBrahamPlayerController* Interactor)
{
}

void AOneBrahamLift::SetInteractionHighlight_Implementation(bool bHighlighted)
{
	if (CarBack != nullptr)
	{
		CarBack->SetRenderCustomDepth(bHighlighted);
	}
}

// ==========================================================================
//  AOneBrahamLiftCallPanel
// ==========================================================================
AOneBrahamLiftCallPanel::AOneBrahamLiftCallPanel()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetCanBeDamaged(false);

	Panel = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Panel"));
	SetRootComponent(Panel);
	Panel->SetCollisionProfileName(TEXT("OneBrahamInteractable"));
	Panel->SetRelativeScale3D(FVector(0.04f, 0.14f, 0.24f));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(LiftCubeMesh);
	if (Cube.Succeeded())
	{
		Panel->SetStaticMesh(Cube.Object);
	}
}

void AOneBrahamLiftCallPanel::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AOneBrahamLiftCallPanel, Lift);
	DOREPLIFETIME(AOneBrahamLiftCallPanel, StopIndex);
}

void AOneBrahamLiftCallPanel::Configure(AOneBrahamLift* InLift, int32 InStopIndex)
{
	Lift = InLift;
	StopIndex = InStopIndex;
	ForceNetUpdate();
}

FText AOneBrahamLiftCallPanel::GetInteractionPrompt_Implementation(const AOneBrahamPlayerController* Interactor) const
{
	FText Reason;
	if (Lift == nullptr || !Lift->CanOperate(Interactor, &Reason))
	{
		return Lift == nullptr ? FText::GetEmpty() : Reason;
	}

	return Lift->GetCurrentFloor() == Lift->StopFloors[StopIndex] && Lift->AreDoorsOpen()
		? NSLOCTEXT("OneBraham", "Lift_Here", "Lift is here")
		: NSLOCTEXT("OneBraham", "Lift_Call", "Call lift");
}

FGameplayTag AOneBrahamLiftCallPanel::GetInteractionTag_Implementation(const AOneBrahamPlayerController* Interactor) const
{
	return OneBrahamTags::Interaction_Lift;
}

bool AOneBrahamLiftCallPanel::CanInteract_Implementation(const AOneBrahamPlayerController* Interactor) const
{
	return Lift != nullptr && Lift->CanOperate(Interactor);
}

bool AOneBrahamLiftCallPanel::RequiresHold_Implementation(const AOneBrahamPlayerController* Interactor) const
{
	return false;
}

float AOneBrahamLiftCallPanel::GetHoldDuration_Implementation(const AOneBrahamPlayerController* Interactor) const
{
	return 0.f;
}

float AOneBrahamLiftCallPanel::GetInteractionRange_Implementation(const AOneBrahamPlayerController* Interactor) const
{
	return AOneBrahamLift::PanelRangeCm;
}

bool AOneBrahamLiftCallPanel::OnInteractionStart_Implementation(AOneBrahamPlayerController* Interactor)
{
	if (!HasAuthority() || Lift == nullptr || !Lift->CanOperate(Interactor))
	{
		return false;
	}

	const bool bWasHere = Lift->GetCurrentFloor() == Lift->StopFloors[StopIndex];
	const bool bCalled = Lift->ServerRequestStop(StopIndex);
	if (bCalled && !bWasHere)
	{
		FOneBrahamVoiceDirector::OnLiftCalled(Cast<AOneBrahamCharacter>(Interactor->GetPawn()));
	}
	return bCalled;
}

void AOneBrahamLiftCallPanel::OnInteractionEnd_Implementation(AOneBrahamPlayerController* Interactor, bool bCompleted)
{
}

void AOneBrahamLiftCallPanel::OnInteractionCancelled_Implementation(AOneBrahamPlayerController* Interactor)
{
}

void AOneBrahamLiftCallPanel::SetInteractionHighlight_Implementation(bool bHighlighted)
{
	if (Panel != nullptr)
	{
		Panel->SetRenderCustomDepth(bHighlighted);
	}
}
