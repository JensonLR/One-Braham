// Copyright (c) One Braham. All Rights Reserved.

#include "Extraction/OneBrahamHelicopter.h"

#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/OneBrahamGameState.h"
#include "Data/OneBrahamMatchSettingsAsset.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Extraction/OneBrahamExtractionComponent.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	const TCHAR* const HeliCubeMesh = TEXT("/Engine/BasicShapes/Cube.Cube");
	const TCHAR* const HeliCylinderMesh = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");

	UStaticMeshComponent* MakeHeliPart(AActor* Owner, USceneComponent* Parent, const TCHAR* Name, UStaticMesh* Mesh,
		const FVector& Location, const FVector& SizeCm)
	{
		UStaticMeshComponent* Part = Owner->CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Part->SetupAttachment(Parent);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetCastShadow(true);
		Part->SetRelativeLocation(Location);
		Part->SetRelativeScale3D(SizeCm / 100.f);
		if (Mesh != nullptr)
		{
			Part->SetStaticMesh(Mesh);
		}
		return Part;
	}

	float Ease(float Alpha)
	{
		Alpha = FMath::Clamp(Alpha, 0.f, 1.f);
		return 1.f - FMath::Square(1.f - Alpha);	// fast in, settling at the end
	}
}

AOneBrahamHelicopter::AOneBrahamHelicopter()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = false;
	SetCanBeDamaged(false);

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Movable);
	SetRootComponent(Root);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(HeliCubeMesh);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(HeliCylinderMesh);

	// A blockout an artist replaces: the read from a roof is the silhouette.
	Body = MakeHeliPart(this, Root, TEXT("Body"), Cube.Object, FVector::ZeroVector, FVector(900.f, 260.f, 280.f));
	TailBoom = MakeHeliPart(this, Root, TEXT("TailBoom"), Cube.Object, FVector(-850.f, 0.f, 60.f), FVector(900.f, 60.f, 70.f));
	Rotor = MakeHeliPart(this, Root, TEXT("Rotor"), Cylinder.Object, FVector(0.f, 0.f, 190.f), FVector(1300.f, 1300.f, 4.f));
	WinchLine = MakeHeliPart(this, Root, TEXT("WinchLine"), Cylinder.Object, FVector(0.f, 0.f, -140.f), FVector(4.f, 4.f, 100.f));
	WinchLine->SetVisibility(false);

	Searchlight = CreateDefaultSubobject<USpotLightComponent>(TEXT("Searchlight"));
	Searchlight->SetupAttachment(Root);
	Searchlight->SetRelativeLocation(FVector(380.f, 0.f, -150.f));
	Searchlight->SetIntensity(400000.f);
	Searchlight->SetAttenuationRadius(6000.f);
	Searchlight->SetOuterConeAngle(14.f);
	Searchlight->SetInnerConeAngle(8.f);
	Searchlight->SetLightColor(FLinearColor(0.92f, 0.95f, 1.f));
	Searchlight->SetCastShadows(true);
	Searchlight->SetVisibility(false);
}

void AOneBrahamHelicopter::BeginPlay()
{
	Super::BeginPlay();
	SetActorHiddenInGame(true);
}

UOneBrahamExtractionComponent* AOneBrahamHelicopter::FindExtraction() const
{
	const AOneBrahamGameState* GameState = GetWorld() != nullptr ? GetWorld()->GetGameState<AOneBrahamGameState>() : nullptr;
	return GameState != nullptr ? GameState->GetExtractionComponent() : nullptr;
}

FTransform AOneBrahamHelicopter::ComputePose(EOneBrahamExtractionState State, float Alpha, double TimeSeconds,
	const FVector& Pad, const FVector& InApproachOffset, const FVector& InHoverOffset)
{
	const FVector Hover = Pad + InHoverOffset;
	const float Bob = 25.f * FMath::Sin(static_cast<float>(TimeSeconds) * 1.3f);

	switch (State)
	{
	case EOneBrahamExtractionState::Arriving:
	{
		// A curve, not a line: in from the City, round, and settling into the
		// hover nose-on to the deck.
		const FVector Start = Pad + InApproachOffset;
		const FVector Mid = Pad + FVector(InApproachOffset.X * 0.15f, InApproachOffset.Y * 0.55f, InHoverOffset.Z + 900.f);
		const float T = Ease(Alpha);
		const FVector A = FMath::Lerp(Start, Mid, T);
		const FVector B = FMath::Lerp(Mid, Hover, T);
		const FVector Position = FMath::Lerp(A, B, T);
		const FVector Ahead = (B - A).GetSafeNormal2D();
		const float Pitch = -14.f * (1.f - T);	// nose down while moving
		const float Yaw = Ahead.IsNearlyZero() ? 180.f : Ahead.Rotation().Yaw;
		return FTransform(FRotator(Pitch, Yaw, 0.f), Position + FVector(0.f, 0.f, Bob * T));
	}

	case EOneBrahamExtractionState::Holding:
		return FTransform(FRotator(0.f, 180.f + 6.f * FMath::Sin(static_cast<float>(TimeSeconds) * 0.4f), 0.f),
			Hover + FVector(0.f, 0.f, Bob));

	default:
		return FTransform(FRotator(0.f, 180.f, 0.f), Hover);
	}
}

void AOneBrahamHelicopter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const UOneBrahamExtractionComponent* Extraction = FindExtraction();
	const UWorld* World = GetWorld();
	if (Extraction == nullptr || World == nullptr)
	{
		return;
	}

	const EOneBrahamExtractionState State = Extraction->GetExtractionState();
	const double Now = World->GetTimeSeconds();
	if (State != LastState)
	{
		if (State == EOneBrahamExtractionState::Complete || State == EOneBrahamExtractionState::Failed)
		{
			DepartFrom = GetActorLocation();
		}
		LastState = State;
		StateEnteredTime = Now;
	}

	const FVector Pad = Extraction->GetHelipadLocation();
	const bool bAirborne = State == EOneBrahamExtractionState::Arriving || State == EOneBrahamExtractionState::Holding
		|| ((State == EOneBrahamExtractionState::Complete || State == EOneBrahamExtractionState::Failed)
			&& Now - StateEnteredTime < DepartSeconds && !DepartFrom.IsNearlyZero());

	SetActorHiddenInGame(!bAirborne);
	Searchlight->SetVisibility(bAirborne && State != EOneBrahamExtractionState::Complete);
	WinchLine->SetVisibility(State == EOneBrahamExtractionState::Holding);

	if (!bAirborne)
	{
		return;
	}

	if (State == EOneBrahamExtractionState::Arriving || State == EOneBrahamExtractionState::Holding)
	{
		float Alpha = 1.f;
		if (State == EOneBrahamExtractionState::Arriving)
		{
			const AOneBrahamGameState* GameState = World->GetGameState<AOneBrahamGameState>();
			const UOneBrahamMatchSettingsAsset* Settings = GameState != nullptr ? GameState->GetMatchSettings() : nullptr;
			const float Total = Settings != nullptr ? FMath::Max(1.f, Settings->HelicopterArrivalSeconds) : 45.f;
			Alpha = 1.f - FMath::Clamp(Extraction->GetSecondsRemainingInState() / Total, 0.f, 1.f);
		}

		SetActorTransform(ComputePose(State, Alpha, Now, Pad, ApproachOffset, HoverOffset));
	}
	else
	{
		// Climbing out: up first, then away the way it came.
		const float T = FMath::Clamp(static_cast<float>((Now - StateEnteredTime) / DepartSeconds), 0.f, 1.f);
		const FVector Away = Pad + ApproachOffset * FVector(1.f, 1.f, 1.6f);
		const FVector Climb = DepartFrom + FVector(0.f, 0.f, 1500.f);
		const FVector Position = FMath::Lerp(FMath::Lerp(DepartFrom, Climb, T), FMath::Lerp(Climb, Away, T), T);
		SetActorLocationAndRotation(Position, FRotator(-10.f * T, (Away - DepartFrom).Rotation().Yaw, 0.f));
	}

	Rotor->AddLocalRotation(FRotator(0.f, RotorDegreesPerSecond * DeltaSeconds, 0.f));

	// The searchlight holds the pad, so a squad on it is lit for everyone.
	const FVector LightFrom = Searchlight->GetComponentLocation();
	Searchlight->SetWorldRotation((Pad - LightFrom).Rotation());

	if (State == EOneBrahamExtractionState::Holding)
	{
		const float Length = FMath::Max(100.f, GetActorLocation().Z - Pad.Z - 140.f);
		WinchLine->SetRelativeScale3D(FVector(0.04f, 0.04f, Length / 100.f));
		WinchLine->SetRelativeLocation(FVector(0.f, 0.f, -140.f - Length / 2.f));
	}
}
